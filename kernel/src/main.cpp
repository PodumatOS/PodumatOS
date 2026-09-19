// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Thinking Developer
//
// PodumatOS — a hobby operating system for x86_64.

#include <cstdint>
#include <cstddef>
#include <limine.h>
#include "console/console.hpp"
#include "kernel/idt.hpp"
#include "kernel/pic.hpp"
#include "kernel/pit.hpp"
#include "shell/shell.hpp"
#include "io/io.hpp"
#include "lib/memory.hpp"
#include "shell/commands.hpp"
#include "drivers/pci.hpp"
#include "drivers/disk.hpp"
#include "fs/mbr.hpp"
#include "fs/fat32.hpp"
#include "drivers/dma.hpp"
#include "drivers/input.hpp"
#include "lib/dmi.hpp"
#include "lib/driver_registry.hpp"
#include "lib/version.hpp"

// colors
static constexpr std::uint32_t COLOR_BLACK  = 0x000000;
static constexpr std::uint32_t COLOR_GRAY   = 0xC0C0C0;
static constexpr std::uint32_t COLOR_WHITE  = 0xFFFFFF;
static constexpr std::uint32_t COLOR_GREEN  = 0x00CC00;
static constexpr std::uint32_t COLOR_RED    = 0xCC0000;
static constexpr std::uint32_t COLOR_YELLOW = 0xCCCC00;
static constexpr std::uint32_t COLOR_CYAN   = 0x00CCCC;
static constexpr std::uint32_t COLOR_BLUE   = 0x0088FF;

static void beep(std::uint32_t freq, std::uint32_t duration_ms) {
    std::uint32_t divisor = 1193180 / freq;
    outb(0x43, 0xB6);
    outb(0x42, (std::uint8_t)(divisor & 0xFF));
    outb(0x42, (std::uint8_t)((divisor >> 8) & 0xFF));
    outb(0x61, inb(0x61) | 0x03);
    for (volatile std::uint32_t i = 0; i < duration_ms * 10000; i++) {}
    outb(0x61, inb(0x61) & ~0x03);
}

static void print_ok(const char* msg) {
    console::set_color(COLOR_GREEN, COLOR_BLACK);
    console::puts(" [ OK ] ");
    console::set_color(COLOR_GRAY, COLOR_BLACK);
    console::puts(msg);
    console::puts("\n");
}

static void print_info(const char* msg) {
    console::set_color(COLOR_CYAN, COLOR_BLACK);
    console::puts(" [INFO] ");
    console::set_color(COLOR_GRAY, COLOR_BLACK);
    console::puts(msg);
    console::puts("\n");
}

static void print_warn(const char* msg) {
    console::set_color(COLOR_YELLOW, COLOR_BLACK);
    console::puts(" [WARN] ");
    console::set_color(COLOR_GRAY, COLOR_BLACK);
    console::puts(msg);
    console::puts("\n");
}

static void print_fail(const char* msg) {
    console::set_color(COLOR_RED, COLOR_BLACK);
    console::puts(" [FAIL] ");
    console::set_color(COLOR_GRAY, COLOR_BLACK);
    console::puts(msg);
    console::puts("\n");
}

// ascii logo
static void print_banner() {
    console::set_color(COLOR_BLUE, COLOR_BLACK);
    console::puts("__________          .___                    __  ________    _________\n");
    console::puts("\\______   \\____   __| _/_ __  _____ _____ _/  |_\\_____  \\  /   _____/\n");
    console::set_color(COLOR_CYAN, COLOR_BLACK);
    console::puts(" |     ___/  _ \\ / __ |  |  \\/     \\\\__  \\\\   __\\/   |   \\ \\_____  \\ \n");
    console::puts(" |    |  (  <_> ) /_/ |  |  /  Y Y  \\/ __ \\|  | /    |    \\/        \\\n");
    console::set_color(COLOR_GREEN, COLOR_BLACK);
    console::puts(" |____|   \\____/\\____ |____/|__|_|  (____  /__| \\_______  /_______  /\n");
    console::puts("                     \\/           \\/     \\/             \\/        \\/ \n");
    console::set_color(COLOR_GRAY, COLOR_BLACK);
    console::puts("                              ");
    console::puts(version::OS_NAME);
    console::puts(" v");
    console::puts(version::OS_VERSION);
    console::puts(" by Thinking Developer\n\n");
}

static void debug_u64(std::uint64_t v) {
    if (v == 0) { console::putc('0'); return; }
    char buf[24]; int i = 0;
    while (v > 0) { buf[i++] = '0' + (v % 10); v /= 10; }
    for (int j = i - 1; j >= 0; j--) console::putc(buf[j]);
}

static void debug_hex64(std::uint64_t v) {
    static const char hex[] = "0123456789ABCDEF";
    console::puts("0x");
    for (int i = 60; i >= 0; i -= 4) console::putc(hex[(v >> i) & 0xF]);
}

