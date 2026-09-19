// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Thinking Developer
//
// PodumatOS — a hobby operating system for x86_64.

#include "shell/commands.hpp"
#include "console/console.hpp"
#include "lib/string.hpp"
#include "io/io.hpp"
#include "kernel/pit.hpp"
#include "kernel/pic.hpp"
#include "kernel/idt.hpp"
#include "drivers/pci.hpp"
#include "drivers/disk.hpp"
#include "drivers/dma.hpp"
#include "drivers/ahci.hpp"
#include "drivers/ata.hpp"
#include "drivers/input.hpp"
#include "drivers/usb/ehci.hpp"
#include "drivers/usb/usb.hpp"
#include "drivers/usb/usb_hid.hpp"
#include "drivers/ps2.hpp"
#include "fs/mbr.hpp"
#include "fs/fat32.hpp"
#include "lib/version.hpp"
#include "lib/dmi.hpp"
#include "lib/driver_registry.hpp"

// color
static constexpr std::uint32_t COLOR_BLACK  = 0x000000;
static constexpr std::uint32_t COLOR_GRAY   = 0xC0C0C0;
static constexpr std::uint32_t COLOR_WHITE  = 0xFFFFFF;
static constexpr std::uint32_t COLOR_GREEN  = 0x00CC00;
static constexpr std::uint32_t COLOR_RED    = 0xCC0000;
static constexpr std::uint32_t COLOR_YELLOW = 0xCCCC00;
static constexpr std::uint32_t COLOR_CYAN   = 0x00CCCC;
static constexpr std::uint32_t COLOR_BLUE   = 0x0088FF;
static constexpr std::uint32_t COLOR_MAGENTA= 0xFF00FF;

static void puts_color(const char* s, std::uint32_t color) {
    console::set_color(color, COLOR_BLACK);
    console::puts(s);
    console::set_color(COLOR_GRAY, COLOR_BLACK);
}

static int simple_atoi(const char* s) {
    int result = 0, sign = 1;
    if (*s == '-') { sign = -1; s++; }
    while (*s >= '0' && *s <= '9') { result = result * 10 + (*s - '0'); s++; }
    return result * sign;
}

static void print_int(int n) {
    if (n < 0) { console::putc('-'); n = -n; }
    char buf[16]; int i = 0;
    if (n == 0) { console::putc('0'); return; }
    while (n > 0) { buf[i++] = '0' + (n % 10); n /= 10; }
    for (int j = i - 1; j >= 0; j--) console::putc(buf[j]);
}

static void print_u64(std::uint64_t v) {
    if (v == 0) { console::putc('0'); return; }
    char buf[24]; int i = 0;
    while (v > 0) { buf[i++] = '0' + (v % 10); v /= 10; }
    for (int j = i - 1; j >= 0; j--) console::putc(buf[j]);
}

static void serial_write(const char* s) {
    while (*s) {
        if (*s == '\n') outb(0x3F8, '\r');
        while ((inb(0x3F8 + 5) & 0x20) == 0) { }
        outb(0x3F8, (std::uint8_t)*s++);
    }
}
static void serial_write_hex64(std::uint64_t v) {
    static const char hex[] = "0123456789ABCDEF";
    serial_write("0x");
    for (int i = 60; i >= 0; i -= 4) {
        char c = hex[(v >> i) & 0xF];
        outb(0x3F8, (std::uint8_t)c);
    }
}

static void teardown_ok(const char* name) {
    console::set_color(COLOR_GREEN, COLOR_BLACK);
    console::puts(" [ OK ] ");
    console::set_color(COLOR_GRAY, COLOR_BLACK);
    console::puts(name);
    console::putc('\n');
    serial_write("[teardown] OK: ");
    serial_write(name);
    serial_write("\n");
}

static void teardown_fail(const char* name) {
    console::set_color(COLOR_RED, COLOR_BLACK);
    console::puts(" [FAIL] ");
    console::set_color(COLOR_GRAY, COLOR_BLACK);
    console::puts(name);
    console::putc('\n');
    serial_write("[teardown] FAIL: ");
    serial_write(name);
    serial_write("\n");
}

static void teardown_info(const char* name) {
    console::set_color(COLOR_CYAN, COLOR_BLACK);
    console::puts(" [INFO] ");
    console::set_color(COLOR_GRAY, COLOR_BLACK);
    console::puts(name);
    console::putc('\n');
    serial_write("[teardown] INFO: ");
    serial_write(name);
    serial_write("\n");
}

// cpuid
static inline void cpuid(std::uint32_t leaf, std::uint32_t subleaf,
                          std::uint32_t* eax, std::uint32_t* ebx,
                          std::uint32_t* ecx, std::uint32_t* edx) {
    asm volatile("cpuid"
        : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
        : "a"(leaf), "c"(subleaf));
}

namespace acpi {

    constexpr std::uint32_t SIG_FADT = 0x50434146;  // FACP
    constexpr std::uint32_t SIG_XSDT = 0x54445358;  // XSDT
    constexpr std::uint32_t SIG_RSDT = 0x54445352;  // RSDT
    constexpr std::uint32_t SIG_DSDT = 0x54445344;  // DSDT

    constexpr std::uint16_t SLP_EN = (1u << 13);
    constexpr std::uint32_t MAX_SDT_LEN = 0x100000;

    static bool          g_ready = false;
    static std::uint16_t g_pm1a_cnt = 0;
    static std::uint16_t g_pm1b_cnt = 0;
    static std::uint16_t g_slp_typa = 0;
    static std::uint16_t g_slp_typb = 0;

    struct RSDP {
        char     signature[8];
        std::uint8_t checksum;
        char     oem_id[6];
        std::uint8_t revision;
        std::uint32_t rsdt_addr;
        std::uint32_t length;
        std::uint64_t xsdt_addr;
        std::uint8_t ext_checksum;
        std::uint8_t reserved[3];
    } __attribute__((packed));

    struct SDTHeader {
        std::uint32_t signature;
        std::uint32_t length;
        std::uint8_t  revision;
        std::uint8_t  checksum;
        char          oem_id[6];
        char          oem_table_id[8];
        std::uint32_t oem_revision;
        std::uint32_t creator_id;
        std::uint32_t creator_revision;
    } __attribute__((packed));

    struct FADT {
        SDTHeader     header;
        std::uint32_t firmware_ctrl;
        std::uint32_t dsdt;
        std::uint8_t  reserved1;
        std::uint8_t  preferred_pm_profile;
        std::uint16_t sci_int;
        std::uint32_t smi_cmd;
        std::uint8_t  acpi_enable;
        std::uint8_t  acpi_disable;
        std::uint8_t  s4bios_req;
        std::uint8_t  pstate_cnt;
        std::uint32_t pm1a_evt_blk;
        std::uint32_t pm1b_evt_blk;
        std::uint32_t pm1a_cnt_blk;
        std::uint32_t pm1b_cnt_blk;

    } __attribute__((packed));

    static std::uint64_t maybe_to_virt(std::uint64_t addr, std::uint64_t hhdm) {
        if (hhdm == 0) return addr;
        if (addr >= hhdm) return addr;
        return addr + hhdm;
    }

