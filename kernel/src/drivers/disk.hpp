// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Thinking Developer
//
// PodumatOS — a hobby operating system for x86_64.

#ifndef DISK_HPP
#define DISK_HPP

#include <cstdint>
#include <cstddef>

namespace disk {

    struct Info {
        bool     present;
        uint32_t sectors;
        uint64_t sectors48;
        char     model[41];
        char     serial[21];
        char     firmware[9];
        bool     lba48;
    };

    enum class Driver {
        NONE = 0,
        ATA,
        AHCI
    };

    extern Info    info;
    extern Driver  active_driver;

    bool init(uint64_t hhdm_offset);

    bool read_sector(uint32_t lba, uint8_t* buffer);
    bool write_sector(uint32_t lba, const uint8_t* buffer);
    bool read_sectors(uint32_t lba, uint8_t count, uint8_t* buffer);
    bool write_sectors(uint32_t lba, uint8_t count, const uint8_t* buffer);
    bool flush();

    const char* active_driver_name();
    bool is_ata();
    bool is_ahci();

}

#endif