namespace {
__attribute__((used, section(".limine_requests")))
volatile std::uint64_t limine_base_revision[] = LIMINE_BASE_REVISION(6);

__attribute__((used, section(".limine_requests")))
volatile limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0,
    .response = nullptr
};

__attribute__((used, section(".limine_requests")))
volatile limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0,
    .response = nullptr
};

__attribute__((used, section(".limine_requests")))
volatile limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID,
    .revision = 0,
    .response = nullptr
};

__attribute__((used, section(".limine_requests")))
volatile limine_bootloader_info_request bootloader_info_request = {
    .id = LIMINE_BOOTLOADER_INFO_REQUEST_ID,
    .revision = 0,
    .response = nullptr
};

__attribute__((used, section(".limine_requests")))
volatile limine_firmware_type_request firmware_type_request = {
    .id = LIMINE_FIRMWARE_TYPE_REQUEST_ID,
    .revision = 0,
    .response = nullptr
};

__attribute__((used, section(".limine_requests")))
volatile limine_smbios_request smbios_request = {
    .id = LIMINE_SMBIOS_REQUEST_ID,
    .revision = 0,
    .response = nullptr
};

__attribute__((used, section(".limine_requests")))
volatile limine_rsdp_request rsdp_request = {
    .id = LIMINE_RSDP_REQUEST_ID,
    .revision = 0,
    .response = nullptr
};

__attribute__((used, section(".limine_requests")))
volatile limine_executable_cmdline_request executable_cmdline_request = {
    .id = LIMINE_EXECUTABLE_CMDLINE_REQUEST_ID,
    .revision = 0,
    .response = nullptr
};

__attribute__((used, section(".limine_requests")))
volatile limine_date_at_boot_request date_at_boot_request = {
    .id = LIMINE_DATE_AT_BOOT_REQUEST_ID,
    .revision = 0,
    .response = nullptr
};

__attribute__((used, section(".limine_requests_start")))
volatile std::uint64_t limine_requests_start_marker[] = LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end")))
volatile std::uint64_t limine_requests_end_marker[] = LIMINE_REQUESTS_END_MARKER;

void hcf() {
    for (;;) asm ("hlt");
}
}

extern "C" {
    int __cxa_atexit(void (*)(void *), void *, void *) { return 0; }
    void __cxa_pure_virtual() { hcf(); }
    void __cxa_deleted_virtual() { hcf(); }
    void *__dso_handle;
    int __cxa_guard_acquire(std::uint64_t *guard) { return *reinterpret_cast<std::uint8_t *>(guard) == 0; }
    void __cxa_guard_release(std::uint64_t *guard) { *reinterpret_cast<std::uint8_t *>(guard) = 1; }
}

extern void (*__init_array[])();
extern void (*__init_array_end[])();