    static std::uint64_t phys_to_virt(std::uint64_t phys, std::uint64_t hhdm) {
        return phys + hhdm;
    }

    static bool checksum_ok(const void* table, std::uint32_t len) {
        const std::uint8_t* p = (const std::uint8_t*)table;
        std::uint8_t sum = 0;
        for (std::uint32_t i = 0; i < len; i++) sum = (std::uint8_t)(sum + p[i]);
        return sum == 0;
    }

    static bool find_slp_typ_in_dsdt(std::uint64_t dsdt_phys, std::uint64_t hhdm,
                                     std::uint16_t* out_slp_typa,
                                     std::uint16_t* out_slp_typb) {
        if (!dsdt_phys) return false;

        const std::uint8_t* dsdt = (const std::uint8_t*)phys_to_virt(dsdt_phys, hhdm);
        const SDTHeader* hdr = (const SDTHeader*)dsdt;
        if (hdr->signature != SIG_DSDT) return false;
        if (hdr->length < sizeof(SDTHeader) || hdr->length > MAX_SDT_LEN) return false;

        const std::uint8_t* aml = dsdt + sizeof(SDTHeader);
        std::uint32_t aml_len = hdr->length - sizeof(SDTHeader);

        for (std::uint32_t i = 0; i + 12 < aml_len; i++) {
            if (aml[i]     == 0x08 &&
                aml[i + 1] == '_'  &&
                aml[i + 2] == 'S'  &&
                aml[i + 3] == '5'  &&
                aml[i + 4] == '_')
            {
                std::uint32_t j = i + 5;
                if (j + 8 >= aml_len) break;
                if (aml[j] != 0x12) break;
                j += 2;
                if (aml[j] == 0x0A) {
                    *out_slp_typa = aml[j + 1];
                    j += 2;
                    if (aml[j] == 0x0A) {
                        *out_slp_typb = aml[j + 1];
                    } else {
                        *out_slp_typb = *out_slp_typa;
                    }
                    return true;
                }
            }
        }
        return false;
    }

    bool init(void* rsdp_ptr, std::uint64_t hhdm) {
        g_ready = false;
        if (!rsdp_ptr) {
            serial_write("[acpi] null rsdp\n");
            return false;
        }

        serial_write("[acpi] rsdp raw=");
        serial_write_hex64((std::uint64_t)rsdp_ptr);
        serial_write(" hhdm=");
        serial_write_hex64(hhdm);
        serial_write("\n");

        std::uint64_t rsdp_va = maybe_to_virt((std::uint64_t)rsdp_ptr, hhdm);
        serial_write("[acpi] rsdp va=");
        serial_write_hex64(rsdp_va);
        serial_write("\n");

        const RSDP* rsdp = (const RSDP*)rsdp_va;

        if (!(rsdp->signature[0] == 'R' && rsdp->signature[1] == 'S' &&
              rsdp->signature[2] == 'D' && rsdp->signature[3] == ' ' &&
              rsdp->signature[4] == 'P' && rsdp->signature[5] == 'T' &&
              rsdp->signature[6] == 'R' && rsdp->signature[7] == ' ')) {
            serial_write("[acpi] bad RSDP signature\n");
            return false;
        }

        {
            const std::uint8_t* p = (const std::uint8_t*)rsdp;
            std::uint8_t sum = 0;
            for (int i = 0; i < 20; i++) sum = (std::uint8_t)(sum + p[i]);
            if (sum != 0) {
                serial_write("[acpi] RSDP checksum invalid\n");
                return false;
            }
        }

        bool use_xsdt = (rsdp->revision >= 2 && rsdp->xsdt_addr != 0);
        std::uint64_t sdt_phys = use_xsdt ? rsdp->xsdt_addr : rsdp->rsdt_addr;
        if (sdt_phys == 0) {
            serial_write("[acpi] no RSDT/XSDT\n");
            return false;
        }

        const SDTHeader* sdt = (const SDTHeader*)phys_to_virt(sdt_phys, hhdm);

        if (sdt->length < sizeof(SDTHeader) || sdt->length > MAX_SDT_LEN) {
            serial_write("[acpi] SDT length out of range\n");
            return false;
        }

        if (use_xsdt) {
            if (sdt->signature != SIG_XSDT) {
                serial_write("[acpi] XSDT signature mismatch\n");
                return false;
            }
        } else {
            if (sdt->signature != SIG_RSDT) {
                serial_write("[acpi] RSDT signature mismatch\n");
                return false;
            }
        }

        if (!checksum_ok(sdt, sdt->length)) {
            serial_write("[acpi] SDT checksum invalid\n");
            return false;
        }

        std::uint32_t entries_size = sdt->length - sizeof(SDTHeader);
        std::uint32_t entry_size   = use_xsdt ? 8 : 4;
        std::uint32_t count        = entries_size / entry_size;

        serial_write("[acpi] SDT entries=");
        serial_write_hex64(count);
        serial_write("\n");

        const std::uint8_t* body = (const std::uint8_t*)sdt + sizeof(SDTHeader);

        const FADT* fadt = nullptr;
        for (std::uint32_t i = 0; i < count; i++) {
            std::uint64_t phys;
            if (use_xsdt) {
                phys = *((const std::uint64_t*)(body + i * 8));
            } else {
                phys = (std::uint64_t)(*((const std::uint32_t*)(body + i * 4)));
            }
            if (phys == 0) continue;

            const SDTHeader* hdr = (const SDTHeader*)phys_to_virt(phys, hhdm);
            if (hdr->signature == SIG_FADT) {
                if (hdr->length < sizeof(SDTHeader) || hdr->length > MAX_SDT_LEN) continue;
                if (checksum_ok(hdr, hdr->length)) {
                    fadt = (const FADT*)hdr;
                    break;
                }
            }
        }

        if (!fadt) {
            serial_write("[acpi] FADT not found\n");
            return false;
        }

        g_pm1a_cnt = (std::uint16_t)fadt->pm1a_cnt_blk;
        g_pm1b_cnt = (std::uint16_t)fadt->pm1b_cnt_blk;

        serial_write("[acpi] PM1a_CNT=");
        serial_write_hex64(g_pm1a_cnt);
        serial_write(" PM1b_CNT=");
        serial_write_hex64(g_pm1b_cnt);
        serial_write("\n");

        if (g_pm1a_cnt == 0) {
            serial_write("[acpi] PM1a_CNT_BLK == 0\n");
            return false;
        }

        std::uint16_t slp_a = 0;
        std::uint16_t slp_b = 0;
        if (!find_slp_typ_in_dsdt((std::uint64_t)fadt->dsdt, hhdm, &slp_a, &slp_b)) {
            serial_write("[acpi] _S5_ not found in DSDT, using SLP_TYP=0\n");
            slp_a = 0;
            slp_b = 0;
        } else {
            serial_write("[acpi] SLP_TYP from DSDT found\n");
        }

        g_slp_typa = slp_a;
        g_slp_typb = slp_b;

        g_ready = true;
        return true;
    }

    bool is_ready() { return g_ready; }

