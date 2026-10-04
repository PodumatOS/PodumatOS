// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Thinking Developer
//
// PodumatOS — a hobby operating system for x86_64.

#ifndef IO_HPP
#define IO_HPP

#include <cstdint>

inline void outb(std::uint16_t port, std::uint8_t val) {
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

inline std::uint8_t inb(std::uint16_t port) {
    std::uint8_t ret;
    asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

inline void outw(std::uint16_t port, std::uint16_t val) {
    asm volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

inline std::uint16_t inw(std::uint16_t port) {
    std::uint16_t ret;
    asm volatile("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

inline void outl(std::uint16_t port, std::uint32_t val) {
    asm volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}

inline std::uint32_t inl(std::uint16_t port) {
    std::uint32_t ret;
    asm volatile("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

inline void io_wait() { outb(0x80, 0); }

#endif