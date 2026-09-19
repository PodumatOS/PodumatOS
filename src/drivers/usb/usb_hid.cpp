// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Thinking Developer
//
// PodumatOS — a hobby operating system for x86_64.

#include "drivers/usb/usb_hid.hpp"
#include "drivers/usb/ehci.hpp"
#include "drivers/usb/usb.hpp"
#include "kernel/pit.hpp"
#include "lib/memory.hpp"
#include "io/io.hpp"

namespace usb_hid {

    static bool     g_ready       = false;
    static uint8_t  g_dev_addr    = 0;
    static uint8_t  g_ep_num      = 0;
    static uint16_t g_ep_mps      = 8;
    static uint8_t  g_ep_interval = 10;
    static uint8_t  g_iface_num   = 0;

    static uint8_t  g_prev[8];
    static uint8_t  g_curr[8];

    static char     g_pending_char = 0;
    static uint16_t g_pending_key  = 0;

    static bool     g_caps_lock = false;

    static constexpr uint32_t REPEAT_DELAY_TICKS = 50;
    static constexpr uint32_t REPEAT_RATE_TICKS  = 5;

    static uint8_t  g_held_usage    = 0;
    static uint32_t g_held_start    = 0;
    static uint32_t g_last_repeat   = 0;

    static void serial_puts(const char* s) {
        while (*s) {
            if (*s == '\n') { while ((inb(0x3F8+5) & 0x20)==0){} outb(0x3F8,'\r'); }
            while ((inb(0x3F8+5) & 0x20)==0){}
            outb(0x3F8, *s++);
        }
    }
    static void serial_hex8(uint8_t v) {
        static const char hex[] = "0123456789ABCDEF";
        char b[5] = {'0','x', hex[(v>>4)&0xF], hex[v&0xF], 0};
        serial_puts(b);
    }
    static void serial_hex16(uint16_t v) {
        static const char hex[] = "0123456789ABCDEF";
        char b[7] = {'0','x', hex[(v>>12)&0xF], hex[(v>>8)&0xF], hex[(v>>4)&0xF], hex[v&0xF], 0};
        serial_puts(b);
    }
    static void serial_dec(uint32_t v) {
        char buf[12]; int i = 0;
        if (v == 0) { serial_puts("0"); return; }
        while (v > 0) { buf[i++] = '0' + (v % 10); v /= 10; }
        char out[12]; int k = 0;
        for (int j = i - 1; j >= 0; j--) out[k++] = buf[j];
        out[k] = 0;
        serial_puts(out);
    }

    static const char g_map_lo[256] = {
        0, 0, 0, 0,
        'a','b','c','d','e','f','g','h','i','j','k','l','m',
        'n','o','p','q','r','s','t','u','v','w','x','y','z',
        '1','2','3','4','5','6','7','8','9',
        '0',
        '\n', 27, '\b', '\t', ' ',
        '-', '=', '[', ']', '\\', 0, ';', '\'', '`', ',', '.', '/',
    };

    static const char g_map_hi[256] = {
        0, 0, 0, 0,
        'A','B','C','D','E','F','G','H','I','J','K','L','M',
        'N','O','P','Q','R','S','T','U','V','W','X','Y','Z',
        '!','@','#','$','%','^','&','*','(',
        ')',
        '\n', 27, '\b', '\t', ' ',
        '_', '+', '{', '}', '|', 0, ':', '"', '~', '<', '>', '?',
    };

    static bool is_special(uint8_t usage) {
        if (usage >= 0x3A && usage <= 0x45) return true;
        if (usage >= 0x4F && usage <= 0x52) return true;
        return false;
    }

    static bool is_held(uint8_t usage) {
        for (int i = 2; i < 8; i++) {
            if (g_curr[i] == usage) return true;
        }
        return false;
    }

    static void usage_to_output(uint8_t usage, bool shift, char* out_char, uint16_t* out_key) {
        *out_char = 0;
        *out_key  = 0;

        if (usage >= 0x04 && usage <= 0x38) {
            char c;
            if (usage >= 0x04 && usage <= 0x1D) {
                bool upper = shift ^ g_caps_lock;
                c = upper ? g_map_hi[usage] : g_map_lo[usage];
            } else {
                c = shift ? g_map_hi[usage] : g_map_lo[usage];
            }

            if (c != 0) {
                *out_char = c;
            } else if (is_special(usage)) {
                *out_key = KEY_SPECIAL_BASE + usage;
            }
        } else if (is_special(usage)) {
            *out_key = KEY_SPECIAL_BASE + usage;
        }
    }

