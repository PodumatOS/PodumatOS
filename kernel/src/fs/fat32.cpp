// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Thinking Developer
//
// PodumatOS — a hobby operating system for x86_64.

#include "fs/fat32.hpp"
#include "drivers/disk.hpp"
#include "console/console.hpp"
#include "lib/string.hpp"

namespace fat32 {

    FS fs;

    static void print_dec(uint32_t v) {
        if (v == 0) { console::putc('0'); return; }
        char buf[12]; int i = 0;
        while (v > 0) { buf[i++] = '0' + (v % 10); v /= 10; }
        for (int j = i - 1; j >= 0; j--) console::putc(buf[j]);
    }

    bool mount(uint32_t partition_lba, char letter) {
        uint8_t sector[512];
        if (!disk::read_sector(partition_lba, sector)) return false;

        BPB* bpb = (BPB*)sector;

        if (bpb->bytes_per_sector != 512) return false;
        if (bpb->fat_size_16 != 0) return false;
        if (bpb->fat_size_32 == 0) return false;

        fs.mounted         = true;
        fs.drive_letter    = letter;
        fs.partition_lba   = partition_lba;
        fs.bpb             = *bpb;
        fs.fat_start       = partition_lba + bpb->reserved_sectors;
        fs.data_start      = fs.fat_start + bpb->num_fats * bpb->fat_size_32;
        fs.root_cluster    = bpb->root_cluster;
        fs.current_cluster = bpb->root_cluster;
        fs.bytes_per_cluster = bpb->bytes_per_sector * bpb->sectors_per_cluster;

        fs.path.depth = 0;
        for (int i = 0; i < 32; i++) {
            fs.path.names[i][0] = '\0';
            fs.path.clusters[i] = 0;
        }

        uint32_t data_sectors = bpb->total_sectors_32 -
                                (bpb->reserved_sectors +
                                 bpb->num_fats * bpb->fat_size_32);
        fs.total_clusters = data_sectors / bpb->sectors_per_cluster;

        return true;
    }

    void unmount() { fs.mounted = false; }
    bool is_mounted() { return fs.mounted; }
    char get_letter() { return fs.drive_letter; }

    uint32_t cluster_to_lba(uint32_t cluster) {
        return fs.data_start + (cluster - 2) * fs.bpb.sectors_per_cluster;
    }

    uint32_t next_cluster(uint32_t cluster) {
        uint32_t fat_offset = cluster * 4;
        uint32_t fat_sector = fs.fat_start + (fat_offset / 512);
        uint32_t ent_offset = fat_offset % 512;

        uint8_t sector[512];
        if (!disk::read_sector(fat_sector, sector)) return 0x0FFFFFFF;
        uint32_t value = *(uint32_t*)&sector[ent_offset];
        return value & 0x0FFFFFFF;
    }

    bool set_cluster(uint32_t cluster, uint32_t value) {
        uint32_t fat_offset = cluster * 4;
        uint32_t fat_sector = fs.fat_start + (fat_offset / 512);
        uint32_t ent_offset = fat_offset % 512;

        uint8_t sector[512];
        if (!disk::read_sector(fat_sector, sector)) return false;
        *(uint32_t*)&sector[ent_offset] = value & 0x0FFFFFFF;
        if (!disk::write_sector(fat_sector, sector)) return false;

        uint32_t fat2_sector = fat_sector + fs.bpb.fat_size_32;
        if (!disk::write_sector(fat2_sector, sector)) return false;
        return true;
    }

    bool read_cluster(uint32_t cluster, uint8_t* buffer) {
        uint32_t lba = cluster_to_lba(cluster);
        for (uint32_t s = 0; s < fs.bpb.sectors_per_cluster; s++) {
            if (!disk::read_sector(lba + s, buffer + s * 512)) return false;
        }
        return true;
    }

    bool write_cluster(uint32_t cluster, const uint8_t* buffer) {
        uint32_t lba = cluster_to_lba(cluster);
        for (uint32_t s = 0; s < fs.bpb.sectors_per_cluster; s++) {
            if (!disk::write_sector(lba + s, buffer + s * 512)) return false;
        }
        return true;
    }

