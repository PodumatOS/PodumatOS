// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Thinking Developer
//
// PodumatOS — a hobby operating system for x86_64.

#include "drivers/dma.hpp"

namespace dma {

    static uint64_t g_hhdm_offset = 0;
    static uint64_t g_phys_base   = 0;
    static uint64_t g_size        = 0;
    static uint64_t g_offset      = 0;
    static bool     g_ready       = false;

    static bool ranges_overlap(uint64_t a_start, uint64_t a_end,
                               uint64_t b_start, uint64_t b_end) {
        return a_start < b_end && b_start < a_end;
    }

    void init(limine_memmap_response* memmap, uint64_t hhdm_offset) {
        g_hhdm_offset = hhdm_offset;
        g_phys_base   = 0;
        g_size        = 0;
        g_offset      = 0;
        g_ready       = false;

        if (!memmap) return;

        const uint64_t NEED     = 16ull * 1024ull * 1024ull;
        const uint64_t MIN_BASE = 0x100000ull;

        for (uint64_t i = 0; i < memmap->entry_count; i++) {
            limine_memmap_entry* e = memmap->entries[i];
            if (!e) continue;
            if (e->type != LIMINE_MEMMAP_USABLE) continue;
            if (e->length < NEED) continue;
            if (e->base < MIN_BASE) continue;

            uint64_t cand_start = e->base;
            uint64_t cand_end   = e->base + e->length;

            bool conflict = false;
            for (uint64_t j = 0; j < memmap->entry_count; j++) {
                limine_memmap_entry* o = memmap->entries[j];
                if (!o) continue;
                if (o->type != LIMINE_MEMMAP_EXECUTABLE_AND_MODULES) continue;

                uint64_t o_start = o->base;
                uint64_t o_end   = o->base + o->length;

                if (ranges_overlap(cand_start, cand_end, o_start, o_end)) {
                    conflict = true;
                    break;
                }
            }
            if (conflict) continue;

            g_phys_base = e->base;
            g_size      = e->length;
            g_offset    = 0;
            g_ready     = true;
            return;
        }
    }

    void* alloc(std::size_t size, std::size_t alignment) {
        if (!g_ready || size == 0) return nullptr;
        if (alignment == 0) alignment = 1;

        const uint64_t mask = (uint64_t)alignment - 1;

        uint64_t cur_phys     = g_phys_base + g_offset;
        uint64_t aligned_phys = (cur_phys + mask) & ~mask;
        uint64_t new_offset   = aligned_phys - g_phys_base;

        if (new_offset + size > g_size) return nullptr;

        g_offset = new_offset + size;

        return (void*)(g_phys_base + g_hhdm_offset + new_offset);
    }

    uint64_t map(void* virt) {
        if (!virt) return 0;
        uint64_t v = (uint64_t)virt;


        if (g_ready) {
            uint64_t region_virt_lo = g_phys_base + g_hhdm_offset;
            uint64_t region_virt_hi = region_virt_lo + g_size;
            if (v >= region_virt_lo && v < region_virt_hi) {
                return v - g_hhdm_offset;
            }
        }


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