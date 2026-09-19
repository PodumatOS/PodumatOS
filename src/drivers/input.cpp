// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Thinking Developer
//
// PodumatOS — a hobby operating system for x86_64.

#include "drivers/input.hpp"
#include "drivers/ps2.hpp"
#include "drivers/usb/ehci.hpp"
#include "drivers/usb/usb_hid.hpp"

namespace input {

    static Source g_source = Source::NONE;

    bool init(uint64_t hhdm_offset) {
        g_source = Source::NONE;

        if (ehci::init(hhdm_offset)) {
            if (usb_hid::init()) {
                g_source = Source::USB;
                return true;
            }
        }

        ps2::init();
        g_source = Source::PS2;
        return true;
    }

    Source active_source() { return g_source; }

    const char* active_source_name() {
        switch (g_source) {
            case Source::USB: return "USB HID";
            case Source::PS2: return "PS/2";
            default:          return "none";
        }
    }

    char get_char() {
        switch (g_source) {
            case Source::USB: return usb_hid::get_char();
            case Source::PS2: return ps2::get_char();
            default:          return 0;
        }
    }

    uint16_t get_key() {
        switch (g_source) {
            case Source::USB: return usb_hid::get_key();
            default:          return 0;
        }
    }

    bool has_data() {
        switch (g_source) {
            case Source::USB: return usb_hid::has_data();
            case Source::PS2: return ps2::has_data();
            default:          return false;
        }
    }

    void poll() {
        if (g_source == Source::USB) usb_hid::poll();
    }
	
	void stop() {
        g_source = Source::NONE;
    }

}