    uint32_t alloc_cluster() {
        for (uint32_t c = 2; c < fs.total_clusters + 2; c++) {
            if (next_cluster(c) == 0) {
                set_cluster(c, 0x0FFFFFFF);
                return c;
            }
        }
        return 0;
    }

    void free_chain(uint32_t start) {
        while (start != 0 && start != 0x0FFFFFFF && start < 0x0FFFFFF8) {
            uint32_t next = next_cluster(start);
            set_cluster(start, 0);
            start = next;
        }
    }

    static void name_to_83(const char* name, char* out) {
        for (int i = 0; i < 11; i++) out[i] = ' ';
        int i = 0, j = 0;
        while (name[i] && name[i] != '.' && j < 8) {
            char c = name[i++];
            if (c >= 'a' && c <= 'z') c -= 32;
            out[j++] = c;
        }
        if (name[i] == '.') {
            i++;
            j = 8;
            while (name[i] && j < 11) {
                char c = name[i++];
                if (c >= 'a' && c <= 'z') c -= 32;
                out[j++] = c;
            }
        }
    }

    static void name_from_83(const DirEntry* e, char* out) {
        int j = 0;
        for (int i = 0; i < 8; i++) {
            if (e->name[i] == ' ') break;
            out[j++] = e->name[i];
        }
        if (e->ext[0] != ' ') {
            out[j++] = '.';
            for (int i = 0; i < 3; i++) {
                if (e->ext[i] == ' ') break;
                out[j++] = e->ext[i];
            }
        }
        out[j] = '\0';
    }

    static bool find_entry(uint32_t dir_cluster, const char* name, DirEntry* out,
                           uint32_t* out_lba, uint32_t* out_offset) {
        char name83[11];
        name_to_83(name, name83);

        uint32_t cluster = dir_cluster;
        while (cluster != 0 && cluster < 0x0FFFFFF8) {
            uint32_t lba = cluster_to_lba(cluster);
            for (uint32_t s = 0; s < fs.bpb.sectors_per_cluster; s++) {
                uint8_t sector[512];
                if (!disk::read_sector(lba + s, sector)) return false;

                DirEntry* entries = (DirEntry*)sector;
                for (int i = 0; i < 16; i++) {
                    DirEntry* e = &entries[i];

                    uint8_t first = (uint8_t)e->name[0];
                    if (first == 0x00) return false;
                    if (first == 0xE5) continue;
                    if (e->attr == ATTR_LONG_NAME) continue;

                    bool match = true;
                    for (int k = 0; k < 8; k++) {
                        if (e->name[k] != name83[k]) { match = false; break; }
                    }
                    if (!match) continue;

                    for (int k = 0; k < 3; k++) {
                        if (e->ext[k] != name83[8 + k]) { match = false; break; }
                    }
                    if (!match) continue;

                    if (out) *out = *e;
                    if (out_lba) *out_lba = lba + s;
                    if (out_offset) *out_offset = i * 32;
                    return true;
                }
            }
            cluster = next_cluster(cluster);
        }
        return false;
    }

