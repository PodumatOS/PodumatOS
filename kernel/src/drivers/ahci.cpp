// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Thinking Developer
//
// PodumatOS — a hobby operating system for x86_64.


#include "drivers/ahci.hpp"
#include "drivers/disk.hpp"
#include "drivers/dma.hpp"
#include "drivers/pci.hpp"
#include "lib/memory.hpp"
#include "io/io.hpp"

// ahci registers
#define AHCI_CAP        0x00
#define AHCI_GHC        0x04
#define AHCI_IS         0x08
#define AHCI_PI         0x0C
#define AHCI_VS         0x10
#define AHCI_BOHC       0x28

#define AHCI_GHC_HR     (1u << 0)
#define AHCI_GHC_IE     (1u << 1)
#define AHCI_GHC_AE     (1u << 31)

#define HBA_PORT_BASE   0x100
#define HBA_PORT_SIZE   0x80

#define P_CLB           0x00
#define P_CLBU          0x04
#define P_FB            0x08
#define P_FBU           0x0C
#define P_IS            0x10
#define P_IE            0x14
#define P_CMD           0x18
#define P_TFD           0x20
#define P_SIG           0x24
#define P_SSTS          0x28
#define P_SCTL          0x2C
#define P_SERR          0x30
#define P_SACT          0x34
#define P_CI            0x38
#define P_SNTF          0x3C

#define P_CMD_ST        (1u << 0)
#define P_CMD_SUD       (1u << 1)
#define P_CMD_POD       (1u << 2)
#define P_CMD_FRE       (1u << 4)
#define P_CMD_FR        (1u << 14)
#define P_CMD_CR        (1u << 15)

#define TFD_BSY         (1u << 7)
#define TFD_DRQ         (1u << 3)
#define TFD_ERR         (1u << 0)

#define HBA_PORT_DET_PRESENT 0x3
#define HBA_PORT_IPM_ACTIVE  0x1

#define SATA_SIG_ATA    0x00000101

#define FIS_TYPE_REG_H2D 0x27

#define ATA_CMD_IDENTIFY         0xEC
#define ATA_CMD_READ_DMA_EXT     0x25
#define ATA_CMD_WRITE_DMA_EXT    0x35
#define ATA_CMD_FLUSH_CACHE_EXT  0xEA

#define PORT_STOP_TIMEOUT    1000000
#define PORT_START_TIMEOUT   1000000
#define CMD_WAIT_TIMEOUT     10000000
#define XFER_WAIT_TIMEOUT    100000000

#define MMIO_MAP_SIZE        0x4000
#define PAGE_SIZE            0x1000
#define PAGE_MASK            (~0xFFFull)
#define ADDR_MASK            0x000FFFFFFFFFF000ull

struct HBA_CMD_HEADER {
    uint16_t opts;
    uint16_t prdtl;
    volatile uint32_t prdbc;
    uint32_t ctba;
    uint32_t ctbau;
    uint32_t rsv1[4];
} __attribute__((packed));

struct HBA_PRDT_ENTRY {
    uint32_t dba;
    uint32_t dbau;
    uint32_t rsv0;
    uint32_t dbc:22;
    uint32_t rsv1:9;
    uint32_t i:1;
} __attribute__((packed));

struct HBA_CMD_TBL {
    uint8_t        cfis[64];
    uint8_t        acmd[16];
    uint8_t        rsv[48];
    HBA_PRDT_ENTRY prdt_entry[1];
} __attribute__((packed));

struct FIS_REG_H2D {
    uint8_t  fis_type;
    uint8_t  pmport:4;
    uint8_t  rsv0:3;
    uint8_t  c:1;
    uint8_t  command;
    uint8_t  featurel;
    uint8_t  lba0;
    uint8_t  lba1;
    uint8_t  lba2;
    uint8_t  device;
    uint8_t  lba3;
    uint8_t  lba4;
    uint8_t  lba5;
    uint8_t  featureh;
    uint8_t  countl;
    uint8_t  counth;
    uint8_t  icc;
    uint8_t  control;
    uint8_t  rsv1[4];
} __attribute__((packed));

namespace ahci {

    static volatile uint8_t* g_abar      = nullptr;
    static uint64_t          g_hhdm      = 0;
    static uint64_t          g_abar_phys = 0;
    static int               g_port      = -1;
    static bool              g_ready     = false;

    static HBA_CMD_HEADER* g_cmd_list = nullptr;
    static uint8_t*        g_fis      = nullptr;
    static HBA_CMD_TBL*    g_cmd_tbl  = nullptr;
    static uint8_t*        g_buf      = nullptr;

