// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Thinking Developer
//
// PodumatOS — a hobby operating system for x86_64.

#include "drivers/usb/xhci.hpp"
#include "drivers/usb/xhci_hw.hpp"
#include "drivers/dma.hpp"
#include "drivers/pci.hpp"
#include "lib/memory.hpp"
#include "console/console.hpp"
#include "io/io.hpp"

namespace xhci {

static void serial_init() {
    outb(0x3F8 + 1, 0x00);
    outb(0x3F8 + 3, 0x80);
    outb(0x3F8 + 0, 0x01);
    outb(0x3F8 + 1, 0x00);
    outb(0x3F8 + 3, 0x03);
    outb(0x3F8 + 2, 0xC7);
    outb(0x3F8 + 4, 0x0B);
}
static void serial_putc(char c) {
    while ((inb(0x3F8 + 5) & 0x20) == 0) { }
    outb(0x3F8, (uint8_t)c);
    if (c != '\r') {
        // console::putc(c);
    }
}
static void serial_puts(const char* s) {
    while (*s) {
        if (*s == '\n') serial_putc('\r');
        serial_putc(*s++);
    }
}
static void serial_hex8(uint8_t v) {
    static const char h[] = "0123456789ABCDEF";
    serial_putc(h[(v >> 4) & 0xF]); serial_putc(h[v & 0xF]);
}
static void serial_hex16(uint16_t v) {
    static const char h[] = "0123456789ABCDEF";
    for (int i = 12; i >= 0; i -= 4) serial_putc(h[(v >> i) & 0xF]);
}
static void serial_hex32(uint32_t v) {
    static const char h[] = "0123456789ABCDEF";
    serial_puts("0x");
    for (int i = 28; i >= 0; i -= 4) serial_putc(h[(v >> i) & 0xF]);
}
static void serial_hex64(uint64_t v) {
    static const char h[] = "0123456789ABCDEF";
    serial_puts("0x");
    for (int i = 60; i >= 0; i -= 4) serial_putc(h[(v >> i) & 0xF]);
}
static void serial_dec(uint32_t v) {
    char b[12]; int i = 0;
    if (v == 0) { serial_putc('0'); return; }
    while (v > 0) { b[i++] = '0' + (v % 10); v /= 10; }
    for (int j = i - 1; j >= 0; j--) serial_putc(b[j]);
}


static uint64_t  g_hhdm     = 0;
static uint64_t  g_cr3_orig = 0;
static uint64_t* g_pml4     = nullptr;

constexpr uint64_t PAGE_SIZE = 0x1000;
constexpr uint64_t PAGE_MASK = ~0xFFFull;
constexpr uint64_t ADDR_MASK = 0x000FFFFFFFFFF000ull;

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
    uint64_t e = g_pml4[i4]; if (!(e & 1)) return false; if (e & 0x80) return true;
    uint64_t* pdpt = (uint64_t*)((e & ADDR_MASK) + g_hhdm);
    e = pdpt[i3]; if (!(e & 1)) return false; if (e & 0x80) return true;
    uint64_t* pd = (uint64_t*)((e & ADDR_MASK) + g_hhdm);
    e = pd[i2]; if (!(e & 1)) return false; if (e & 0x80) return true;
    uint64_t* pt = (uint64_t*)((e & ADDR_MASK) + g_hhdm);
    return (pt[i1] & 1) != 0;
}
static bool map_one_page(uint64_t virt, uint64_t phys) {
    uint64_t i4 = (virt >> 39) & 0x1FF;
    uint64_t i3 = (virt >> 30) & 0x1FF;
    uint64_t i2 = (virt >> 21) & 0x1FF;
    uint64_t i1 = (virt >> 12) & 0x1FF;
    uint64_t* pdpt = next_table(g_pml4, i4); if (!pdpt) return false;
    uint64_t* pd   = next_table(pdpt, i3);   if (!pd)   return false;
    uint64_t* pt   = next_table(pd, i2);     if (!pt)   return false;
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

//  Global state
static volatile uint8_t* g_mmio     = nullptr;
static uint32_t          g_op_base  = 0;
static uint32_t          g_rt_base  = 0;
static uint32_t          g_db_base  = 0;

static uint32_t g_csz         = 0;
static uint32_t g_ctx_stride  = 32;
static uint32_t g_max_slots   = 0;
static uint32_t g_max_ports   = 0;
static uint32_t g_max_intrs   = 0;
static bool     g_ready       = false;

static pci::Device* g_pci_dev    = nullptr;
static uint16_t     g_pci_vendor = 0;
static uint16_t     g_pci_device = 0;

static uint64_t* g_dcbaa      = nullptr;
static uint64_t  g_dcbaa_phys = 0;

static Trb*      g_cmd_ring      = nullptr;
static uint64_t  g_cmd_ring_phys = 0;
static uint32_t  g_cmd_enq       = 0;
static uint32_t  g_cmd_cycle     = 1;

static Trb*      g_event_ring      = nullptr;
static uint64_t  g_event_ring_phys = 0;
static uint32_t  g_event_deq       = 0;
static uint32_t  g_event_cycle     = 1;

static ErstEntry* g_erst      = nullptr;
static uint64_t   g_erst_phys = 0;

static uint32_t  g_slot        = 0;
static uint32_t  g_port        = 0;
static uint32_t  g_speed       = 0;
static uint8_t   g_device_addr = 0;
static uint16_t  g_ep0_mps     = 8;

static uint8_t*  g_input_ctx       = nullptr;
static uint64_t  g_input_ctx_phys  = 0;
static uint8_t*  g_device_ctx      = nullptr;
static uint64_t  g_device_ctx_phys = 0;

static Trb*      g_ep0_ring      = nullptr;
static uint64_t  g_ep0_ring_phys = 0;
static uint32_t  g_ep0_enq       = 0;
static uint32_t  g_ep0_cycle     = 1;

static uint8_t*  g_ctrl_buf      = nullptr;
static uint64_t  g_ctrl_buf_phys = 0;

static Trb*      g_intr_ring      = nullptr;
static uint64_t  g_intr_ring_phys = 0;
static uint32_t  g_intr_enq       = 0;
static uint32_t  g_intr_cycle     = 1;
static uint8_t*  g_intr_buf       = nullptr;
static uint64_t  g_intr_buf_phys  = 0;
static uint8_t   g_intr_dci       = 0;
static uint16_t  g_intr_mps       = 8;
static bool      g_intr_open      = false;


//  Register accessors
static inline uint32_t mmio_rd32(uint32_t off) {
    return *(volatile uint32_t*)((uintptr_t)g_mmio + off);
}
static inline void mmio_wr32(uint32_t off, uint32_t v) {
    *(volatile uint32_t*)((uintptr_t)g_mmio + off) = v;
}
static inline uint64_t mmio_rd64(uint32_t off) {
    uint32_t lo = *(volatile uint32_t*)((uintptr_t)g_mmio + off);
    uint32_t hi = *(volatile uint32_t*)((uintptr_t)g_mmio + off + 4);
    return ((uint64_t)hi << 32) | lo;
}
static inline void mmio_wr64(uint32_t off, uint64_t v) {
    *(volatile uint32_t*)((uintptr_t)g_mmio + off)     = (uint32_t)(v & 0xFFFFFFFF);
    *(volatile uint32_t*)((uintptr_t)g_mmio + off + 4) = (uint32_t)(v >> 32);
}

static inline uint32_t cap_rd32(uint32_t off) { return mmio_rd32(off); }
static inline uint32_t op_rd32(uint32_t off)  { return mmio_rd32(g_op_base + off); }
static inline void     op_wr32(uint32_t off, uint32_t v) { mmio_wr32(g_op_base + off, v); }
static inline uint64_t op_rd64(uint32_t off)  { return mmio_rd64(g_op_base + off); }
static inline void     op_wr64(uint32_t off, uint64_t v) { mmio_wr64(g_op_base + off, v); }

static inline uint32_t rt_rd32(uint32_t off)  { return mmio_rd32(g_rt_base + off); }
static inline void     rt_wr32(uint32_t off, uint32_t v) { mmio_wr32(g_rt_base + off, v); }
static inline uint64_t rt_rd64(uint32_t off)  { return mmio_rd64(g_rt_base + off); }
static inline void     rt_wr64(uint32_t off, uint64_t v) { mmio_wr64(g_rt_base + off, v); }

static inline uint32_t port_rd32(uint32_t port)  { return op_rd32(OP_PORTS + port * 0x10 + PORTSC); }
static inline void     port_wr32(uint32_t port, uint32_t v) { op_wr32(OP_PORTS + port * 0x10 + PORTSC, v); }

static inline void doorbell(uint32_t slot, uint32_t target) {
    mmio_wr32(g_db_base + slot * 4, target & 0xFFFF);
    (void)mmio_rd32(g_db_base + slot * 4);
}

static inline void memory_barrier() { asm volatile("mfence" ::: "memory"); }
static void delay_us(uint32_t us) {
    for (volatile uint32_t i = 0; i < us * 100; i++) asm volatile("pause");
}


//  PCI helpers
static constexpr uint16_t PCI_CMD_IO       = 1u << 0;
static constexpr uint16_t PCI_CMD_MEM      = 1u << 1;
static constexpr uint16_t PCI_CMD_BUSMSTR  = 1u << 2;
static constexpr uint16_t PCI_CMD_INT_DIS  = 1u << 10;

static void pci_enable_bus_master(pci::Device* dev) {
    uint32_t cmd_dw = pci::read_config(dev->bus, dev->slot, dev->func, 0x04);
    uint16_t cmd    = (uint16_t)(cmd_dw & 0xFFFF);
    cmd &= ~PCI_CMD_IO;
    cmd |=  PCI_CMD_MEM;
    cmd |=  PCI_CMD_BUSMSTR;
    cmd &= ~PCI_CMD_INT_DIS;
    uint32_t new_dw = (cmd_dw & 0xFFFF0000u) | cmd;
    pci::write_config(dev->bus, dev->slot, dev->func, 0x04, new_dw);

    serial_puts("[xhci] PCI cmd: "); serial_hex16(cmd);
    serial_puts(" -> ");
    uint32_t verify = pci::read_config(dev->bus, dev->slot, dev->func, 0x04);
    serial_hex16((uint16_t)(verify & 0xFFFF));
    serial_puts("\n");
}

// Intel port switch
static constexpr uint8_t  INTEL_XUSB2PR    = 0xD0;
static constexpr uint8_t  INTEL_USB2PRM    = 0xD4;
static constexpr uint8_t  INTEL_USB3_PSSEN = 0xD8;
static constexpr uint8_t  INTEL_USB3PRM    = 0xDC;

static bool has_intel_ehci() {
    int n = pci::get_device_count();
    for (int i = 0; i < n; i++) {
        pci::Device* d = pci::get_device(i);
        if (!d) continue;
        if (d->vendor_id != 0x8086) continue;
        if (d->class_code == 0x0C && d->subclass == 0x03 && d->prog_if == 0x20) {
            return true;
        }
    }
    return false;
}

static void intel_switch_ports(pci::Device* dev) {
    if (!has_intel_ehci()) {
        serial_puts("[xhci] No Intel EHCI present, port switch skipped\n");
        return;
    }

    serial_puts("[xhci] Intel EHCI present, switching ports to xHCI\n");

    uint32_t usb3prm = pci::read_config(dev->bus, dev->slot, dev->func, INTEL_USB3PRM);
    uint32_t usb2prm = pci::read_config(dev->bus, dev->slot, dev->func, INTEL_USB2PRM);

    serial_puts("[xhci]   USB3PRM="); serial_hex32(usb3prm); serial_puts("\n");
    serial_puts("[xhci]   USB2PRM="); serial_hex32(usb2prm); serial_puts("\n");

    pci::write_config(dev->bus, dev->slot, dev->func, INTEL_USB3_PSSEN, usb3prm);
    pci::write_config(dev->bus, dev->slot, dev->func, INTEL_XUSB2PR, usb2prm);

    uint32_t usb3_pssen = pci::read_config(dev->bus, dev->slot, dev->func, INTEL_USB3_PSSEN);
    uint32_t xusb2pr    = pci::read_config(dev->bus, dev->slot, dev->func, INTEL_XUSB2PR);

    serial_puts("[xhci]   USB3_PSSEN="); serial_hex32(usb3_pssen); serial_puts("\n");
    serial_puts("[xhci]   XUSB2PR=");    serial_hex32(xusb2pr);    serial_puts("\n");

    delay_us(20000);
}

//  PORTSC helpers
static constexpr uint32_t PORTSC_RO  =
    (1u << 0)  |
    (1u << 3)  |
    (0xFu << 10) |
    (1u << 30);

static constexpr uint32_t PORTSC_RWS =
    (0xFu << 5) |
    (1u << 9)   |
    (0x3u << 14) |
    (0x7u << 25);

static inline uint32_t port_state_to_neutral(uint32_t portsc) {
    return (portsc & PORTSC_RO) | (portsc & PORTSC_RWS);
}

static inline void port_write_rmw(uint32_t port, uint32_t set_bits) {
    uint32_t cur = port_rd32(port);
    uint32_t nv  = port_state_to_neutral(cur) | PS_PP | set_bits;
    port_wr32(port, nv);
    (void)port_rd32(port);
}

static void port_set_link_state(uint32_t port, uint32_t pls) {
    uint32_t cur = port_rd32(port);
    uint32_t nv  = port_state_to_neutral(cur);
    nv &= ~PS_PLS_MASK;
    nv |= PS_LWS | (pls << 5);
    port_wr32(port, nv);
    (void)port_rd32(port);
}

static constexpr uint32_t PORT_CHANGE_BITS =
    PS_CSC | PS_PEC | PS_WRC | PS_OCC | PS_PRC | PS_PLC | PS_CEC;

static void port_bring_up(uint32_t port) {
    port_write_rmw(port, PORT_CHANGE_BITS);
    delay_us(50000);

    uint32_t v = port_rd32(port);
    uint32_t pls = (v & PS_PLS_MASK) >> 5;

    serial_puts("[xhci]   port "); serial_dec(port);
    serial_puts(" init pls="); serial_dec(pls);
    serial_puts(" PORTSC="); serial_hex32(v); serial_puts("\n");

    if (pls == PLS_DISABLED) {
        port_set_link_state(port, PLS_RXDETECT);
        delay_us(30000);
    }
}

static bool port_reset(uint32_t port, uint32_t* out_speed) {
    port_write_rmw(port, PORT_CHANGE_BITS);
    delay_us(20000);

    uint32_t v = port_rd32(port);
    serial_puts("[xhci] port "); serial_dec(port);
    serial_puts(" pre-reset PORTSC="); serial_hex32(v); serial_puts("\n");

    port_write_rmw(port, PS_PR);

    for (uint32_t i = 0; i < TMO_PORT_RESET; i++) {
        v = port_rd32(port);
        if (!(v & PS_PR)) break;
        delay_us(1);
    }

    bool prc_seen = false;
    for (uint32_t i = 0; i < 200000; i++) {
        v = port_rd32(port);
        if (v & PS_PRC) { prc_seen = true; break; }
        delay_us(1);
    }

    delay_us(30000);

    v = port_rd32(port);
    uint32_t speed = (v >> PS_SPEED_SHIFT) & 0xF;
    uint32_t pls   = (v & PS_PLS_MASK) >> 5;
    bool enabled   = (v & PS_PED) != 0;
    bool ccs       = (v & PS_CCS) != 0;
    bool pls_ok    = (pls == PLS_U0);

    serial_puts("[xhci] port "); serial_dec(port);
    serial_puts(" post-reset PORTSC="); serial_hex32(v);
    serial_puts(" speed="); serial_dec(speed);
    serial_puts(" pls=");   serial_dec(pls);
    serial_puts(" PED=");   serial_puts(enabled ? "1" : "0");
    serial_puts(" CCS=");   serial_puts(ccs ? "1" : "0");
    serial_puts(" PRC=");   serial_puts(prc_seen ? "1" : "0");
    serial_puts("\n");

    if (!enabled && !pls_ok && pls != PLS_RXDETECT) {
        serial_puts("[xhci]   port not enabled, forcing RxDetect\n");
        port_set_link_state(port, PLS_RXDETECT);
        delay_us(30000);
        v = port_rd32(port);
        speed = (v >> PS_SPEED_SHIFT) & 0xF;
        pls   = (v & PS_PLS_MASK) >> 5;
        enabled = (v & PS_PED) != 0;
        pls_ok  = (pls == PLS_U0);
        serial_puts("[xhci]   after RxDetect PORTSC="); serial_hex32(v);
        serial_puts(" pls="); serial_dec(pls);
        serial_puts(" PED="); serial_puts(enabled ? "1" : "0");
        serial_puts("\n");
    }

	port_write_rmw(port, PORT_CHANGE_BITS);

	delay_us(50000);  // 50 ms settle

	v = port_rd32(port);
	uint32_t pls_final = (v & PS_PLS_MASK) >> 5;
	bool ccs_final = (v & PS_CCS) != 0;
	bool ped_final = (v & PS_PED) != 0;

	serial_puts("[xhci] port "); serial_dec(port);
	serial_puts(" final PORTSC="); serial_hex32(v);
	serial_puts(" pls="); serial_dec(pls_final);
	serial_puts(" CCS="); serial_puts(ccs_final ? "1" : "0");
	serial_puts(" PED="); serial_puts(ped_final ? "1" : "0");
	serial_puts("\n");

	if (!ccs_final || !ped_final || pls_final != PLS_U0) {
		serial_puts("[xhci] port not stable after reset\n");
		if (out_speed) *out_speed = speed;
		return false;
	}

	if (out_speed) *out_speed = speed;
		return true;
	}

//  Ring management
static void ring_init(Trb* ring, uint64_t ring_phys) {
    memset(ring, 0, RING_TRBS * sizeof(Trb));
    ring[USABLE_TRBS].param  = ring_phys;
    ring[USABLE_TRBS].status = 0;
    ring[USABLE_TRBS].flags  = TRB_TYPE(TRB_LINK) | TRB_TC;
}

static void ring_enqueue(Trb* ring, uint64_t ring_phys,
                         uint32_t* idx, uint32_t* cycle,
                         uint64_t param, uint32_t status, uint32_t flags) {
    Trb* t = &ring[*idx];
    t->param  = param;
    t->status = status;
    t->flags  = (flags & ~TRB_CYCLE) | (*cycle ? TRB_CYCLE : 0);

    (*idx)++;
    if (*idx >= USABLE_TRBS) {
        Trb* link = &ring[USABLE_TRBS];
        link->param  = ring_phys;
        link->status = 0;
        link->flags  = TRB_TYPE(TRB_LINK) | TRB_TC | (*cycle ? TRB_CYCLE : 0);
        *idx = 0;
        *cycle ^= 1;
    }
}

static bool wait_bits(uint32_t off, uint32_t mask, uint32_t expected, uint32_t tmo_us, bool op) {
    for (uint32_t i = 0; i < tmo_us; i++) {
        uint32_t v = op ? op_rd32(off) : mmio_rd32(off);
        if ((v & mask) == expected) return true;
        if (i % 100 == 0) delay_us(1);
    }
    return false;
}

//  Event ring polling
static bool poll_event(Trb* out_trb, uint32_t tmo_us) {
    for (uint32_t i = 0; i < tmo_us; i++) {
        memory_barrier();
        Trb* t = &g_event_ring[g_event_deq];
        uint32_t f = t->flags;
        if ((f & TRB_CYCLE) == (g_event_cycle ? TRB_CYCLE : 0)) {
            *out_trb = *t;
            g_event_deq++;
            if (g_event_deq >= RING_TRBS) {
                g_event_deq = 0;
                g_event_cycle ^= 1;
            }
            uint64_t erdp = g_event_ring_phys + g_event_deq * sizeof(Trb);
            rt_wr64(RT_IR0 + IR_ERDP, erdp | ERDP_EHB);
            return true;
        }
        delay_us(1);
    }
    return false;
}

//  Command execution
static bool do_command(uint64_t param, uint32_t status, uint32_t flags,
                       uint32_t* out_status, uint32_t* out_flags) {
    ring_enqueue(g_cmd_ring, g_cmd_ring_phys, &g_cmd_enq, &g_cmd_cycle,
                 param, status, flags);
    memory_barrier();
    doorbell(0, 0);

    uint32_t tmo = TMO_CMD_US;
    while (tmo > 0) {
        Trb ev;
        if (!poll_event(&ev, 1000)) { tmo -= 1000; continue; }
        uint32_t type = TRB_TYPE_GET(ev.flags);
        if (type == TRB_CMD_COMPLETE) {
            uint32_t comp = TRB_COMP_GET(ev.status);
            if (out_status) *out_status = ev.status;
            if (out_flags)  *out_flags  = ev.flags;
            return comp == COMP_SUCCESS;
        }
    }
    serial_puts("[xhci] command timeout\n");
    return false;
}

//  Context helpers
static inline uint8_t* ictx(uint32_t index) { return g_input_ctx + index * g_ctx_stride; }
static inline uint32_t ictx_rd32(uint32_t index, uint32_t dw) {
    return *(uint32_t*)(g_input_ctx + index * g_ctx_stride + dw * 4);
}
static inline void ictx_wr32(uint32_t index, uint32_t dw, uint32_t v) {
    *(uint32_t*)(g_input_ctx + index * g_ctx_stride + dw * 4) = v;
}
static inline uint32_t dctx_rd32(uint32_t index, uint32_t dw) {
    return *(uint32_t*)(g_device_ctx + index * g_ctx_stride + dw * 4);
}

//  Controller halt / reset
static bool controller_halt() {
    op_wr32(OP_USBCMD, op_rd32(OP_USBCMD) & ~CMD_RUN);
    return wait_bits(OP_USBSTS, STS_HCH, STS_HCH, TMO_RESET_US, true);
}

static bool controller_reset() {
    op_wr32(OP_USBCMD, op_rd32(OP_USBCMD) | CMD_HCRST);
    if (g_pci_vendor == 0x8086) delay_us(1000);

    if (!wait_bits(OP_USBCMD, CMD_HCRST, 0, TMO_RESET_US, true)) {
        serial_puts("[xhci] reset: HCRST stuck\n");
        return false;
    }
    if (!wait_bits(OP_USBSTS, STS_CNR, 0, TMO_RESET_US, true)) {
        serial_puts("[xhci] reset: CNR stuck\n");
        return false;
    }
    return true;
}

//  Extended capabilities
static void bios_handoff(uint32_t hcc) {
    uint32_t eecp = XECP_PTR(hcc);
    int guard = 0;
    while (eecp >= 0x40 && guard++ < 64) {
        uint32_t v = cap_rd32(eecp);
        uint32_t id = XECP_ID(v);
        if (id == XECP_LEGSUP_ID) {
            serial_puts("[xhci] LEGSUP found\n");
            if (v & LEGSUP_BIOS_OWNED) {
                cap_rd32(eecp);
                mmio_wr32(eecp, v | LEGSUP_OS_OWNED);
                for (int i = 0; i < 20; i++) {
                    delay_us(50000);
                    v = cap_rd32(eecp);
                    if (!(v & LEGSUP_BIOS_OWNED)) break;
                }
                if (v & LEGSUP_BIOS_OWNED) {
                    serial_puts("[xhci] BIOS won't release HC (forcing)\n");
                } else {
                    serial_puts("[xhci] BIOS handoff OK\n");
                }
                mmio_wr32(eecp, cap_rd32(eecp) & ~LEGSUP_BIOS_OWNED);
            } else {
                serial_puts("[xhci] BIOS doesn't own HC\n");
            }
            mmio_wr32(eecp + LEGSUP_LEGCTLSTS_OFF, 0);
            return;
        }
        uint32_t next = XECP_NEXT(v);
        if (!next) break;
        eecp += next << 2;
    }
    serial_puts("[xhci] LEGSUP not found\n");
}


// xHCI Supported Protocol Capabilities
static void dump_supported_protocols(uint32_t hcc) {
    serial_puts("[xhci] --- xHCI Supported Protocol Capabilities ---\n");
    uint32_t eecp = XECP_PTR(hcc);
    int guard = 0;
    while (eecp >= 0x40 && guard++ < 64) {
        uint32_t v = cap_rd32(eecp);
        uint32_t id = XECP_ID(v);
        if (id == 0x02) {
            uint32_t cap0 = v;
            uint32_t cap1 = cap_rd32(eecp + 4);
            uint32_t cap2 = cap_rd32(eecp + 8);

            uint32_t major  = (cap0 >> 24) & 0xFF;
            uint32_t minor  = (cap0 >> 16) & 0xFF;
            uint32_t psioff = (cap2 >> 16) & 0xFF;
            uint32_t psicnt = (cap2 >> 24) & 0xFF;

            serial_puts("[xhci]   PROTO major=");
            serial_dec(major);
            serial_puts(" minor=");
            serial_dec(minor);
            serial_puts(" port_offset=");
            serial_dec(psioff);
            serial_puts(" port_count=");
            serial_dec(psicnt);

            serial_puts(" name=\"");
            uint8_t* p = (uint8_t*)&cap1;
            for (int i = 0; i < 4; i++) {
                char c = (char)p[i];
                if (c >= 32 && c < 127) serial_putc(c);
                else serial_putc('.');
            }
            serial_puts("\"\n");
        }
        uint32_t next = XECP_NEXT(v);
        if (!next) break;
        eecp += next << 2;
    }
    serial_puts("[xhci] --- end protocols ---\n");
}


//  Commands
static bool cmd_enable_slot(uint32_t* out_slot) {
    uint32_t st = 0, fl = 0;
    if (!do_command(0, 0, TRB_TYPE(TRB_ENABLE_SLOT), &st, &fl)) {
        serial_puts("[xhci] EnableSlot failed\n");
        return false;
    }
    *out_slot = TRB_SLOT_GET(fl);
    if (*out_slot == 0) return false;
    serial_puts("[xhci] slot="); serial_dec(*out_slot); serial_puts("\n");
    return true;
}

static bool cmd_disable_slot(uint32_t slot) {
    uint32_t st = 0, fl = 0;
    uint32_t f = TRB_TYPE(TRB_DISABLE_SLOT) | TRB_SLOT(slot);
    if (!do_command(0, 0, f, &st, &fl)) {
        serial_puts("[xhci] DisableSlot failed, comp=");
        serial_dec(TRB_COMP_GET(st)); serial_puts("\n");
        return false;
    }
    serial_puts("[xhci] slot "); serial_dec(slot); serial_puts(" disabled\n");
    return true;
}

static bool cmd_address_device(bool bsr, uint32_t slot) {
    uint32_t st = 0, fl = 0;
    uint32_t f = TRB_TYPE(TRB_ADDRESS_DEV) | TRB_SLOT(slot);
    if (bsr) f |= TRB_BSR;
    if (!do_command(g_input_ctx_phys, 0, f, &st, &fl)) {
        serial_puts("[xhci] AddressDevice failed, comp=");
        serial_dec(TRB_COMP_GET(st)); serial_puts("\n");
        return false;
    }
    return true;
}

static bool cmd_configure_endpoint(uint32_t slot) {
    uint32_t st = 0, fl = 0;
    uint32_t f = TRB_TYPE(TRB_CONFIG_EP) | TRB_SLOT(slot);
    if (!do_command(g_input_ctx_phys, 0, f, &st, &fl)) {
        serial_puts("[xhci] ConfigureEndpoint failed, comp=");
        serial_dec(TRB_COMP_GET(st)); serial_puts("\n");
        return false;
    }
    return true;
}

static bool cmd_evaluate_context(uint32_t slot) {
    uint32_t st = 0, fl = 0;
    uint32_t f = TRB_TYPE(TRB_EVAL_CTX) | TRB_SLOT(slot);
    if (!do_command(g_input_ctx_phys, 0, f, &st, &fl)) {
        serial_puts("[xhci] EvaluateContext failed, comp=");
        serial_dec(TRB_COMP_GET(st)); serial_puts("\n");
        return false;
    }
    return true;
}

static bool cmd_noop() {
    uint32_t st = 0, fl = 0;
    return do_command(0, 0, TRB_TYPE(TRB_NOOP_CMD), &st, &fl);
}


//  Context setup
static uint16_t speed_to_default_mps(uint32_t speed) {
    switch (speed) {
        case 1: return 8;    // FullSpeed
        case 2: return 8;    // LowSpeed
        case 3: return 64;   // HighSpeed
        case 4: return 512;  // SuperSpeed
        case 5: return 512;  // SuperSpeedPlus
        default: return 8;
    }
}
static void setup_slot_ctx(uint8_t* sctx, uint32_t speed, uint32_t rh_port,
                           uint32_t context_entries) {
    uint32_t dw0 = SLOT0_SPEED(speed) | SLOT0_ENTRIES(context_entries);
    *(uint32_t*)(sctx + 0)  = dw0;
    *(uint32_t*)(sctx + 4)  = SLOT1_RH_PORT(rh_port + 1);
    *(uint32_t*)(sctx + 8)  = 0;
    *(uint32_t*)(sctx + 12) = 0;
}

static void setup_ep0_ctx(uint8_t* ectx, uint16_t mps, uint64_t ring_phys) {
    *(uint32_t*)(ectx + 0)  = 0;
    *(uint32_t*)(ectx + 4)  = EP1_CERR(3) | EP1_EPTYPE(EP_TYPE_CTRL) | EP1_MPS(mps);
    *(uint64_t*)(ectx + 8)  = (ring_phys & ~0xFull) | EP2_DCS;
    *(uint32_t*)(ectx + 16) = EP4_AVG_TRB(8);
}

static bool update_ep0_mps(uint16_t mps) {
    if (mps == 0 || mps > 1024) return false;

    memset(g_input_ctx, 0, 33 * g_ctx_stride);
    memcpy(ictx(IC_EP0), g_device_ctx + DC_EP0 * g_ctx_stride, g_ctx_stride);

    uint32_t ep_info = ictx_rd32(IC_EP0, 0);
    ep_info &= ~0x7u;
    ictx_wr32(IC_EP0, 0, ep_info);

    uint32_t ep_info2 = ictx_rd32(IC_EP0, 1);
    ep_info2 = (ep_info2 & 0x0000FFFFu) | EP1_MPS(mps);
    ictx_wr32(IC_EP0, 1, ep_info2);
	ictx_wr32(IC_EP0, 4, EP4_AVG_TRB(8));

    ictx_wr32(IC_CTRL, 0, 0);
    ictx_wr32(IC_CTRL, 1, ICTRL_ADD_EP0);

    memory_barrier();
    if (!cmd_evaluate_context(g_slot)) return false;

    g_ep0_mps = mps;
    return true;
}

static void setup_intr_ep_ctx(uint8_t* ectx, uint16_t mps, uint8_t interval,
                              uint8_t ep_type, uint64_t ring_phys) {
    *(uint32_t*)(ectx + 0)  = EP0_INTERVAL(interval);
    *(uint32_t*)(ectx + 4)  = EP1_CERR(3) | EP1_EPTYPE(ep_type) | EP1_MPS(mps);
    *(uint64_t*)(ectx + 8)  = (ring_phys & ~0xFull) | EP2_DCS;
    *(uint32_t*)(ectx + 16) = EP4_AVG_TRB(mps);
}

static uint8_t calc_interval(uint32_t speed, uint8_t binterval) {
    if (binterval == 0) binterval = 1;
    switch (speed) {
        case 3: case 4: case 5:
            if (binterval > 16) binterval = 16;
            return (uint8_t)(binterval - 1);
        case 1: case 2:
        default: {
            uint32_t temp = binterval;
            if (temp < 1) temp = 1;
            if (temp > 255) temp = 255;
            uint8_t n = 0;
            while (temp != 1) { temp >>= 1; n++; }
            uint8_t v = n + 3;
            if (v < 3) v = 3;
            if (v > 10) v = 10;
            return v;
        }
    }
}


//  Public: init
bool init(uint64_t hhdm_offset) {
    g_hhdm = hhdm_offset;
    g_ready = false;
    g_pci_dev = nullptr;

    serial_init();
    serial_puts("\n[xhci] ==== init ====\n");

    vmm_init(hhdm_offset);

    int total = pci::get_device_count();
    serial_puts("[xhci] Total PCI devices: "); serial_dec(total); serial_puts("\n");
    for (int i = 0; i < total; i++) {
        pci::Device* d = pci::get_device(i);
        if (!d) continue;
        if (d->class_code == 0x0C && d->subclass == 0x03) {
            serial_puts("[xhci] USB ctrl: vendor=");
            serial_hex16(d->vendor_id);
            serial_puts(" device=");
            serial_hex16(d->device_id);
            serial_puts(" prog_if=");
            serial_hex8(d->prog_if);
            serial_puts(" bus="); serial_dec(d->bus);
            serial_puts(" slot="); serial_dec(d->slot);
            serial_puts(" func="); serial_dec(d->func);
            serial_puts("\n");
        }
    }

    pci::Device* dev = nullptr;
    int n = pci::get_device_count();
    for (int i = 0; i < n; i++) {
        pci::Device* d = pci::get_device(i);
        if (!d) continue;
        if (d->class_code == 0x0C && d->subclass == 0x03 && d->prog_if == 0x30) {
            dev = d; break;
        }
    }
    if (!dev) { serial_puts("[xhci] PCI device not found\n"); return false; }

    g_pci_dev    = dev;
    g_pci_vendor = dev->vendor_id;
    g_pci_device = dev->device_id;

    serial_puts("[xhci] PCI dev: vendor="); serial_hex16(dev->vendor_id);
    serial_puts(" device=");                 serial_hex16(dev->device_id);
    serial_puts(" bus="); serial_dec(dev->bus);
    serial_puts(" slot="); serial_dec(dev->slot);
    serial_puts(" func="); serial_dec(dev->func);
    serial_puts("\n");

    pci_enable_bus_master(dev);

    if (dev->vendor_id == 0x8086) {
        serial_puts("[xhci] Intel xHCI detected\n");
        intel_switch_ports(dev);
    }

    uint32_t bar0 = dev->bar[0];
    if (bar0 & 0x1) { serial_puts("[xhci] BAR0 is I/O\n"); return false; }
    uint64_t mmio_phys = (uint64_t)(bar0 & ~0xFULL);
    if (bar0 & 0x4) mmio_phys |= ((uint64_t)dev->bar[1] << 32);
    if (!mmio_phys) { serial_puts("[xhci] BAR0 == 0\n"); return false; }

    serial_puts("[xhci] mmio_phys="); serial_hex64(mmio_phys); serial_puts("\n");

    if (!map_mmio(mmio_phys, 0x10000)) {
        serial_puts("[xhci] map_mmio FAILED\n");
        return false;
    }
    g_mmio = (volatile uint8_t*)(mmio_phys + hhdm_offset);

    uint32_t hc_capbase = cap_rd32(CAP_CAPLENGTH);
    uint32_t cap_len    = hc_capbase & 0xFF;
    uint32_t hci_ver    = (hc_capbase >> 16) & 0xFFFF;
    uint32_t hcs1       = cap_rd32(CAP_HCSPARAMS1);
    uint32_t hcs2       = cap_rd32(CAP_HCSPARAMS2);
    uint32_t hcc        = cap_rd32(CAP_HCCPARAMS1);

    if (hc_capbase == 0xFFFFFFFF) {
        serial_puts("[xhci] HC not accessible\n");
        return false;
    }
    if (cap_len == 0 || cap_len == 0xFF) {
        serial_puts("[xhci] CAPLENGTH invalid\n");
        return false;
    }
    if (hci_ver < 0x0090 || hci_ver > 0x0120) {
        serial_puts("[xhci] unsupported HCI version: ");
        serial_hex16((uint16_t)hci_ver); serial_puts("\n");
        return false;
    }

    g_op_base = cap_len;
    g_rt_base = cap_rd32(CAP_RTSOFF) & ~0x1F;
    g_db_base = cap_rd32(CAP_DBOFF)   & ~0x3;

    g_max_slots = HCS1_MAX_SLOTS(hcs1);
    g_max_ports = HCS1_MAX_PORTS(hcs1);
    g_max_intrs = HCS1_MAX_INTRS(hcs1);
    g_csz        = HCC1_CSZ(hcc);
    g_ctx_stride = g_csz ? CTX_SIZE_64 : CTX_SIZE_32;

    serial_puts("[xhci] hci_ver=");   serial_hex16((uint16_t)hci_ver); serial_puts("\n");
    serial_puts("[xhci] cap_len=");   serial_dec(cap_len);  serial_puts("\n");
    serial_puts("[xhci] op_base=");   serial_hex32(g_op_base); serial_puts("\n");
    serial_puts("[xhci] rt_base=");   serial_hex32(g_rt_base); serial_puts("\n");
    serial_puts("[xhci] db_base=");   serial_hex32(g_db_base); serial_puts("\n");
    serial_puts("[xhci] max_slots="); serial_dec(g_max_slots); serial_puts("\n");
    serial_puts("[xhci] max_ports="); serial_dec(g_max_ports); serial_puts("\n");
    serial_puts("[xhci] csz=");       serial_dec(g_csz);       serial_puts("\n");
    serial_puts("[xhci] hcc=");       serial_hex32(hcc);       serial_puts("\n");

    if (g_max_slots == 0 || g_max_ports == 0) {
        serial_puts("[xhci] invalid HCSPARAMS1\n");
        return false;
    }

    bios_handoff(hcc);

    if (!controller_halt()) { serial_puts("[xhci] halt timeout\n"); return false; }
    if (!controller_reset()) return false;
    serial_puts("[xhci] reset OK\n");

    op_wr32(OP_CONFIG, g_max_slots);

    uint32_t pagesize = op_rd32(OP_PAGESIZE);
    if (!(pagesize & 1)) {
        serial_puts("[xhci] HC doesn't support 4K pages\n");
        return false;
    }

    uint32_t dcbaa_size = (g_max_slots + 1) * sizeof(uint64_t);
    g_dcbaa = (uint64_t*)dma::alloc(dcbaa_size, 64);
    if (!g_dcbaa) return false;
    memset(g_dcbaa, 0, dcbaa_size);
    g_dcbaa_phys = dma::map(g_dcbaa);
    op_wr64(OP_DCBAAP, g_dcbaa_phys);
    serial_puts("[xhci] DCBAA phys="); serial_hex64(g_dcbaa_phys); serial_puts("\n");

    bool ac64 = (hcc & 1) != 0;
    if (!ac64 && (g_dcbaa_phys >> 32) != 0) {
        serial_puts("[xhci] WARN: AC64=0 but DCBAA phys > 4GB!\n");
    }

    uint32_t max_sp = HCS2_MAX_SP(hcs2);
    if (max_sp > 0) {
        uint64_t* sp_array = (uint64_t*)dma::alloc(max_sp * sizeof(uint64_t), 64);
        if (!sp_array) return false;
        memset(sp_array, 0, max_sp * sizeof(uint64_t));
        for (uint32_t i = 0; i < max_sp; i++) {
            void* buf = dma::alloc(PAGE_SIZE, PAGE_SIZE);
            if (!buf) return false;
            memset(buf, 0, PAGE_SIZE);
            sp_array[i] = dma::map(buf);
        }
        g_dcbaa[0] = dma::map(sp_array);
        serial_puts("[xhci] scratchpads="); serial_dec(max_sp); serial_puts("\n");
    }

    g_cmd_ring = (Trb*)dma::alloc(RING_TRBS * sizeof(Trb), 64);
    if (!g_cmd_ring) return false;
    g_cmd_ring_phys = dma::map(g_cmd_ring);
    ring_init(g_cmd_ring, g_cmd_ring_phys);
    g_cmd_enq = 0;
    g_cmd_cycle = 1;

    op_wr64(OP_CRCR, (g_cmd_ring_phys & ~0x3Full) | CRCR_RCS);
    serial_puts("[xhci] CRCR="); serial_hex64(g_cmd_ring_phys); serial_puts("\n");

    g_erst = (ErstEntry*)dma::alloc(sizeof(ErstEntry), 64);
    if (!g_erst) return false;
    memset(g_erst, 0, sizeof(ErstEntry));
    g_erst_phys = dma::map(g_erst);

    g_event_ring = (Trb*)dma::alloc(RING_TRBS * sizeof(Trb), 64);
    if (!g_event_ring) return false;
    memset(g_event_ring, 0, RING_TRBS * sizeof(Trb));
    g_event_ring_phys = dma::map(g_event_ring);
    g_event_deq = 0;
    g_event_cycle = 1;

    g_erst->rs_addr = g_event_ring_phys;
    g_erst->rs_size = RING_TRBS;
    g_erst->rsvdz   = 0;

    rt_wr32(RT_IR0 + IR_ERSTSZ, 1);
    rt_wr64(RT_IR0 + IR_ERSTBA, g_erst_phys);
    rt_wr64(RT_IR0 + IR_ERDP, g_event_ring_phys | ERDP_EHB);
    rt_wr32(RT_IR0 + IR_IMOD, 0);
    rt_wr32(RT_IR0 + IR_IMAN, IMAN_IE);

    serial_puts("[xhci] ERST=");  serial_hex64(g_erst_phys);       serial_puts("\n");
    serial_puts("[xhci] ERing="); serial_hex64(g_event_ring_phys);  serial_puts("\n");

    memory_barrier();
    op_wr32(OP_USBCMD, CMD_RUN | CMD_INTE | CMD_HSEE);
    if (!wait_bits(OP_USBSTS, STS_HCH, 0, TMO_RESET_US, true)) {
        serial_puts("[xhci] controller didn't start\n");
        return false;
    }
    serial_puts("[xhci] controller running\n");

    delay_us(100000);

    if (cmd_noop()) serial_puts("[xhci] Noop OK\n");
    else            serial_puts("[xhci] Noop FAILED\n");

    for (uint32_t i = 0; i < g_max_ports; i++) {
        port_bring_up(i);
    }
    delay_us(500000);

    g_port = 0xFFFFFFFF;
    for (uint32_t i = 0; i < g_max_ports; i++) {
        uint32_t v = port_rd32(i);
        bool ccs = (v & PS_CCS) != 0;
        uint32_t pls = (v & PS_PLS_MASK) >> 5;
        serial_puts("[xhci] port "); serial_dec(i);
        serial_puts(" PORTSC="); serial_hex32(v);
        serial_puts(" CCS="); serial_puts(ccs ? "1" : "0");
        serial_puts(" pls="); serial_dec(pls);
        serial_puts("\n");
        if (!ccs) continue;
        g_port = i;
        g_speed = (v >> PS_SPEED_SHIFT) & 0xF;
        break;
    }
    if (g_port == 0xFFFFFFFF) {
        serial_puts("[xhci] no device on any port\n");
        return false;
    }

    serial_puts("[xhci] resetting port "); serial_dec(g_port); serial_puts("\n");
    if (!port_reset(g_port, &g_speed)) {
        serial_puts("[xhci] port reset failed\n");
        return false;
    }
    serial_puts("[xhci] port speed="); serial_dec(g_speed); serial_puts("\n");

    if (!cmd_enable_slot(&g_slot)) return false;
    if (g_slot == 0 || g_slot > g_max_slots) {
        serial_puts("[xhci] invalid slot\n");
        return false;
    }

    uint32_t ic_size = 33 * g_ctx_stride;
    uint32_t dc_size = 32 * g_ctx_stride;
    g_input_ctx  = (uint8_t*)dma::alloc(ic_size, 64);
    g_device_ctx = (uint8_t*)dma::alloc(dc_size, 64);
    if (!g_input_ctx || !g_device_ctx) return false;
    memset(g_input_ctx,  0, ic_size);
    memset(g_device_ctx, 0, dc_size);
    g_input_ctx_phys  = dma::map(g_input_ctx);
    g_device_ctx_phys = dma::map(g_device_ctx);
    g_dcbaa[g_slot] = g_device_ctx_phys;

    if ((g_input_ctx_phys & 0x3F) != 0) {
        serial_puts("[xhci] WARN: input ctx not 64-byte aligned: ");
        serial_hex64(g_input_ctx_phys); serial_puts("\n");
    }
    if ((g_device_ctx_phys & 0x3F) != 0) {
        serial_puts("[xhci] WARN: device ctx not 64-byte aligned: ");
        serial_hex64(g_device_ctx_phys); serial_puts("\n");
    }

    g_ep0_ring = (Trb*)dma::alloc(RING_TRBS * sizeof(Trb), 64);
    if (!g_ep0_ring) return false;
    g_ep0_ring_phys = dma::map(g_ep0_ring);
    ring_init(g_ep0_ring, g_ep0_ring_phys);
    g_ep0_enq = 0;
    g_ep0_cycle = 1;

    g_ctrl_buf = (uint8_t*)dma::alloc(4096, 4096);
    if (!g_ctrl_buf) return false;
    memset(g_ctrl_buf, 0, 4096);
    g_ctrl_buf_phys = dma::map(g_ctrl_buf);

    // AddressDevice (BSR=1)
    ictx_wr32(IC_CTRL, 0, 0);
    ictx_wr32(IC_CTRL, 1, ICTRL_ADD_SLOT | ICTRL_ADD_EP0);

    setup_slot_ctx(ictx(IC_SLOT), g_speed, g_port, 1);

    uint16_t mps0 = speed_to_default_mps(g_speed);
    g_ep0_mps = mps0;
    setup_ep0_ctx(ictx(IC_EP0), mps0, g_ep0_ring_phys);

    memory_barrier();
    if (!cmd_address_device(true, g_slot)) {
        serial_puts("[xhci] AddressDevice(BSR=1) failed\n");
        return false;
    }
    serial_puts("[xhci] AddressDevice(BSR=1) OK\n");

    g_device_addr = 0;
    g_ready = true;
    serial_puts("[xhci] ==== init OK ====\n\n");
    return true;
}

bool is_present() { return g_ready; }
bool poll()       { return false; }


// AddressDevice BSR=0
static bool do_set_address(uint8_t new_addr, uint16_t mps) {
    (void)new_addr;


    memset(g_input_ctx, 0, 33 * g_ctx_stride);

    setup_slot_ctx(ictx(IC_SLOT), g_speed, g_port, 1);

    uint64_t ep0_deq = g_ep0_ring_phys +
                       (uint64_t)g_ep0_enq * sizeof(Trb);
    ep0_deq = (ep0_deq & ~0xFull) |
              (g_ep0_cycle ? EP2_DCS : 0);
    setup_ep0_ctx(ictx(IC_EP0), mps, ep0_deq);

    ictx_wr32(IC_CTRL, 0, 0);
    ictx_wr32(IC_CTRL, 1, ICTRL_ADD_SLOT | ICTRL_ADD_EP0);

    serial_puts("[xhci] AddressDevice(BSR=0) input slot: ");
    serial_hex32(ictx_rd32(IC_SLOT, 0)); serial_putc(' ');
    serial_hex32(ictx_rd32(IC_SLOT, 1)); serial_putc(' ');
    serial_hex32(ictx_rd32(IC_SLOT, 2)); serial_putc(' ');
    serial_hex32(ictx_rd32(IC_SLOT, 3)); serial_puts("\n");

    serial_puts("[xhci] AddressDevice(BSR=0) input ep0: ");
    serial_hex32(ictx_rd32(IC_EP0, 0)); serial_putc(' ');
    serial_hex32(ictx_rd32(IC_EP0, 1)); serial_putc(' ');
    serial_hex32(ictx_rd32(IC_EP0, 2)); serial_putc(' ');
    serial_hex32(ictx_rd32(IC_EP0, 3)); serial_puts("\n");

    memory_barrier();
    if (!cmd_address_device(false, g_slot)) {
        serial_puts("[xhci] AddressDevice(BSR=0) failed\n");
        return false;
    }

    uint32_t dw3 = dctx_rd32(DC_SLOT, 3);
    g_device_addr = SLOT3_ADDR_GET(dw3);
    g_ep0_mps = mps;

    serial_puts("[xhci] AddressDevice(BSR=0) OK, device_addr=");
    serial_dec(g_device_addr);
    serial_puts("\n");
	delay_us(10000);
    return true;
}

//  Control transfer (EP0)
bool control(uint8_t addr, uint16_t mps,
             const uint8_t setup[8],
             void* data, uint16_t len) {
    if (!g_ready || !setup) return false;

    if (setup[0] == 0x00 && setup[1] == 0x05) {
        return do_set_address(setup[2], g_ep0_mps);
    }

    (void)addr; (void)mps;

    uint8_t bmRequestType = setup[0];
    bool dir_in = (bmRequestType & 0x80) != 0;
    bool has_data = (len > 0);

    if (has_data && !dir_in) {
        if (!data) return false;
        if (len > 4096) return false;
        memcpy(g_ctrl_buf, data, len);
    }

    uint64_t setup_val = 0;
    for (int i = 0; i < 8; i++) setup_val |= ((uint64_t)setup[i]) << (i * 8);

    uint32_t trt = TRB_TRT_OUT;
    if (len == 0) trt = 0;
    else if (dir_in) trt = TRB_TRT_IN;

    uint32_t setup_flags = TRB_TYPE(TRB_SETUP) | TRB_IDT | trt;

    ring_enqueue(g_ep0_ring, g_ep0_ring_phys, &g_ep0_enq, &g_ep0_cycle,
                 setup_val, TRB_LEN(8) | TRB_IRQ(0), setup_flags);

    if (has_data) {
        uint32_t data_flags = TRB_TYPE(TRB_DATA);
        if (dir_in) data_flags |= TRB_DIR_IN;
        ring_enqueue(g_ep0_ring, g_ep0_ring_phys, &g_ep0_enq, &g_ep0_cycle,
                     g_ctrl_buf_phys, TRB_LEN(len) | TRB_IRQ(0), data_flags);
    }

    uint32_t status_flags = TRB_TYPE(TRB_STATUS) | TRB_IOC;
    if (len == 0 || !dir_in) status_flags |= TRB_DIR_IN;

    ring_enqueue(g_ep0_ring, g_ep0_ring_phys, &g_ep0_enq, &g_ep0_cycle,
                 0, TRB_IRQ(0), status_flags);

    memory_barrier();
    doorbell(g_slot, 1);

    Trb ev;
    bool got = false;
    for (uint32_t t = 0; t < TMO_XFER_US; t += 1000) {
        if (!poll_event(&ev, 1000)) continue;
        uint32_t type = TRB_TYPE_GET(ev.flags);
        if (type == TRB_TRANSFER) {
            uint32_t slot = TRB_SLOT_GET(ev.flags);
            uint32_t ep   = TRB_EP_GET(ev.flags);
            if (slot == g_slot && ep == 1) { got = true; break; }
        }
    }
    if (!got) {
        serial_puts("[xhci] control xfer timeout\n");
        return false;
    }

    uint32_t comp = TRB_COMP_GET(ev.status);
    if (comp != COMP_SUCCESS && comp != COMP_SHORT_PACKET) {
        serial_puts("[xhci] control comp="); serial_dec(comp);
        serial_puts(" setup=");
        for (int i = 0; i < 8; i++) {
            serial_putc(' '); serial_hex8(setup[i]);
        }
        serial_puts(" trb="); serial_hex64(ev.param);
        serial_puts(" ep_state="); serial_dec(dctx_rd32(DC_EP0, 0) & 0x7);
        serial_puts("\n");
        return false;
    }

    if (has_data && dir_in && data) {
        uint32_t remaining = ev.status & 0xFFFFFF;
        uint32_t xfer_len;
        if (comp == COMP_SUCCESS) xfer_len = len;
        else xfer_len = (remaining < len) ? (len - remaining) : 0;
        memcpy(data, g_ctrl_buf, xfer_len);

        if (setup[1] == 0x06 && setup[2] == 0x00 && setup[3] == 0x01 &&
            len >= 8 && g_ctrl_buf[7] != 0 && g_ctrl_buf[7] != g_ep0_mps) {
            if (!update_ep0_mps(g_ctrl_buf[7])) {
                serial_puts("[xhci] EvaluateContext(EP0 MPS) failed\n");
                return false;
            }
        }
    }
    return true;
}

//  Interrupt endpoint
bool intr_open(uint8_t addr, uint8_t ep, uint16_t mps, uint8_t interval) {
    (void)addr;
    if (!g_ready) return false;
    if (ep == 0 || ep > 15) return false;

    uint32_t dci = ep * 2 + 1;
    if (mps == 0 || mps > 1024) return false;

    g_intr_dci = dci;
    g_intr_mps = mps;

    g_intr_ring = (Trb*)dma::alloc(RING_TRBS * sizeof(Trb), 64);
    if (!g_intr_ring) return false;
    g_intr_ring_phys = dma::map(g_intr_ring);
    ring_init(g_intr_ring, g_intr_ring_phys);
    g_intr_enq = 0;
    g_intr_cycle = 1;

    g_intr_buf = (uint8_t*)dma::alloc(mps, 64);
    if (!g_intr_buf) return false;
    memset(g_intr_buf, 0, mps);
    g_intr_buf_phys = dma::map(g_intr_buf);

    ictx_wr32(IC_CTRL, 0, 0);
    ictx_wr32(IC_CTRL, 1, ICTRL_ADD_SLOT | (1u << dci));

    uint32_t dw0 = dctx_rd32(DC_SLOT, 0);
    dw0 &= ~(0x1Fu << 27);
    dw0 |= SLOT0_ENTRIES(dci);
    ictx_wr32(IC_SLOT, 0, dw0);
    ictx_wr32(IC_SLOT, 1, dctx_rd32(DC_SLOT, 1));
    ictx_wr32(IC_SLOT, 2, dctx_rd32(DC_SLOT, 2));
    ictx_wr32(IC_SLOT, 3, dctx_rd32(DC_SLOT, 3));

    uint8_t xhci_interval = calc_interval(g_speed, interval);
    uint32_t ep_index = IC_EPn(dci);
    memset(ictx(ep_index), 0, g_ctx_stride);
    setup_intr_ep_ctx(ictx(ep_index), mps, xhci_interval,
                      EP_TYPE_INT_IN, g_intr_ring_phys);

    if (!cmd_configure_endpoint(g_slot)) {
        serial_puts("[xhci] ConfigureEndpoint(intr) failed\n");
        return false;
    }

    uint32_t ep_state = dctx_rd32(dci, 0) & 0x7;
    serial_puts("[xhci] intr EP configured, dci="); serial_dec(dci);
    serial_puts(" interval="); serial_dec(xhci_interval);
    serial_puts(" state="); serial_dec(ep_state);
    serial_puts("\n");

    ring_enqueue(g_intr_ring, g_intr_ring_phys, &g_intr_enq, &g_intr_cycle,
                 g_intr_buf_phys, TRB_LEN(mps) | TRB_IRQ(0),
                 TRB_TYPE(TRB_NORMAL) | TRB_IOC);
    memory_barrier();
    doorbell(g_slot, dci);

    g_intr_open = true;
    return true;
}

void intr_close() {
    if (!g_intr_open) return;
    g_intr_open = false;
}

bool intr_read(void* out, uint16_t len) {
    if (!g_intr_open || !out) return false;

    Trb ev;
    if (!poll_event(&ev, 2000)) return false;

    uint32_t type = TRB_TYPE_GET(ev.flags);
    if (type != TRB_TRANSFER) return false;
    uint32_t slot = TRB_SLOT_GET(ev.flags);
    uint32_t ep   = TRB_EP_GET(ev.flags);
    if (slot != g_slot || ep != g_intr_dci) return false;

    uint32_t comp = TRB_COMP_GET(ev.status);
    if (comp != COMP_SUCCESS && comp != COMP_SHORT_PACKET) {
        serial_puts("[xhci] intr comp="); serial_dec(comp); serial_puts("\n");
        return false;
    }

    uint32_t remaining = TRB_REM_GET(ev.status);
    uint32_t xfer_len;
    if (comp == COMP_SUCCESS) xfer_len = g_intr_mps;
    else xfer_len = (remaining < g_intr_mps) ? (g_intr_mps - remaining) : 0;
    if (xfer_len > len) xfer_len = len;

    memcpy(out, g_intr_buf, xfer_len);

    memset(g_intr_buf, 0, g_intr_mps);
    ring_enqueue(g_intr_ring, g_intr_ring_phys, &g_intr_enq, &g_intr_cycle,
                 g_intr_buf_phys, TRB_LEN(g_intr_mps) | TRB_IRQ(0),
                 TRB_TYPE(TRB_NORMAL) | TRB_IOC);
    memory_barrier();
    doorbell(g_slot, g_intr_dci);

    return true;
}


//  Stop
void stop() {
    if (!g_ready) return;
    g_intr_open = false;
    op_wr32(OP_USBCMD, 0);
    for (int i = 0; i < 100000; i++) {
        if (op_rd32(OP_USBCMD) & CMD_RUN) delay_us(1);
        else break;
    }
    g_ready = false;
}

}