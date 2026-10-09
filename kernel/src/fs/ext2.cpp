// SPDX-License-Identifier: BSD-2-Clause-Patent
// Copyright (c) 2026 Thinking Developer
//
// PodumatOS — a hobby operating system for x86_64.

#include "fs/ext2.hpp"
#include "drivers/disk.hpp"
#include "drivers/dma.hpp"
#include "console/console.hpp"
#include "lib/string.hpp"
#include "lib/memory.hpp"

namespace ext2 {

    FS fs;

    static uint8_t g_buf[8192];
    static uint8_t g_buf2[8192];
    static uint8_t dir_buf[8192];

    // low-level block I/O
    static bool read_block(uint32_t block, void* buf) {
        uint32_t lba = fs.partition_lba + block * fs.sectors_per_block;
        uint8_t* p = (uint8_t*)buf;
        for (uint32_t s = 0; s < fs.sectors_per_block; s++) {
            if (!disk::read_sector(lba + s, p + s * 512)) return false;
        }
        return true;
    }
    static bool write_block(uint32_t block, const void* buf) {
        uint32_t lba = fs.partition_lba + block * fs.sectors_per_block;
        const uint8_t* p = (const uint8_t*)buf;
        for (uint32_t s = 0; s < fs.sectors_per_block; s++) {
            if (!disk::write_sector(lba + s, p + s * 512)) return false;
        }
        return true;
    }

    // superblock
    static bool read_superblock() {
        uint8_t sec[512];
        if (!disk::read_sector(fs.partition_lba + 2, sec)) return false;
        memcpy(&fs.sb, sec, 512);
        if (!disk::read_sector(fs.partition_lba + 3, sec)) return false;
        memcpy(((uint8_t*)&fs.sb) + 512, sec, 512);
        return true;
    }
    static bool write_superblock() {
        uint8_t sec[512];
        const uint8_t* src = (const uint8_t*)&fs.sb;
        memcpy(sec, src, 512);
        if (!disk::write_sector(fs.partition_lba + 2, sec)) return false;
        memcpy(sec, src + 512, 512);
        if (!disk::write_sector(fs.partition_lba + 3, sec)) return false;
        return true;
    }

    // block group descriptors
    static uint32_t bgd_start_block() { return fs.first_data_block + 1; }

    static bool read_bgd(uint32_t group, BlockGroupDescriptor* bgd) {
        if (group >= fs.groups_count) return false;
        uint32_t byte_off = group * sizeof(BlockGroupDescriptor);
        uint32_t blk = bgd_start_block() + byte_off / fs.block_size;
        uint32_t off = byte_off % fs.block_size;
        if (!read_block(blk, g_buf)) return false;
        memcpy(bgd, g_buf + off, sizeof(BlockGroupDescriptor));
        return true;
    }

    static bool write_bgd(uint32_t group, const BlockGroupDescriptor* bgd) {
        if (group >= fs.groups_count) return false;
        uint32_t byte_off = group * sizeof(BlockGroupDescriptor);
        uint32_t blk = bgd_start_block() + byte_off / fs.block_size;
        uint32_t off = byte_off % fs.block_size;
        if (!read_block(blk, g_buf)) return false;
        memcpy(g_buf + off, bgd, sizeof(BlockGroupDescriptor));
        return write_block(blk, g_buf);
    }

    // inode I/O
    static bool read_inode(uint32_t inode_num, Inode* out) {
        if (inode_num == 0 || inode_num > fs.sb.s_inodes_count) return false;
        uint32_t group = (inode_num - 1) / fs.inodes_per_group;
        uint32_t index = (inode_num - 1) % fs.inodes_per_group;
        BlockGroupDescriptor bgd;
        if (!read_bgd(group, &bgd)) return false;

        uint32_t byte_off = bgd.bg_inode_table * fs.block_size + index * fs.inode_size;
        uint32_t blk = byte_off / fs.block_size;
        uint32_t off = byte_off % fs.block_size;
        if (!read_block(blk, g_buf)) return false;

        memset(out, 0, sizeof(Inode));
        memcpy(out, g_buf + off, sizeof(Inode));
        return true;
    }

    static bool write_inode(uint32_t inode_num, const Inode* in) {
        if (inode_num == 0 || inode_num > fs.sb.s_inodes_count) return false;
        uint32_t group = (inode_num - 1) / fs.inodes_per_group;
        uint32_t index = (inode_num - 1) % fs.inodes_per_group;
        BlockGroupDescriptor bgd;
        if (!read_bgd(group, &bgd)) return false;

        uint32_t byte_off = bgd.bg_inode_table * fs.block_size + index * fs.inode_size;
        uint32_t blk = byte_off / fs.block_size;
        uint32_t off = byte_off % fs.block_size;
        if (!read_block(blk, g_buf)) return false;
        memcpy(g_buf + off, in, sizeof(Inode));
        return write_block(blk, g_buf);
    }

    // block pointer resolution
    static uint32_t get_inode_block(const Inode* in, uint32_t block_index) {
        uint32_t ppb = fs.block_size / 4;
        if (block_index < 12) return in->i_block[block_index];
        block_index -= 12;

        if (block_index < ppb) {
            if (in->i_block[12] == 0) return 0;
            if (!read_block(in->i_block[12], g_buf)) return 0;
            return ((uint32_t*)g_buf)[block_index];
        }
        block_index -= ppb;

        if (block_index < ppb * ppb) {
            if (in->i_block[13] == 0) return 0;
            if (!read_block(in->i_block[13], g_buf)) return 0;
            uint32_t i1 = block_index / ppb;
            uint32_t i2 = block_index % ppb;
            uint32_t ind = ((uint32_t*)g_buf)[i1];
            if (ind == 0) return 0;
            if (!read_block(ind, g_buf)) return 0;
            return ((uint32_t*)g_buf)[i2];
        }
        block_index -= ppb * ppb;

        if (block_index < ppb * ppb * ppb) {
            if (in->i_block[14] == 0) return 0;
            if (!read_block(in->i_block[14], g_buf)) return 0;
            uint32_t i1 = block_index / (ppb * ppb);
            uint32_t rem = block_index % (ppb * ppb);
            uint32_t i2 = rem / ppb;
            uint32_t i3 = rem % ppb;
            uint32_t ind = ((uint32_t*)g_buf)[i1];
            if (ind == 0) return 0;
            if (!read_block(ind, g_buf)) return 0;
            uint32_t ind2 = ((uint32_t*)g_buf)[i2];
            if (ind2 == 0) return 0;
            if (!read_block(ind2, g_buf)) return 0;
            return ((uint32_t*)g_buf)[i3];
        }
        return 0;
    }

