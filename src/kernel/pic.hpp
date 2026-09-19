// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Thinking Developer
//
// PodumatOS — a hobby operating system for x86_64.

#ifndef PIC_HPP
#define PIC_HPP
#include <cstdint>

namespace pic {
    void remap(std::uint8_t offset1, std::uint8_t offset2);
    void eoi(std::uint8_t irq);
	void stop();
}

#endif