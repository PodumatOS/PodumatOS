// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Thinking Developer
//
// PodumatOS — a hobby operating system for x86_64.

#ifndef MBR_HPP
#define MBR_HPP

#include <cstdint>

namespace mbr {

    struct Partition {
        uint8_t  boot_flag;
        uint8_t  chs_start[3];
        uint8_t  type;
        uint8_t  chs_end[3];
        uint32_t lba_start;
        uint32_t sector_count;
    } __attribute__((packed));

    struct MBR {
        uint8_t   bootstrap[446];
        Partition partitions[4];
        uint16_t  signature;
    } __attribute__((packed));

    bool read(MBR* mbr);
    bool write(const MBR* mbr);
    void create_empty(MBR* mbr);

    bool create_partition(int index, uint32_t start_lba, uint32_t sectors, uint8_t type);
    bool delete_partition(int index);
    uint32_t find_free_lba(uint32_t sectors_needed);

    const char* type_name(uint8_t type);
    void print_all();
}

#endif