    static uint32_t alloc_block_internal();

    static bool set_inode_block(Inode* in, uint32_t block_index, uint32_t block_num) {
        uint32_t ppb = fs.block_size / 4;

        if (block_index < 12) {
            in->i_block[block_index] = block_num;
            return true;
        }
        block_index -= 12;

        if (block_index < ppb) {
            if (in->i_block[12] == 0) {
                uint32_t ind = alloc_block_internal();
                if (ind == 0) return false;
                memset(g_buf, 0, fs.block_size);
                if (!write_block(ind, g_buf)) return false;
                in->i_block[12] = ind;
            }
            if (!read_block(in->i_block[12], g_buf)) return false;
            ((uint32_t*)g_buf)[block_index] = block_num;
            return write_block(in->i_block[12], g_buf);
        }
        block_index -= ppb;

        if (block_index < ppb * ppb) {
            uint32_t i1 = block_index / ppb;
            uint32_t i2 = block_index % ppb;
            if (in->i_block[13] == 0) {
                uint32_t ind = alloc_block_internal();
                if (ind == 0) return false;
                memset(g_buf, 0, fs.block_size);
                if (!write_block(ind, g_buf)) return false;
                in->i_block[13] = ind;
            }
            if (!read_block(in->i_block[13], g_buf2)) return false;
            uint32_t ind = ((uint32_t*)g_buf2)[i1];
            if (ind == 0) {
                ind = alloc_block_internal();
                if (ind == 0) return false;
                memset(g_buf, 0, fs.block_size);
                if (!write_block(ind, g_buf)) return false;
                ((uint32_t*)g_buf2)[i1] = ind;
                if (!write_block(in->i_block[13], g_buf2)) return false;
            }
            if (!read_block(ind, g_buf)) return false;
            ((uint32_t*)g_buf)[i2] = block_num;
            return write_block(ind, g_buf);
        }
        block_index -= ppb * ppb;

        if (block_index < ppb * ppb * ppb) {
            uint32_t i1 = block_index / (ppb * ppb);
            uint32_t rem = block_index % (ppb * ppb);
            uint32_t i2 = rem / ppb;
            uint32_t i3 = rem % ppb;

            if (in->i_block[14] == 0) {
                uint32_t ind = alloc_block_internal();
                if (ind == 0) return false;
                memset(g_buf, 0, fs.block_size);
                if (!write_block(ind, g_buf)) return false;
                in->i_block[14] = ind;
            }
            if (!read_block(in->i_block[14], g_buf2)) return false;
            uint32_t dind = ((uint32_t*)g_buf2)[i1];
            if (dind == 0) {
                dind = alloc_block_internal();
                if (dind == 0) return false;
                memset(g_buf, 0, fs.block_size);
                if (!write_block(dind, g_buf)) return false;
                ((uint32_t*)g_buf2)[i1] = dind;
                if (!write_block(in->i_block[14], g_buf2)) return false;
            }
            if (!read_block(dind, g_buf2)) return false;
            uint32_t ind = ((uint32_t*)g_buf2)[i2];
            if (ind == 0) {
                ind = alloc_block_internal();
                if (ind == 0) return false;
                memset(g_buf, 0, fs.block_size);
                if (!write_block(ind, g_buf)) return false;
                ((uint32_t*)g_buf2)[i2] = ind;
                if (!write_block(dind, g_buf2)) return false;
            }
            if (!read_block(ind, g_buf)) return false;
            ((uint32_t*)g_buf)[i3] = block_num;
            return write_block(ind, g_buf);
        }

        return false;
    }

    // allocation
    static uint32_t alloc_block_internal() {
        for (uint32_t g = 0; g < fs.groups_count; g++) {
            BlockGroupDescriptor bgd;
            if (!read_bgd(g, &bgd)) return 0;
            if (bgd.bg_free_blocks_count == 0) continue;

            if (!read_block(bgd.bg_block_bitmap, g_buf)) return 0;
            for (uint32_t i = 0; i < fs.block_size; i++) {
                if (g_buf[i] == 0xFF) continue;
                for (int bit = 0; bit < 8; bit++) {
                    if (!(g_buf[i] & (1u << bit))) {
                        g_buf[i] |= (1u << bit);
                        uint32_t idx = i * 8 + bit;
                        uint32_t blk = g * fs.blocks_per_group + idx + fs.first_data_block;
                        if (!write_block(bgd.bg_block_bitmap, g_buf)) return 0;
                        if (bgd.bg_free_blocks_count > 0) bgd.bg_free_blocks_count--;
                        if (!write_bgd(g, &bgd)) return 0;
                        if (fs.sb.s_free_blocks_count > 0) fs.sb.s_free_blocks_count--;
                        write_superblock();
                        return blk;
                    }
                }
            }
        }
        return 0;
    }

    static void free_block(uint32_t block) {
        if (block == 0) return;
        if (block < fs.first_data_block) return;
        uint32_t adjusted = block - fs.first_data_block;
        uint32_t group = adjusted / fs.blocks_per_group;
        uint32_t idx = adjusted % fs.blocks_per_group;
        if (group >= fs.groups_count) return;

        BlockGroupDescriptor bgd;
        if (!read_bgd(group, &bgd)) return;
        if (!read_block(bgd.bg_block_bitmap, g_buf)) return;
        g_buf[idx / 8] &= ~(1u << (idx % 8));
        write_block(bgd.bg_block_bitmap, g_buf);
        bgd.bg_free_blocks_count++;
        write_bgd(group, &bgd);
        fs.sb.s_free_blocks_count++;
        write_superblock();
    }