    bool poweroff() {
        if (!g_ready) return false;

        std::uint16_t val_a = (std::uint16_t)((g_slp_typa << 10) | SLP_EN);
        outw(g_pm1a_cnt, val_a);

        if (g_pm1b_cnt != 0) {
            std::uint16_t val_b = (std::uint16_t)((g_slp_typb << 10) | SLP_EN);
            outw(g_pm1b_cnt, val_b);
        }
        return true;
    }
}

namespace commands {

    CPUInfo cpu_info;
    RAMInfo ram_info = {0, 0, 0, 0, 0};

    static char         g_boot_name[64]    = {0};
    static char         g_boot_version[64] = {0};
    static std::uint64_t g_firmware_type   = 0;
    static char         g_cmdline[256]     = {0};
    static std::int64_t g_boot_timestamp   = 0;

    static void*         g_rsdp_phys = nullptr;
    static std::uint64_t g_hhdm      = 0;

    static void copy_into(char* dst, std::size_t sz, const char* src) {
        if (!sz) return;
        if (!src) { dst[0] = '\0'; return; }
        std::size_t i = 0;
        while (src[i] && i + 1 < sz) { dst[i] = src[i]; i++; }
        dst[i] = '\0';
    }

    void set_bootloader_info(const char* name, const char* version) {
        copy_into(g_boot_name,    sizeof(g_boot_name),    name);
        copy_into(g_boot_version, sizeof(g_boot_version), version);
    }
    void set_firmware_type(std::uint64_t type) { g_firmware_type = type; }
    void set_cmdline(const char* cmdline)      { copy_into(g_cmdline, sizeof(g_cmdline), cmdline); }
    void set_boot_timestamp(std::int64_t ts)   { g_boot_timestamp = ts; }

    void set_acpi_rsdp(void* rsdp_phys, std::uint64_t hhdm) {
        g_rsdp_phys = rsdp_phys;
        g_hhdm      = hhdm;

        if (!rsdp_phys) {
            teardown_info("ACPI: RSDP not provided");
            return;
        }

        if (acpi::init(rsdp_phys, hhdm)) {
            teardown_ok("ACPI: FADT parsed");
        } else {
            teardown_info("ACPI: FADT not usable — poweroff may be limited");
        }
    }

    static const char* firmware_str() {
        switch (g_firmware_type) {
            case LIMINE_FIRMWARE_TYPE_X86BIOS: return "BIOS";
            case LIMINE_FIRMWARE_TYPE_EFI32:   return "UEFI 32-bit";
            case LIMINE_FIRMWARE_TYPE_EFI64:   return "UEFI 64-bit";
            case LIMINE_FIRMWARE_TYPE_SBI:     return "SBI";
            default:                           return "Unknown";
        }
    }

    void init_cpuinfo() {
        std::uint32_t eax, ebx, ecx, edx;

        cpuid(0, 0, &eax, &ebx, &ecx, &edx);
        *(std::uint32_t*)&cpu_info.vendor[0] = ebx;
        *(std::uint32_t*)&cpu_info.vendor[4] = edx;
        *(std::uint32_t*)&cpu_info.vendor[8] = ecx;
        cpu_info.vendor[12] = '\0';

        cpuid(0x80000000, 0, &eax, &ebx, &ecx, &edx);
        if (eax >= 0x80000004) {
            std::uint32_t* brand = (std::uint32_t*)cpu_info.brand;
            cpuid(0x80000002, 0, &brand[0], &brand[1], &brand[2], &brand[3]);
            cpuid(0x80000003, 0, &brand[4], &brand[5], &brand[6], &brand[7]);
            cpuid(0x80000004, 0, &brand[8], &brand[9], &brand[10], &brand[11]);
            cpu_info.brand[48] = '\0';

            int start = 0;
            while (cpu_info.brand[start] == ' ') start++;
            if (start > 0) {
                int j = 0;
                for (int i = start; cpu_info.brand[i]; i++) cpu_info.brand[j++] = cpu_info.brand[i];
                cpu_info.brand[j] = '\0';
            }
        } else {
            const char* fb = "Unknown x86_64 CPU";
            for (int i = 0; fb[i] && i < 48; i++) cpu_info.brand[i] = fb[i];
            cpu_info.brand[48] = '\0';
        }

        cpuid(1, 0, &eax, &ebx, &ecx, &edx);
        cpu_info.has_sse  = (edx & (1 << 25)) != 0;
        cpu_info.has_sse2 = (edx & (1 << 26)) != 0;
        cpu_info.has_avx  = (ecx & (1 << 28)) != 0;

        cpuid(7, 0, &eax, &ebx, &ecx, &edx);
        cpu_info.has_avx2 = (ebx & (1 << 5)) != 0;
    }

    void init_ram_info(limine_memmap_response* response) {
        if (!response) return;

        ram_info.total_bytes       = 0;
        ram_info.usable_bytes      = 0;
        ram_info.reserved_bytes    = 0;
        ram_info.kernel_bytes      = 0;
        ram_info.framebuffer_bytes = 0;

        for (std::uint64_t i = 0; i < response->entry_count; i++) {
            limine_memmap_entry* entry = response->entries[i];
            ram_info.total_bytes += entry->length;

            switch (entry->type) {
                case LIMINE_MEMMAP_USABLE:
                    ram_info.usable_bytes += entry->length;
                    break;
                case LIMINE_MEMMAP_EXECUTABLE_AND_MODULES:
                    ram_info.kernel_bytes += entry->length;
                    break;
                case LIMINE_MEMMAP_FRAMEBUFFER:
                    ram_info.framebuffer_bytes += entry->length;
                    break;
                default:
                    ram_info.reserved_bytes += entry->length;
                    break;
            }
        }
    }

    static void teardown_stop_interrupts() {
        asm volatile("cli");
        teardown_ok("Interrupts disabled (CLI)");
    }

    static void teardown_unmount_fs() {
        if (!fat32::is_mounted()) {
            teardown_info("No filesystem mounted — skipping unmount");
            return;
        }
        fat32::unmount();
        teardown_ok("FAT32 unmounted");
    }

    static void teardown_flush_disks() {
        if (!disk::info.present) {
            teardown_info("No disk — skipping flush");
            return;
        }
        if (disk::flush()) {
            teardown_ok("Disk caches flushed");
        } else {
            teardown_fail("Disk flush failed");
        }
    }

    static void teardown_stop_input() {
        if (input::active_source() == input::Source::USB) {
            if (ehci::is_present()) {
                ehci::intr_close();
                teardown_ok("USB HID interrupt endpoint closed");
            }
        } else {
            teardown_info("USB HID not active — skipping");
        }

        if (input::active_source() == input::Source::PS2) {
            ps2::stop();
            teardown_ok("PS/2 controller stopped");
        } else {
            teardown_info("PS/2 not active — skipping");
        }

        input::stop();
        teardown_ok("Input subsystem stopped");
    }

    static void teardown_stop_block() {
        if (disk::active_driver == disk::Driver::AHCI) {
            ahci::stop();
            teardown_ok("AHCI controller stopped");
        } else if (disk::active_driver == disk::Driver::ATA) {
            ata::stop();
            teardown_ok("ATA PIO controller stopped");
        } else {
            teardown_info("No block driver active — skipping");
        }
    }

    static void teardown_stop_dma() {
        if (dma::is_initialized()) {
            dma::stop();
            teardown_ok("DMA allocator stopped");
        } else {
            teardown_info("DMA not initialized — skipping");
        }
    }

