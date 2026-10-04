// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Thinking Developer
//
// PodumatOS — a hobby operating system for x86_64.

#include "drivers/usb/ehci.hpp"
#include "drivers/dma.hpp"
#include "drivers/pci.hpp"
#include "lib/memory.hpp"
#include "io/io.hpp"

namespace ehci {

    // capability registers
    constexpr uint32_t CAP_CAPLENGTH    = 0x00;
    constexpr uint32_t CAP_HCIVERSION   = 0x02;
    constexpr uint32_t CAP_HCSPARAMS    = 0x04;
    constexpr uint32_t CAP_HCCPARAMS    = 0x08;

    // operational registers
    constexpr uint32_t OP_USBCMD        = 0x00;
    constexpr uint32_t OP_USBSTS        = 0x04;
    constexpr uint32_t OP_USBINTR       = 0x08;
    constexpr uint32_t OP_FRINDEX       = 0x0C;
    constexpr uint32_t OP_CTRLDSSEGMENT = 0x10;
    constexpr uint32_t OP_PERIODICLIST  = 0x14;
    constexpr uint32_t OP_ASYNCLISTADDR = 0x18;
    constexpr uint32_t OP_CONFIGFLAG    = 0x40;
    constexpr uint32_t OP_PORTSC_BASE   = 0x44;

    constexpr uint32_t CMD_RUN          = (1u << 0);
    constexpr uint32_t CMD_HCRESET      = (1u << 1);
    constexpr uint32_t CMD_PERIODIC_EN  = (1u << 4);
    constexpr uint32_t CMD_ASYNC_EN     = (1u << 5);
    constexpr uint32_t CMD_IAAD         = (1u << 6);

    constexpr uint32_t STS_HCHALTED     = (1u << 12);
    constexpr uint32_t STS_PSS          = (1u << 14);
    constexpr uint32_t STS_ASS          = (1u << 15);

    constexpr uint32_t PORT_CONNECT     = (1u << 0);
    constexpr uint32_t PORT_CONNECT_CHG = (1u << 1);
    constexpr uint32_t PORT_ENABLE      = (1u << 2);
    constexpr uint32_t PORT_ENABLE_CHG  = (1u << 3);
    constexpr uint32_t PORT_OVERCUR_CHG = (1u << 5);
    constexpr uint32_t PORT_RESET       = (1u << 8);
    constexpr uint32_t PORT_POWER       = (1u << 12);
    constexpr uint32_t PORT_OWNER       = (1u << 13);
    constexpr uint32_t PORT_LINE_MASK   = (3u << 10);
    constexpr uint32_t PORT_CHG_BITS    = PORT_CONNECT_CHG | PORT_ENABLE_CHG | PORT_OVERCUR_CHG;

    constexpr uint32_t TD_ACTIVE        = (1u << 7);
    constexpr uint32_t TD_HALTED        = (1u << 6);
    constexpr uint32_t TD_DBE           = (1u << 5);
    constexpr uint32_t TD_BABBLE        = (1u << 4);
    constexpr uint32_t TD_XACTERR       = (1u << 3);
    constexpr uint32_t TD_MMF           = (1u << 2);
    constexpr uint32_t TD_STS_SPLIT     = (1u << 1);
    constexpr uint32_t TD_PING          = (1u << 0);
    constexpr uint32_t TD_CERR          = (3u << 10);
    constexpr uint32_t TD_IOC           = (1u << 15);
    constexpr uint32_t TD_ERR_MASK      = TD_HALTED | TD_DBE | TD_BABBLE | TD_XACTERR | TD_MMF;

    constexpr uint32_t TD_PID_OUT       = (0u << 8);
    constexpr uint32_t TD_PID_IN        = (1u << 8);
    constexpr uint32_t TD_PID_SETUP     = (2u << 8);

    constexpr uint32_t QH_CTRL_EP       = (1u << 27);
    constexpr uint32_t QH_HEAD          = (1u << 15);
    constexpr uint32_t QH_EPS_FS        = (0u << 12);
    constexpr uint32_t QH_EPS_LS        = (1u << 12);
    constexpr uint32_t QH_EPS_HS        = (2u << 12);

    constexpr uint32_t QH_CAPS_MULT_SHIFT = 30;
    constexpr uint32_t QH_CAPS_HUB_SHIFT  = 16;
    constexpr uint32_t QH_CAPS_PORT_SHIFT = 23;
    constexpr uint32_t QH_CAPS_SCM_SHIFT  = 8;
    constexpr uint32_t QH_CAPS_ISM_SHIFT  = 0;

    constexpr uint32_t HLP_TYP_QH       = (1u << 1);
    constexpr uint32_t HLP_TERMINATE    = (1u << 0);

    constexpr uint64_t MMIO_MAP_SIZE    = 0x1000;
    constexpr uint64_t PAGE_SIZE        = 0x1000;
    constexpr uint64_t PAGE_MASK        = ~(PAGE_SIZE - 1);
    constexpr uint64_t ADDR_MASK        = 0x000FFFFFFFFFF000ull;

    constexpr int RESET_TIMEOUT         = 1000000;
    constexpr int XFER_TIMEOUT          = 10000000;
    constexpr int FRAME_LIST_ENTRIES    = 1024;

    constexpr uint32_t EECP_MASK        = 0xFF00;
    constexpr uint32_t EECP_SHIFT       = 8;
    constexpr uint32_t USBLEGSUP_CAPID  = 0x01;
    constexpr uint32_t USBLEGSUP_BIOS   = (1u << 16);
    constexpr uint32_t USBLEGSUP_OS     = (1u << 24);

