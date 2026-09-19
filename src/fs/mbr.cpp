// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Thinking Developer
//
// PodumatOS — a hobby operating system for x86_64.

#include "fs/mbr.hpp"
#include "drivers/disk.hpp"
#include "console/console.hpp"
#include "lib/string.hpp"

namespace mbr {

    static const char* hex_chars = "0123456789ABCDEF";

    static void print_dec(uint32_t v) {
        if (v == 0) { console::putc('0'); return; }
        char buf[12]; int i = 0;
        while (v > 0) { buf[i++] = '0' + (v % 10); v /= 10; }
        for (int j = i - 1; j >= 0; j--) console::putc(buf[j]);
    }

    bool read(MBR* mbr) {
        return disk::read_sector(0, (uint8_t*)mbr);
    }

    bool write(const MBR* mbr) {
        return disk::write_sector(0, (const uint8_t*)mbr);
    }

    void create_empty(MBR* mbr) {
        for (size_t i = 0; i < sizeof(MBR); i++) ((uint8_t*)mbr)[i] = 0;
        mbr->signature = 0xAA55;
    }

    bool create_partition(int index, uint32_t start_lba, uint32_t sectors, uint8_t type) {
        if (index < 0 || index > 3) return false;
        if (start_lba < 2048) return false;
        if (sectors == 0) return false;

        MBR m;
        if (!read(&m)) return false;

        m.partitions[index].boot_flag = 0x00;
        m.partitions[index].chs_start[0] = 0;
        m.partitions[index].chs_start[1] = 0;
        m.partitions[index].chs_start[2] = 0;
        m.partitions[index].type = type;
        m.partitions[index].chs_end[0] = 0;
        m.partitions[index].chs_end[1] = 0;
        m.partitions[index].chs_end[2] = 0;
        m.partitions[index].lba_start = start_lba;
        m.partitions[index].sector_count = sectors;
        m.signature = 0xAA55;
        return write(&m);
    }

    bool delete_partition(int index) {
        if (index < 0 || index > 3) return false;
        MBR m;
        if (!read(&m)) return false;
        for (size_t i = 0; i < sizeof(Partition); i++)
            ((uint8_t*)&m.partitions[index])[i] = 0;
        m.signature = 0xAA55;
        return write(&m);
    }

    uint32_t find_free_lba(uint32_t sectors_needed) {
        MBR m;
        if (!read(&m)) return 0;

        uint32_t candidate = 2048;
        for (int i = 0; i < 4; i++) {
            Partition* p = &m.partitions[i];
            if (p->type == 0 && p->sector_count == 0) continue;
            if (candidate >= p->lba_start &&
                candidate < p->lba_start + p->sector_count) {
                candidate = p->lba_start + p->sector_count;
                if (candidate % 2048 != 0)
                    candidate = (candidate / 2048 + 1) * 2048;
            }
        }
        if (candidate + sectors_needed > disk::info.sectors) return 0;
        return candidate;
    }

    const char* type_name(uint8_t type) {
        switch (type) {
            case 0x00: return "Empty";
            case 0x01: return "FAT12";
            case 0x04: return "FAT16";
            case 0x05: return "Extended";
            case 0x06: return "FAT16B";
            case 0x07: return "NTFS";
            case 0x0B: return "FAT32";
            case 0x0C: return "FAT32 LBA";
            case 0x0E: return "FAT16 LBA";
            case 0x0F: return "Ext LBA";
            case 0x82: return "Linux Swap";
            case 0x83: return "Linux";
            case 0x8E: return "Linux LVM";
            case 0xEE: return "GPT";
            case 0xEF: return "EFI";
            default:   return "Unknown";
        }
    }

    void print_all() {
        MBR m;
        if (!read(&m)) { console::puts("Cannot read MBR\n"); return; }

        console::puts("  #  Boot  Type        Start LBA    Sectors      Size\n");

        for (int i = 0; i < 4; i++) {
            Partition* p = &m.partitions[i];
            if (p->type == 0 && p->sector_count == 0) continue;

            console::puts("  ");
            console::putc('0' + i);
            console::puts("  ");
            console::putc(p->boot_flag == 0x80 ? '*' : ' ');
            console::puts("     ");

            const char* tn = type_name(p->type);
            console::puts(tn);
            int len = 0; while (tn[len]) len++;
            for (int j = len; j < 12; j++) console::putc(' ');

            print_dec(p->lba_start);
            for (int j = 0; j < 12; j++) console::putc(' ');

            print_dec(p->sector_count);
            console::puts("  ");
            print_dec(p->sector_count / 2048);
            console::puts(" MB\n");
        }
    }
}