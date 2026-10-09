// SPDX-License-Identifier: BSD-2-Clause-Patent
// Copyright (c) 2026 Thinking Developer
//
// PodumatOS — a hobby operating system for x86_64.

#include "drivers/dma.hpp"
#include "mm/phys.hpp"

namespace dma {

    static uint64_t g_hhdm_offset = 0;
    static uint64_t g_phys_base   = 0;
    static uint64_t g_size        = 0;
    static bool     g_ready       = false;

    void init(limine_memmap_response* memmap, uint64_t hhdm_offset) {
        g_hhdm_offset = hhdm_offset;
        g_phys_base   = 0;
        g_size        = 0;
        g_ready       = false;

        if (!memmap) return;

        phys::init(memmap, hhdm_offset);
        if (!phys::is_initialized()) return;

        const uint64_t MIN_BASE = 0x100000ull;

        for (uint64_t i = 0; i < memmap->entry_count; i++) {
            limine_memmap_entry* e = memmap->entries[i];
            if (!e) continue;
            if (e->type != LIMINE_MEMMAP_USABLE) continue;
            if (e->base < MIN_BASE) continue;
            if (e->length > g_size) {
                g_phys_base = e->base;
                g_size      = e->length;
            }
        }

        g_ready = true;
    }

    void* alloc(std::size_t size, std::size_t alignment) {
        if (!g_ready || size == 0) return nullptr;
        if (alignment == 0) alignment = 1;

        if (alignment > phys::PAGE_SIZE) return nullptr;

        uint64_t pages = (size + phys::PAGE_SIZE - 1) / phys::PAGE_SIZE;
        uint64_t phys  = phys::alloc_pages(pages);
        if (phys == 0) return nullptr;

        return (void*)(phys + g_hhdm_offset);
    }

    uint64_t map(void* virt) {
        if (!virt) return 0;
        uint64_t v = (uint64_t)virt;

        if (g_hhdm_offset != 0 && v >= g_hhdm_offset) {
            return v - g_hhdm_offset;
        }

        return v;
    }

    uint64_t get_phys_base()   { return g_phys_base; }
    uint64_t get_size()        { return g_size; }
    uint64_t get_hhdm_offset() { return g_hhdm_offset; }
    bool     is_initialized()  { return g_ready; }

    void stop() {
        g_ready = false;
    }

}