    static bool parse_config_for_hid(const uint8_t* cfg, uint16_t len) {
        const uint8_t* p   = cfg;
        const uint8_t* end = cfg + len;

        bool in_hid_iface = false;

        while (p + 2 <= end) {
            uint8_t bLength = p[0];
            uint8_t bType   = p[1];

            serial_puts("[hid] desc type=");
            serial_hex8(bType);
            serial_puts(" len=");
            serial_dec(bLength);
            serial_puts("\n");

            if (bLength == 0) break;
            if (p + bLength > end) {
                serial_puts("[hid] descriptor overruns buffer, stop\n");
                break;
            }

            if (bType == usb::DESC_INTERFACE && bLength >= 9) {
                const usb::InterfaceDescriptor* id = (const usb::InterfaceDescriptor*)p;

                serial_puts("[hid] iface class=");
                serial_hex8(id->bInterfaceClass);
                serial_puts(" subclass=");
                serial_hex8(id->bInterfaceSubClass);
                serial_puts(" proto=");
                serial_hex8(id->bInterfaceProtocol);
                serial_puts(" num=");
                serial_dec(id->bInterfaceNumber);
                serial_puts(" eps=");
                serial_dec(id->bNumEndpoints);
                serial_puts("\n");

                if (id->bInterfaceClass == 0x03) {
                    in_hid_iface = true;
                    g_iface_num  = id->bInterfaceNumber;
                } else {
                    in_hid_iface = false;
                }
            }
            else if (bType == usb::DESC_ENDPOINT && bLength >= 7) {
                const usb::EndpointDescriptor* ed = (const usb::EndpointDescriptor*)p;

                serial_puts("[hid] endpoint addr=");
                serial_hex8(ed->bEndpointAddress);
                serial_puts(" attr=");
                serial_hex8(ed->bmAttributes);
                serial_puts(" mps=");
                serial_dec(ed->wMaxPacketSize & 0x7FF);
                serial_puts(" interval=");
                serial_dec(ed->bInterval);
                serial_puts("\n");

                if (!in_hid_iface) {
                    p += bLength;
                    continue;
                }

                bool is_in   = (ed->bEndpointAddress & 0x80) != 0;
                uint8_t attr = ed->bmAttributes & 0x03;

                if (is_in && attr == 0x03) {
                    g_ep_num      = ed->bEndpointAddress & 0x0F;
                    g_ep_mps      = ed->wMaxPacketSize & 0x7FF;
                    g_ep_interval = ed->bInterval;
                    if (g_ep_mps > 64) g_ep_mps = 64;

                    serial_puts("[hid] HID ep found: ep=0x");
                    serial_hex8(g_ep_num);
                    serial_puts(" mps=");
                    serial_dec(g_ep_mps);
                    serial_puts(" interval=");
                    serial_dec(g_ep_interval);
                    serial_puts("\n");
                    return true;
                }
            }

            p += bLength;
        }
        return false;
    }

