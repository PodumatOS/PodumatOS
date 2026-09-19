// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Thinking Developer
//
// PodumatOS — a hobby operating system for x86_64.

#ifndef PS2_HPP
#define PS2_HPP

#include <cstdint>

namespace ps2 {
    void init();
    bool has_data();
    std::uint8_t read_data();
    char get_char();
	void stop();
}

#endif