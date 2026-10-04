// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Thinking Developer
//
// PodumatOS — a hobby operating system for x86_64.

#ifndef DMA_HPP
#define DMA_HPP

#include <cstdint>
#include <cstddef>
#include <limine.h>

namespace dma {

    void init(limine_memmap_response* memmap, uint64_t hhdm_offset);
    void* alloc(std::size_t size, std::size_t alignment = 4096);
    uint64_t map(void* virt);

    uint64_t get_phys_base();
    uint64_t get_size();
    uint64_t get_hhdm_offset();
    bool     is_initialized();
	
	void stop();

}

#endif