    //com1
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
    static void serial_hex64(uint64_t v) {
        static const char hex[] = "0123456789ABCDEF";
        serial_puts("0x");
        for (int i = 60; i >= 0; i -= 4) serial_putc(hex[(v >> i) & 0xF]);
    }

    // vmm
    static uint64_t  g_cr3        = 0;
    static uint64_t  g_cr3_orig   = 0;
    static uint64_t* g_pml4       = nullptr;

    static void vmm_init(uint64_t hhdm) {
        asm volatile("mov %%cr3, %0" : "=r"(g_cr3_orig));
        g_cr3  = g_cr3_orig & PAGE_MASK;
        g_pml4 = (uint64_t*)(g_cr3 + hhdm);
    }

    static void vmm_flush_tlb() {
        asm volatile("mov %0, %%cr3" :: "r"(g_cr3_orig) : "memory");
    }

    static uint64_t* next_table(uint64_t* table, int idx) {
        uint64_t e = table[idx];
        if (e & 1) {
            if (e & 0x80) return nullptr; // huge page
            return (uint64_t*)((e & ADDR_MASK) + g_hhdm);
        }
        void* t = dma::alloc(PAGE_SIZE, PAGE_SIZE);
        if (!t) return nullptr;
        memset(t, 0, PAGE_SIZE);
        uint64_t phys = dma::map(t);
        table[idx] = (phys & ADDR_MASK) | 0x03; // P | RW
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

        pt[i1] = (phys & ADDR_MASK) | 0x13; // P | RW | PCD
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

    // mmio
    static inline uint32_t mmio_read(uint32_t off) {
        return *(volatile uint32_t*)((uintptr_t)g_abar + off);
    }
    static inline void mmio_write(uint32_t off, uint32_t v) {
        *(volatile uint32_t*)((uintptr_t)g_abar + off) = v;
    }
    static inline uint32_t port_read(int p, uint32_t off) {
        return mmio_read(HBA_PORT_BASE + (uint32_t)p * HBA_PORT_SIZE + off);
    }
    static inline void port_write(int p, uint32_t off, uint32_t v) {
        mmio_write(HBA_PORT_BASE + (uint32_t)p * HBA_PORT_SIZE + off, v);
    }
    static inline void memory_barrier() {
        asm volatile("mfence" ::: "memory");
    }

	// port control
    static void port_stop(int p) {
        uint32_t cmd = port_read(p, P_CMD);
        cmd &= ~P_CMD_ST;
        port_write(p, P_CMD, cmd);

        for (int i = 0; i < PORT_STOP_TIMEOUT; i++) {
            if (!(port_read(p, P_CMD) & P_CMD_CR)) break;
        }

        cmd = port_read(p, P_CMD);
        cmd &= ~P_CMD_FRE;
        port_write(p, P_CMD, cmd);

        for (int i = 0; i < PORT_STOP_TIMEOUT; i++) {
            if (!(port_read(p, P_CMD) & P_CMD_FR)) break;
        }
    }

    static void port_start(int p) {
        for (int i = 0; i < PORT_START_TIMEOUT; i++) {
            if (!(port_read(p, P_CMD) & P_CMD_CR)) break;
        }

        uint32_t cmd = port_read(p, P_CMD);
        cmd |= P_CMD_FRE;
        port_write(p, P_CMD, cmd);

        cmd = port_read(p, P_CMD);
        cmd |= P_CMD_ST | P_CMD_SUD | P_CMD_POD;
        port_write(p, P_CMD, cmd);
    }

    static bool wait_port_ready(int p) {
        for (int i = 0; i < PORT_START_TIMEOUT; i++) {
            uint32_t cmd = port_read(p, P_CMD);
            if ((cmd & P_CMD_FR) && (cmd & P_CMD_CR)) return true;
        }
        return false;
    }

    static bool wait_not_busy(int p) {
        for (int i = 0; i < CMD_WAIT_TIMEOUT; i++) {
            uint32_t tfd = port_read(p, P_TFD);
            if (!(tfd & (TFD_BSY | TFD_DRQ))) return true;
        }
        return false;
    }

    static int find_port() {
        uint32_t pi = mmio_read(AHCI_PI);
        serial_puts("[ahci] PI="); serial_hex64(pi); serial_puts("\n");
        for (int i = 0; i < 32; i++) {
            if (!(pi & (1u << i))) continue;
            uint32_t ssts = port_read(i, P_SSTS);
            uint8_t det = ssts & 0x0F;
            uint8_t ipm = (ssts >> 8) & 0x0F;
            if (det != HBA_PORT_DET_PRESENT) continue;
            if (ipm != HBA_PORT_IPM_ACTIVE)  continue;
            uint32_t sig = port_read(i, P_SIG);
            if (sig != SATA_SIG_ATA) continue;
            return i;
        }
        return -1;
    }


    //  port init
    static bool init_port(int p) {
        serial_puts("[ahci] init_port starting\n");
        port_stop(p);

        port_write(p, P_SERR, 0xFFFFFFFF);
        port_write(p, P_IS,   0xFFFFFFFF);
        port_write(p, P_IE,   0x00000000);

        g_cmd_list = (HBA_CMD_HEADER*)dma::alloc(1024, 1024);
        g_fis      = (uint8_t*)        dma::alloc(256,  256);
        g_cmd_tbl  = (HBA_CMD_TBL*)    dma::alloc(4096, 128);
        g_buf      = (uint8_t*)        dma::alloc(4096, 4096);

        if (!g_cmd_list || !g_fis || !g_cmd_tbl || !g_buf) {
            serial_puts("[ahci] dma::alloc FAILED\n");
            return false;
        }

        memset(g_cmd_list, 0, 1024);
        memset(g_fis,      0, 256);
        memset(g_cmd_tbl,  0, 4096);
        memset(g_buf,      0, 4096);

        uint64_t clb_phys = dma::map(g_cmd_list);
        uint64_t fb_phys  = dma::map(g_fis);

        port_write(p, P_CLB,  (uint32_t)(clb_phys & 0xFFFFFFFF));
        port_write(p, P_CLBU, (uint32_t)(clb_phys >> 32));
        port_write(p, P_FB,   (uint32_t)(fb_phys & 0xFFFFFFFF));
        port_write(p, P_FBU,  (uint32_t)(fb_phys >> 32));

        port_start(p);
        if (!wait_port_ready(p)) {
            serial_puts("[ahci] wait_port_ready TIMEOUT\n");
            return false;
        }
        serial_puts("[ahci] init_port ok\n");
        return true;
    }

    //  transfer
    static bool ahci_transfer(int p, const uint8_t* fis, uint64_t buf_phys,
                              uint32_t buf_size, bool write) {
        if (!wait_not_busy(p)) return false;

        bool slot_free = false;
        for (int i = 0; i < CMD_WAIT_TIMEOUT; i++) {
            if (!(port_read(p, P_CI) & 1u)) { slot_free = true; break; }
        }
        if (!slot_free) return false;

        HBA_CMD_HEADER* hdr = &g_cmd_list[0];
        memset(hdr, 0, sizeof(HBA_CMD_HEADER));

        uint16_t opts = (uint16_t)(sizeof(FIS_REG_H2D) / 4);
        if (write) opts |= (1u << 6);
        hdr->opts  = opts;
        hdr->prdtl = (buf_size > 0) ? 1 : 0;
        hdr->prdbc = 0;

        uint64_t ctba = dma::map(g_cmd_tbl);
        hdr->ctba  = (uint32_t)(ctba & 0xFFFFFFFF);
        hdr->ctbau = (uint32_t)(ctba >> 32);

        memset(g_cmd_tbl->cfis, 0, sizeof(g_cmd_tbl->cfis));
        memcpy(g_cmd_tbl->cfis, fis, sizeof(FIS_REG_H2D));

        if (buf_size > 0) {
            g_cmd_tbl->prdt_entry[0].dba  = (uint32_t)(buf_phys & 0xFFFFFFFF);
            g_cmd_tbl->prdt_entry[0].dbau = (uint32_t)(buf_phys >> 32);
            g_cmd_tbl->prdt_entry[0].rsv0 = 0;
            g_cmd_tbl->prdt_entry[0].dbc  = (buf_size - 1) & 0x3FFFFF;
            g_cmd_tbl->prdt_entry[0].rsv1 = 0;
            g_cmd_tbl->prdt_entry[0].i    = 1;
        }

        memory_barrier();
        port_write(p, P_CI, 1u);

        for (int i = 0; i < XFER_WAIT_TIMEOUT; i++) {
            uint32_t ci  = port_read(p, P_CI);
            uint32_t tfd = port_read(p, P_TFD);
            if (tfd & TFD_ERR) return false;
            if (!(ci & 1u)) { memory_barrier(); return true; }
        }
        return false;
    }

    static void build_rw_fis(uint8_t* fis, uint8_t cmd, uint64_t lba, uint16_t count) {
        memset(fis, 0, sizeof(FIS_REG_H2D));
        FIS_REG_H2D* f = (FIS_REG_H2D*)fis;
        f->fis_type = FIS_TYPE_REG_H2D;
        f->c        = 1;
        f->command  = cmd;
        f->lba0     = (uint8_t)(lba & 0xFF);
        f->lba1     = (uint8_t)((lba >> 8) & 0xFF);
        f->lba2     = (uint8_t)((lba >> 16) & 0xFF);
        f->device   = 0x40;
        f->lba3     = (uint8_t)((lba >> 24) & 0xFF);
        f->lba4     = (uint8_t)((lba >> 32) & 0xFF);
        f->lba5     = (uint8_t)((lba >> 40) & 0xFF);
        f->countl   = (uint8_t)(count & 0xFF);
        f->counth   = (uint8_t)((count >> 8) & 0xFF);
    }

    static bool identify(int p) {
        uint8_t fis[sizeof(FIS_REG_H2D)];
        memset(fis, 0, sizeof(fis));
        FIS_REG_H2D* f = (FIS_REG_H2D*)fis;
        f->fis_type = FIS_TYPE_REG_H2D;
        f->c        = 1;
        f->command  = ATA_CMD_IDENTIFY;
        f->device   = 0;

        uint64_t buf_phys = dma::map(g_buf);
        if (!ahci_transfer(p, fis, buf_phys, 512, false)) {
            serial_puts("[ahci] identify transfer failed\n");
            return false;
        }

        uint16_t* id = (uint16_t*)g_buf;

        for (int i = 0; i < 20; i++) {
            uint16_t w = id[27 + i];
            disk::info.model[i * 2]     = (char)((w >> 8) & 0xFF);
            disk::info.model[i * 2 + 1] = (char)(w & 0xFF);
        }
        disk::info.model[40] = '\0';
        for (int i = 39; i >= 0 && disk::info.model[i] == ' '; i--) disk::info.model[i] = '\0';

        for (int i = 0; i < 10; i++) {
            uint16_t w = id[10 + i];
            disk::info.serial[i * 2]     = (char)((w >> 8) & 0xFF);
            disk::info.serial[i * 2 + 1] = (char)(w & 0xFF);
        }
        disk::info.serial[20] = '\0';
        for (int i = 19; i >= 0 && disk::info.serial[i] == ' '; i--) disk::info.serial[i] = '\0';

        for (int i = 0; i < 4; i++) {
            uint16_t w = id[23 + i];
            disk::info.firmware[i * 2]     = (char)((w >> 8) & 0xFF);
            disk::info.firmware[i * 2 + 1] = (char)(w & 0xFF);
        }
        disk::info.firmware[8] = '\0';

        disk::info.sectors = id[60] | ((uint32_t)id[61] << 16);
        disk::info.lba48 = (id[83] & (1u << 10)) != 0;
        if (disk::info.lba48) {
            disk::info.sectors48 =
                (uint64_t)id[100] |
                ((uint64_t)id[101] << 16) |
                ((uint64_t)id[102] << 32) |
                ((uint64_t)id[103] << 48);
        } else {
            disk::info.sectors48 = disk::info.sectors;
        }

        disk::info.present = true;
        serial_puts("[ahci] identify ok\n");
        return true;
    }

    //  public init
    bool init(uint64_t hhdm_offset) {
        g_hhdm  = hhdm_offset;
        g_abar  = nullptr;
        g_port  = -1;
        g_ready = false;

        serial_init();
        serial_puts("\n[ahci] ==== init ====\n");

        vmm_init(hhdm_offset);
        serial_puts("[ahci] vmm_init ok\n");

        pci::Device* dev = nullptr;
        int n = pci::get_device_count();
        for (int i = 0; i < n; i++) {
            pci::Device* d = pci::get_device(i);
            if (!d) continue;
            if (d->class_code == 0x01 && d->subclass == 0x06 && d->prog_if == 0x01) {
                dev = d;
                break;
            }
        }
        if (!dev) {
            serial_puts("[ahci] PCI device not found\n");
            return false;
        }
        serial_puts("[ahci] PCI dev found\n");

        uint32_t bar5 = dev->bar[5];
        if (bar5 & 0x1) {
            serial_puts("[ahci] BAR5 is I/O space\n");
            return false;
        }
        uint64_t abar_phys = (uint64_t)(bar5 & ~0xFULL);
        if (bar5 & 0x4) {
            abar_phys |= ((uint64_t)dev->bar[4] << 32);
        }
        if (!abar_phys) {
            serial_puts("[ahci] ABAR==0\n");
            return false;
        }
        g_abar_phys = abar_phys;

        serial_puts("[ahci] abar_phys="); serial_hex64(abar_phys);
        serial_puts(" hhdm=");           serial_hex64(hhdm_offset);
        serial_puts("\n");

        if (!map_mmio(abar_phys, MMIO_MAP_SIZE)) {
            serial_puts("[ahci] map_mmio FAILED\n");
            return false;
        }
        serial_puts("[ahci] map_mmio ok\n");

        g_abar = (volatile uint8_t*)(abar_phys + hhdm_offset);

        uint32_t cap = mmio_read(AHCI_CAP);
        uint32_t vs  = mmio_read(AHCI_VS);
        serial_puts("[ahci] CAP="); serial_hex64(cap);
        serial_puts(" VS=");        serial_hex64(vs);
        serial_puts("\n");

        if (cap == 0xFFFFFFFF || vs == 0xFFFFFFFF) {
            serial_puts("[ahci] MMIO reads 0xFFFFFFFF\n");
            return false;
        }

        uint32_t ghc = mmio_read(AHCI_GHC);
        mmio_write(AHCI_GHC, ghc | AHCI_GHC_AE);
        uint32_t ghc_after = mmio_read(AHCI_GHC);
        if (!(ghc_after & AHCI_GHC_AE)) {
            serial_puts("[ahci] AE bit not set\n");
            return false;
        }
        serial_puts("[ahci] AE enabled\n");

        int p = find_port();
        if (p < 0) {
            serial_puts("[ahci] no SATA port with disk\n");
            return false;
        }
        serial_puts("[ahci] using port "); serial_hex64(p); serial_puts("\n");

        if (!init_port(p)) {
            serial_puts("[ahci] init_port failed\n");
            return false;
        }
        g_port = p;

        if (!identify(p)) {
            serial_puts("[ahci] identify failed\n");
            g_port = -1;
            return false;
        }

        g_ready = true;
        serial_puts("[ahci] ==== init OK ====\n\n");
        return true;
    }

    bool is_present() { return g_ready && g_port >= 0; }

    bool read_sector(uint32_t lba, uint8_t* buffer) {
        if (!is_present() || !buffer) return false;
        uint8_t fis[sizeof(FIS_REG_H2D)];
        build_rw_fis(fis, ATA_CMD_READ_DMA_EXT, lba, 1);
        uint64_t buf_phys = dma::map(g_buf);
        if (!ahci_transfer(g_port, fis, buf_phys, 512, false)) return false;
        memcpy(buffer, g_buf, 512);
        return true;
    }

    bool write_sector(uint32_t lba, const uint8_t* buffer) {
        if (!is_present() || !buffer) return false;
        memcpy(g_buf, buffer, 512);
        uint8_t fis[sizeof(FIS_REG_H2D)];
        build_rw_fis(fis, ATA_CMD_WRITE_DMA_EXT, lba, 1);
        uint64_t buf_phys = dma::map(g_buf);
        return ahci_transfer(g_port, fis, buf_phys, 512, true);
    }

    bool read_sectors(uint32_t lba, uint8_t count, uint8_t* buffer) {
        if (!is_present() || !buffer || count == 0) return false;
        for (uint8_t i = 0; i < count; i++) {
            if (!read_sector(lba + i, buffer + (uint32_t)i * 512)) return false;
        }
        return true;
    }

    bool write_sectors(uint32_t lba, uint8_t count, const uint8_t* buffer) {
        if (!is_present() || !buffer || count == 0) return false;
        for (uint8_t i = 0; i < count; i++) {
            if (!write_sector(lba + i, buffer + (uint32_t)i * 512)) return false;
        }
        return true;
    }

    bool flush() {
        if (!is_present()) return false;
        uint8_t fis[sizeof(FIS_REG_H2D)];
        memset(fis, 0, sizeof(fis));
        FIS_REG_H2D* f = (FIS_REG_H2D*)fis;
        f->fis_type = FIS_TYPE_REG_H2D;
        f->c        = 1;
        f->command  = ATA_CMD_FLUSH_CACHE_EXT;
        return ahci_transfer(g_port, fis, 0, 0, true);
    }
	
	bool stop() {
        if (!g_ready || g_port < 0) return true;
        port_stop(g_port);
        g_port  = -1;
        g_ready = false;
        return true;
    }

}
