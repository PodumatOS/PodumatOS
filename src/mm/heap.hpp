// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Thinking Developer
//
// PodumatOS — a hobby operating system for x86_64.
//
// Kernel heap allocator (kmalloc / kfree)
//
// Allocations are stored in page-backed regions.
// Freed blocks are reused when possible.
// Empty regions are returned to the physical allocator.
//
// Not thread-safe; the kernel is currently single-core.
//
// PUBLIC API
//   init()                — call once during boot, after phys::init().
//   kmalloc(size)         — allocate size bytes.
//   kcalloc(count, size)  — allocate count*size bytes, zeroed.
//   krealloc(ptr, size)   — resize an existing allocation.
//   kfree(ptr)            — release an allocation.
//   print_stats()         — dump a human-readable summary.
//
//   Statistics accessors are provided for the shell and boot log.
//

#ifndef HEAP_HPP
#define HEAP_HPP

#include <cstdint>
#include <cstddef>

namespace heap {

	// Initialize the heap. Must be called after phys::init().
    void init();

    // Allocation functions.
    void* kmalloc(size_t size);
    void* kcalloc(size_t count, size_t size);
    void* krealloc(void* ptr, size_t newsize);
    void  kfree(void* ptr);

    // Statistics.
    uint64_t total_region_bytes();
    uint64_t current_in_use_bytes();
    uint64_t peak_in_use_bytes();
    uint64_t region_count();
    uint64_t allocation_count();

    void print_stats();

}

#endif