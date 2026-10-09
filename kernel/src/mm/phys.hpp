// SPDX-License-Identifier: BSD-2-Clause-Patent
// Copyright (c) 2026 Thinking Developer
//
// PodumatOS — a hobby operating system for x86_64.
//
// Physical page frame allocator (bitmap-based).
//
// Each bit in the bitmap tracks one 4 KB physical page:
//   1 = used
//   0 = free
//
// The bitmap itself is stored in a usable memory region.

#ifndef PHYS_HPP
#define PHYS_HPP

#include <cstdint>
#include <cstddef>
#include <limine.h>

namespace phys {

    constexpr uint64_t PAGE_SIZE  = 4096;
    constexpr uint64_t PAGE_SHIFT = 12;

    // Initialise from the Limine memory map.
    void init(limine_memmap_response* memmap, uint64_t hhdm_offset);

    bool is_initialized();

    // Total pages tracked by the bitmap, including reserved pages.
    uint64_t total_pages();
    uint64_t free_pages();

    // Includes reserved and allocated pages.
    uint64_t used_pages();

    // Allocate one 4 KB page. Returns 0 on failure.
    uint64_t alloc_page();

    // Allocate `count` contiguous pages. Returns 0 on failure.
    uint64_t alloc_pages(uint64_t count);

    void free_page(uint64_t phys);
    void free_pages(uint64_t phys, uint64_t count);

    bool is_allocated(uint64_t phys);

    // HHDM offset passed to init(), used to convert physical addresses to virtual.
    uint64_t hhdm_offset();

}

#endif
