// SPDX-License-Identifier: BSD-2-Clause-Patent
// Copyright (c) 2026 Thinking Developer
//
// PodumatOS — a hobby operating system for x86_64.

#include "fs/mount_config.hpp"
#include "drivers/disk.hpp"
#include "lib/memory.hpp"

namespace mount_config {

    static constexpr uint32_t CONFIG_MAGIC   = 0x504D5443;  // "PMTC"
    static constexpr uint32_t CONFIG_VERSION = 2;           // v2 = multi-entry
    static constexpr uint32_t CONFIG_LBA     = 1;           // sector 1 of boot disk

    // On-disk entry: 8 bytes, packed.
    struct SavedEntry {
        char     letter;
        uint8_t  fs_type;
        uint8_t  part_num;
        uint8_t  reserved;
        uint32_t lba;
    } __attribute__((packed));

    static_assert(sizeof(SavedEntry) == 8, "SavedEntry must be 8 bytes");

    // Whole sector layout (512 bytes).
    struct DiskLayout {
        uint32_t   magic;
        uint32_t   version;
        uint32_t   count;
        uint32_t   reserved;
        SavedEntry entries[MAX_ENTRIES];                       // 32 bytes
        uint8_t    padding[512 - 16 - MAX_ENTRIES * 8 - 4];    // 460 bytes
        uint32_t   checksum;
    } __attribute__((packed));

    static_assert(sizeof(DiskLayout) == 512, "DiskLayout must be 512 bytes");

    static DiskLayout g_cfg;
    static bool       g_loaded = false;

    static uint32_t compute_checksum(const DiskLayout* c) {
        const uint8_t* p = (const uint8_t*)c;
        uint32_t sum = 0x811C9DC5u;
        for (uint32_t i = 0; i < 512 - 4; i++) {
            sum ^= p[i];
            sum *= 16777619u;
        }
        return sum;
    }

    static void wipe_entry(SavedEntry* e) {
        e->letter   = 0;
        e->fs_type  = FS_NONE;
        e->part_num = 0;
        e->reserved = 0;
        e->lba      = 0;
    }

    static void reset() {
        memset(&g_cfg, 0, sizeof(g_cfg));
        g_cfg.magic   = CONFIG_MAGIC;
        g_cfg.version = CONFIG_VERSION;
        g_cfg.count   = 0;
        for (int i = 0; i < MAX_ENTRIES; i++) wipe_entry(&g_cfg.entries[i]);
        g_cfg.checksum = 0;
        g_cfg.checksum = compute_checksum(&g_cfg);
    }

    bool load() {
        if (g_loaded) return true;

        if (!disk::info.present) {
            reset();
            g_loaded = true;
            return false;
        }

        uint8_t sector[512];
        if (!disk::read_sector(CONFIG_LBA, sector)) {
            reset();
            g_loaded = true;
            return false;
        }

        DiskLayout tmp;
        memcpy(&tmp, sector, 512);

        if (tmp.magic != CONFIG_MAGIC || tmp.version != CONFIG_VERSION) {
            reset();
            g_loaded = true;
            return false;
        }

        uint32_t saved = tmp.checksum;
        tmp.checksum = 0;
        uint32_t computed = compute_checksum(&tmp);
        if (saved != computed) {
            reset();
            g_loaded = true;
            return false;
        }

        if (tmp.count > MAX_ENTRIES) {
            reset();
            g_loaded = true;
            return false;
        }

        g_cfg = tmp;
        g_loaded = true;
        return g_cfg.count > 0;
    }

    bool save() {
        if (!disk::info.present) return false;

        g_cfg.magic    = CONFIG_MAGIC;
        g_cfg.version  = CONFIG_VERSION;
        g_cfg.checksum = 0;
        g_cfg.checksum = compute_checksum(&g_cfg);

        uint8_t sector[512];
        memcpy(sector, &g_cfg, 512);
        return disk::write_sector(CONFIG_LBA, sector);
    }

    bool add(char letter, uint8_t fs_type, uint8_t part_num, uint32_t lba) {
        if (!g_loaded) load();

        // replace existing entry with same partition number.
        for (uint32_t i = 0; i < g_cfg.count; i++) {
            if (g_cfg.entries[i].part_num == part_num) {
                g_cfg.entries[i].letter  = letter;
                g_cfg.entries[i].fs_type = fs_type;
                g_cfg.entries[i].lba     = lba;
                return save();
            }
        }

        // replace existing entry with same drive letter.
        for (uint32_t i = 0; i < g_cfg.count; i++) {
            if (g_cfg.entries[i].letter == letter) {
                g_cfg.entries[i].part_num = part_num;
                g_cfg.entries[i].fs_type  = fs_type;
                g_cfg.entries[i].lba      = lba;
                return save();
            }
        }

        // no free slot: drop the oldest entry to make room.
        if (g_cfg.count >= MAX_ENTRIES) {
            for (uint32_t i = 1; i < MAX_ENTRIES; i++) {
                g_cfg.entries[i - 1] = g_cfg.entries[i];
            }
            g_cfg.count = MAX_ENTRIES - 1;
        }

        SavedEntry* e = &g_cfg.entries[g_cfg.count];
        e->letter   = letter;
        e->fs_type  = fs_type;
        e->part_num = part_num;
        e->reserved = 0;
        e->lba      = lba;
        g_cfg.count++;
        return save();
    }

    bool remove_by_part(uint8_t part_num) {
        if (!g_loaded) load();
        for (uint32_t i = 0; i < g_cfg.count; i++) {
            if (g_cfg.entries[i].part_num == part_num) {
                for (uint32_t j = i + 1; j < g_cfg.count; j++) {
                    g_cfg.entries[j - 1] = g_cfg.entries[j];
                }
                g_cfg.count--;
                wipe_entry(&g_cfg.entries[g_cfg.count]);
                return save();
            }
        }
        return save();
    }

    bool remove_by_letter(char letter) {
        if (!g_loaded) load();
        for (uint32_t i = 0; i < g_cfg.count; i++) {
            if (g_cfg.entries[i].letter == letter) {
                for (uint32_t j = i + 1; j < g_cfg.count; j++) {
                    g_cfg.entries[j - 1] = g_cfg.entries[j];
                }
                g_cfg.count--;
                wipe_entry(&g_cfg.entries[g_cfg.count]);
                return save();
            }
        }
        return save();
    }

    bool clear() {
        if (!g_loaded) load();
        g_cfg.count = 0;
        for (int i = 0; i < MAX_ENTRIES; i++) wipe_entry(&g_cfg.entries[i]);
        return save();
    }

    int count() {
        if (!g_loaded) load();
        return (int)g_cfg.count;
    }

    bool get(int idx, Entry* out) {
        if (!g_loaded) load();
        if (idx < 0 || (uint32_t)idx >= g_cfg.count) return false;
        if (out) {
            out->letter   = g_cfg.entries[idx].letter;
            out->fs_type  = g_cfg.entries[idx].fs_type;
            out->part_num = g_cfg.entries[idx].part_num;
            out->lba      = g_cfg.entries[idx].lba;
        }
        return true;
    }

    bool get_last(Entry* out) {
        if (!g_loaded) load();
        if (g_cfg.count == 0) return false;
        return get((int)g_cfg.count - 1, out);
    }

}
