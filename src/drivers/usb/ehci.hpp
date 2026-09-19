// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Thinking Developer
//
// PodumatOS — a hobby operating system for x86_64.

#ifndef EHCI_HPP
#define EHCI_HPP

#include <cstdint>
#include <cstddef>

namespace ehci {

    bool init(uint64_t hhdm_offset);
    bool is_present();
    bool poll();

    bool control(uint8_t addr, uint16_t mps,
                 const uint8_t setup[8],
                 void* data, uint16_t len);

    bool intr_open(uint8_t addr, uint8_t ep, uint16_t mps, uint8_t interval);
    void intr_close();
    bool intr_read(void* out, uint16_t len);
	
	void stop();

}

#endif