    bool init() {
        serial_puts("[hid] ==== init ====\n");

        g_ready       = false;
        g_dev_addr    = 0;
        g_ep_num      = 0;
        g_ep_mps      = 8;
        g_ep_interval = 10;
        g_iface_num   = 0;
        g_pending_char = 0;
        g_pending_key  = 0;
        g_caps_lock    = false;
        g_held_usage   = 0;
        g_held_start   = 0;
        g_last_repeat  = 0;
        memset(g_prev, 0, 8);
        memset(g_curr, 0, 8);

        if (!ehci::is_present()) {
            serial_puts("[hid] ehci not present\n");
            return false;
        }

        if (!usb::enumerate_device(1)) {
            serial_puts("[hid] enumerate failed\n");
            return false;
        }
        g_dev_addr = usb::get_device_addr();

        uint16_t mps0 = usb::get_device_mps();
        serial_puts("[hid] using cached mps=");
        serial_dec(mps0);
        serial_puts(" addr=");
        serial_dec(g_dev_addr);
        serial_puts("\n");

        usb::ConfigDescriptor cd;
        memset(&cd, 0, sizeof(cd));
        if (!usb::get_descriptor(g_dev_addr, mps0, usb::DESC_CONFIGURATION, 0, 0, &cd, 9)) {
            serial_puts("[hid] GET_DESC(CONFIG,9) failed\n");
            return false;
        }
        uint16_t total = cd.wTotalLength;
        serial_puts("[hid] wTotalLength=");
        serial_dec(total);
        serial_puts("\n");
        if (total == 0 || total > 4096) return false;

        uint8_t cfg_buf[4096];
        memset(cfg_buf, 0, sizeof(cfg_buf));
        if (!usb::get_descriptor(g_dev_addr, mps0, usb::DESC_CONFIGURATION, 0, 0, cfg_buf, total)) {
            serial_puts("[hid] GET_DESC(CONFIG,full) failed\n");
            return false;
        }

        if (!parse_config_for_hid(cfg_buf, total)) {
            serial_puts("[hid] no HID interrupt IN endpoint\n");
            return false;
        }

        usb::send_setup(g_dev_addr, mps0, 0x21, 0x0B, 0x0000, g_iface_num, 0, nullptr);
        usb::send_setup(g_dev_addr, mps0, 0x21, 0x0A, 0x0000, g_iface_num, 0, nullptr);

        if (!ehci::intr_open(g_dev_addr, g_ep_num, g_ep_mps, g_ep_interval)) {
            serial_puts("[hid] intr_open failed\n");
            return false;
        }

        g_ready = true;
        serial_puts("[hid] ==== init OK ====\n");
        return true;
    }

    bool poll() {
        if (!g_ready) return false;

        uint8_t report[8];
        memset(report, 0, 8);

        if (!ehci::intr_read(report, 8)) {
            goto check_repeat;
        }

        memcpy(g_prev, g_curr, 8);
        memcpy(g_curr, report, 8);

        {
            uint8_t modifiers = g_curr[0];
            bool shift = (modifiers & 0x22) != 0;

            for (int i = 2; i < 8; i++) {
                uint8_t usage = g_curr[i];
                if (usage == 0) continue;

                bool is_new = true;
                for (int j = 2; j < 8; j++) {
                    if (g_prev[j] == usage) { is_new = false; break; }
                }
                if (!is_new) continue;

                if (usage == 0x39) {
                    g_caps_lock = !g_caps_lock;
                    serial_puts("[hid] Caps Lock ");
                    serial_puts(g_caps_lock ? "ON\n" : "OFF\n");
                    continue;
                }

                char     c = 0;
                uint16_t k = 0;
                usage_to_output(usage, shift, &c, &k);
                if (c != 0) g_pending_char = c;
                else if (k != 0) g_pending_key = k;

                g_held_usage  = usage;
                g_held_start  = (uint32_t)pit::ticks;
                g_last_repeat = g_held_start;
            }
        }

    check_repeat:
        if (g_held_usage == 0) return true;

        if (!is_held(g_held_usage)) {
            g_held_usage = 0;
            return true;
        }

        {
            uint32_t now = (uint32_t)pit::ticks;

            if (now - g_held_start < REPEAT_DELAY_TICKS) return true;
            if (now - g_last_repeat < REPEAT_RATE_TICKS) return true;

            g_last_repeat = now;

            uint8_t modifiers = g_curr[0];
            bool shift = (modifiers & 0x22) != 0;

            char     c = 0;
            uint16_t k = 0;
            usage_to_output(g_held_usage, shift, &c, &k);
            if (c != 0) g_pending_char = c;
            else if (k != 0) g_pending_key = k;
        }

        return true;
    }

    char get_char() {
        if (g_pending_char) {
            char c = g_pending_char;
            g_pending_char = 0;
            return c;
        }
        if (!poll()) return 0;
        if (g_pending_char) {
            char c = g_pending_char;
            g_pending_char = 0;
            return c;
        }
        return 0;
    }

    uint16_t get_key() {
        if (g_pending_key) {
            uint16_t k = g_pending_key;
            g_pending_key = 0;
            return k;
        }
        if (!poll()) return 0;
        if (g_pending_key) {
            uint16_t k = g_pending_key;
            g_pending_key = 0;
            return k;
        }
        return 0;
    }

    bool has_data() {
        return g_pending_char != 0 || g_pending_key != 0;
    }

}