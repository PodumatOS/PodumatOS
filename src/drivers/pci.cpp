// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Thinking Developer
//
// PodumatOS — a hobby operating system for x86_64.

#include "drivers/pci.hpp"
#include "io/io.hpp"
#include "console/console.hpp"
#include "lib/string.hpp"

namespace pci {

    static Device devices[256];
    static int device_count = 0;

    // config pci
    uint32_t read_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
        uint32_t address = (uint32_t)((1 << 31) |
                                      ((uint32_t)bus << 16) |
                                      ((uint32_t)slot << 11) |
                                      ((uint32_t)func << 8) |
                                      (offset & 0xFC));
        outl(0xCF8, address);
        return inl(0xCFC);
    }

    // writing pci
    void write_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value) {
        uint32_t address = (uint32_t)((1 << 31) |
                                      ((uint32_t)bus << 16) |
                                      ((uint32_t)slot << 11) |
                                      ((uint32_t)func << 8) |
                                      (offset & 0xFC));
        outl(0xCF8, address);
        outl(0xCFC, value);
    }

    // scan devices
    static void scan_function(uint8_t bus, uint8_t slot, uint8_t func) {
        uint32_t reg0 = read_config(bus, slot, func, 0x00);
        uint16_t vendor_id = reg0 & 0xFFFF;
        if (vendor_id == 0xFFFF) return;
		
        if (device_count >= 256) return;

        Device* d = &devices[device_count++];
        d->bus      = bus;
        d->slot     = slot;
        d->func     = func;
        d->vendor_id = vendor_id;
        d->device_id = (reg0 >> 16) & 0xFFFF;

        uint32_t reg8 = read_config(bus, slot, func, 0x08);
        d->revision    = reg8 & 0xFF;
        d->prog_if     = (reg8 >> 8) & 0xFF;
        d->subclass    = (reg8 >> 16) & 0xFF;
        d->class_code  = (reg8 >> 24) & 0xFF;

        uint32_t regC = read_config(bus, slot, func, 0x0C);
        d->header_type = (regC >> 16) & 0xFF;

        if ((d->header_type & 0x7F) == 0x00) {
            for (int i = 0; i < 6; i++) {
                d->bar[i] = read_config(bus, slot, func, 0x10 + i * 4);
            }
        } else {
            for (int i = 0; i < 6; i++) d->bar[i] = 0;
        }

        // irq line/pin (offset 0x3C)
        uint32_t reg3C = read_config(bus, slot, func, 0x3C);
        d->irq_line = reg3C & 0xFF;
        d->irq_pin  = (reg3C >> 8) & 0xFF;
    }

    void init() {
        device_count = 0;

        for (uint16_t bus = 0; bus < 256; bus++) {
            for (uint8_t slot = 0; slot < 32; slot++) {
                uint32_t reg0 = read_config(bus, slot, 0, 0x00);
                uint16_t vendor = reg0 & 0xFFFF;
                if (vendor == 0xFFFF) continue;

                scan_function(bus, slot, 0);

                uint32_t regC = read_config(bus, slot, 0, 0x0C);
                uint8_t header = (regC >> 16) & 0xFF;
                if (header & 0x80) {
                    for (uint8_t func = 1; func < 8; func++) {
                        uint32_t reg = read_config(bus, slot, func, 0x00);
                        if ((reg & 0xFFFF) != 0xFFFF) {
                            scan_function(bus, slot, func);
                        }
                    }
                }
            }
        }
    }

    int get_device_count() { return device_count; }
    Device* get_device(int index) {
        if (index < 0 || index >= device_count) return nullptr;
        return &devices[index];
    }

    Device* find_by_class(uint8_t class_code, uint8_t subclass) {
        for (int i = 0; i < device_count; i++) {
            if (devices[i].class_code == class_code &&
                devices[i].subclass == subclass) {
                return &devices[i];
            }
        }
        return nullptr;
    }

    // class name
    const char* class_name(uint8_t c, uint8_t s) {
        switch (c) {
            case 0x00: return "Unclassified";
            case 0x01:
                switch (s) {
                    case 0x00: return "SCSI controller";
                    case 0x01: return "IDE controller";
                    case 0x06: return "SATA controller";
                    default:   return "Storage controller";
                }
            case 0x02:
                switch (s) {
                    case 0x00: return "Ethernet";
                    default:   return "Network controller";
                }
            case 0x03:
                switch (s) {
                    case 0x00: return "VGA controller";
                    default:   return "Display controller";
                }
            case 0x04: return "Multimedia device";
            case 0x05: return "Memory controller";
            case 0x06:
                switch (s) {
                    case 0x00: return "Host bridge";
                    case 0x01: return "ISA bridge";
                    case 0x04: return "PCI bridge";
                    default:   return "Bridge";
                }
            case 0x07: return "Communication controller";
            case 0x08: return "System peripheral";
            case 0x09: return "Input device";
            case 0x0A: return "Docking station";
            case 0x0B: return "Processor";
            case 0x0C:
                switch (s) {
                    case 0x03: return "USB controller";
                    default:   return "Serial bus controller";
                }
            case 0x0D: return "Wireless controller";
            case 0x0E: return "Intelligent I/O";
            case 0x0F: return "Satellite controller";
            case 0x10: return "Encryption controller";
            case 0x11: return "Signal processing";
            default:   return "Unknown";
        }
    }

    static const char* hex_chars = "0123456789ABCDEF";

    static void print_hex16(uint16_t v) {
        for (int i = 12; i >= 0; i -= 4) console::putc(hex_chars[(v >> i) & 0xF]);
    }
    static void print_hex8(uint8_t v) {
        console::putc(hex_chars[(v >> 4) & 0xF]);
        console::putc(hex_chars[v & 0xF]);
    }
    static void print_dec(uint32_t v) {
        if (v == 0) { console::putc('0'); return; }
        char buf[12]; int i = 0;
        while (v > 0) { buf[i++] = '0' + (v % 10); v /= 10; }
        for (int j = i - 1; j >= 0; j--) console::putc(buf[j]);
    }

    void print_all() {
        console::puts("PCI devices: ");
        print_dec(device_count);
        console::puts("\n\n");

        console::puts("BDF      Vendor Device Class          Description\n");
        console::puts("--------------------------------------------------------\n");

        for (int i = 0; i < device_count; i++) {
            Device* d = &devices[i];

            print_hex8(d->bus);
            console::putc(':');
            print_hex8(d->slot);
            console::putc('.');
            console::putc(hex_chars[d->func & 0xF]);
            console::puts("  ");

            print_hex16(d->vendor_id);
            console::putc(' ');
            print_hex16(d->device_id);
            console::putc(' ');

            print_hex8(d->class_code);
            console::putc(':');
            print_hex8(d->subclass);
            console::puts(" ");

            console::puts(class_name(d->class_code, d->subclass));
            console::putc('\n');
        }
    }
}