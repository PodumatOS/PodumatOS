// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Thinking Developer
//
// PodumatOS — a hobby operating system for x86_64.


#include <cstdint>
#include "kernel/pic.hpp"
#include "kernel/pit.hpp"
#include "io/io.hpp"

extern "C" void irq_handler(std::uint64_t irq) {
    if (irq == 32) pit::handle();
    if (irq >= 32 && irq <= 47) pic::eoi(irq - 32);
}