    static bool create_file_entry(uint32_t dir_cluster, const char* name,
                                  uint8_t attr, uint32_t first_cluster) {
        char name83[11];
        name_to_83(name, name83);

        uint32_t cluster = dir_cluster;
        while (cluster != 0 && cluster < 0x0FFFFFF8) {
            uint32_t lba = cluster_to_lba(cluster);
            for (uint32_t s = 0; s < fs.bpb.sectors_per_cluster; s++) {
                uint8_t sector[512];
                if (!disk::read_sector(lba + s, sector)) return false;

                DirEntry* entries = (DirEntry*)sector;
                for (int i = 0; i < 16; i++) {
                    DirEntry* e = &entries[i];

                    uint8_t first = (uint8_t)e->name[0];
                    if (first == 0x00 || first == 0xE5) {
                        for (int k = 0; k < 8; k++) e->name[k] = name83[k];
                        for (int k = 0; k < 3; k++) e->ext[k]  = name83[8 + k];

                        e->attr = attr;
                        e->reserved = 0;
                        e->create_time_tenth = 0;
                        e->create_time = 0;
                        e->create_date = 0;
                        e->access_date = 0;
                        e->cluster_high = (first_cluster >> 16) & 0xFFFF;
                        e->write_time = 0;
                        e->write_date = 0;
                        e->cluster_low  = first_cluster & 0xFFFF;
                        e->file_size = 0;

                        if (!disk::write_sector(lba + s, sector)) return false;
                        return true;
                    }
                }
            }
            uint32_t next = next_cluster(cluster);
            if (next == 0 || next >= 0x0FFFFFF8) {
                uint32_t new_c = alloc_cluster();
                if (new_c == 0) return false;
                set_cluster(cluster, new_c);
                set_cluster(new_c, 0x0FFFFFFF);
                cluster = new_c;
            } else {
                cluster = next;
            }
        }
        return false;
    }

    bool read_file(const char* path, uint8_t* buffer, uint32_t* size_out) {
        if (!fs.mounted) return false;
        if (path[0] == '/' && path[1] == '\0') return false;

        if (path[0] == '/') path++;

        DirEntry entry;
        if (!find_entry(fs.current_cluster, path, &entry, nullptr, nullptr)) {
            return false;
        }
        if (entry.attr & ATTR_DIRECTORY) return false;

        uint32_t cluster = ((uint32_t)entry.cluster_high << 16) | entry.cluster_low;
        uint32_t remaining = entry.file_size;
        uint8_t* ptr = buffer;

        while (cluster != 0 && cluster < 0x0FFFFFF8 && remaining > 0) {
            uint32_t to_read = remaining < fs.bytes_per_cluster ? remaining : fs.bytes_per_cluster;
            if (!read_cluster(cluster, ptr)) return false;
            ptr += to_read;
            remaining -= to_read;
            cluster = next_cluster(cluster);
        }

        if (size_out) *size_out = entry.file_size;
        return true;
    }

    bool write_file(const char* path, const uint8_t* data, uint32_t size) {
        if (!fs.mounted) return false;
        if (path[0] == '/') path++;

        DirEntry old;
        if (find_entry(fs.current_cluster, path, &old, nullptr, nullptr)) {
            uint32_t c = ((uint32_t)old.cluster_high << 16) | old.cluster_low;
            if (c >= 2) free_chain(c);
            delete_file(path);
        }

        uint32_t first = 0;
        uint32_t prev = 0;
        uint32_t remaining = size;
        const uint8_t* ptr = data;

        uint8_t cluster_buf[4096];

        while (remaining > 0) {
            uint32_t c = alloc_cluster();
            if (c == 0) return false;
            if (first == 0) first = c;
            if (prev != 0) set_cluster(prev, c);

            uint32_t to_write = remaining < fs.bytes_per_cluster ? remaining : fs.bytes_per_cluster;
            for (uint32_t i = 0; i < fs.bytes_per_cluster; i++) cluster_buf[i] = 0;
            for (uint32_t i = 0; i < to_write; i++) cluster_buf[i] = ptr[i];
            write_cluster(c, cluster_buf);

            ptr += to_write;
            remaining -= to_write;
            prev = c;
        }

        if (!create_file_entry(fs.current_cluster, path, ATTR_ARCHIVE, first)) return false;

        DirEntry entry;
        uint32_t lba, off;
        if (find_entry(fs.current_cluster, path, &entry, &lba, &off)) {
            uint8_t sector[512];
            disk::read_sector(lba, sector);
            DirEntry* entries = (DirEntry*)sector;
            entries[off / 32].file_size = size;
            disk::write_sector(lba, sector);
        }

        return true;
    }