    static void teardown_stop_pic() {
        pic::stop();
        teardown_ok("PIC masked");
    }

    static void teardown_stop_idt() {
        idt::stop();
        teardown_ok("IDT replaced with null");
    }

    static void teardown_stop_dmi() {
        dmi::stop();
        teardown_ok("DMI/SMBIOS subsystem stopped");
    }

    static bool system_teardown() {
        console::set_color(COLOR_YELLOW, COLOR_BLACK);
        console::puts("\n[TEARDOWN] Stopping system services...\n");
        console::set_color(COLOR_GRAY, COLOR_BLACK);
        serial_write("[teardown] ===== START =====\n");

        teardown_stop_interrupts();
        teardown_unmount_fs();
        teardown_flush_disks();
        teardown_stop_input();
        teardown_stop_block();
        teardown_stop_dma();
        teardown_stop_pic();
        teardown_stop_idt();
        teardown_stop_dmi();

        serial_write("[teardown] ===== DONE =====\n");

        console::set_color(COLOR_GREEN, COLOR_BLACK);
        console::puts("\n[ OK ] All subsystems stopped.\n");
        console::set_color(COLOR_GRAY, COLOR_BLACK);
        return true;
    }

    void shutdown() {
        system_teardown();

        console::set_color(COLOR_YELLOW, COLOR_BLACK);
        console::puts("\n[SHUTDOWN] System halted.\n");
        console::puts("[SHUTDOWN] Powering off in 2 seconds...\n");
        console::set_color(COLOR_GRAY, COLOR_BLACK);
        serial_write("[shutdown] Halting CPU, then ACPI S5\n");

        for (volatile std::uint32_t i = 0; i < 200000000; i++) { }
        asm volatile("cli");

        if (acpi::poweroff()) {
            serial_write("[shutdown] ACPI SLP_TYP|SLP_EN written to PM1x_CNT\n");
        }

        outw(0x604,  0x2000);
        outw(0xB004, 0x2000);
        outw(0x4004, 0x3400);

        console::set_color(COLOR_RED, COLOR_BLACK);
        console::puts("[FAIL] ACPI shutdown not supported by this machine.\n");
        console::set_color(COLOR_GRAY, COLOR_BLACK);
        serial_write("[shutdown] ACPI failed — halting\n");

        for (;;) asm volatile("hlt");
    }

    void reboot() {
        system_teardown();

        console::set_color(COLOR_YELLOW, COLOR_BLACK);
        console::puts("\n[REBOOT] Sending reset command...\n");
        console::set_color(COLOR_GRAY, COLOR_BLACK);
        serial_write("[reboot] 8042 reset sequence\n");

        for (int i = 0; i < 100000; i++) {
            if ((inb(0x64) & 0x02) == 0) break;
            io_wait();
        }
        outb(0x64, 0xFE);

        struct { std::uint16_t limit; std::uint64_t base; } __attribute__((packed)) null_idt = { 0, 0 };
        asm volatile("lidt %0" : : "m"(null_idt));
        asm volatile("int $0x03");

        for (;;) asm volatile("hlt");
    }

    void emerdown() {
        asm volatile("cli");
        struct { std::uint16_t limit; std::uint64_t base; } __attribute__((packed)) null_idt = { 0, 0 };
        asm volatile("lidt %0" : : "m"(null_idt));
        asm volatile("int $0x03");
        for (;;) asm volatile("hlt");
    }

    void neofetch() {
        puts_color("__________          .___                    __  ________    _________\n", COLOR_BLUE);
        puts_color("\\______   \\____   __| _/_ __  _____ _____ _/  |_\\_____  \\  /   _____/\n", COLOR_BLUE);
        puts_color(" |     ___/  _ \\ / __ |  |  \\/     \\\\__  \\\\   __\\/   |   \\ \\_____  \\ \n", COLOR_CYAN);
        puts_color(" |    |  (  <_> ) /_/ |  |  /  Y Y  \\/ __ \\|  | /    |    \\/        \\\n", COLOR_CYAN);
        puts_color(" |____|   \\____/\\____ |____/|__|_|  (____  /__| \\_______  /_______  /\n", COLOR_GREEN);
        puts_color("                     \\/           \\/     \\/             \\/        \\/ \n", COLOR_GREEN);
        console::set_color(COLOR_WHITE, COLOR_BLACK);
        console::puts("                              ");
        console::puts(version::OS_NAME);
        console::puts(" v");
        console::puts(version::OS_VERSION);
        console::putc('\n');
        console::set_color(COLOR_GRAY, COLOR_BLACK);
        console::putc('\n');

        puts_color("  CPU:         ", COLOR_YELLOW);
        console::puts(cpu_info.brand);
        console::putc('\n');

        puts_color("  CPU FEAT:    ", COLOR_YELLOW);
        console::puts("Vendor: ");
        console::puts(cpu_info.vendor);
        console::puts(" | ");
        if (cpu_info.has_sse)  console::puts("SSE ");
        if (cpu_info.has_sse2) console::puts("SSE2 ");
        if (cpu_info.has_avx)  console::puts("AVX ");
        if (cpu_info.has_avx2) console::puts("AVX2 ");
        console::putc('\n');

        puts_color("  RAM:         ", COLOR_YELLOW);
        print_u64(ram_info.total_bytes / (1024 * 1024));
        console::puts(" MB total (");
        print_u64(ram_info.usable_bytes / (1024 * 1024));
        console::puts(" MB usable)\n");

        puts_color("  RAM USAGE:   ", COLOR_YELLOW);
        {
            std::uint64_t used_bytes = ram_info.total_bytes - ram_info.usable_bytes;
            std::uint64_t used_mb    = used_bytes / (1024 * 1024);
            std::uint64_t total_mb   = ram_info.total_bytes / (1024 * 1024);
            int percent = (ram_info.total_bytes > 0)
                ? (int)((used_bytes * 100) / ram_info.total_bytes)
                : 0;

            print_u64(used_mb);
            console::puts(" MB / ");
            print_u64(total_mb);
            console::puts(" MB (");
            print_int(percent);
            console::puts("%)\n");
        }

        puts_color("  MOTHERBOARD: ", COLOR_YELLOW);
        console::puts(dmi::motherboard());
        console::putc('\n');

        puts_color("  MODEL:       ", COLOR_YELLOW);
        console::puts(dmi::product());
        console::putc('\n');

        puts_color("  BIOS:        ", COLOR_YELLOW);
        console::puts(dmi::bios_vendor());
        console::puts(" ");
        console::puts(dmi::bios_version());
        console::puts(" (");
        console::puts(dmi::bios_date());
        console::puts(")\n");

        puts_color("  FIRMWARE:    ", COLOR_YELLOW);
        console::puts(firmware_str());
        console::putc('\n');

        puts_color("  KERNEL:      ", COLOR_YELLOW);
        console::puts(version::OS_NAME);
        console::puts(" ");
        console::puts(version::OS_VERSION);
        console::puts(" (");
        console::puts(version::ARCH);
        console::puts(", ");
        if (g_boot_name[0]) {
            console::puts(g_boot_name);
            if (g_boot_version[0]) {
                console::puts(" v");
                console::puts(g_boot_version);
            }
        } else {
            console::puts("unknown bootloader");
        }
        console::puts(")\n");

        puts_color("  CMDLINE:     ", COLOR_YELLOW);
        console::puts(g_cmdline[0] ? g_cmdline : "(empty)");
        console::putc('\n');

        puts_color("  STORAGE:     ", COLOR_YELLOW);
        console::puts(disk::active_driver_name());
        console::putc('\n');

        puts_color("  INPUT:       ", COLOR_YELLOW);
        console::puts(input::active_source_name());
        console::putc('\n');

        puts_color("  UPTIME:      ", COLOR_YELLOW);
        {
            std::uint64_t total_seconds = pit::ticks / 100;
            std::uint64_t hours   = total_seconds / 3600;
            std::uint64_t minutes = (total_seconds % 3600) / 60;
            std::uint64_t seconds = total_seconds % 60;
            print_u64(hours); console::puts("h ");
            print_u64(minutes); console::puts("m ");
            print_u64(seconds); console::puts("s\n");
        }

        puts_color("  SHELL:       ", COLOR_YELLOW);
        console::puts("PodumShell v1.0\n");

        puts_color("  TIMER:       ", COLOR_YELLOW);
        console::puts("PIT @ 100 Hz, ticks=");
        print_u64(pit::ticks);
        console::putc('\n');

        console::putc('\n');

        puts_color("  COLOR TEST:  ", COLOR_YELLOW);
        puts_color("###", COLOR_RED);
        puts_color("###", COLOR_GREEN);
        puts_color("###", COLOR_YELLOW);
        puts_color("###", COLOR_BLUE);
        puts_color("###", COLOR_MAGENTA);
        puts_color("###", COLOR_CYAN);
        puts_color("###", COLOR_WHITE);
        console::putc('\n');
    }