extern "C" void kmain() {
    console::set_color(COLOR_GRAY, COLOR_BLACK);

    if (LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision) == false) hcf();

    for (std::size_t i = 0; &__init_array[i] != __init_array_end; i++) {
        __init_array[i]();
    }

    if (framebuffer_request.response == nullptr || framebuffer_request.response->framebuffer_count < 1) hcf();

    limine_framebuffer *framebuffer = framebuffer_request.response->framebuffers[0];
    console::init(framebuffer);

    print_banner();
    beep(880, 80);

    print_ok("Kernel loaded");
    print_info("Limine boot protocol: base revision 6");

    console::set_color(COLOR_CYAN, COLOR_BLACK);
    console::puts(" [INFO] Framebuffer: ");
    console::set_color(COLOR_GRAY, COLOR_BLACK);
    debug_u64(framebuffer->width); console::puts("x");
    debug_u64(framebuffer->height); console::puts("x");
    debug_u64(framebuffer->bpp); console::puts("bpp, pitch=");
    debug_u64(framebuffer->pitch); console::puts(" bytes\n");

    if (bootloader_info_request.response != nullptr) {
        const char* bname = bootloader_info_request.response->name;
        const char* bver  = bootloader_info_request.response->version;
        commands::set_bootloader_info(bname, bver);

        console::set_color(COLOR_CYAN, COLOR_BLACK);
        console::puts(" [INFO] Bootloader: ");
        console::set_color(COLOR_GRAY, COLOR_BLACK);
        console::puts(bname ? bname : "?");
        console::puts(" v");
        console::puts(bver ? bver : "?");
        console::putc('\n');
    } else {
        print_warn("No bootloader info from Limine");
    }

    if (firmware_type_request.response != nullptr) {
        commands::set_firmware_type(firmware_type_request.response->firmware_type);

        const char* fw = "Unknown";
        switch (firmware_type_request.response->firmware_type) {
            case LIMINE_FIRMWARE_TYPE_X86BIOS: fw = "BIOS";        break;
            case LIMINE_FIRMWARE_TYPE_EFI32:   fw = "UEFI 32-bit"; break;
            case LIMINE_FIRMWARE_TYPE_EFI64:   fw = "UEFI 64-bit"; break;
            case LIMINE_FIRMWARE_TYPE_SBI:     fw = "SBI";         break;
        }
        console::set_color(COLOR_CYAN, COLOR_BLACK);
        console::puts(" [INFO] Firmware: ");
        console::set_color(COLOR_GRAY, COLOR_BLACK);
        console::puts(fw);
        console::putc('\n');
    }

    if (executable_cmdline_request.response != nullptr &&
        executable_cmdline_request.response->cmdline != nullptr) {
        commands::set_cmdline(executable_cmdline_request.response->cmdline);

        console::set_color(COLOR_CYAN, COLOR_BLACK);
        console::puts(" [INFO] Kernel cmdline: ");
        console::set_color(COLOR_GRAY, COLOR_BLACK);
        console::puts(executable_cmdline_request.response->cmdline);
        console::putc('\n');
    }

    if (date_at_boot_request.response != nullptr) {
        commands::set_boot_timestamp(date_at_boot_request.response->timestamp);
    }

    // cpuid
    commands::init_cpuinfo();
    print_ok("CPUID info gathered");

    console::set_color(COLOR_CYAN, COLOR_BLACK);
    console::puts(" [INFO] CPU: ");
    console::set_color(COLOR_GRAY, COLOR_BLACK);
    console::puts(commands::cpu_info.brand);
    console::putc('\n');

    console::set_color(COLOR_CYAN, COLOR_BLACK);
    console::puts(" [INFO] Vendor: ");
    console::set_color(COLOR_GRAY, COLOR_BLACK);
    console::puts(commands::cpu_info.vendor);
    console::puts(" | ");
    if (commands::cpu_info.has_sse)  console::puts("SSE ");
    if (commands::cpu_info.has_sse2) console::puts("SSE2 ");
    if (commands::cpu_info.has_avx)  console::puts("AVX ");
    if (commands::cpu_info.has_avx2) console::puts("AVX2 ");
    console::putc('\n');

    if (memmap_request.response != nullptr) {
        commands::init_ram_info(memmap_request.response);
        print_ok("Memory map parsed");

        console::set_color(COLOR_CYAN, COLOR_BLACK);
        console::puts(" [INFO] RAM: total ");
        console::set_color(COLOR_GRAY, COLOR_BLACK);
        debug_u64(commands::ram_info.total_bytes / (1024 * 1024)); console::puts(" MB, usable ");
        debug_u64(commands::ram_info.usable_bytes / (1024 * 1024)); console::puts(" MB\n");
    } else {
        print_warn("No memory map from Limine");
    }

    idt::init();
    print_ok("IDT loaded (256 vectors)");

    pic::remap(32, 40);
    print_ok("PIC remapped (IRQ 32-47)");

    pit::init(100);
    print_ok("PIT initialized (100 Hz)");

    pci::init();
    print_ok("PCI enumerated");

    console::set_color(COLOR_CYAN, COLOR_BLACK);
    console::puts(" [INFO] PCI devices: ");
    console::set_color(COLOR_GRAY, COLOR_BLACK);
    debug_u64(pci::get_device_count());
    console::putc('\n');

    // HHDM offset
    uint64_t hhdm_offset = 0;
    if (hhdm_request.response != nullptr) {
        hhdm_offset = hhdm_request.response->offset;

        console::set_color(COLOR_CYAN, COLOR_BLACK);
        console::puts(" [INFO] HHDM offset: ");
        console::set_color(COLOR_GRAY, COLOR_BLACK);
        debug_hex64(hhdm_offset);
        console::putc('\n');
        print_ok("HHDM offset available");
    } else {
        print_warn("No HHDM from Limine");
    }

    if (smbios_request.response != nullptr) {
        dmi::init(smbios_request.response, hhdm_offset);
        if (dmi::available()) {
            print_ok("SMBIOS/DMI parsed");

            console::set_color(COLOR_CYAN, COLOR_BLACK);
            console::puts(" [INFO] Motherboard: ");
            console::set_color(COLOR_GRAY, COLOR_BLACK);
            console::puts(dmi::motherboard());
            console::putc('\n');

            console::set_color(COLOR_CYAN, COLOR_BLACK);
            console::puts(" [INFO] BIOS: ");
            console::set_color(COLOR_GRAY, COLOR_BLACK);
            console::puts(dmi::bios_vendor());
            console::puts(" ");
            console::puts(dmi::bios_version());
            console::putc('\n');
        } else {
            print_warn("SMBIOS table not usable");
        }
    } else {
        print_warn("No SMBIOS from Limine");
    }

    // acpi
    if (rsdp_request.response != nullptr && rsdp_request.response->address != nullptr) {
        commands::set_acpi_rsdp(rsdp_request.response->address, hhdm_offset);
    } else {
        print_warn("No RSDP from Limine - ACPI poweroff unavailable");
    }

    // dma bump allocator
    if (memmap_request.response != nullptr) {
        dma::init(memmap_request.response, hhdm_offset);
        if (dma::is_initialized()) {
            console::set_color(COLOR_CYAN, COLOR_BLACK);
            console::puts(" [INFO] DMA region phys=");
            console::set_color(COLOR_GRAY, COLOR_BLACK);
            debug_hex64(dma::get_phys_base());
            console::puts(" size=");
            debug_u64(dma::get_size() / (1024 * 1024));
            console::puts(" MB\n");
            print_ok("DMA bump allocator ready");
        } else {
            print_warn("DMA region not found (need 16 MB usable)");
        }
    }

    if (input::init(hhdm_offset)) {
        console::set_color(COLOR_CYAN, COLOR_BLACK);
        console::puts(" [INFO] Input source: ");
        console::set_color(COLOR_GRAY, COLOR_BLACK);
        console::puts(input::active_source_name());
        console::putc('\n');
        print_ok("Input initialized");
    } else {
        print_warn("No input device found");
    }

    // ─── Block device: AHCI → fallback ATA ───
    if (disk::init(hhdm_offset)) {
        console::set_color(COLOR_CYAN, COLOR_BLACK);
        console::puts(" [INFO] Block driver: ");
        console::set_color(COLOR_GRAY, COLOR_BLACK);
        console::puts(disk::active_driver_name());
        console::putc('\n');

        console::set_color(COLOR_CYAN, COLOR_BLACK);
        console::puts(" [INFO] Disk: ");
        console::set_color(COLOR_GRAY, COLOR_BLACK);
        console::puts(disk::info.model);
        console::puts(" ("); debug_u64(disk::info.sectors / 2048); console::puts(" MB)\n");
        print_ok("Block device initialized");
    } else {
        print_warn("No ATA/AHCI disk found");
    }

    if (disk::info.present) {
        mbr::MBR m;
        if (mbr::read(&m)) {
            if (m.signature == 0xAA55) {
                print_ok("MBR signature valid (0xAA55)");

                char next_letter = 'C';
                for (int i = 0; i < 4; i++) {
                    uint8_t type = m.partitions[i].type;
                    if (type == 0x0B || type == 0x0C) {
                        console::set_color(COLOR_CYAN, COLOR_BLACK);
                        console::puts(" [INFO] Found FAT32 at LBA ");
                        console::set_color(COLOR_GRAY, COLOR_BLACK);
                        debug_u64(m.partitions[i].lba_start);
                        console::putc('\n');

                        if (fat32::mount(m.partitions[i].lba_start, next_letter)) {
                            console::set_color(COLOR_GREEN, COLOR_BLACK);
                            console::puts(" [ OK ] Mounted as ");
                            console::putc(next_letter);
                            console::puts(":\n");
                            console::set_color(COLOR_GRAY, COLOR_BLACK);
                            next_letter++;
                            if (next_letter > 'Z') break;
                        } else {
                            print_warn("Mount failed");
                        }
                    }
                }
            } else {
                print_warn("No valid MBR signature");
            }
        }
    } else {
        print_info("No disk, skipping FAT32 mount");
    }

    driver_registry::clear();
    driver_registry::add("serial",      "COM1 (0x3F8)",   true);
    driver_registry::add("framebuffer", "VESA (Limine)",  true);
    driver_registry::add("input",       input::active_source_name(),
                        input::active_source() != input::Source::NONE);
    driver_registry::add("pit",         "PIT @ 100 Hz",   true);
    driver_registry::add("pci",         "PCI enumeration", true);

    if (disk::info.present) {
        driver_registry::add(disk::active_driver_name(), disk::info.model, true);
    } else {
        driver_registry::add("block", "no disk", false);
    }

    if (fat32::is_mounted()) {
        driver_registry::add("fat32", "mounted", true);
    } else {
        driver_registry::add("fat32", "not mounted", false);
    }

    asm volatile("sti");
    print_ok("Interrupts enabled (IF=1)");

    beep(1320, 60);

    console::puts("\n");
    console::set_color(COLOR_WHITE, COLOR_BLACK);
    console::puts("Welcome to ");
    console::puts(version::OS_NAME);
    console::puts(" v");
    console::puts(version::OS_VERSION);
    console::putc('\n');
    console::set_color(COLOR_GRAY, COLOR_BLACK);
    console::puts("\n");

    shell::run();
    hcf();
}