    static uint32_t alloc_inode(bool is_dir) {
        for (uint32_t g = 0; g < fs.groups_count; g++) {
            BlockGroupDescriptor bgd;
            if (!read_bgd(g, &bgd)) return 0;
            if (bgd.bg_free_inodes_count == 0) continue;

            if (!read_block(bgd.bg_inode_bitmap, g_buf)) return 0;
            for (uint32_t i = 0; i < fs.block_size; i++) {
                if (g_buf[i] == 0xFF) continue;
                for (int bit = 0; bit < 8; bit++) {
                    if (!(g_buf[i] & (1u << bit))) {
                        uint32_t idx = i * 8 + bit;
                        uint32_t inode_num = g * fs.inodes_per_group + idx + 1;
                        if (inode_num < fs.sb.s_first_ino) continue;

                        g_buf[i] |= (1u << bit);
                        if (!write_block(bgd.bg_inode_bitmap, g_buf)) return 0;
                        if (bgd.bg_free_inodes_count > 0) bgd.bg_free_inodes_count--;
                        if (is_dir) bgd.bg_used_dirs_count++;
                        if (!write_bgd(g, &bgd)) return 0;
                        if (fs.sb.s_free_inodes_count > 0) fs.sb.s_free_inodes_count--;
                        write_superblock();
                        return inode_num;
                    }
                }
            }
        }
        return 0;
    }

    static void free_inode(uint32_t inode_num, bool is_dir) {
        if (inode_num == 0 || inode_num <= 10) return;
        uint32_t group = (inode_num - 1) / fs.inodes_per_group;
        uint32_t idx = (inode_num - 1) % fs.inodes_per_group;
        if (group >= fs.groups_count) return;

        BlockGroupDescriptor bgd;
        if (!read_bgd(group, &bgd)) return;
        if (!read_block(bgd.bg_inode_bitmap, g_buf)) return;
        g_buf[idx / 8] &= ~(1u << (idx % 8));
        write_block(bgd.bg_inode_bitmap, g_buf);
        bgd.bg_free_inodes_count++;
        if (is_dir && bgd.bg_used_dirs_count > 0) bgd.bg_used_dirs_count--;
        write_bgd(group, &bgd);
        fs.sb.s_free_inodes_count++;
        write_superblock();
    }

    static void free_inode_blocks(Inode* in) {
        uint32_t ppb = fs.block_size / 4;
        for (int i = 0; i < 12; i++) {
            if (in->i_block[i]) { free_block(in->i_block[i]); in->i_block[i] = 0; }
        }
        if (in->i_block[12]) {
            if (read_block(in->i_block[12], g_buf)) {
                for (uint32_t j = 0; j < ppb; j++) {
                    uint32_t b = ((uint32_t*)g_buf)[j];
                    if (b) free_block(b);
                }
            }
            free_block(in->i_block[12]); in->i_block[12] = 0;
        }
        if (in->i_block[13]) {
            if (read_block(in->i_block[13], g_buf2)) {
                for (uint32_t j = 0; j < ppb; j++) {
                    uint32_t ind = ((uint32_t*)g_buf2)[j];
                    if (ind == 0) continue;
                    if (read_block(ind, g_buf)) {
                        for (uint32_t k = 0; k < ppb; k++) {
                            uint32_t b = ((uint32_t*)g_buf)[k];
                            if (b) free_block(b);
                        }
                    }
                    free_block(ind);
                }
            }
            free_block(in->i_block[13]); in->i_block[13] = 0;
        }
        if (in->i_block[14]) {
            if (read_block(in->i_block[14], g_buf2)) {
                for (uint32_t i = 0; i < ppb; i++) {
                    uint32_t dind = ((uint32_t*)g_buf2)[i];
                    if (dind == 0) continue;
                    if (read_block(dind, g_buf)) {
                        for (uint32_t j = 0; j < ppb; j++) {
                            uint32_t ind = ((uint32_t*)g_buf)[j];
                            if (ind == 0) continue;
                            if (read_block(ind, g_buf)) {
                                for (uint32_t k = 0; k < ppb; k++) {
                                    uint32_t b = ((uint32_t*)g_buf)[k];
                                    if (b) free_block(b);
                                }
                            }
                            free_block(ind);
                        }
                    }
                    free_block(dind);
                }
            }
            free_block(in->i_block[14]); in->i_block[14] = 0;
        }
        in->i_blocks = 0;
    }

    // directory operations
    static uint32_t find_dir_entry(uint32_t dir_inode, const char* name, uint8_t* out_type) {
        Inode dir;
        if (!read_inode(dir_inode, &dir)) return 0;
        if ((dir.i_mode & S_IFMT) != S_IFDIR) return 0;

        uint32_t name_len = strlen(name);
        if (name_len == 0 || name_len > 255) return 0;

        uint32_t num_blocks = (dir.i_size + fs.block_size - 1) / fs.block_size;
        for (uint32_t b = 0; b < num_blocks; b++) {
            uint32_t blk = get_inode_block(&dir, b);
            if (blk == 0) continue;
            if (!read_block(blk, dir_buf)) return 0;

            uint32_t off = 0;
            while (off + sizeof(DirEntry) <= fs.block_size) {
                DirEntry* de = (DirEntry*)(dir_buf + off);
                if (de->rec_len < 8) break;
                if (off + de->rec_len > fs.block_size) break;

                if (de->inode != 0 && de->name_len == name_len &&
                    memcmp(dir_buf + off + 8, name, name_len) == 0) {
                    if (out_type) *out_type = de->file_type;
                    return de->inode;
                }
                off += de->rec_len;
            }
        }
        return 0;
    }