    void drivers() {
        console::set_color(COLOR_CYAN, COLOR_BLACK);
        console::puts("Loaded drivers:\n");
        console::set_color(COLOR_GRAY, COLOR_BLACK);

        int n = driver_registry::count();
        for (int i = 0; i < n; i++) {
            const driver_registry::Entry* e = driver_registry::get(i);
            if (!e) continue;

            if (e->ok) {
                console::set_color(COLOR_GREEN, COLOR_BLACK);
                console::puts("  [ OK ] ");
            } else {
                console::set_color(COLOR_RED, COLOR_BLACK);
                console::puts("  [FAIL] ");
            }
            console::set_color(COLOR_GRAY, COLOR_BLACK);

            console::puts(e->name);
            int len = 0;
            while (e->name[len]) len++;
            for (int j = len; j < 12; j++) console::putc(' ');

            console::puts("- ");
            console::puts(e->description);
            console::putc('\n');
        }
    }

    void lspci() {
        pci::print_all();
    }

    void diskinfo() {
        console::set_color(COLOR_CYAN, COLOR_BLACK);
        console::puts("Disk Information:\n");
        console::set_color(COLOR_GRAY, COLOR_BLACK);

        if (!disk::info.present) {
            console::set_color(COLOR_RED, COLOR_BLACK);
            console::puts("  No disk detected!\n");
            console::set_color(COLOR_GRAY, COLOR_BLACK);
            return;
        }

        console::puts("  Driver:    "); console::puts(disk::active_driver_name()); console::putc('\n');
        console::puts("  Model:     "); console::puts(disk::info.model); console::putc('\n');
        console::puts("  Serial:    "); console::puts(disk::info.serial); console::putc('\n');
        console::puts("  Firmware:  "); console::puts(disk::info.firmware); console::putc('\n');
        console::puts("  LBA28:     "); print_u64(disk::info.sectors); console::puts(" sectors\n");
        console::puts("  LBA48:     ");
        if (disk::info.lba48) {
            print_u64(disk::info.sectors48);
            console::puts(" sectors\n");
        } else {
            console::puts("not supported\n");
        }
        console::puts("  Size:      "); print_u64(disk::info.sectors / 2048); console::puts(" MB\n");
        console::puts("  Sector:    512 bytes\n");
    }

    void disktest() {
        console::set_color(COLOR_CYAN, COLOR_BLACK);
        console::puts("Reading sector 0 (MBR)...\n");
        console::set_color(COLOR_GRAY, COLOR_BLACK);

        uint8_t buffer[512];
        if (!disk::read_sector(0, buffer)) {
            console::set_color(COLOR_RED, COLOR_BLACK);
            console::puts("[FAIL] Cannot read sector 0\n");
            console::set_color(COLOR_GRAY, COLOR_BLACK);
            return;
        }

        console::puts("  First 16 bytes: ");
        static const char hex[] = "0123456789ABCDEF";
        for (int i = 0; i < 16; i++) {
            console::putc(hex[(buffer[i] >> 4) & 0xF]);
            console::putc(hex[buffer[i] & 0xF]);
            console::putc(' ');
        }
        console::putc('\n');

        if (buffer[510] == 0x55 && buffer[511] == 0xAA) {
            console::set_color(COLOR_GREEN, COLOR_BLACK);
            console::puts("[ OK ] MBR signature found (0x55AA)\n");
        } else {
            console::set_color(COLOR_RED, COLOR_BLACK);
            console::puts("[FAIL] No MBR signature\n");
        }
        console::set_color(COLOR_GRAY, COLOR_BLACK);
    }

    void diskread(const char* args) {
        uint32_t lba = (uint32_t)simple_atoi(args);
        uint8_t buffer[512];
        if (!disk::read_sector(lba, buffer)) {
            console::set_color(COLOR_RED, COLOR_BLACK);
            console::puts("Read failed!\n");
            console::set_color(COLOR_GRAY, COLOR_BLACK);
            return;
        }

        console::set_color(COLOR_CYAN, COLOR_BLACK);
        console::puts("Sector "); print_u64(lba); console::puts(":\n");
        console::set_color(COLOR_GRAY, COLOR_BLACK);

        static const char hex[] = "0123456789ABCDEF";
        for (int row = 0; row < 32; row++) {
            for (int col = 0; col < 16; col++) {
                uint8_t b = buffer[row * 16 + col];
                console::putc(hex[(b >> 4) & 0xF]);
                console::putc(hex[b & 0xF]);
                console::putc(' ');
            }
            console::puts(" | ");
            for (int col = 0; col < 16; col++) {
                uint8_t b = buffer[row * 16 + col];
                console::putc((b >= 32 && b < 127) ? (char)b : '.');
            }
            console::putc('\n');
        }
    }

