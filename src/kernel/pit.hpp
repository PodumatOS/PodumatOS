// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Thinking Developer
//
// PodumatOS — a hobby operating system for x86_64.

#ifndef PIT_HPP
#define PIT_HPP

#include <cstdint>

namespace pit {
    extern volatile std::uint64_t ticks;
    void init(std::uint32_t hz);
    void handle();
    void sleep(std::uint64_t ms);
}

#endif