    static bool add_dir_entry(uint32_t dir_inode, uint32_t new_inode,
                              const char* name, uint8_t type) {
        Inode dir;
        if (!read_inode(dir_inode, &dir)) return false;

        uint32_t name_len = strlen(name);
        if (name_len > 255) return false;
        uint32_t needed = (8 + name_len + 3) & ~3u;

        uint32_t num_blocks = (dir.i_size + fs.block_size - 1) / fs.block_size;

        for (uint32_t b = 0; b < num_blocks; b++) {
            uint32_t blk = get_inode_block(&dir, b);
            if (blk == 0) continue;
            if (!read_block(blk, dir_buf)) return false;

            uint32_t off = 0;
            while (off + sizeof(DirEntry) <= fs.block_size) {
                DirEntry* de = (DirEntry*)(dir_buf + off);
                if (de->rec_len < 8) break;
                if (off + de->rec_len > fs.block_size) break;
                uint32_t real = (8 + de->name_len + 3) & ~3u;

                if (de->inode == 0 && de->rec_len >= needed) {
                    de->inode = new_inode;
                    de->name_len = (uint8_t)name_len;
                    de->file_type = type;
                    memcpy(dir_buf + off + 8, name, name_len);
                    for (uint32_t k = 8 + name_len; k < de->rec_len; k++)
                        dir_buf[off + k] = 0;
                    return write_block(blk, dir_buf);
                } else if (de->inode != 0 && de->rec_len >= real + needed) {
                    uint32_t old_rec = de->rec_len;
                    de->rec_len = (uint16_t)real;
                    DirEntry* nd = (DirEntry*)(dir_buf + off + real);
                    nd->inode = new_inode;
                    nd->rec_len = (uint16_t)(old_rec - real);
                    nd->name_len = (uint8_t)name_len;
                    nd->file_type = type;
                    memcpy(dir_buf + off + real + 8, name, name_len);
                    for (uint32_t k = real + 8 + name_len; k < old_rec; k++)
                        dir_buf[off + k] = 0;
                    return write_block(blk, dir_buf);
                }
                off += de->rec_len;
            }
        }

        uint32_t blk = alloc_block_internal();
        if (blk == 0) return false;
        memset(dir_buf, 0, fs.block_size);
        DirEntry* de = (DirEntry*)dir_buf;
        de->inode = new_inode;
        de->rec_len = (uint16_t)fs.block_size;
        de->name_len = (uint8_t)name_len;
        de->file_type = type;
        memcpy(dir_buf + 8, name, name_len);
        if (!write_block(blk, dir_buf)) { free_block(blk); return false; }
        if (!set_inode_block(&dir, num_blocks, blk)) { free_block(blk); return false; }

        dir.i_size = (num_blocks + 1) * fs.block_size;
        dir.i_blocks += fs.block_size / 512;
        return write_inode(dir_inode, &dir);
    }

    static bool remove_dir_entry(uint32_t dir_inode, const char* name,
                                 uint32_t* freed_inode) {
        Inode dir;
        if (!read_inode(dir_inode, &dir)) return false;

        uint32_t name_len = strlen(name);
        uint32_t num_blocks = (dir.i_size + fs.block_size - 1) / fs.block_size;

        for (uint32_t b = 0; b < num_blocks; b++) {
            uint32_t blk = get_inode_block(&dir, b);
            if (blk == 0) continue;
            if (!read_block(blk, dir_buf)) return false;

            uint32_t off = 0;
            uint32_t prev = 0xFFFFFFFF;
            while (off + sizeof(DirEntry) <= fs.block_size) {
                DirEntry* de = (DirEntry*)(dir_buf + off);
                if (de->rec_len < 8) break;
                if (off + de->rec_len > fs.block_size) break;

                if (de->inode != 0 && de->name_len == name_len &&
                    memcmp(dir_buf + off + 8, name, name_len) == 0) {
                    if (freed_inode) *freed_inode = de->inode;
                    if (prev != 0xFFFFFFFF) {
                        DirEntry* pd = (DirEntry*)(dir_buf + prev);
                        pd->rec_len += de->rec_len;
                    } else {
                        de->inode = 0;
                    }
                    return write_block(blk, dir_buf);
                }
                prev = off;
                off += de->rec_len;
            }
        }
        return false;
    }

    // path utilities
    static void split_path(const char* path, char* parent, char* name) {
        if (path[0] == '/' || path[0] == '\\') path++;
        const char* last = path;
        const char* p = path;
        while (*p) {
            if (*p == '/' || *p == '\\') last = p + 1;
            p++;
        }
        int len = (int)(last - path);
        if (len > 0 && (path[len-1] == '/' || path[len-1] == '\\')) len--;
        int i;
        for (i = 0; i < len && i < 255; i++) parent[i] = path[i];
        parent[i] = '\0';
        i = 0;
        while (last[i] && i < 63) { name[i] = last[i]; i++; }
        name[i] = '\0';
    }

    static uint32_t resolve_relative(uint32_t start, const char* path) {
        if (!path || !*path) return start;
        uint32_t cur = start;

        if (path[0] == '/' || path[0] == '\\') {
            cur = fs.root_inode;
            path++;
        }

        char comp[64];
        const char* p = path;
        while (*p) {
            int i = 0;
            while (*p && *p != '/' && *p != '\\' && i < 63) comp[i++] = *p++;
            comp[i] = '\0';
            if (*p) p++;
            if (i == 0) continue;

            if (strcmp(comp, ".") == 0) continue;
            if (strcmp(comp, "..") == 0) {
                if (cur == fs.root_inode) continue;
                Inode in;
                if (read_inode(cur, &in)) {
                    uint32_t blk = get_inode_block(&in, 0);
                    if (blk && read_block(blk, dir_buf)) {
                        uint32_t off = 0;
                        while (off + sizeof(DirEntry) <= fs.block_size) {
                            DirEntry* de = (DirEntry*)(dir_buf + off);
                            if (de->rec_len < 8) break;
                            if (off + de->rec_len > fs.block_size) break;
                            if (de->name_len == 2 &&
                                dir_buf[off+8] == '.' && dir_buf[off+9] == '.') {
                                cur = de->inode;
                                break;
                            }
                            off += de->rec_len;
                        }
                    }
                }
                continue;
            }

            uint32_t next = find_dir_entry(cur, comp, nullptr);
            if (next == 0) return 0;
            cur = next;
        }
        return cur;
    }

    // public API
    bool mount(uint32_t partition_lba, char letter) {
        memset(&fs, 0, sizeof(fs));
        fs.partition_lba = partition_lba;

        if (!read_superblock()) return false;
        if (fs.sb.s_magic != EXT2_MAGIC) return false;

        fs.block_size = 1024u << fs.sb.s_log_block_size;
        if (fs.block_size < 1024 || fs.block_size > 4096) return false;
        fs.sectors_per_block = fs.block_size / 512;
        fs.inode_size = (fs.sb.s_rev_level >= 1 && fs.sb.s_inode_size >= 128)
                        ? fs.sb.s_inode_size : 128;
        if (fs.inode_size > 1024) fs.inode_size = 128;

        fs.inodes_per_group = fs.sb.s_inodes_per_group;
        fs.blocks_per_group = fs.sb.s_blocks_per_group;
        fs.first_data_block = fs.sb.s_first_data_block;
        fs.root_inode    = 2;
        fs.current_inode = 2;
        fs.drive_letter  = letter;
        fs.depth = 0;

        uint32_t data_blocks = fs.sb.s_blocks_count - fs.first_data_block;
        fs.groups_count = (data_blocks + fs.blocks_per_group - 1) / fs.blocks_per_group;

        fs.mounted = true;
        return true;
    }