    void diskwrite(const char* args) {
        const char* p = args;
        uint32_t lba = (uint32_t)simple_atoi(p);
        while (*p && *p != ' ') p++;
        while (*p == ' ') p++;

        uint8_t buffer[512];
        for (int i = 0; i < 512; i++) buffer[i] = 0;

        int i = 0;
        while (*p && i < 512) {
            while (*p == ' ') p++;
            if (!*p) break;

            uint8_t hi = 0, lo = 0;
            if (*p >= '0' && *p <= '9') hi = *p - '0';
            else if (*p >= 'A' && *p <= 'F') hi = *p - 'A' + 10;
            else if (*p >= 'a' && *p <= 'f') hi = *p - 'a' + 10;
            p++;
            if (*p >= '0' && *p <= '9') lo = *p - '0';
            else if (*p >= 'A' && *p <= 'F') lo = *p - 'A' + 10;
            else if (*p >= 'a' && *p <= 'f') lo = *p - 'a' + 10;
            p++;

            buffer[i++] = (hi << 4) | lo;
        }

        if (!disk::write_sector(lba, buffer)) {
            console::set_color(COLOR_RED, COLOR_BLACK);
            console::puts("[FAIL] Write failed\n");
            console::set_color(COLOR_GRAY, COLOR_BLACK);
            return;
        }

        console::set_color(COLOR_GREEN, COLOR_BLACK);
        console::puts("[ OK ] Written ");
        print_int(i);
        console::puts(" bytes to LBA ");
        print_u64(lba);
        console::putc('\n');
        console::set_color(COLOR_GRAY, COLOR_BLACK);
    }

    void fdisk(const char* args) {
        if (!disk::info.present) {
            console::set_color(COLOR_RED, COLOR_BLACK);
            console::puts("No disk!\n");
            console::set_color(COLOR_GRAY, COLOR_BLACK);
            return;
        }

        if (*args == '\0' || strcmp(args, "list") == 0) {
            console::set_color(COLOR_CYAN, COLOR_BLACK);
            console::puts("Disk: "); console::puts(disk::info.model);
            console::puts(" ("); print_u64(disk::info.sectors / 2048); console::puts(" MB)\n\n");
            console::set_color(COLOR_GRAY, COLOR_BLACK);
            mbr::print_all();
            return;
        }

        if (strcmp(args, "init") == 0) {
            mbr::MBR m;
            mbr::create_empty(&m);
            if (mbr::write(&m)) {
                console::set_color(COLOR_GREEN, COLOR_BLACK);
                console::puts("[ OK ] Empty MBR created\n");
            } else {
                console::set_color(COLOR_RED, COLOR_BLACK);
                console::puts("[FAIL] Cannot write MBR\n");
            }
            console::set_color(COLOR_GRAY, COLOR_BLACK);
            return;
        }

        if (strncmp(args, "create ", 7) == 0) {
            const char* p = args + 7;
            int n = simple_atoi(p);
            while (*p && *p != ' ') p++;
            while (*p == ' ') p++;

            char type_str[16] = {0};
            int i = 0;
            while (*p && *p != ' ' && i < 15) type_str[i++] = *p++;
            type_str[i] = '\0';
            while (*p == ' ') p++;

            int size_mb = simple_atoi(p);
            uint32_t sectors = (uint32_t)size_mb * 2048;

            uint8_t type = 0x83;
            if (strcmp(type_str, "FAT32") == 0) type = 0x0B;
            else if (strcmp(type_str, "FAT16") == 0) type = 0x06;
            else if (strcmp(type_str, "NTFS") == 0) type = 0x07;
            else if (strcmp(type_str, "Linux") == 0) type = 0x83;
            else if (strcmp(type_str, "Swap") == 0) type = 0x82;

            uint32_t start = mbr::find_free_lba(sectors);
            if (start == 0) {
                console::set_color(COLOR_RED, COLOR_BLACK);
                console::puts("[FAIL] No free space\n");
                console::set_color(COLOR_GRAY, COLOR_BLACK);
                return;
            }

            if (mbr::create_partition(n, start, sectors, type)) {
                console::set_color(COLOR_GREEN, COLOR_BLACK);
                console::puts("[ OK ] Partition created: ");
                console::puts(type_str);
                console::puts(" at LBA "); print_u64(start);
                console::puts(" ("); print_u64(size_mb); console::puts(" MB)\n");
            } else {
                console::set_color(COLOR_RED, COLOR_BLACK);
                console::puts("[FAIL] Cannot create partition\n");
            }
            console::set_color(COLOR_GRAY, COLOR_BLACK);
            return;
        }

        if (strncmp(args, "delete ", 7) == 0) {
            int n = simple_atoi(args + 7);
            if (mbr::delete_partition(n)) {
                console::set_color(COLOR_GREEN, COLOR_BLACK);
                console::puts("[ OK ] Partition deleted\n");
            } else {
                console::set_color(COLOR_RED, COLOR_BLACK);
                console::puts("[FAIL] Cannot delete\n");
            }
            console::set_color(COLOR_GRAY, COLOR_BLACK);
            return;
        }

        console::puts("Usage:\n");
        console::puts("  fdisk                      - show partitions\n");
        console::puts("  fdisk init                 - create empty MBR\n");
        console::puts("  fdisk create <n> <type> <mb> - create partition\n");
        console::puts("  fdisk delete <n>           - delete partition\n");
    }

    void format(const char* args) {
        if (!disk::info.present) {
            console::set_color(COLOR_RED, COLOR_BLACK);
            console::puts("[FAIL] No disk\n");
            console::set_color(COLOR_GRAY, COLOR_BLACK);
            return;
        }

        uint32_t lba = 0;
        uint32_t size_mb = 0;

        if (*args == '\0') {
            mbr::MBR m;
            if (!mbr::read(&m)) {
                console::set_color(COLOR_RED, COLOR_BLACK);
                console::puts("[FAIL] Cannot read MBR\n");
                console::set_color(COLOR_GRAY, COLOR_BLACK);
                return;
            }
            bool found = false;
            for (int i = 0; i < 4; i++) {
                if (m.partitions[i].type == 0x0B || m.partitions[i].type == 0x0C) {
                    lba = m.partitions[i].lba_start;
                    size_mb = m.partitions[i].sector_count / 2048;
                    found = true;
                    break;
                }
            }
            if (!found) {
                console::set_color(COLOR_RED, COLOR_BLACK);
                console::puts("[FAIL] No FAT32 partition. Use 'fdisk create 0 FAT32 <mb>' first.\n");
                console::set_color(COLOR_GRAY, COLOR_BLACK);
                return;
            }
        } else {
            const char* p = args;
            lba = (uint32_t)simple_atoi(p);
            while (*p && *p != ' ') p++;
            while (*p == ' ') p++;
            size_mb = (uint32_t)simple_atoi(p);
        }

        if (size_mb == 0) {
            console::set_color(COLOR_RED, COLOR_BLACK);
            console::puts("[FAIL] Invalid size\n");
            console::set_color(COLOR_GRAY, COLOR_BLACK);
            return;
        }

        console::set_color(COLOR_YELLOW, COLOR_BLACK);
        console::puts("[FORMAT] Formatting FAT32 at LBA ");
        print_u64(lba);
        console::puts(" (");
        print_u64(size_mb);
        console::puts(" MB)...\n");
        console::set_color(COLOR_GRAY, COLOR_BLACK);

        uint32_t sectors = size_mb * 2048;
        if (fat32::format(lba, sectors)) {
            console::set_color(COLOR_GREEN, COLOR_BLACK);
            console::puts("[ OK ] FAT32 formatted\n");
            console::set_color(COLOR_GRAY, COLOR_BLACK);
        } else {
            console::set_color(COLOR_RED, COLOR_BLACK);
            console::puts("[FAIL] Format failed\n");
            console::set_color(COLOR_GRAY, COLOR_BLACK);
        }
    }

