// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Thinking Developer
//
// PodumatOS — a hobby operating system for x86_64.

#ifndef EXT2_HPP
#define EXT2_HPP

#include <cstdint>
#include <cstddef>

namespace ext2 {

    struct Superblock {
        uint32_t s_inodes_count;
        uint32_t s_blocks_count;
        uint32_t s_r_blocks_count;
        uint32_t s_free_blocks_count;
        uint32_t s_free_inodes_count;
        uint32_t s_first_data_block;
        uint32_t s_log_block_size;
        int32_t  s_log_frag_size;
        uint32_t s_blocks_per_group;
        uint32_t s_frags_per_group;
        uint32_t s_inodes_per_group;
        uint32_t s_mtime;
        uint32_t s_wtime;
        uint16_t s_mnt_count;
        uint16_t s_max_mnt_count;
        uint16_t s_magic;
        uint16_t s_state;
        uint16_t s_errors;
        uint16_t s_minor_rev_level;
        uint32_t s_lastcheck;
        uint32_t s_checkinterval;
        uint32_t s_creator_os;
        uint32_t s_rev_level;
        uint16_t s_def_resuid;
        uint16_t s_def_resgid;
        uint32_t s_first_ino;
        uint16_t s_inode_size;
        uint16_t s_block_group_nr;
        uint32_t s_feature_compat;
        uint32_t s_feature_incompat;
        uint32_t s_feature_ro_compat;
        uint8_t  s_uuid[16];
        char     s_volume_name[16];
        char     s_last_mounted[64];
        uint32_t s_algo_bitmap;
        uint8_t  s_prealloc_blocks;
        uint8_t  s_prealloc_dir_blocks;
        uint16_t s_padding1;
        uint8_t  s_journal_uuid[16];
        uint32_t s_journal_inum;
        uint32_t s_journal_dev;
        uint32_t s_last_orphan;
        uint32_t s_hash_seed[4];
        uint8_t  s_def_hash_version;
        uint8_t  s_reserved_char_pad;
        uint16_t s_reserved_word_pad;
        uint32_t s_default_mount_opts;
        uint32_t s_first_meta_bg;
        uint8_t  s_reserved[760];
    } __attribute__((packed));

    struct BlockGroupDescriptor {
        uint32_t bg_block_bitmap;
        uint32_t bg_inode_bitmap;
        uint32_t bg_inode_table;
        uint16_t bg_free_blocks_count;
        uint16_t bg_free_inodes_count;
        uint16_t bg_used_dirs_count;
        uint16_t bg_pad;
        uint8_t  bg_reserved[12];
    } __attribute__((packed));

    struct Inode {
        uint16_t i_mode;
        uint16_t i_uid;
        uint32_t i_size;
        uint32_t i_atime;
        uint32_t i_ctime;
        uint32_t i_mtime;
        uint32_t i_dtime;
        uint16_t i_gid;
        uint16_t i_links_count;
        uint32_t i_blocks;
        uint32_t i_flags;
        uint32_t i_osd1;
        uint32_t i_block[15];
        uint32_t i_generation;
        uint32_t i_file_acl;
        uint32_t i_dir_acl;
        uint32_t i_faddr;
        uint8_t  i_osd2[12];
    } __attribute__((packed));

    struct DirEntry {
        uint32_t inode;
        uint16_t rec_len;
        uint8_t  name_len;
        uint8_t  file_type;
    } __attribute__((packed));

    constexpr uint16_t EXT2_MAGIC = 0xEF53;

    constexpr uint8_t FT_UNKNOWN  = 0;
    constexpr uint8_t FT_REG_FILE = 1;
    constexpr uint8_t FT_DIR      = 2;
    constexpr uint8_t FT_CHRDEV   = 3;
    constexpr uint8_t FT_BLKDEV   = 4;
    constexpr uint8_t FT_FIFO     = 5;
    constexpr uint8_t FT_SOCK     = 6;
    constexpr uint8_t FT_SYMLINK  = 7;

    constexpr uint16_t S_IFMT   = 0xF000;
    constexpr uint16_t S_IFIFO  = 0x1000;
    constexpr uint16_t S_IFCHR  = 0x2000;
    constexpr uint16_t S_IFDIR  = 0x4000;
    constexpr uint16_t S_IFBLK  = 0x6000;
    constexpr uint16_t S_IFREG  = 0x8000;
    constexpr uint16_t S_IFLNK  = 0xA000;
    constexpr uint16_t S_IFSOCK = 0xC000;

    struct FS {
        bool      mounted;
        char      drive_letter;
        uint32_t  partition_lba;
        uint32_t  block_size;
        uint32_t  sectors_per_block;
        uint32_t  inode_size;
        uint32_t  inodes_per_group;
        uint32_t  blocks_per_group;
        uint32_t  groups_count;
        uint32_t  first_data_block;
        uint32_t  current_inode;
        uint32_t  root_inode;
        Superblock sb;

        char      names[32][64];
        uint32_t  inodes[32];
        int       depth;
    };

    extern FS fs;

    bool mount(uint32_t partition_lba, char letter);
    void unmount();
    bool is_mounted();
    char get_letter();

    bool read_file(const char* path, uint8_t* buffer, uint32_t* size_out);
    bool write_file(const char* path, const uint8_t* data, uint32_t size);
    bool create_dir(const char* path);
    bool delete_file(const char* path);

    bool cd(const char* path);
    void pwd();
    void get_path(char* buf, std::size_t size);
    void list_dir(const char* path);

    bool file_exists(const char* path);
    uint32_t file_size(const char* path);

    bool format(uint32_t partition_lba, uint32_t sector_count);
}

#endif