    struct __attribute__((packed, aligned(32))) QH {
        volatile uint32_t hlp;
        volatile uint32_t ep_chars;
        volatile uint32_t ep_caps;
        volatile uint32_t current_td;
        volatile uint32_t next_td;
        volatile uint32_t alt_next_td;
        volatile uint32_t token;
        volatile uint32_t buffer[5];
        uint32_t          rsv[4];
    };

    struct __attribute__((packed, aligned(32))) TD {
        volatile uint32_t next_td;
        volatile uint32_t alt_next_td;
        volatile uint32_t token;
        volatile uint32_t buffer[5];
    };

    static volatile uint8_t* g_cap    = nullptr;
    static volatile uint8_t* g_op     = nullptr;
    static uint64_t          g_hhdm   = 0;
    static uint32_t          g_nports = 0;
    static uint32_t          g_caplength = 0;
    static bool              g_ready  = false;
    static int               g_active_port = -1;
    static uint32_t          g_port_speed  = QH_EPS_HS;

    static QH*       g_ctrl_qh    = nullptr;
    static TD*       g_ctrl_tds   = nullptr;
    static uint8_t*  g_ctrl_setup = nullptr;
    static uint8_t*  g_ctrl_data  = nullptr;

    static QH*       g_intr_qh    = nullptr;
    static TD*       g_intr_td    = nullptr;
    static uint8_t*  g_intr_buf   = nullptr;
    static bool      g_intr_open  = false;
    static uint16_t  g_intr_mps   = 8;
    static uint8_t   g_intr_interval = 1;
    static uint32_t  g_intr_smask = 0x01;
    static uint32_t  g_intr_frame_interval = 1;
    static uint32_t* g_frame_list = nullptr;
    static bool      g_intr_report_logged = false;
    static uint32_t  g_intr_reads = 0;

    static void serial_init() {
        outb(0x3F8 + 1, 0x00);
        outb(0x3F8 + 3, 0x80);
        outb(0x3F8 + 0, 0x03);
        outb(0x3F8 + 1, 0x00);
        outb(0x3F8 + 3, 0x03);
        outb(0x3F8 + 2, 0xC7);
        outb(0x3F8 + 4, 0x0B);
    }
    static void serial_putc(char c) {
        while ((inb(0x3F8 + 5) & 0x20) == 0) { }
        outb(0x3F8, (uint8_t)c);
    }
    static void serial_puts(const char* s) {
        while (*s) { if (*s == '\n') serial_putc('\r'); serial_putc(*s++); }
    }
    static void serial_hex32(uint32_t v) {
        static const char hex[] = "0123456789ABCDEF";
        serial_puts("0x");
        for (int i = 28; i >= 0; i -= 4) serial_putc(hex[(v >> i) & 0xF]);
    }
    static void serial_hex8(uint8_t v) {
        static const char hex[] = "0123456789ABCDEF";
        serial_putc(hex[(v >> 4) & 0xF]);
        serial_putc(hex[v & 0xF]);
    }
    static void serial_dec(uint32_t v) {
        char buf[12]; int i = 0;
        if (v == 0) { serial_putc('0'); return; }
        while (v > 0) { buf[i++] = '0' + (v % 10); v /= 10; }
        for (int j = i - 1; j >= 0; j--) serial_putc(buf[j]);
    }
    static void serial_td(const char* tag, const TD* td) {
        serial_puts(tag);
        serial_puts(" next=");  serial_hex32(td->next_td);
        serial_puts(" alt=");   serial_hex32(td->alt_next_td);
        serial_puts(" tok=");   serial_hex32(td->token);
        serial_puts(" buf0=");  serial_hex32(td->buffer[0]);
        serial_puts("\n");
    }
    static void serial_qh(const char* tag, const QH* qh) {
        serial_puts(tag);
        serial_puts(" hlp=");   serial_hex32(qh->hlp);
        serial_puts(" chars="); serial_hex32(qh->ep_chars);
        serial_puts(" caps=");  serial_hex32(qh->ep_caps);
        serial_puts(" cur=");   serial_hex32(qh->current_td);
        serial_puts(" next=");  serial_hex32(qh->next_td);
        serial_puts(" tok=");   serial_hex32(qh->token);
        serial_puts(" buf0=");  serial_hex32(qh->buffer[0]);
        serial_puts("\n");
    }

    static inline uint8_t  cap_rd8 (uint32_t o) { return *(volatile uint8_t *)((uintptr_t)g_cap + o); }
    static inline uint16_t cap_rd16(uint32_t o) { return *(volatile uint16_t*)((uintptr_t)g_cap + o); }
    static inline uint32_t cap_rd32(uint32_t o) { return *(volatile uint32_t*)((uintptr_t)g_cap + o); }
    static inline uint32_t op_rd32 (uint32_t o) { return *(volatile uint32_t*)((uintptr_t)g_op  + o); }
    static inline void     op_wr32 (uint32_t o, uint32_t v) { *(volatile uint32_t*)((uintptr_t)g_op + o) = v; }

    static inline void memory_barrier() { asm volatile("mfence" ::: "memory"); }
    static void delay_us(int us) { for (volatile int i = 0; i < us * 100; i++) { asm volatile("pause"); } }

    // vmm
    static uint64_t  g_cr3_orig = 0;
    static uint64_t* g_pml4     = nullptr;

    static void vmm_init(uint64_t hhdm) {
        asm volatile("mov %%cr3, %0" : "=r"(g_cr3_orig));
        uint64_t cr3 = g_cr3_orig & PAGE_MASK;
        g_pml4 = (uint64_t*)(cr3 + hhdm);
    }

    static void vmm_flush_tlb() {
        asm volatile("mov %0, %%cr3" :: "r"(g_cr3_orig) : "memory");
    }