    bool create_dir(const char* path) {
        if (!fs.mounted) return false;
        if (path[0] == '/') path++;

        uint32_t c = alloc_cluster();
        if (c == 0) return false;

        uint8_t buf[4096];
        for (uint32_t i = 0; i < fs.bytes_per_cluster; i++) buf[i] = 0;

        DirEntry* dot = (DirEntry*)buf;
        for (int i = 0; i < 8; i++) dot->name[i] = ' ';
        for (int i = 0; i < 3; i++) dot->ext[i]  = ' ';
        dot->name[0] = '.';
        dot->attr = ATTR_DIRECTORY;
        dot->cluster_high = (c >> 16) & 0xFFFF;
        dot->cluster_low  = c & 0xFFFF;

        DirEntry* dotdot = (DirEntry*)(buf + 32);
        for (int i = 0; i < 8; i++) dotdot->name[i] = ' ';
        for (int i = 0; i < 3; i++) dotdot->ext[i]  = ' ';
        dotdot->name[0] = '.'; dotdot->name[1] = '.';
        dotdot->attr = ATTR_DIRECTORY;
        dotdot->cluster_high = (fs.current_cluster >> 16) & 0xFFFF;
        dotdot->cluster_low  = fs.current_cluster & 0xFFFF;

        write_cluster(c, buf);
        return create_file_entry(fs.current_cluster, path, ATTR_DIRECTORY, c);
    }

    bool delete_file(const char* path) {
        if (!fs.mounted) return false;
        if (path[0] == '/') path++;

        DirEntry entry;
        uint32_t lba, off;
        if (!find_entry(fs.current_cluster, path, &entry, &lba, &off)) return false;

        uint8_t sector[512];
        disk::read_sector(lba, sector);
        DirEntry* entries = (DirEntry*)sector;
        entries[off / 32].name[0] = (char)0xE5;
        return disk::write_sector(lba, sector);
    }

    bool cd(const char* path) {
        if (!fs.mounted) return false;

        if (path[0] == '.' && path[1] == '.' && path[2] == '\0') {
            if (fs.current_cluster == fs.root_cluster) return true;
            if (fs.path.depth > 0) fs.path.depth--;

            DirEntry d;
            if (find_entry(fs.current_cluster, "..", &d, nullptr, nullptr)) {
                uint32_t c = ((uint32_t)d.cluster_high << 16) | d.cluster_low;
                fs.current_cluster = (c == 0) ? fs.root_cluster : c;
                return true;
            }
            return false;
        }

        if (path[0] == '/' && path[1] == '\0') {
            fs.current_cluster = fs.root_cluster;
            fs.path.depth = 0;
            return true;
        }

        const char* p = path;
        if (p[0] == '/') p++;

        DirEntry d;
        if (find_entry(fs.current_cluster, p, &d, nullptr, nullptr)) {
            if (d.attr & ATTR_DIRECTORY) {
                uint32_t c = ((uint32_t)d.cluster_high << 16) | d.cluster_low;
                fs.current_cluster = (c == 0) ? fs.root_cluster : c;

                if (fs.path.depth < 32) {
                    int i = 0;
                    while (p[i] && i < 12) {
                        fs.path.names[fs.path.depth][i] = p[i];
                        i++;
                    }
                    fs.path.names[fs.path.depth][i] = '\0';
                    fs.path.clusters[fs.path.depth] = fs.current_cluster;
                    fs.path.depth++;
                }
                return true;
            }
        }
        return false;
    }

    void pwd() {
        console::putc(fs.drive_letter);
        console::puts(":\\");
        for (int i = 0; i < fs.path.depth; i++) {
            console::puts(fs.path.names[i]);
            if (i < fs.path.depth - 1) console::putc('\\');
        }
    }

