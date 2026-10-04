// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Thinking Developer
//
// PodumatOS — a hobby operating system for x86_64.

#include "lib/dmi.hpp"
#include "lib/memory.hpp"
#include "io/io.hpp"

namespace dmi {

    static bool g_available = false;
    static char g_motherboard[128];
    static char g_product[128];
    static char g_bios_vendor[64];
    static char g_bios_version[64];
    static char g_bios_date[64];

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

    //  HHDM conversion
    static uint64_t to_virt(uint64_t phys, uint64_t hhdm) {
        if (hhdm == 0) return phys;
        if (phys >= hhdm) return phys;
        return phys + hhdm;
    }

    static void copy_str(char* dst, std::size_t dst_size, const char* src) {
        if (!dst_size) return;
        if (!src) { dst[0] = '\0'; return; }
        std::size_t i = 0;
        while (src[i] && i + 1 < dst_size) { dst[i] = src[i]; i++; }
        dst[i] = '\0';
    }

    static void copy_join(char* dst, std::size_t dst_size,
                          const char* a, const char* b) {
        if (!dst_size) return;
        std::size_t cur = 0;
        if (a) {
            while (a[cur] && cur + 1 < dst_size) { dst[cur] = a[cur]; cur++; }
        }
        if (b && b[0]) {
            if (cur > 0 && cur + 1 < dst_size) dst[cur++] = ' ';
            std::size_t i = 0;
            while (b[i] && cur + 1 < dst_size) { dst[cur++] = b[i++]; }
        }
        dst[cur] = '\0';
    }

    // returns a pointer to string idx from str_table, or nullptr.
    static const char* get_string(const uint8_t* str_table,
                                  const uint8_t* end, uint8_t idx) {
        if (idx == 0) return nullptr;
        if (!str_table || str_table >= end) return nullptr;

        uint8_t cur = 1;
        const uint8_t* p = str_table;
        while (p < end && *p) {
            if (cur == idx) return (const char*)p;
            while (p < end && *p) p++;
            if (p >= end) return nullptr;
            p++;
            cur++;
        }
        return nullptr;
    }

    static const uint8_t* next_struct(const uint8_t* s, const uint8_t* end) {
        if (s + 1 >= end) return end;

        uint8_t hlen = s[1];
        if (hlen < 4) return end;
        if (s + hlen >= end) return end;

        const uint8_t* p = s + hlen;
        while (p + 1 < end) {
            if (p[0] == 0 && p[1] == 0) return p + 2;
            p++;
        }
        return end;
    }

    //  searching entry point
    struct EntryInfo {
        uint64_t table_phys;
        uint32_t table_max_size;
        bool     valid;
    };

    static EntryInfo parse_entry_64(const uint8_t* ep,
                                    const uint8_t* ep_end,
                                    uint64_t hhdm) {
        (void)hhdm;
        EntryInfo r = {0, 0, false};
        if (ep + 0x18 > ep_end) return r;

        if (!(ep[0] == '_' && ep[1] == 'S' && ep[2] == 'M' &&
              ep[3] == '3' && ep[4] == '_')) return r;

        uint32_t max_size = 0;
        memcpy(&max_size, ep + 0x0C, 4);

        uint64_t addr = 0;
        memcpy(&addr, ep + 0x10, 8);

        if (!addr) return r;

        r.table_phys     = addr;
        r.table_max_size = max_size;
        r.valid          = true;
        return r;
    }

    static EntryInfo parse_entry_32(const uint8_t* ep,
                                    const uint8_t* ep_end,
                                    uint64_t hhdm) {
        (void)hhdm;
        EntryInfo r = {0, 0, false};
        if (ep + 0x1F > ep_end) return r;

        if (!(ep[0] == '_' && ep[1] == 'S' && ep[2] == 'M' && ep[3] == '_')) return r;

        uint16_t len = 0;
        memcpy(&len, ep + 0x16, 2);

        uint32_t addr = 0;
        memcpy(&addr, ep + 0x18, 4);

        if (!addr) return r;

        r.table_phys     = (uint64_t)addr;
        r.table_max_size = (uint32_t)len;
        r.valid          = true;
        return r;
    }