    static uint64_t* next_table(uint64_t* table, int idx) {
        uint64_t e = table[idx];
        if (e & 1) {
            if (e & 0x80) return nullptr;
            return (uint64_t*)((e & ADDR_MASK) + g_hhdm);
        }
        void* t = dma::alloc(PAGE_SIZE, PAGE_SIZE);
        if (!t) return nullptr;
        memset(t, 0, PAGE_SIZE);
        uint64_t phys = dma::map(t);
        table[idx] = (phys & ADDR_MASK) | 0x03;
        return (uint64_t*)t;
    }

    static bool is_page_mapped(uint64_t virt) {
        uint64_t i4 = (virt >> 39) & 0x1FF;
        uint64_t i3 = (virt >> 30) & 0x1FF;
        uint64_t i2 = (virt >> 21) & 0x1FF;
        uint64_t i1 = (virt >> 12) & 0x1FF;

        uint64_t e = g_pml4[i4];
        if (!(e & 1)) return false;
        if (e & 0x80) return true;
        uint64_t* pdpt = (uint64_t*)((e & ADDR_MASK) + g_hhdm);

        e = pdpt[i3];
        if (!(e & 1)) return false;
        if (e & 0x80) return true;
        uint64_t* pd = (uint64_t*)((e & ADDR_MASK) + g_hhdm);

        e = pd[i2];
        if (!(e & 1)) return false;
        if (e & 0x80) return true;
        uint64_t* pt = (uint64_t*)((e & ADDR_MASK) + g_hhdm);

        return (pt[i1] & 1) != 0;
    }

    static bool map_one_page(uint64_t virt, uint64_t phys) {
        uint64_t i4 = (virt >> 39) & 0x1FF;
        uint64_t i3 = (virt >> 30) & 0x1FF;
        uint64_t i2 = (virt >> 21) & 0x1FF;
        uint64_t i1 = (virt >> 12) & 0x1FF;

        uint64_t* pdpt = next_table(g_pml4, i4);
        if (!pdpt) return false;
        uint64_t* pd = next_table(pdpt, i3);
        if (!pd) return false;
        uint64_t* pt = next_table(pd, i2);
        if (!pt) return false;

        pt[i1] = (phys & ADDR_MASK) | 0x13;
        return true;
    }

    static bool map_mmio(uint64_t phys, uint64_t size) {
        uint64_t v_lo = (phys + g_hhdm) & PAGE_MASK;
        uint64_t v_hi = (phys + g_hhdm + size + PAGE_SIZE - 1) & PAGE_MASK;
        uint64_t p    = phys & PAGE_MASK;

        for (uint64_t v = v_lo; v < v_hi; v += PAGE_SIZE, p += PAGE_SIZE) {
            if (!is_page_mapped(v)) {
                if (!map_one_page(v, p)) return false;
            }
        }
        vmm_flush_tlb();
        return true;
    }

    static void qh_init_control(QH* qh, uint8_t addr, uint16_t mps) {
        memset((void*)qh, 0, sizeof(QH));
        uint64_t qh_phys = dma::map(qh);
        qh->hlp = ((uint32_t)qh_phys & ~0x1Fu) | HLP_TYP_QH;

        uint32_t ep_chars = (0u << 28)
                          | g_port_speed
                          | ((uint32_t)(mps & 0x7FF) << 16)
                          | QH_HEAD
                          | (0u << 8)
                          | ((uint32_t)addr & 0x7F);

        if (g_port_speed != QH_EPS_HS) {
            ep_chars |= QH_CTRL_EP;
        }

        qh->ep_chars = ep_chars;
        qh->ep_caps  = (1u << QH_CAPS_MULT_SHIFT);

        qh->current_td  = 0;
        qh->next_td     = HLP_TERMINATE;
        qh->alt_next_td = HLP_TERMINATE;
        qh->token       = 0;
    }

    static void qh_init_interrupt(QH* qh, uint8_t addr, uint8_t ep, uint16_t mps,
                                  uint32_t smask, uint32_t cmask,
                                  uint32_t hub_addr, uint32_t hub_port) {
        memset((void*)qh, 0, sizeof(QH));
        qh->hlp = HLP_TERMINATE;

        uint32_t ep_chars = (15u << 28)
                          | g_port_speed
                          | ((uint32_t)(mps & 0x7FF) << 16)
                          | ((uint32_t)(ep & 0xF) << 8)
                          | ((uint32_t)addr & 0x7F);

        uint32_t ep_caps = (1u << QH_CAPS_MULT_SHIFT)
                         | ((smask & 0xFF) << QH_CAPS_ISM_SHIFT)
                         | ((cmask & 0xFF) << QH_CAPS_SCM_SHIFT);

        if (g_port_speed != QH_EPS_HS) {
            ep_caps |= ((hub_port & 0x7F) << QH_CAPS_PORT_SHIFT);
            ep_caps |= ((hub_addr & 0x7F) << QH_CAPS_HUB_SHIFT);
        }

        qh->ep_chars = ep_chars;
        qh->ep_caps  = ep_caps;

        qh->current_td  = 0;
        qh->next_td     = HLP_TERMINATE;
        qh->alt_next_td = HLP_TERMINATE;
        qh->token       = 0;
    }

    static void td_init(TD* td, uint32_t pid, uint32_t bytes, uint32_t buf_phys, bool ioc) {
        memset((void*)td, 0, sizeof(TD));
        td->next_td     = HLP_TERMINATE;
        td->alt_next_td = HLP_TERMINATE;
        td->token       = TD_ACTIVE | TD_CERR | pid | ((bytes & 0x7FFF) << 16);
        if (ioc) td->token |= TD_IOC;

        if (buf_phys && bytes > 0) {
            td->buffer[0] = buf_phys;
            uint32_t page = buf_phys & ~0xFFFu;
            td->buffer[1] = page + 0x1000;
            td->buffer[2] = page + 0x2000;
            td->buffer[3] = page + 0x3000;
            td->buffer[4] = page + 0x4000;
        }
    }

