// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Thinking Developer
//
// PodumatOS — a hobby operating system for x86_64.

#include "kernel/pit.hpp"
#include <cstdint>

static inline void outb(std::uint16_t port, std::uint8_t val) {
    asm volatile ( "outb %0, %1" : : "a"(val), "Nd"(port) );
}

namespace pit {
    volatile std::uint64_t ticks = 0;

    void init(std::uint32_t hz) {
        std::uint32_t divisor = 1193180 / hz;
        outb(0x43, 0x36);
        outb(0x40, static_cast<std::uint8_t>(divisor & 0xFF));
        outb(0x40, static_cast<std::uint8_t>((divisor >> 8) & 0xFF));
    }

    void handle() {
        ticks = ticks + 1; 
    }

    void sleep(std::uint64_t ms) {
        std::uint64_t wait_ticks = ms / 10;
        if (wait_ticks == 0 && ms > 0) wait_ticks = 1;

        std::uint64_t end = ticks + wait_ticks;
        while (ticks < end) {
            asm volatile("hlt");
        }
    }
}