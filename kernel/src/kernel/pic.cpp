// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Thinking Developer
//
// PodumatOS — a hobby operating system for x86_64.

#include "kernel/pic.hpp"
#include <cstdint>

static inline void outb(std::uint16_t port, std::uint8_t val) {
    asm volatile ( "outb %0, %1" : : "a"(val), "Nd"(port) );
}

static inline std::uint8_t inb(std::uint16_t port) {
    std::uint8_t ret;
    asm volatile ( "inb %1, %0" : "=a"(ret) : "Nd"(port) );
    return ret;
}

static inline void io_wait() {
    asm volatile ( "outb %%al, $0x80" : : "a"(0) );
}

namespace pic {
    void remap(std::uint8_t offset1, std::uint8_t offset2) {
        [[maybe_unused]] std::uint8_t a1 = inb(0x21);
        [[maybe_unused]] std::uint8_t a2 = inb(0xA1);

        outb(0x20, 0x11); io_wait();
        outb(0xA0, 0x11); io_wait();
        
        outb(0x21, offset1); io_wait();
        outb(0xA1, offset2); io_wait();
        
        outb(0x21, 0x04); io_wait();
        outb(0xA1, 0x02); io_wait();
        
        outb(0x21, 0x01); io_wait();
        outb(0xA1, 0x01); io_wait();
        
        outb(0x21, 0xFC); io_wait(); 
        outb(0xA1, 0xFF); io_wait();
    }

    void eoi(std::uint8_t irq) {
        if (irq >= 8) {
            outb(0xA0, 0x20);
        }
        outb(0x20, 0x20);
    }
	
	void stop() {
        outb(0x21, 0xFF);
        outb(0xA1, 0xFF);
        io_wait();
    }
}