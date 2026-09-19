// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Thinking Developer
//
// PodumatOS — a hobby operating system for x86_64.

#ifndef AHCI_HPP
#define AHCI_HPP

#include <cstdint>
#include <cstddef>

namespace ahci {

    bool init(uint64_t hhdm_offset);
    bool is_present();

    bool read_sector(uint32_t lba, uint8_t* buffer);
    bool write_sector(uint32_t lba, const uint8_t* buffer);
    bool read_sectors(uint32_t lba, uint8_t count, uint8_t* buffer);
    bool write_sectors(uint32_t lba, uint8_t count, const uint8_t* buffer);
    bool flush();
	
	bool stop();

}

#endif