    // controller reset
    static bool controller_reset() {
        op_wr32(OP_USBCMD, 0);
        for (int i = 0; i < RESET_TIMEOUT; i++) {
            if (!(op_rd32(OP_USBCMD) & CMD_RUN)) break;
        }
        op_wr32(OP_USBCMD, CMD_HCRESET);
        for (int i = 0; i < RESET_TIMEOUT; i++) {
            if (!(op_rd32(OP_USBCMD) & CMD_HCRESET)) {
                serial_puts("[ehci] reset ok\n");
                return true;
            }
        }
        serial_puts("[ehci] reset TIMEOUT\n");
        return false;
    }

    // bios handoff
    static void bios_handoff(pci::Device* dev) {
        uint32_t hcc = cap_rd32(CAP_HCCPARAMS);
        uint32_t eecp = (hcc & EECP_MASK) >> EECP_SHIFT;
        if (eecp < 0x40) {
            serial_puts("[ehci] no EECP\n");
            return;
        }

        serial_puts("[ehci] EECP=0x"); serial_hex8((uint8_t)eecp); serial_puts("\n");

        uint32_t cap = eecp;
        int guard = 0;
        while (cap >= 0x40 && guard++ < 64) {
            uint32_t v = pci::read_config(dev->bus, dev->slot, dev->func, cap);
            uint8_t id = v & 0xFF;
            uint8_t next = (v >> 8) & 0xFF;

            if (id == USBLEGSUP_CAPID) {
                serial_puts("[ehci] USBLEGSUP found\n");
                uint32_t legsup = pci::read_config(dev->bus, dev->slot, dev->func, cap);

                if (legsup & USBLEGSUP_BIOS) {
                    pci::write_config(dev->bus, dev->slot, dev->func,
                                      cap + 3, USBLEGSUP_OS >> 24);
                    for (int i = 0; i < 100; i++) {
                        delay_us(1000);
                        legsup = pci::read_config(dev->bus, dev->slot, dev->func, cap);
                        if (!(legsup & USBLEGSUP_BIOS)) break;
                    }
                    if (legsup & USBLEGSUP_BIOS) {
                        serial_puts("[ehci] WARN: BIOS still owns HC\n");
                    } else {
                        serial_puts("[ehci] BIOS handoff ok\n");
                    }
                } else {
                    serial_puts("[ehci] BIOS doesn't own HC\n");
                }

                pci::write_config(dev->bus, dev->slot, dev->func, cap + 4, 0);
                return;
            }

            if (next < 0x40 || next == 0) break;
            cap = next;
        }
        serial_puts("[ehci] USBLEGSUP not found\n");
    }

    // port reset
    static bool port_reset(int port, uint32_t* out_speed) {
        uint32_t base = OP_PORTSC_BASE + (uint32_t)port * 4;

        uint32_t v = op_rd32(base);
        op_wr32(base, v | PORT_CHG_BITS);
        delay_us(2000);

        v = op_rd32(base);
        if (!(v & PORT_POWER)) {
            op_wr32(base, v | PORT_POWER);
            delay_us(20000);
        }

        v = op_rd32(base);
        op_wr32(base, (v & ~PORT_ENABLE) | PORT_RESET);
        delay_us(50000);

        v = op_rd32(base);
        op_wr32(base, v & ~PORT_RESET);
        delay_us(2000);

        for (int i = 0; i < 100000; i++) {
            v = op_rd32(base);
            if (!(v & PORT_RESET)) break;
            delay_us(1);
        }
        delay_us(20000);

        v = op_rd32(base);

        serial_puts("[ehci] port_reset port="); serial_dec((uint32_t)port);
        serial_puts(" PORTSC="); serial_hex32(v); serial_puts("\n");

        bool enabled = (v & PORT_ENABLE) != 0;
        bool owner   = (v & PORT_OWNER) != 0;

        serial_puts("[ehci] port_reset ENABLE=");
        serial_puts(enabled ? "1" : "0");
        serial_puts(" OWNER=");
        serial_puts(owner ? "1" : "0");
        serial_puts("\n");

        if (out_speed) {
            if (enabled && !owner) {
                *out_speed = QH_EPS_HS;
            } else {
                uint32_t ls = (v & PORT_LINE_MASK) >> 10;
                *out_speed = (ls == 0x1) ? QH_EPS_LS : QH_EPS_FS;
            }
        }
        serial_puts("[ehci] port speed=");
        serial_dec((*out_speed) >> 12);
        serial_puts("\n");

        op_wr32(base, v | PORT_CHG_BITS);
        return enabled && !owner;
    }

