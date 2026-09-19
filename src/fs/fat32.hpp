// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Thinking Developer
//
// PodumatOS — a hobby operating system for x86_64.

#ifndef FAT32_HPP
#define FAT32_HPP

#include <cstdint>
#include <cstddef>

namespace fat32 {

    struct BPB {
        uint8_t  jmp[3];
        char     oem[8];
        uint16_t bytes_per_sector;
        uint8_t  sectors_per_cluster;
        uint16_t reserved_sectors;
        uint8_t  num_fats;
        uint16_t root_entries;
        uint16_t total_sectors_16;
        uint8_t  media;
        uint16_t fat_size_16;
        uint16_t sectors_per_track;
        uint16_t num_heads;
        uint32_t hidden_sectors;
        uint32_t total_sectors_32;
        uint32_t fat_size_32;
        uint16_t ext_flags;
        uint16_t fs_version;
        uint32_t root_cluster;
        uint16_t fs_info;
        uint16_t backup_boot;
        uint8_t  reserved[12];
        uint8_t  drive_num;
        uint8_t  reserved1;
        uint8_t  boot_sig;
        uint32_t volume_id;
        char     volume_label[11];
        char     fs_type[8];
    } __attribute__((packed));

    struct DirEntry {
        char     name[8];
        char     ext[3];
        uint8_t  attr;
        uint8_t  reserved;
        uint8_t  create_time_tenth;
        uint16_t create_time;
        uint16_t create_date;
        uint16_t access_date;
        uint16_t cluster_high;
        uint16_t write_time;
        uint16_t write_date;
        uint16_t cluster_low;
        uint32_t file_size;
    } __attribute__((packed));

    #define ATTR_READ_ONLY  0x01
    #define ATTR_HIDDEN     0x02
    #define ATTR_SYSTEM     0x04
    #define ATTR_VOLUME_ID  0x08
    #define ATTR_DIRECTORY  0x10
    #define ATTR_ARCHIVE    0x20
    #define ATTR_LONG_NAME  0x0F

    struct PathStack {
        char     names[32][13];
        uint32_t clusters[32];
        int      depth;
    };

    struct FS {
        bool      mounted;
        char      drive_letter;
        uint32_t  partition_lba;
        BPB       bpb;
        uint32_t  fat_start;
        uint32_t  data_start;
        uint32_t  root_cluster;
        uint32_t  current_cluster;
        uint32_t  total_clusters;
        uint32_t  bytes_per_cluster;
        PathStack path;
    };

    extern FS fs;

    bool mount(uint32_t partition_lba, char letter);
    void unmount();
    bool is_mounted();
    char get_letter();

    uint32_t cluster_to_lba(uint32_t cluster);
    uint32_t next_cluster(uint32_t cluster);
    bool     set_cluster(uint32_t cluster, uint32_t value);

    bool read_cluster(uint32_t cluster, uint8_t* buffer);
    bool write_cluster(uint32_t cluster, const uint8_t* buffer);

    uint32_t alloc_cluster();
    void     free_chain(uint32_t start);

    bool read_file(const char* path, uint8_t* buffer, uint32_t* size_out);
    bool write_file(const char* path, const uint8_t* data, uint32_t size);
    bool create_dir(const char* path);
    bool delete_file(const char* path);

    bool cd(const char* path);
    void pwd();
    void list_dir(const char* path);

    bool file_exists(const char* path);
    uint32_t file_size(const char* path);

    bool format(uint32_t partition_lba, uint32_t sector_count);
}

#endif