// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Thinking Developer
//
// PodumatOS — a hobby operating system for x86_64.

#ifndef USB_HID_HPP
#define USB_HID_HPP

#include <cstdint>

namespace usb_hid {

    bool     init();
    bool     poll();
    char     get_char();
    uint16_t get_key();
    bool     has_data();

    constexpr uint16_t KEY_NONE  = 0;
    constexpr uint16_t KEY_SPECIAL_BASE = 0x100;

}

#endif