    void mount_cmd(const char* args) {
        const char* p = args;
        while (*p == ' ') p++;

        if (p[0] == '\0' || p[1] != ':') {
            console::puts("Usage: mount <A-Z>: <lba>\n");
            return;
        }

        char letter = p[0];
        if (letter >= 'a' && letter <= 'z') letter -= 32;
        if (letter < 'A' || letter > 'Z') {
            console::puts("Invalid drive letter\n");
            return;
        }

        p += 2;
        while (*p == ' ') p++;
        uint32_t lba = (uint32_t)simple_atoi(p);

        if (fat32::mount(lba, letter)) {
            console::set_color(COLOR_GREEN, COLOR_BLACK);
            console::puts("[ OK ] Mounted as ");
            console::putc(letter);
            console::puts(": at LBA ");
            print_u64(lba);
            console::putc('\n');
            console::set_color(COLOR_GRAY, COLOR_BLACK);
        } else {
            console::set_color(COLOR_RED, COLOR_BLACK);
            console::puts("[FAIL] Cannot mount\n");
            console::set_color(COLOR_GRAY, COLOR_BLACK);
        }
    }

    void cd_cmd(const char* args) {
        if (!fat32::is_mounted()) {
            console::set_color(COLOR_RED, COLOR_BLACK);
            console::puts("[FAIL] No filesystem mounted\n");
            console::set_color(COLOR_GRAY, COLOR_BLACK);
            return;
        }

        if (args[0] && args[1] == ':' && args[2] == '\0') {
            char l = args[0];
            if (l >= 'a' && l <= 'z') l -= 32;
            if (l == fat32::get_letter()) {
                fat32::fs.current_cluster = fat32::fs.root_cluster;
                return;
            }
            console::set_color(COLOR_RED, COLOR_BLACK);
            console::puts("[FAIL] Drive not mounted\n");
            console::set_color(COLOR_GRAY, COLOR_BLACK);
            return;
        }

        if (fat32::cd(args)) {
        } else {
            console::set_color(COLOR_RED, COLOR_BLACK);
            console::puts("[FAIL] Directory not found\n");
            console::set_color(COLOR_GRAY, COLOR_BLACK);
        }
    }

    void pwd_cmd() {
        if (!fat32::is_mounted()) {
            console::puts("No filesystem\n");
            return;
        }
        console::putc(fat32::get_letter());
        console::puts(":\\");
        fat32::pwd();
        console::putc('\n');
    }

    void drives() {
        console::set_color(COLOR_CYAN, COLOR_BLACK);
        console::puts("Mounted drives:\n");
        console::set_color(COLOR_GRAY, COLOR_BLACK);

        if (fat32::is_mounted()) {
            console::puts("  ");
            console::putc(fat32::get_letter());
            console::puts(":  FAT32  at LBA ");
            print_u64(fat32::fs.partition_lba);
            console::puts(" (");
            print_u64(fat32::fs.total_clusters * fat32::fs.bytes_per_cluster / (1024 * 1024));
            console::puts(" MB)\n");
        } else {
            console::puts("  (none)\n");
        }
    }

    void fat_ls(const char* args) {
        fat32::list_dir(args);
    }

    void fat_cat(const char* args) {
        if (!fat32::is_mounted()) { console::puts("FAT32 not mounted\n"); return; }

        static uint8_t buf[65536];
        uint32_t size = 0;
        if (!fat32::read_file(args, buf, &size)) {
            console::puts("Cannot read file\n");
            return;
        }
        for (uint32_t i = 0; i < size; i++) console::putc((char)buf[i]);
        if (size == 0 || buf[size - 1] != '\n') console::putc('\n');
    }

    void fat_mkdir(const char* args) {
        if (fat32::create_dir(args)) {
            console::set_color(COLOR_GREEN, COLOR_BLACK);
            console::puts("[ OK ] Directory created\n");
            console::set_color(COLOR_GRAY, COLOR_BLACK);
        } else {
            console::set_color(COLOR_RED, COLOR_BLACK);
            console::puts("[FAIL] Cannot create directory\n");
            console::set_color(COLOR_GRAY, COLOR_BLACK);
        }
    }

    void fat_rm(const char* args) {
        if (fat32::delete_file(args)) {
            console::set_color(COLOR_GREEN, COLOR_BLACK);
            console::puts("[ OK ] File deleted\n");
            console::set_color(COLOR_GRAY, COLOR_BLACK);
        } else {
            console::set_color(COLOR_RED, COLOR_BLACK);
            console::puts("[FAIL] Cannot delete\n");
            console::set_color(COLOR_GRAY, COLOR_BLACK);
        }
    }

    void fat_write(const char* args) {
        char path[64] = {0};
        const char* p = args;
        int i = 0;
        while (*p && *p != ' ' && i < 63) path[i++] = *p++;
        path[i] = '\0';
        while (*p == ' ') p++;

        if (path[0] == '\0') {
            console::puts("Usage: fat_write <path> <text>\n");
            return;
        }

        uint32_t len = 0;
        while (p[len]) len++;

        if (fat32::write_file(path, (const uint8_t*)p, len)) {
            console::set_color(COLOR_GREEN, COLOR_BLACK);
            console::puts("[ OK ] File written\n");
            console::set_color(COLOR_GRAY, COLOR_BLACK);
        } else {
            console::set_color(COLOR_RED, COLOR_BLACK);
            console::puts("[FAIL] Cannot write file\n");
            console::set_color(COLOR_GRAY, COLOR_BLACK);
        }
    }

    void calc(const char* args) {
        int a = 0, b = 0;
        char op[8] = {0};
        int op_len = 0;

        const char* p = args;
        while (*p == ' ') p++;
        a = simple_atoi(p);
        while (*p && *p != ' ') p++;
        while (*p == ' ') p++;

        while (*p && *p != ' ' && op_len < 7) op[op_len++] = *p++;
        op[op_len] = '\0';
        while (*p == ' ') p++;

        b = simple_atoi(p);

        int result = 0;
        bool ok = true;

        if (strcmp(op, "ADD") == 0) result = a + b;
        else if (strcmp(op, "SUB") == 0) result = a - b;
        else if (strcmp(op, "MUL") == 0) result = a * b;
        else if (strcmp(op, "DIV") == 0) {
            if (b == 0) { console::set_color(COLOR_RED, COLOR_BLACK); console::puts("Error: division by zero\n"); console::set_color(COLOR_GRAY, COLOR_BLACK); return; }
            result = a / b;
        }
        else if (strcmp(op, "MOD") == 0) {
            if (b == 0) { console::set_color(COLOR_RED, COLOR_BLACK); console::puts("Error: modulo by zero\n"); console::set_color(COLOR_GRAY, COLOR_BLACK); return; }
            result = a % b;
        } else ok = false;

        if (!ok) {
            console::set_color(COLOR_RED, COLOR_BLACK);
            console::puts("Unknown operation: "); console::puts(op);
            console::puts("\nValid: ADD, SUB, MUL, DIV, MOD\n");
            console::set_color(COLOR_GRAY, COLOR_BLACK);
            return;
        }

        console::set_color(COLOR_GREEN, COLOR_BLACK);
        print_int(a); console::puts(" "); console::puts(op); console::puts(" "); print_int(b);
        console::puts(" = ");
        console::set_color(COLOR_WHITE, COLOR_BLACK);
        print_int(result);
        console::putc('\n');
        console::set_color(COLOR_GRAY, COLOR_BLACK);
    }