    bool init(uint64_t hhdm_offset) {
        g_hhdm = hhdm_offset;
        g_cap = nullptr;
        g_op = nullptr;
        g_nports = 0;
        g_caplength = 0;
        g_ready = false;
        g_active_port = -1;
        g_port_speed = QH_EPS_HS;

        serial_init();
        serial_puts("\n[ehci] ==== init ====\n");

        vmm_init(hhdm_offset);
        serial_puts("[ehci] vmm_init ok\n");

        pci::Device* dev = nullptr;
        int n = pci::get_device_count();
        for (int i = 0; i < n; i++) {
            pci::Device* d = pci::get_device(i);
            if (!d) continue;
            if (d->class_code == 0x0C && d->subclass == 0x03 && d->prog_if == 0x20) {
                dev = d; break;
            }
        }
        if (!dev) { serial_puts("[ehci] PCI device not found\n"); return false; }
        serial_puts("[ehci] PCI dev found\n");

        uint32_t bar0 = dev->bar[0];
        if (bar0 & 0x1) { serial_puts("[ehci] BAR0 is I/O\n"); return false; }
        uint64_t mmio_phys = (uint64_t)(bar0 & ~0xFULL);
        if (bar0 & 0x4) mmio_phys |= ((uint64_t)dev->bar[1] << 32);
        if (!mmio_phys) { serial_puts("[ehci] BAR0 == 0\n"); return false; }

        serial_puts("[ehci] mmio_phys="); serial_hex32((uint32_t)mmio_phys); serial_puts("\n");

        if (!map_mmio(mmio_phys, MMIO_MAP_SIZE)) {
            serial_puts("[ehci] map_mmio FAILED\n");
            return false;
        }
        serial_puts("[ehci] map_mmio ok\n");

        g_cap = (volatile uint8_t*)(mmio_phys + hhdm_offset);

        uint8_t  caplength  = cap_rd8 (CAP_CAPLENGTH);
        uint16_t hciversion = cap_rd16(CAP_HCIVERSION);
        uint32_t hcsparams  = cap_rd32(CAP_HCSPARAMS);
        uint32_t hccparams  = cap_rd32(CAP_HCCPARAMS);

        if (caplength == 0 || caplength == 0xFF) {
            serial_puts("[ehci] CAPLENGTH invalid\n");
            return false;
        }
        g_caplength = caplength;
        g_nports    = hcsparams & 0x0F;

        serial_puts("[ehci] CAPLENGTH=");  serial_hex32(caplength);  serial_puts("\n");
        serial_puts("[ehci] HCIVERSION="); serial_hex32(hciversion); serial_puts("\n");
        serial_puts("[ehci] HCSPARAMS=");  serial_hex32(hcsparams);  serial_puts("\n");
        serial_puts("[ehci] HCCPARAMS=");  serial_hex32(hccparams);  serial_puts("\n");
        serial_puts("[ehci] N_PORTS=");    serial_dec(g_nports);     serial_puts("\n");

        if (g_nports == 0) { serial_puts("[ehci] no ports\n"); return false; }

        g_op = g_cap + caplength;

        bios_handoff(dev);

        if (!controller_reset()) return false;

        op_wr32(OP_CONFIGFLAG, 1);
        op_wr32(OP_USBINTR, 0);

        g_ctrl_qh    = (QH*)     dma::alloc(sizeof(QH), 64);
        g_ctrl_tds   = (TD*)     dma::alloc(sizeof(TD) * 3, 32);
        g_ctrl_setup = (uint8_t*)dma::alloc(8, 8);
        g_ctrl_data  = (uint8_t*)dma::alloc(4096, 4096);

        if (!g_ctrl_qh || !g_ctrl_tds || !g_ctrl_setup || !g_ctrl_data) {
            serial_puts("[ehci] dma::alloc FAILED\n");
            return false;
        }

        memset(g_ctrl_qh, 0, sizeof(QH));
        memset(g_ctrl_tds, 0, sizeof(TD) * 3);
        memset(g_ctrl_setup, 0, 8);
        memset(g_ctrl_data, 0, 4096);

        for (uint32_t i = 0; i < g_nports; i++) {
            uint32_t base = OP_PORTSC_BASE + i * 4;
            uint32_t v = op_rd32(base);
            op_wr32(base, v | PORT_POWER);
        }
        delay_us(100000);

        serial_puts("[ehci] scanning ports\n");
        for (uint32_t i = 0; i < g_nports; i++) {
            uint32_t base = OP_PORTSC_BASE + i * 4;
            uint32_t v = op_rd32(base);

            bool connect = (v & PORT_CONNECT) != 0;
            bool owner   = (v & PORT_OWNER) != 0;

            serial_puts("[ehci] port "); serial_dec(i);
            serial_puts(": PORTSC="); serial_hex32(v);
            serial_puts(" CONNECT="); serial_puts(connect ? "1" : "0");
            serial_puts(" OWNER=");   serial_puts(owner ? "1" : "0");
            serial_puts("\n");

            if (!connect) continue;
            if (owner) {
                serial_puts("[ehci] port owned by companion\n");
                continue;
            }

            uint32_t speed = QH_EPS_HS;
            if (port_reset((int)i, &speed)) {
                g_active_port = (int)i;
                g_port_speed  = speed;
                serial_puts("[ehci] active port="); serial_dec(i); serial_puts("\n");
                break;
            } else {
                serial_puts("[ehci] port_reset failed, next port\n");
            }
        }

        if (g_active_port < 0) serial_puts("[ehci] no device on any port\n");

        qh_init_control(g_ctrl_qh, 0, 64);
        uint64_t qh_phys = dma::map(g_ctrl_qh);
        op_wr32(OP_ASYNCLISTADDR, (uint32_t)qh_phys);
        serial_puts("[ehci] ASYNCLISTADDR="); serial_hex32((uint32_t)qh_phys); serial_puts("\n");
        serial_puts("[ehci] CTRL QH phys=");  serial_hex32((uint32_t)qh_phys);
        serial_puts(" TDs=");
        serial_hex32((uint32_t)dma::map(&g_ctrl_tds[0]));
        serial_puts(",");
        serial_hex32((uint32_t)dma::map(&g_ctrl_tds[1]));
        serial_puts(",");
        serial_hex32((uint32_t)dma::map(&g_ctrl_tds[2]));
        serial_puts("\n");

        op_wr32(OP_USBSTS, op_rd32(OP_USBSTS));

        uint32_t cmd = op_rd32(OP_USBCMD);
        cmd |= CMD_RUN;
        op_wr32(OP_USBCMD, cmd);

        g_ready = true;
        serial_puts("[ehci] ==== init OK ====\n\n");
        return true;
    }