    void list_dir(const char* path) {
        if (!fs.mounted) { console::puts("FAT32 not mounted\n"); return; }

        uint32_t cluster = fs.current_cluster;
        if (path[0] && !(path[0] == '/' && path[1] == '\0')) {
            if (path[0] == '/') path++;
            DirEntry d;
            if (!find_entry(fs.current_cluster, path, &d, nullptr, nullptr)) {
                console::puts("Directory not found\n");
                return;
            }
            if (!(d.attr & ATTR_DIRECTORY)) {
                console::puts("Not a directory\n");
                return;
            }
            cluster = ((uint32_t)d.cluster_high << 16) | d.cluster_low;
        }

        int count = 0;
        while (cluster != 0 && cluster < 0x0FFFFFF8) {
            uint32_t lba = cluster_to_lba(cluster);
            for (uint32_t s = 0; s < fs.bpb.sectors_per_cluster; s++) {
                uint8_t sector[512];
                if (!disk::read_sector(lba + s, sector)) return;

                DirEntry* entries = (DirEntry*)sector;
                for (int i = 0; i < 16; i++) {
                    DirEntry* e = &entries[i];

                    uint8_t first = (uint8_t)e->name[0];
                    if (first == 0x00) {
                        console::puts("\nTotal: ");
                        print_dec(count);
                        console::puts(" entries\n");
                        return;
                    }
                    if (first == 0xE5) continue;
                    if (e->attr == ATTR_LONG_NAME) continue;
                    if (e->attr & ATTR_VOLUME_ID) continue;

                    char name[13];
                    name_from_83(e, name);
                    console::puts(e->attr & ATTR_DIRECTORY ? "  [DIR]  " : "  [FILE] ");
                    console::puts(name);

                    if (!(e->attr & ATTR_DIRECTORY)) {
                        int name_len = 0;
                        while (name[name_len]) name_len++;
                        for (int j = name_len; j < 14; j++) console::putc(' ');
                        print_dec(e->file_size);
                        console::puts(" bytes");
                    }
                    console::putc('\n');
                    count++;
                }
            }
            cluster = next_cluster(cluster);
        }
    }

    bool file_exists(const char* path) {
        if (!fs.mounted) return false;
        if (path[0] == '/') path++;
        DirEntry e;
        return find_entry(fs.current_cluster, path, &e, nullptr, nullptr);
    }

    uint32_t file_size(const char* path) {
        if (!fs.mounted) return 0;
        if (path[0] == '/') path++;
        DirEntry e;
        if (!find_entry(fs.current_cluster, path, &e, nullptr, nullptr)) return 0;
        return e.file_size;
    }

