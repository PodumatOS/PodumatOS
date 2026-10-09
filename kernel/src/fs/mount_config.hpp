// SPDX-License-Identifier: BSD-2-Clause-Patent
// Copyright (c) 2026 Thinking Developer
//
// PodumatOS — a hobby operating system for x86_64.

#ifndef MOUNT_CONFIG_HPP
#define MOUNT_CONFIG_HPP

#include <cstdint>

namespace mount_config {

    constexpr uint8_t FS_NONE  = 0;
    constexpr uint8_t FS_EXT2  = 1;
    constexpr uint8_t FS_FAT32 = 2;

    // MBR allows at most 4 primary partitions, so we never need more slots.
    constexpr int MAX_ENTRIES = 4;

    struct Entry {
        char     letter;
        uint8_t  fs_type;
        uint8_t  part_num;
        uint32_t lba;
    };

    // Load config from disk on first access.
    bool load();

    // Persist the current in-memory config to disk.
    bool save();

    // Add a new entry or update an existing one.
    bool add(char letter, uint8_t fs_type, uint8_t part_num, uint32_t lba);

    // Remove the entry whose part_num matches.
    bool remove_by_part(uint8_t part_num);

    // Remove the entry whose letter matches.
    bool remove_by_letter(char letter);

    // Remove all entries.
    bool clear();

    // Number of stored entries.
    int count();

    // Get entry by index (0 .. count()-1).
    bool get(int idx, Entry* out);

    // Get the most recently added entry (used for boot restore).
    bool get_last(Entry* out);

}

#endif
