// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Thinking Developer
//
// PodumatOS — a hobby operating system for x86_64.

#ifndef COMMANDS_HPP
#define COMMANDS_HPP

#include <cstdint>
#include <cstddef>
#include <limine.h>

namespace commands {

    struct CPUInfo {
        char vendor[13];
        char brand[49];
        bool has_sse;
        bool has_sse2;
        bool has_avx;
        bool has_avx2;
    };
    extern CPUInfo cpu_info;
    void init_cpuinfo();

    struct RAMInfo {
        std::uint64_t total_bytes;
        std::uint64_t usable_bytes;
        std::uint64_t reserved_bytes;
        std::uint64_t kernel_bytes;
        std::uint64_t framebuffer_bytes;
    };
    extern RAMInfo ram_info;
    void init_ram_info(limine_memmap_response* response);

    void set_bootloader_info(const char* name, const char* version);
    void set_firmware_type(std::uint64_t type);
    void set_cmdline(const char* cmdline);
    void set_boot_timestamp(std::int64_t ts);
	void set_acpi_rsdp(void* rsdp_phys, std::uint64_t hhdm);

    void execute(const char* cmd);
    void neofetch();
    void shutdown();
    void emerdown();
    void reboot();
    void calc(const char* args);

    void drivers();
    void lspci();
    void diskinfo();
    void disktest();
    void diskread(const char* args);
    void diskwrite(const char* args);
    void fdisk(const char* args);
    void format(const char* args);
    void mount_cmd(const char* args);
    void drives();
    void cd_cmd(const char* args);
    void pwd_cmd();
    void fat_ls(const char* args);
    void fat_cat(const char* args);
    void fat_mkdir(const char* args);
    void fat_rm(const char* args);
    void fat_write(const char* args);
}

#endif