    void unmount() { fs.mounted = false; }
    bool is_mounted() { return fs.mounted; }
    char get_letter() { return fs.drive_letter; }

    bool read_file(const char* path, uint8_t* buffer, uint32_t* size_out) {
        if (!fs.mounted || !path) return false;
        uint32_t inode_num = resolve_relative(fs.current_inode, path);
        if (inode_num == 0) return false;

        Inode in;
        if (!read_inode(inode_num, &in)) return false;
        if ((in.i_mode & S_IFMT) == S_IFDIR) return false;

        uint32_t size = in.i_size;
        uint32_t remaining = size;
        uint8_t* p = buffer;
        uint32_t blk_idx = 0;
        while (remaining > 0) {
            uint32_t blk = get_inode_block(&in, blk_idx);
            uint32_t chunk = remaining < fs.block_size ? remaining : fs.block_size;
            if (blk == 0) {
                memset(p, 0, chunk);
            } else {
                if (!read_block(blk, g_buf)) return false;
                memcpy(p, g_buf, chunk);
            }
            p += chunk; remaining -= chunk; blk_idx++;
        }
        if (size_out) *size_out = size;
        return true;
    }

    bool write_file(const char* path, const uint8_t* data, uint32_t size) {
        if (!fs.mounted || !path) return false;

        char parent[256], name[64];
        split_path(path, parent, name);
        if (name[0] == '\0') return false;

        uint32_t parent_inode = resolve_relative(fs.current_inode, parent);
        if (parent_inode == 0) return false;

        Inode parent_in;
        if (!read_inode(parent_inode, &parent_in)) return false;
        if ((parent_in.i_mode & S_IFMT) != S_IFDIR) return false;

        uint32_t inode_num = find_dir_entry(parent_inode, name, nullptr);
        Inode in;
        bool is_new = false;

        if (inode_num != 0) {
            if (!read_inode(inode_num, &in)) return false;
            free_inode_blocks(&in);
            in.i_size = 0;
            in.i_blocks = 0;
            memset(in.i_block, 0, sizeof(in.i_block));
        } else {
            inode_num = alloc_inode(false);
            if (inode_num == 0) return false;
            memset(&in, 0, sizeof(in));
            in.i_mode = S_IFREG | 0644;
            in.i_links_count = 1;
            is_new = true;
        }

        uint32_t remaining = size;
        const uint8_t* p = data;
        uint32_t blk_idx = 0;
        uint32_t blocks_used = 0;

        while (remaining > 0) {
            uint32_t blk = alloc_block_internal();
            if (blk == 0) {
                if (is_new) { free_inode_blocks(&in); free_inode(inode_num, false); }
                return false;
            }
            memset(g_buf, 0, fs.block_size);
            uint32_t chunk = remaining < fs.block_size ? remaining : fs.block_size;
            memcpy(g_buf, p, chunk);
            if (!write_block(blk, g_buf)) { free_block(blk); return false; }
            if (!set_inode_block(&in, blk_idx, blk)) { free_block(blk); return false; }
            p += chunk; remaining -= chunk; blk_idx++; blocks_used++;
        }

        in.i_size = size;
        in.i_blocks = blocks_used * (fs.block_size / 512);
        if (!write_inode(inode_num, &in)) return false;

        if (is_new) {
            if (!add_dir_entry(parent_inode, inode_num, name, FT_REG_FILE)) {
                free_inode_blocks(&in);
                free_inode(inode_num, false);
                return false;
            }
        }
        return true;
    }

    bool create_dir(const char* path) {
        if (!fs.mounted || !path) return false;

        char parent[256], name[64];
        split_path(path, parent, name);
        if (name[0] == '\0') return false;

        uint32_t parent_inode = resolve_relative(fs.current_inode, parent);
        if (parent_inode == 0) return false;

        if (find_dir_entry(parent_inode, name, nullptr) != 0) return false;

        uint32_t new_inode_num = alloc_inode(true);
        if (new_inode_num == 0) return false;

        uint32_t data_blk = alloc_block_internal();
        if (data_blk == 0) { free_inode(new_inode_num, true); return false; }

        memset(g_buf, 0, fs.block_size);
        DirEntry* dot = (DirEntry*)g_buf;
        dot->inode = new_inode_num;
        dot->rec_len = 12;
        dot->name_len = 1;
        dot->file_type = FT_DIR;
        g_buf[8] = '.';

        DirEntry* dotdot = (DirEntry*)(g_buf + 12);
        dotdot->inode = parent_inode;
        dotdot->rec_len = (uint16_t)(fs.block_size - 12);
        dotdot->name_len = 2;
        dotdot->file_type = FT_DIR;
        g_buf[20] = '.'; g_buf[21] = '.';

        if (!write_block(data_blk, g_buf)) {
            free_block(data_blk);
            free_inode(new_inode_num, true);
            return false;
        }

        Inode nd;
        memset(&nd, 0, sizeof(nd));
        nd.i_mode = S_IFDIR | 0755;
        nd.i_links_count = 2;
        nd.i_size = fs.block_size;
        nd.i_blocks = fs.block_size / 512;
        nd.i_block[0] = data_blk;
        if (!write_inode(new_inode_num, &nd)) {
            free_block(data_blk);
            free_inode(new_inode_num, true);
            return false;
        }

        Inode parent_in;
        if (read_inode(parent_inode, &parent_in)) {
            parent_in.i_links_count++;
            write_inode(parent_inode, &parent_in);
        }

        if (!add_dir_entry(parent_inode, new_inode_num, name, FT_DIR)) {
            free_block(data_blk);
            free_inode(new_inode_num, true);
            return false;
        }
        return true;
    }