    bool format(uint32_t partition_lba, uint32_t sector_count) {
        if (!disk::info.present) return false;
        if (sector_count < 65536) return false;

        uint16_t bytes_per_sector    = 512;
        uint8_t  sectors_per_cluster = 1;

        if (sector_count > 260000)      sectors_per_cluster = 8;
        else if (sector_count > 65000)  sectors_per_cluster = 4;
        else if (sector_count > 16000)  sectors_per_cluster = 2;

        uint32_t total_sectors    = sector_count;
        uint16_t reserved_sectors = 32;
        uint8_t  num_fats         = 2;

        uint32_t cluster_count = (total_sectors - reserved_sectors) /
                                 (sectors_per_cluster + 2);
        uint32_t fat_size_sectors = ((cluster_count + 2) * 4 + 511) / 512;

        uint32_t data_sectors = total_sectors - reserved_sectors - num_fats * fat_size_sectors;
        cluster_count = data_sectors / sectors_per_cluster;

        uint8_t bs[512];
        for (int i = 0; i < 512; i++) bs[i] = 0;

        bs[0] = 0xEB; bs[1] = 0x58; bs[2] = 0x90;
        const char* oem = "MSWIN4.1";
        for (int i = 0; i < 8; i++) bs[3 + i] = oem[i];

        bs[11] = bytes_per_sector & 0xFF;
        bs[12] = (bytes_per_sector >> 8) & 0xFF;
        bs[13] = sectors_per_cluster;
        bs[14] = reserved_sectors & 0xFF;
        bs[15] = (reserved_sectors >> 8) & 0xFF;
        bs[16] = num_fats;
        bs[17] = 0; bs[18] = 0;
        bs[19] = 0; bs[20] = 0;
        bs[21] = 0xF8;
        bs[22] = 0; bs[23] = 0;
        bs[24] = 0x20; bs[25] = 0x00;
        bs[26] = 0x40; bs[27] = 0x00;
        bs[28] = 0; bs[29] = 0; bs[30] = 0; bs[31] = 0;
        bs[32] = total_sectors & 0xFF;
        bs[33] = (total_sectors >> 8) & 0xFF;
        bs[34] = (total_sectors >> 16) & 0xFF;
        bs[35] = (total_sectors >> 24) & 0xFF;

        bs[36] = fat_size_sectors & 0xFF;
        bs[37] = (fat_size_sectors >> 8) & 0xFF;
        bs[38] = (fat_size_sectors >> 16) & 0xFF;
        bs[39] = (fat_size_sectors >> 24) & 0xFF;
        bs[40] = 0; bs[41] = 0;
        bs[42] = 0; bs[43] = 0;
        bs[44] = 2; bs[45] = 0; bs[46] = 0; bs[47] = 0;
        bs[48] = 1; bs[49] = 0;
        bs[50] = 6; bs[51] = 0;
        for (int i = 52; i < 64; i++) bs[i] = 0;
        bs[64] = 0x80;
        bs[65] = 0;
        bs[66] = 0x29;
        bs[67] = 0x12; bs[68] = 0x34; bs[69] = 0x56; bs[70] = 0x78;
        const char* label = "PODUMATOS  ";
        for (int i = 0; i < 11; i++) bs[71 + i] = label[i];
        const char* fstype = "FAT32   ";
        for (int i = 0; i < 8; i++) bs[82 + i] = fstype[i];

        bs[510] = 0x55; bs[511] = 0xAA;

        if (!disk::write_sector(partition_lba, bs)) return false;
        if (!disk::write_sector(partition_lba + 6, bs)) return false;

        uint8_t fsinfo[512];
        for (int i = 0; i < 512; i++) fsinfo[i] = 0;
        fsinfo[0] = 0x52; fsinfo[1] = 0x52; fsinfo[2] = 0x61; fsinfo[3] = 0x41;
        fsinfo[484] = 0xFF; fsinfo[485] = 0xFF; fsinfo[486] = 0xFF; fsinfo[487] = 0xFF;
        fsinfo[488] = 0xFF; fsinfo[489] = 0xFF; fsinfo[490] = 0xFF; fsinfo[491] = 0xFF;
        fsinfo[510] = 0x55; fsinfo[511] = 0xAA;
        disk::write_sector(partition_lba + 1, fsinfo);

        uint8_t zero[512];
        for (int i = 0; i < 512; i++) zero[i] = 0;

        uint32_t fat_start  = partition_lba + reserved_sectors;
        uint32_t fat2_start = fat_start + fat_size_sectors;

        zero[0] = 0xF8; zero[1] = 0xFF; zero[2] = 0xFF; zero[3] = 0x0F;
        zero[4] = 0xFF; zero[5] = 0xFF; zero[6] = 0xFF; zero[7] = 0x0F;
        zero[8] = 0xFF; zero[9] = 0xFF; zero[10] = 0xFF; zero[11] = 0x0F;

        disk::write_sector(fat_start, zero);
        disk::write_sector(fat2_start, zero);

        for (uint32_t s = 1; s < fat_size_sectors; s++) {
            uint8_t z[512];
            for (int i = 0; i < 512; i++) z[i] = 0;
            disk::write_sector(fat_start + s, z);
            disk::write_sector(fat2_start + s, z);
        }

        uint32_t data_start = fat_start + num_fats * fat_size_sectors;
        uint32_t root_lba = data_start;

        for (uint32_t s = 0; s < sectors_per_cluster; s++) {
            uint8_t z[512];
            for (int i = 0; i < 512; i++) z[i] = 0;
            disk::write_sector(root_lba + s, z);
        }

        disk::flush();
        return true;
    }

}