    bool is_present() { return g_ready && g_active_port >= 0; }
    bool poll() { return false; }

    // control transfer
    bool control(uint8_t addr, uint16_t mps,
                 const uint8_t setup[8],
                 void* data, uint16_t len) {
        if (!g_ready) return false;
        if (len > 4096) return false;
        if (!setup) return false;

        memcpy(g_ctrl_setup, setup, 8);

        uint8_t bmRequestType = setup[0];
        bool dir_in    = (bmRequestType & 0x80) != 0;
        bool has_data  = (len > 0);

        serial_puts("[usb] SETUP bmReq="); serial_hex8(bmRequestType);
        serial_puts(" bReq=");              serial_hex8(setup[1]);
        serial_puts(" wVal=");              serial_hex32(((uint16_t)setup[3] << 8) | setup[2]);
        serial_puts(" wIdx=");              serial_hex32(((uint16_t)setup[5] << 8) | setup[4]);
        serial_puts(" wLen=");              serial_dec(len);
        serial_puts(" addr=");              serial_dec(addr);
        serial_puts(" mps=");               serial_dec(mps);
        serial_puts("\n");

        if (has_data && !dir_in) {
            if (!data) return false;
            memcpy(g_ctrl_data, data, len);
        }

        uint32_t cmd = op_rd32(OP_USBCMD);
        cmd &= ~CMD_ASYNC_EN;
        op_wr32(OP_USBCMD, cmd);
        for (int i = 0; i < 100000; i++) {
            if (!(op_rd32(OP_USBSTS) & STS_ASS)) break;
            delay_us(1);
        }

        uint64_t qh_phys         = dma::map(g_ctrl_qh);
        uint64_t setup_buf_phys  = dma::map(g_ctrl_setup);
        uint64_t data_buf_phys   = dma::map(g_ctrl_data);
        uint64_t setup_td_phys   = dma::map(&g_ctrl_tds[0]);
        uint64_t data_td_phys    = dma::map(&g_ctrl_tds[1]);
        uint64_t status_td_phys  = dma::map(&g_ctrl_tds[2]);

        qh_init_control(g_ctrl_qh, addr, mps);
        op_wr32(OP_ASYNCLISTADDR, (uint32_t)qh_phys);

        TD* setup_td  = &g_ctrl_tds[0];
        TD* data_td   = &g_ctrl_tds[1];
        TD* status_td = &g_ctrl_tds[2];

        td_init(setup_td, TD_PID_SETUP, 8, (uint32_t)setup_buf_phys, false);

        uint32_t status_pid = (has_data && dir_in) ? TD_PID_OUT : TD_PID_IN;
        td_init(status_td, status_pid, 0, 0, true);

        if (has_data) {
            td_init(data_td, dir_in ? TD_PID_IN : TD_PID_OUT, len,
                    (uint32_t)data_buf_phys, false);

            setup_td->next_td     = (uint32_t)data_td_phys;
            setup_td->alt_next_td = HLP_TERMINATE;

            data_td->next_td      = (uint32_t)status_td_phys;
            data_td->alt_next_td  = (uint32_t)status_td_phys;
        } else {
            setup_td->next_td     = (uint32_t)status_td_phys;
            setup_td->alt_next_td = HLP_TERMINATE;
        }

        serial_puts("[usb] TD chain:\n");
        serial_puts("  setup @");  serial_hex32((uint32_t)setup_td_phys);
        serial_puts(" next=");     serial_hex32(setup_td->next_td);
        serial_puts(" tok=");      serial_hex32(setup_td->token);
        serial_puts(" buf0=");     serial_hex32(setup_td->buffer[0]);
        serial_puts("\n");
        if (has_data) {
            serial_puts("  data  @");  serial_hex32((uint32_t)data_td_phys);
            serial_puts(" next=");     serial_hex32(data_td->next_td);
            serial_puts(" tok=");      serial_hex32(data_td->token);
            serial_puts(" buf0=");     serial_hex32(data_td->buffer[0]);
            serial_puts(" len=");      serial_dec(len);
            serial_puts("\n");
        }
        serial_puts("  status @"); serial_hex32((uint32_t)status_td_phys);
        serial_puts(" next=");     serial_hex32(status_td->next_td);
        serial_puts(" tok=");      serial_hex32(status_td->token);
        serial_puts("\n");

        g_ctrl_qh->current_td  = (uint32_t)setup_td_phys;
        g_ctrl_qh->next_td     = setup_td->next_td;
        g_ctrl_qh->alt_next_td = setup_td->alt_next_td;
        g_ctrl_qh->token       = setup_td->token;
        g_ctrl_qh->buffer[0]   = setup_td->buffer[0];
        g_ctrl_qh->buffer[1]   = setup_td->buffer[1];
        g_ctrl_qh->buffer[2]   = setup_td->buffer[2];
        g_ctrl_qh->buffer[3]   = setup_td->buffer[3];
        g_ctrl_qh->buffer[4]   = setup_td->buffer[4];

        serial_puts("[usb] QH loaded: cur="); serial_hex32(g_ctrl_qh->current_td);
        serial_puts(" next=");                serial_hex32(g_ctrl_qh->next_td);
        serial_puts(" tok=");                 serial_hex32(g_ctrl_qh->token);
        serial_puts("\n");

        memory_barrier();

        cmd = op_rd32(OP_USBCMD);
        cmd |= CMD_RUN | CMD_ASYNC_EN;
        op_wr32(OP_USBCMD, cmd);

        bool ok = false;
        uint32_t final_token  = 0;
        uint32_t final_qh_tok = 0;
        uint32_t final_qh_cur = 0;
        int poll_count = 0;

        for (int i = 0; i < XFER_TIMEOUT; i++) {
            memory_barrier();

            uint32_t qh_tok = g_ctrl_qh->token;
            uint32_t st_tok = status_td->token;

            if (!(st_tok & TD_ACTIVE)) {
                final_token  = st_tok;
                final_qh_tok = qh_tok;
                final_qh_cur = g_ctrl_qh->current_td;
                ok = !(st_tok & TD_ERR_MASK);
                break;
            }

            if (qh_tok & TD_HALTED) {
                final_token  = qh_tok;
                final_qh_tok = qh_tok;
                final_qh_cur = g_ctrl_qh->current_td;
                serial_puts("[usb] QH halted early: "); serial_hex32(qh_tok); serial_puts("\n");
                break;
            }

            poll_count++;
            delay_us(1);
        }

        serial_puts("[usb] xfer ok=");   serial_puts(ok ? "1" : "0");
        serial_puts(" polls=");          serial_dec(poll_count);
        serial_puts(" status_tok=");     serial_hex32(final_token);
        serial_puts("\n");

        serial_puts("[usb] final QH: tok="); serial_hex32(final_qh_tok);
        serial_puts(" cur=");                serial_hex32(final_qh_cur);
        serial_puts("\n");
        serial_td("[usb] final setup ",  setup_td);
        if (has_data) serial_td("[usb] final data  ",  data_td);
        serial_td("[usb] final status", status_td);

        if (final_token & TD_DBE)     serial_puts("[usb]   err bit: DBE\n");
        if (final_token & TD_BABBLE)  serial_puts("[usb]   err bit: BABBLE\n");
        if (final_token & TD_XACTERR) serial_puts("[usb]   err bit: XACTERR\n");
        if (final_token & TD_MMF)     serial_puts("[usb]   err bit: MMF\n");
        if (final_token & TD_HALTED)  serial_puts("[usb]   bit: HALTED\n");

        cmd = op_rd32(OP_USBCMD);
        cmd &= ~CMD_ASYNC_EN;
        op_wr32(OP_USBCMD, cmd);
        for (int i = 0; i < 100000; i++) {
            if (!(op_rd32(OP_USBSTS) & STS_ASS)) break;
            delay_us(1);
        }

        if (ok && has_data && dir_in && data) {
            memcpy(data, g_ctrl_data, len);
        }
        return ok;
    }