    bool delete_file(const char* path) {
        if (!fs.mounted || !path) return false;

        char parent[256], name[64];
        split_path(path, parent, name);
        uint32_t parent_inode = resolve_relative(fs.current_inode, parent);
        if (parent_inode == 0) return false;

        uint32_t target = 0;
        if (!remove_dir_entry(parent_inode, name, &target)) return false;
        if (target == 0) return false;

        Inode in;
        if (!read_inode(target, &in)) return false;

        free_inode_blocks(&in);
        free_inode(target, (in.i_mode & S_IFMT) == S_IFDIR);
        return true;
    }

    bool cd(const char* path) {
        if (!fs.mounted || !path) return false;
        if (path[0] == '\0') return false;

        uint32_t save_inode = fs.current_inode;
        int save_depth = fs.depth;
        char save_names[32][64];
        uint32_t save_inodes[32];
        for (int i = 0; i < fs.depth; i++) {
            for (int k = 0; k < 64; k++) save_names[i][k] = fs.names[i][k];
            save_inodes[i] = fs.inodes[i];
        }

        uint32_t cur = fs.current_inode;

        if (path[0] == '/' || path[0] == '\\') {
            cur = fs.root_inode;
            fs.depth = 0;
            path++;
        }

        char comp[64];
        const char* p = path;
        while (*p) {
            int i = 0;
            while (*p && *p != '/' && *p != '\\' && i < 63) comp[i++] = *p++;
            comp[i] = '\0';
            if (*p) p++;
            if (i == 0) continue;

            if (strcmp(comp, ".") == 0) continue;

            if (strcmp(comp, "..") == 0) {
                if (cur == fs.root_inode) continue;
                Inode in;
                if (!read_inode(cur, &in)) goto rollback;
                uint32_t blk = get_inode_block(&in, 0);
                uint32_t parent = 0;
                if (blk && read_block(blk, dir_buf)) {
                    uint32_t off = 0;
                    while (off + sizeof(DirEntry) <= fs.block_size) {
                        DirEntry* de = (DirEntry*)(dir_buf + off);
                        if (de->rec_len < 8) break;
                        if (off + de->rec_len > fs.block_size) break;
                        if (de->name_len == 2 &&
                            dir_buf[off+8] == '.' && dir_buf[off+9] == '.') {
                            parent = de->inode;
                            break;
                        }
                        off += de->rec_len;
                    }
                }
                if (parent == 0) parent = fs.root_inode;
                cur = parent;
                if (fs.depth > 0) fs.depth--;
                continue;
            }

            uint32_t next = find_dir_entry(cur, comp, nullptr);
            if (next == 0) goto rollback;

            Inode in;
            if (!read_inode(next, &in)) goto rollback;
            if ((in.i_mode & S_IFMT) != S_IFDIR) goto rollback;

            cur = next;
            if (fs.depth < 32) {
                int k = 0;
                while (comp[k] && k < 63) { fs.names[fs.depth][k] = comp[k]; k++; }
                fs.names[fs.depth][k] = '\0';
                fs.inodes[fs.depth] = next;
                fs.depth++;
            }
        }

        fs.current_inode = cur;
        return true;

    rollback:
        fs.current_inode = save_inode;
        fs.depth = save_depth;
        for (int i = 0; i < save_depth; i++) {
            for (int k = 0; k < 64; k++) fs.names[i][k] = save_names[i][k];
            fs.inodes[i] = save_inodes[i];
        }
        return false;
    }

    void get_path(char* buf, std::size_t size) {
        std::size_t pos = 0;
        const char* root = "root";
        for (int i = 0; root[i] && pos + 1 < size; i++) buf[pos++] = root[i];
        for (int d = 0; d < fs.depth && pos + 1 < size; d++) {
            if (pos + 1 < size) buf[pos++] = '/';
            for (int i = 0; fs.names[d][i] && pos + 1 < size; i++) {
                buf[pos++] = fs.names[d][i];
            }
        }
        buf[pos] = '\0';
    }

    void pwd() {
        char buf[512];
        get_path(buf, sizeof(buf));
        console::puts(buf);
    }

    // list_dir - using dir_buf for the directory iteration
    void list_dir(const char* path) {
        if (!fs.mounted) { console::puts("Ext2 not mounted\n"); return; }

        uint32_t dir_inode = fs.current_inode;
        if (path && path[0] && !(path[0] == '/' && path[1] == '\0')) {
            dir_inode = resolve_relative(fs.current_inode, path);
            if (dir_inode == 0) { console::puts("Directory not found\n"); return; }
        }

        Inode dir;
        if (!read_inode(dir_inode, &dir)) return;
        if ((dir.i_mode & S_IFMT) != S_IFDIR) {
            console::puts("Not a directory\n");
            return;
        }

        int count = 0;
        uint32_t num_blocks = (dir.i_size + fs.block_size - 1) / fs.block_size;

        for (uint32_t b = 0; b < num_blocks; b++) {
            uint32_t blk = get_inode_block(&dir, b);
            if (blk == 0) continue;

            if (!read_block(blk, dir_buf)) return;

            uint32_t off = 0;
            while (off + sizeof(DirEntry) <= fs.block_size) {
                DirEntry* de = (DirEntry*)(dir_buf + off);
                if (de->rec_len < 8) break;
                if (off + de->rec_len > fs.block_size) break;

                if (de->inode != 0 && de->name_len > 0) {
                    // snapshot the entire entry first.
                    uint32_t saved_inode    = de->inode;
                    uint8_t  saved_name_len = de->name_len;
                    uint8_t  saved_type     = de->file_type;
                    uint16_t saved_rec_len  = de->rec_len;

                    char saved_name[256];
                    uint32_t copy_len = saved_name_len;
                    if (copy_len > 255) copy_len = 255;
                    for (uint32_t i = 0; i < copy_len; i++) {
                        saved_name[i] = (char)dir_buf[off + 8 + i];
                    }
                    saved_name[copy_len] = '\0';

                    bool is_dot    = (saved_name_len == 1 && saved_name[0] == '.');
                    bool is_dotdot = (saved_name_len == 2 && saved_name[0] == '.' && saved_name[1] == '.');

                    if (!is_dot && !is_dotdot) {
                        console::puts(saved_type == FT_DIR ? "  [DIR]  " : "  [FILE] ");
                        console::puts(saved_name);

                        if (saved_type != FT_DIR) {
                            uint32_t fsize = 0;
                            Inode si;
                            if (read_inode(saved_inode, &si)) {
                                fsize = si.i_size;
                            }

                            int name_len = (int)saved_name_len;
                            if (name_len < 20) {
                                for (int j = name_len; j < 20; j++) console::putc(' ');
                            } else {
                                console::putc(' ');
                            }

                            if (fsize == 0) {
                                console::putc('0');
                            } else {
                                char tmp[16];
                                int ti = 0;
                                uint32_t v = fsize;
                                while (v > 0) { tmp[ti++] = '0' + (v % 10); v /= 10; }
                                for (int j = ti - 1; j >= 0; j--) console::putc(tmp[j]);
                            }
                            console::puts(" bytes");
                        }
                        console::putc('\n');
                        count++;
                    }

                    off += saved_rec_len;
                } else {
                    off += de->rec_len;
                }
            }
        }

        console::puts("Total: ");
        char tmp[16];
        int ti = 0;
        uint32_t v = count;
        if (v == 0) {
            console::putc('0');
        } else {
            while (v > 0) { tmp[ti++] = '0' + (v % 10); v /= 10; }
            for (int j = ti - 1; j >= 0; j--) console::putc(tmp[j]);
        }
        console::puts(" entries\n");
    }

