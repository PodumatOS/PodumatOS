// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Thinking Developer
//
// PodumatOS — a hobby operating system for x86_64.

#ifndef INPUT_HPP
#define INPUT_HPP

#include <cstdint>

namespace input {

    enum class Source {
        NONE = 0,
        PS2,
        USB
    };

    bool init(uint64_t hhdm_offset);
    Source active_source();
    const char* active_source_name();

    char     get_char();
    uint16_t get_key();
    bool     has_data();
    void     poll();
	void stop();

}

#endif