    // interrupt transfer
    bool intr_open(uint8_t addr, uint8_t ep, uint16_t mps, uint8_t interval) {
        if (!g_ready || g_active_port < 0) return false;

        g_intr_qh    = (QH*)     dma::alloc(sizeof(QH), 64);
        g_intr_td    = (TD*)     dma::alloc(sizeof(TD), 32);
        g_intr_buf   = (uint8_t*)dma::alloc(64, 64);
        g_frame_list = (uint32_t*)dma::alloc(FRAME_LIST_ENTRIES * 4, PAGE_SIZE);

        if (!g_intr_qh || !g_intr_td || !g_intr_buf || !g_frame_list) return false;

        memset(g_intr_qh, 0, sizeof(QH));
        memset(g_intr_td, 0, sizeof(TD));
        memset(g_intr_buf, 0, 64);
        memset(g_frame_list, 0, FRAME_LIST_ENTRIES * 4);

        g_intr_mps            = mps;
        g_intr_interval       = interval ? interval : 1;
        g_intr_report_logged  = false;
        g_intr_reads          = 0;

        serial_puts("[ehci] intr_open ep="); serial_hex8(ep);
        serial_puts(" mps=");                serial_dec(mps);
        serial_puts(" interval=");           serial_dec(interval);
        serial_puts(" speed=");              serial_dec(g_port_speed >> 12);
        serial_puts(" port=");               serial_dec((uint32_t)(g_active_port + 1));
        serial_puts("\n");

        uint32_t smask          = 0x01;
        uint32_t cmask          = 0x00;
        uint32_t frame_interval = 1;

        if (g_port_speed == QH_EPS_HS) {
            uint8_t iv = g_intr_interval;
            if (iv == 0) iv = 1;
            if (iv > 16) iv = 16;

            if (iv <= 4) {
                uint32_t uf = 1u << (iv - 1);   // microframes between surveys
                if (uf >= 8) {
                    smask = 0x01;
                } else {
                    smask = 0;
                    for (uint32_t b = 0; b < 8; b += uf) smask |= (1u << b);
                }
                frame_interval = 1;
            } else {
                smask = 0x01;
                frame_interval = 1u << (iv - 4);
            }
        } else {
            smask = 0x01;
            cmask = 0x1C;
            frame_interval = 1;
        }

        if (frame_interval < 1) frame_interval = 1;
        if (frame_interval > FRAME_LIST_ENTRIES) frame_interval = FRAME_LIST_ENTRIES;

        g_intr_smask          = smask;
        g_intr_frame_interval = frame_interval;

        serial_puts("[ehci] smask=");      serial_hex32(smask);
        serial_puts(" cmask=");            serial_hex32(cmask);
        serial_puts(" frame_interval=");   serial_dec(frame_interval);
        serial_puts("\n");

        uint32_t hub_addr = 0, hub_port = 0;
        if (g_port_speed != QH_EPS_HS) {
            hub_port = (uint32_t)(g_active_port + 1);
            hub_addr = 0;
        }

        qh_init_interrupt(g_intr_qh, addr, ep, mps,
                          smask, cmask, hub_addr, hub_port);

        serial_qh("[ehci] intr_qh ", g_intr_qh);

        uint64_t buf_phys = dma::map(g_intr_buf);
        td_init(g_intr_td, TD_PID_IN, mps, (uint32_t)buf_phys, false);

        serial_td("[ehci] intr_td ", g_intr_td);

        g_intr_qh->current_td  = (uint32_t)dma::map(g_intr_td);
        g_intr_qh->next_td     = g_intr_td->next_td;
        g_intr_qh->alt_next_td = g_intr_td->alt_next_td;
        g_intr_qh->token       = g_intr_td->token;
        g_intr_qh->buffer[0]   = g_intr_td->buffer[0];
        g_intr_qh->buffer[1]   = g_intr_td->buffer[1];
        g_intr_qh->buffer[2]   = g_intr_td->buffer[2];
        g_intr_qh->buffer[3]   = g_intr_td->buffer[3];
        g_intr_qh->buffer[4]   = g_intr_td->buffer[4];

        uint64_t qh_phys = dma::map(g_intr_qh);
        uint32_t qh_val  = ((uint32_t)qh_phys & ~0x1Fu) | HLP_TYP_QH;

        serial_puts("[ehci] intr_qh phys=");  serial_hex32((uint32_t)qh_phys);
        serial_puts(" qh_val=");              serial_hex32(qh_val);
        serial_puts(" intr_td phys=");        serial_hex32((uint32_t)dma::map(g_intr_td));
        serial_puts(" intr_buf phys=");       serial_hex32((uint32_t)buf_phys);
        serial_puts("\n");

        for (int i = 0; i < FRAME_LIST_ENTRIES; i++) {
            g_frame_list[i] = HLP_TERMINATE;
        }
        for (int i = 0; i < FRAME_LIST_ENTRIES; i += frame_interval) {
            g_frame_list[i] = qh_val;
        }

        uint64_t fl_phys = dma::map(g_frame_list);
        serial_puts("[ehci] frame_list phys="); serial_hex32((uint32_t)fl_phys);
        serial_puts("\n");

        memory_barrier();

        op_wr32(OP_PERIODICLIST, (uint32_t)fl_phys);

        uint32_t cmd = op_rd32(OP_USBCMD);
        cmd &= ~CMD_ASYNC_EN;
        cmd |= CMD_RUN | CMD_PERIODIC_EN;
        op_wr32(OP_USBCMD, cmd);

        bool pss_ok = false;
        for (int i = 0; i < 100000; i++) {
            if (op_rd32(OP_USBSTS) & STS_PSS) { pss_ok = true; break; }
            delay_us(1);
        }
        uint32_t sts = op_rd32(OP_USBSTS);
        serial_puts("[ehci] periodic enabled: PSS=");
        serial_puts(pss_ok ? "1" : "0");
        serial_puts(" USBSTS="); serial_hex32(sts);
        serial_puts("\n");

        if (!pss_ok) {
            serial_puts("[ehci] periodic schedule did NOT start\n");
            g_intr_open = false;
            return false;
        }

        g_intr_open = true;
        return true;
    }