    void init(limine_smbios_response* response, uint64_t hhdm_offset) {
        g_available = false;
        g_motherboard[0]  = '\0';
        g_product[0]      = '\0';
        g_bios_vendor[0]  = '\0';
        g_bios_version[0] = '\0';
        g_bios_date[0]    = '\0';

        serial_init();
        serial_puts("\n[dmi] ==== init ====\n");

        if (!response) {
            serial_puts("[dmi] response == nullptr\n");
            return;
        }

        serial_puts("[dmi] hhdm=");      serial_hex64(hhdm_offset);      serial_puts("\n");
        serial_puts("[dmi] entry_32=");  serial_hex64((uint64_t)response->entry_32); serial_puts("\n");
        serial_puts("[dmi] entry_64=");  serial_hex64((uint64_t)response->entry_64); serial_puts("\n");

        EntryInfo info = {0, 0, false};

        if (response->entry_64) {
            uint64_t ep_phys = (uint64_t)response->entry_64;
            uint64_t ep_virt = to_virt(ep_phys, hhdm_offset);
            serial_puts("[dmi] entry_64 phys="); serial_hex64(ep_phys);
            serial_puts(" virt=");                serial_hex64(ep_virt);
            serial_puts("\n");

            const uint8_t* ep = (const uint8_t*)ep_virt;
            const uint8_t* ep_end = ep + 0x20;
            info = parse_entry_64(ep, ep_end, hhdm_offset);

            if (info.valid) {
                serial_puts("[dmi] using entry_64, table=");
                serial_hex64(info.table_phys);
                serial_puts(" size=");
                serial_hex64(info.table_max_size);
                serial_puts("\n");
            } else {
                serial_puts("[dmi] entry_64 anchor/parse failed\n");
            }
        }

        if (!info.valid && response->entry_32) {
            uint64_t ep_phys = (uint64_t)response->entry_32;
            uint64_t ep_virt = to_virt(ep_phys, hhdm_offset);
            serial_puts("[dmi] entry_32 phys="); serial_hex64(ep_phys);
            serial_puts(" virt=");                serial_hex64(ep_virt);
            serial_puts("\n");

            const uint8_t* ep = (const uint8_t*)ep_virt;
            const uint8_t* ep_end = ep + 0x20;
            info = parse_entry_32(ep, ep_end, hhdm_offset);

            if (info.valid) {
                serial_puts("[dmi] using entry_32, table=");
                serial_hex64(info.table_phys);
                serial_puts(" size=");
                serial_hex64(info.table_max_size);
                serial_puts("\n");
            } else {
                serial_puts("[dmi] entry_32 anchor/parse failed\n");
            }
        }

        if (!info.valid) {
            serial_puts("[dmi] no valid SMBIOS entry point\n");
            return;
        }

        uint64_t table_virt_u64 = to_virt(info.table_phys, hhdm_offset);
        const uint8_t* table = (const uint8_t*)table_virt_u64;

        uint32_t table_len = info.table_max_size ? info.table_max_size : 0xFFFF;
        const uint8_t* end = table + table_len;

        serial_puts("[dmi] table_phys="); serial_hex64(info.table_phys);
        serial_puts(" table_virt=");       serial_hex64(table_virt_u64);
        serial_puts(" len=");              serial_hex64(table_len);
        serial_puts("\n");

        const uint8_t* s = table;
        int guard = 0;
        while (s + 4 <= end && guard++ < 1024) {
            uint8_t type = s[0];
            uint8_t len  = s[1];

            serial_puts("[dmi] type=");
            serial_hex64(type);
            serial_puts(" len=");
            serial_hex64(len);
            serial_puts("\n");

            if (type == 127) {
                serial_puts("[dmi] reached Type 127 (end)\n");
                break;
            }
            if (len < 4) {
                serial_puts("[dmi] header too short, abort\n");
                break;
            }
            if (s + len > end) {
                serial_puts("[dmi] struct overflows table, abort\n");
                break;
            }

            const uint8_t* str_table = s + len;

            if (type == 0 && len >= 0x08) {
                const char* v   = get_string(str_table, end, s[0x04]);
                const char* ver = get_string(str_table, end, s[0x05]);
                const char* d   = get_string(str_table, end, s[0x07]);
                copy_str(g_bios_vendor,  sizeof(g_bios_vendor),  v);
                copy_str(g_bios_version, sizeof(g_bios_version), ver);
                copy_str(g_bios_date,    sizeof(g_bios_date),    d);
                serial_puts("[dmi] type 0 (BIOS) found\n");
            } else if (type == 1 && len >= 0x06) {
                const char* m = get_string(str_table, end, s[0x04]);
                const char* p = get_string(str_table, end, s[0x05]);
                copy_join(g_product, sizeof(g_product), m, p);
                serial_puts("[dmi] type 1 (System) found\n");
            } else if (type == 2 && len >= 0x06) {
                const char* m = get_string(str_table, end, s[0x04]);
                const char* p = get_string(str_table, end, s[0x05]);
                copy_join(g_motherboard, sizeof(g_motherboard), m, p);
                serial_puts("[dmi] type 2 (Baseboard) found\n");
            }

            const uint8_t* next = next_struct(s, end);
            if (next <= s || next > end) {
                serial_puts("[dmi] next_struct returned invalid, abort\n");
                break;
            }
            s = next;
        }

        g_available = true;
        serial_puts("[dmi] init done\n\n");
    }

    bool available() { return g_available; }

    const char* motherboard() {
        return g_motherboard[0] ? g_motherboard : "Unknown";
    }
    const char* product() {
        return g_product[0] ? g_product : "Unknown";
    }
    const char* bios_vendor() {
        return g_bios_vendor[0] ? g_bios_vendor : "Unknown";
    }
    const char* bios_version() {
        return g_bios_version[0] ? g_bios_version : "Unknown";
    }
    const char* bios_date() {
        return g_bios_date[0] ? g_bios_date : "Unknown";
    }
	
	void stop() {
        g_available = false;
        g_motherboard[0]  = '\0';
        g_product[0]      = '\0';
        g_bios_vendor[0]  = '\0';
        g_bios_version[0] = '\0';
        g_bios_date[0]    = '\0';
    }

}