    void inputinfo() {
        console::set_color(COLOR_CYAN, COLOR_BLACK);
        console::puts("Input Information:\n");
        console::set_color(COLOR_GRAY, COLOR_BLACK);

        console::puts("  Active source: ");
        console::puts(input::active_source_name());
        console::putc('\n');

        console::puts("  has_data():    ");
        console::puts(input::has_data() ? "yes" : "no");
        console::putc('\n');
    }

    void execute(const char* cmd) {
        if (strcmp(cmd, "") == 0) return;
        else if (strcmp(cmd, "clear") == 0) console::clear();
        else if (strncmp(cmd, "echo ", 5) == 0) { console::puts(cmd + 5); console::puts("\n"); }
        else if (strcmp(cmd, "help") == 0) {
            console::set_color(COLOR_CYAN, COLOR_BLACK);
            console::puts("PodumShell v1.0 commands:\n");
            console::set_color(COLOR_GRAY, COLOR_BLACK);
            console::puts("  help                - this message\n");
            console::puts("  clear               - clear screen\n");
            console::puts("  echo <text>         - print text\n");
            console::puts("  neofetch            - system info\n");
            console::puts("  cpuinfo             - CPU details (CPUID)\n");
            console::puts("  meminfo             - RAM details\n");
            console::puts("  inputinfo           - input device info\n");
            console::puts("  drivers             - list loaded drivers\n");
            console::puts("  lspci               - list PCI devices\n");
            console::puts("  diskinfo            - disk information\n");
            console::puts("  disktest            - test disk read\n");
            console::puts("  diskread <lba>      - hex dump sector\n");
            console::puts("  diskwrite <lba> <hex> - write sector\n");
            console::puts("  fdisk [args]        - MBR partition manager\n");
            console::puts("  format [lba] [mb]   - format FAT32\n");
            console::puts("  mount <L>: <lba>    - mount FAT32 as drive\n");
            console::puts("  drives              - list mounted drives\n");
            console::puts("  cd <path>           - change directory\n");
            console::puts("  pwd                 - print working directory\n");
            console::puts("  ls [path]           - list directory\n");
            console::puts("  cat <file>          - show file\n");
            console::puts("  mkdir <dir>         - create directory\n");
            console::puts("  rm <file>           - delete file\n");
            console::puts("  fat_write <f> <txt> - write file\n");
            console::puts("  calc <a> <op> <b>   - calculator (ADD/SUB/MUL/DIV/MOD)\n");
            console::puts("  reboot              - graceful reboot\n");
            console::puts("  shutdown            - graceful power off\n");
            console::puts("  emerdown            - emergency halt (triple fault)\n");
        }
        else if (strcmp(cmd, "neofetch") == 0) neofetch();
        else if (strcmp(cmd, "cpuinfo") == 0) {
            console::set_color(COLOR_CYAN, COLOR_BLACK);
            console::puts("CPU Information (CPUID):\n");
            console::set_color(COLOR_GRAY, COLOR_BLACK);
            console::puts("  Vendor:   "); console::puts(cpu_info.vendor); console::putc('\n');
            console::puts("  Brand:    "); console::puts(cpu_info.brand); console::putc('\n');
            console::puts("  Features: ");
            if (cpu_info.has_sse)  console::puts("SSE ");
            if (cpu_info.has_sse2) console::puts("SSE2 ");
            if (cpu_info.has_avx)  console::puts("AVX ");
            if (cpu_info.has_avx2) console::puts("AVX2 ");
            console::putc('\n');
        }
        else if (strcmp(cmd, "meminfo") == 0) {
            console::set_color(COLOR_CYAN, COLOR_BLACK);
            console::puts("Memory Information:\n");
            console::set_color(COLOR_GRAY, COLOR_BLACK);
            console::puts("  Total:        "); print_u64(ram_info.total_bytes / (1024 * 1024));       console::puts(" MB\n");
            console::puts("  Usable:       "); print_u64(ram_info.usable_bytes / (1024 * 1024));      console::puts(" MB\n");
            console::puts("  Reserved:     "); print_u64(ram_info.reserved_bytes / (1024 * 1024));    console::puts(" MB\n");
            console::puts("  Kernel:       "); print_u64(ram_info.kernel_bytes / (1024 * 1024));      console::puts(" MB\n");
            console::puts("  Framebuffer:  "); print_u64(ram_info.framebuffer_bytes / (1024 * 1024)); console::puts(" MB\n");
        }
        else if (strcmp(cmd, "inputinfo") == 0) inputinfo();
        else if (strcmp(cmd, "drivers") == 0) drivers();
        else if (strcmp(cmd, "lspci") == 0) lspci();
        else if (strcmp(cmd, "diskinfo") == 0) diskinfo();
        else if (strcmp(cmd, "disktest") == 0) disktest();
        else if (strncmp(cmd, "diskread ", 9) == 0) diskread(cmd + 9);
        else if (strncmp(cmd, "diskwrite ", 10) == 0) diskwrite(cmd + 10);
        else if (strcmp(cmd, "fdisk") == 0) fdisk("");
        else if (strncmp(cmd, "fdisk ", 6) == 0) fdisk(cmd + 6);
        else if (strcmp(cmd, "format") == 0) format("");
        else if (strncmp(cmd, "format ", 7) == 0) format(cmd + 7);
        else if (strncmp(cmd, "mount ", 6) == 0) mount_cmd(cmd + 6);
        else if (strcmp(cmd, "drives") == 0) drives();
        else if (strcmp(cmd, "cd") == 0) cd_cmd("/");
        else if (strncmp(cmd, "cd ", 3) == 0) cd_cmd(cmd + 3);
        else if (strcmp(cmd, "pwd") == 0) pwd_cmd();
        else if (strcmp(cmd, "ls") == 0) fat_ls("");
        else if (strncmp(cmd, "ls ", 3) == 0) fat_ls(cmd + 3);
        else if (strncmp(cmd, "cat ", 4) == 0) fat_cat(cmd + 4);
        else if (strncmp(cmd, "mkdir ", 6) == 0) fat_mkdir(cmd + 6);
        else if (strncmp(cmd, "rm ", 3) == 0) fat_rm(cmd + 3);
        else if (strncmp(cmd, "fat_write ", 10) == 0) fat_write(cmd + 10);
        else if (strncmp(cmd, "calc ", 5) == 0) calc(cmd + 5);
        else if (strcmp(cmd, "reboot") == 0) reboot();
        else if (strcmp(cmd, "shutdown") == 0) shutdown();
        else if (strcmp(cmd, "emerdown") == 0) emerdown();
        else {
            console::set_color(COLOR_RED, COLOR_BLACK);
            console::puts("Command not found: ");
            console::puts(cmd);
            console::puts("\n");
            console::set_color(COLOR_GRAY, COLOR_BLACK);
        }
    }
}