    void intr_close() {
        if (!g_intr_open) return;

        uint32_t cmd = op_rd32(OP_USBCMD);
        cmd &= ~CMD_PERIODIC_EN;
        op_wr32(OP_USBCMD, cmd);

        if (g_frame_list) memset(g_frame_list, 0, FRAME_LIST_ENTRIES * 4);

        g_intr_open = false;
    }

    bool intr_read(void* out, uint16_t len) {
        if (!g_intr_open) return false;
        if (!out) return false;

        memory_barrier();
        uint32_t qh_token = g_intr_qh->token;

        if (qh_token & TD_ACTIVE) return false;

        bool ok = !(qh_token & TD_ERR_MASK);

        if (ok) {
            uint16_t copy_len = (len < g_intr_mps) ? len : g_intr_mps;
            memcpy(out, g_intr_buf, copy_len);

            if (!g_intr_report_logged) {
                serial_puts("[ehci] intr first report:");
                for (int i = 0; i < copy_len; i++) {
                    serial_putc(' ');
                    serial_hex8(g_intr_buf[i]);
                }
                serial_puts(" (token="); serial_hex32(qh_token); serial_puts(")\n");
                g_intr_report_logged = true;
            }
        } else {
            serial_puts("[ehci] intr error: qh_tok="); serial_hex32(qh_token);
            serial_puts("\n");
        }

        g_intr_reads++;

        memset(g_intr_buf, 0, 64);
        uint64_t buf_phys = dma::map(g_intr_buf);
        td_init(g_intr_td, TD_PID_IN, g_intr_mps, (uint32_t)buf_phys, false);

        g_intr_qh->current_td  = (uint32_t)dma::map(g_intr_td);
        g_intr_qh->next_td     = g_intr_td->next_td;
        g_intr_qh->alt_next_td = g_intr_td->alt_next_td;
        g_intr_qh->token       = g_intr_td->token;
        g_intr_qh->buffer[0]   = g_intr_td->buffer[0];
        g_intr_qh->buffer[1]   = g_intr_td->buffer[1];
        g_intr_qh->buffer[2]   = g_intr_td->buffer[2];
        g_intr_qh->buffer[3]   = g_intr_td->buffer[3];
        g_intr_qh->buffer[4]   = g_intr_td->buffer[4];

        memory_barrier();
        return ok;
    }
	
	void stop() {
        if (!g_ready) return;
        intr_close();
        op_wr32(OP_USBCMD, 0);
        for (int i = 0; i < RESET_TIMEOUT; i++) {
            if (!(op_rd32(OP_USBCMD) & CMD_RUN)) break;
        }
        g_ready = false;
        g_active_port = -1;
    }

}