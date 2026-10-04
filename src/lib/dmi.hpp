// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Thinking Developer
//
// PodumatOS — a hobby operating system for x86_64.

#ifndef DMI_HPP
#define DMI_HPP

#include <cstdint>
#include <limine.h>

namespace dmi {

    void init(limine_smbios_response* response, uint64_t hhdm_offset);
    bool available();

    const char* motherboard();   // SMBIOS type 2
    const char* product();       // SMBIOS type 1 
    const char* bios_vendor();   // SMBIOS type 0
    const char* bios_version();  // SMBIOS type 0
    const char* bios_date();     // SMBIOS type 0
	void stop();

}

#endif