    bool file_exists(const char* path) {
        if (!fs.mounted || !path) return false;
        return resolve_relative(fs.current_inode, path) != 0;
    }

    uint32_t file_size(const char* path) {
        if (!fs.mounted || !path) return 0;
        uint32_t inode_num = resolve_relative(fs.current_inode, path);
        if (inode_num == 0) return 0;
        Inode in;
        if (!read_inode(inode_num, &in)) return 0;
        return in.i_size;
    }

    // format
    bool format(uint32_t partition_lba, uint32_t sector_count) {
        if (!disk::info.present) return false;
        if (sector_count < 8192) return false;

        uint32_t total_bytes = sector_count * 512;
        uint32_t block_size = 1024;
        uint32_t blocks_per_group = 8192;
        if (total_bytes > 128u * 1024 * 1024) { block_size = 2048; blocks_per_group = 16384; }
        if (total_bytes > 512u * 1024 * 1024) { block_size = 4096; blocks_per_group = 32768; }

        uint32_t sectors_per_block = block_size / 512;
        uint32_t first_data_block = (block_size == 1024) ? 1 : 0;
        uint32_t total_blocks = sector_count / sectors_per_block;
        if (total_blocks < first_data_block + 8) return false;
        uint32_t data_blocks = total_blocks - first_data_block;
        uint32_t groups = (data_blocks + blocks_per_group - 1) / blocks_per_group;

        uint32_t inode_size = 128;
        uint32_t inodes_per_group = 2048;
        uint32_t inode_table_blocks = (inodes_per_group * inode_size + block_size - 1) / block_size;
        uint32_t bgd_blocks = (groups * 32 + block_size - 1) / block_size;

        uint32_t g0_overhead = 1 + bgd_blocks + 2 + inode_table_blocks;
        uint32_t gN_overhead = 2 + inode_table_blocks;
        if (g0_overhead + 1 >= blocks_per_group) return false;

        uint32_t total_inodes = groups * inodes_per_group;
        uint32_t free_blocks = 0;
        for (uint32_t g = 0; g < groups; g++) {
            uint32_t tb = blocks_per_group;
            if (g == groups - 1) {
                uint32_t rem = data_blocks - g * blocks_per_group;
                if (rem < tb) tb = rem;
            }
            uint32_t ov = (g == 0) ? g0_overhead : gN_overhead;
            if (tb > ov) free_blocks += tb - ov;
        }
        if (free_blocks > 0) free_blocks--;
        uint32_t free_inodes = total_inodes - 10;

        uint8_t sb_bytes[1024];
        memset(sb_bytes, 0, 1024);
        Superblock* sb = (Superblock*)sb_bytes;

        sb->s_inodes_count      = total_inodes;
        sb->s_blocks_count      = total_blocks;
        sb->s_r_blocks_count    = total_blocks / 20;
        sb->s_free_blocks_count = free_blocks;
        sb->s_free_inodes_count = free_inodes;
        sb->s_first_data_block  = first_data_block;
        sb->s_log_block_size    = (block_size == 1024) ? 0 : (block_size == 2048 ? 1 : 2);
        sb->s_log_frag_size     = (int32_t)sb->s_log_block_size;
        sb->s_blocks_per_group  = blocks_per_group;
        sb->s_frags_per_group   = blocks_per_group;
        sb->s_inodes_per_group  = inodes_per_group;
        sb->s_magic             = EXT2_MAGIC;
        sb->s_state             = 1;
        sb->s_errors            = 1;
        sb->s_creator_os        = 0;
        sb->s_rev_level         = 1;
        sb->s_first_ino         = 11;
        sb->s_inode_size        = (uint16_t)inode_size;
        sb->s_feature_incompat  = 0x0002;

        {
            uint8_t sec[512];
            memcpy(sec, sb_bytes, 512);
            if (!disk::write_sector(partition_lba + 2, sec)) return false;
            memcpy(sec, sb_bytes + 512, 512);
            if (!disk::write_sector(partition_lba + 3, sec)) return false;
        }

        uint32_t bgd_bytes_total = groups * 32;
        uint8_t* bgd_buf = (uint8_t*)dma::alloc(bgd_bytes_total, 8);
        if (!bgd_buf) return false;
        memset(bgd_buf, 0, bgd_bytes_total);

        for (uint32_t g = 0; g < groups; g++) {
            uint32_t group_start = first_data_block + g * blocks_per_group;
            uint32_t bitmaps_start = group_start + ((g == 0) ? (1 + bgd_blocks) : 0);
            uint32_t bb_blk = bitmaps_start;
            uint32_t ib_blk = bitmaps_start + 1;
            uint32_t it_blk = bitmaps_start + 2;

            BlockGroupDescriptor bgd;
            memset(&bgd, 0, sizeof(bgd));
            bgd.bg_block_bitmap = bb_blk;
            bgd.bg_inode_bitmap = ib_blk;
            bgd.bg_inode_table  = it_blk;

            uint32_t tb = blocks_per_group;
            if (g == groups - 1) {
                uint32_t rem = data_blocks - g * blocks_per_group;
                if (rem < tb) tb = rem;
            }
            uint32_t ov = (g == 0) ? g0_overhead : gN_overhead;
            bgd.bg_free_blocks_count = (uint16_t)(tb - ov);
            bgd.bg_free_inodes_count = (uint16_t)inodes_per_group;
            bgd.bg_used_dirs_count   = 0;
            if (g == 0) {
                if (bgd.bg_free_inodes_count >= 10) bgd.bg_free_inodes_count -= 10;
                bgd.bg_used_dirs_count = 1;
                if (bgd.bg_free_blocks_count > 0) bgd.bg_free_blocks_count--;
            }
            memcpy(bgd_buf + g * 32, &bgd, 32);
        }

        for (uint32_t b = 0; b < bgd_blocks; b++) {
            uint8_t blk_buf[4096];
            memset(blk_buf, 0, block_size);
            uint32_t off = b * block_size;
            if (off < bgd_bytes_total) {
                uint32_t chunk = bgd_bytes_total - off;
                if (chunk > block_size) chunk = block_size;
                memcpy(blk_buf, bgd_buf + off, chunk);
            }
            uint32_t lba = partition_lba + (first_data_block + 1 + b) * sectors_per_block;
            for (uint32_t s = 0; s < sectors_per_block; s++) {
                if (!disk::write_sector(lba + s, blk_buf + s * 512)) return false;
            }
        }

        for (uint32_t g = 0; g < groups; g++) {
            uint32_t group_start = first_data_block + g * blocks_per_group;
            uint32_t bitmaps_start = group_start + ((g == 0) ? (1 + bgd_blocks) : 0);
            uint32_t bb_blk = bitmaps_start;
            uint32_t ib_blk = bitmaps_start + 1;
            uint32_t it_blk = bitmaps_start + 2;

            uint32_t tb = blocks_per_group;
            if (g == groups - 1) {
                uint32_t rem = data_blocks - g * blocks_per_group;
                if (rem < tb) tb = rem;
            }
            uint32_t ov = (g == 0) ? g0_overhead : gN_overhead;

            uint8_t bbm[4096];
            memset(bbm, 0, sizeof(bbm));
            for (uint32_t i = 0; i < ov; i++) bbm[i / 8] |= (1u << (i % 8));
            if (g == groups - 1 && tb < blocks_per_group) {
                uint32_t lim = blocks_per_group;
                if (lim > block_size * 8) lim = block_size * 8;
                for (uint32_t i = tb; i < lim; i++) bbm[i / 8] |= (1u << (i % 8));
            }
            if (g == 0) {
                uint32_t root_idx = ov;
                bbm[root_idx / 8] |= (1u << (root_idx % 8));
            }
            for (uint32_t s = 0; s < sectors_per_block; s++) {
                uint8_t sec[512];
                memcpy(sec, bbm + s * 512, 512);
                uint32_t lba = partition_lba + bb_blk * sectors_per_block + s;
                if (!disk::write_sector(lba, sec)) return false;
            }

            uint8_t ibm[4096];
            memset(ibm, 0, sizeof(ibm));
            if (g == 0) {
                for (int i = 0; i < 10; i++) ibm[i / 8] |= (1u << (i % 8));
            }
            for (uint32_t s = 0; s < sectors_per_block; s++) {
                uint8_t sec[512];
                memcpy(sec, ibm + s * 512, 512);
                uint32_t lba = partition_lba + ib_blk * sectors_per_block + s;
                if (!disk::write_sector(lba, sec)) return false;
            }

            uint8_t zero[512];
            memset(zero, 0, 512);
            for (uint32_t ib = 0; ib < inode_table_blocks; ib++) {
                for (uint32_t s = 0; s < sectors_per_block; s++) {
                    uint32_t lba = partition_lba + (it_blk + ib) * sectors_per_block + s;
                    if (!disk::write_sector(lba, zero)) return false;
                }
            }
        }

        uint32_t root_data_blk = first_data_block + g0_overhead;

        {
            uint8_t dir_block[4096];
            memset(dir_block, 0, block_size);
            DirEntry* dot = (DirEntry*)dir_block;
            dot->inode = 2;
            dot->rec_len = 12;
            dot->name_len = 1;
            dot->file_type = FT_DIR;
            dir_block[8] = '.';

            DirEntry* dotdot = (DirEntry*)(dir_block + 12);
            dotdot->inode = 2;
            dotdot->rec_len = (uint16_t)(block_size - 12);
            dotdot->name_len = 2;
            dotdot->file_type = FT_DIR;
            dir_block[20] = '.'; dir_block[21] = '.';

            uint32_t lba = partition_lba + root_data_blk * sectors_per_block;
            for (uint32_t s = 0; s < sectors_per_block; s++) {
                if (!disk::write_sector(lba + s, dir_block + s * 512)) return false;
            }
        }

        {
            Inode root;
            memset(&root, 0, sizeof(root));
            root.i_mode = S_IFDIR | 0755;
            root.i_links_count = 2;
            root.i_size = block_size;
            root.i_blocks = block_size / 512;
            root.i_block[0] = root_data_blk;

            uint32_t group_start = first_data_block;
            uint32_t bitmaps_start = group_start + 1 + bgd_blocks;
            uint32_t it_blk = bitmaps_start + 2;

            uint32_t byte_off = it_blk * block_size + 1 * inode_size;
            uint32_t blk = byte_off / block_size;
            uint32_t off = byte_off % block_size;

            uint8_t blk_buf[4096];
            uint32_t lba = partition_lba + blk * sectors_per_block;
            for (uint32_t s = 0; s < sectors_per_block; s++) {
                if (!disk::read_sector(lba + s, blk_buf + s * 512)) return false;
            }
            memcpy(blk_buf + off, &root, sizeof(root));
            for (uint32_t s = 0; s < sectors_per_block; s++) {
                if (!disk::write_sector(lba + s, blk_buf + s * 512)) return false;
            }
        }

        disk::flush();
        return true;
    }

}
