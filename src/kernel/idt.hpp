// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Thinking Developer
//
// PodumatOS — a hobby operating system for x86_64.

#ifndef IDT_HPP
#define IDT_HPP
#include <cstdint>
namespace idt {
    void init();
    void set_gate(std::uint8_t num, std::uint64_t base, std::uint16_t sel, std::uint8_t flags);
	void stop();
}
#endif