// SPDX-License-Identifier: BSD-2-Clause-Patent
// Copyright (c) 2026 Thinking Developer
//
// PodumatOS — a hobby operating system for x86_64.

#include "mm/phys.hpp"
#include "lib/memory.hpp"

namespace phys {

    // Bitmap of physical pages.  g_bitmap[i / 8] holds 8 pages:
    //   bit (i % 8) = 1 page i is used
    //   bit (i % 8) = 0 page i is free
    static uint8_t* g_bitmap       = nullptr;
    static uint64_t g_bitmap_size  = 0;    // bytes
    static uint64_t g_total_pages  = 0;
    static uint64_t g_free_pages   = 0;
    static uint64_t g_used_pages   = 0;
	static uint64_t g_hhdm_offset = 0;
    static bool     g_initialized  = false;

    // Hint for the next scan: the page index right after the last allocation
    // Makes sequential allocations O(1) in the common case instead of O(n)
    static uint64_t g_last_alloc_page = 0;

    static inline void bit_set(uint64_t page) {
        g_bitmap[page >> 3] |= (uint8_t)(1u << (page & 7));
    }
    static inline void bit_clear(uint64_t page) {
        g_bitmap[page >> 3] &= (uint8_t)~(1u << (page & 7));
    }
    static inline bool bit_test(uint64_t page) {
        return (g_bitmap[page >> 3] & (uint8_t)(1u << (page & 7))) != 0;
    }

    static constexpr uint64_t INVALID_INDEX = (uint64_t)-1;

    static uint64_t find_free_run(uint64_t count, uint64_t hint) {
        if (count == 0 || count > g_total_pages) return INVALID_INDEX;

        uint64_t start = hint;
        if (start >= g_total_pages) start = 0;

        uint64_t run = 0;
        uint64_t run_start = 0;

        for (uint64_t p = start; p < g_total_pages; p++) {
            if (!bit_test(p)) {
                if (run == 0) run_start = p;
                run++;
                if (run >= count) return run_start;
            } else {
                run = 0;
            }
        }
        for (uint64_t p = 0; p < start; p++) {
            if (!bit_test(p)) {
                if (run == 0) run_start = p;
                run++;
                if (run >= count) return run_start;
            } else {
                run = 0;
            }
        }
        return INVALID_INDEX;
    }

    static uint64_t reserve_bitmap_storage(limine_memmap_response* memmap,
                                           uint64_t bitmap_size) {
        constexpr uint64_t LOW_RESERVED = 0x100000; // 1 MiB

        for (uint64_t i = 0; i < memmap->entry_count; i++) {
            limine_memmap_entry* e = memmap->entries[i];
            if (!e) continue;
            if (e->type != LIMINE_MEMMAP_USABLE) continue;
            if (e->length < bitmap_size) continue;

            uint64_t candidate = e->base;
            if (candidate < LOW_RESERVED) {
                uint64_t skip = LOW_RESERVED - candidate;
                if (skip >= e->length) continue;
                if (e->length - skip < bitmap_size) continue;
                candidate += skip;
            }
            return candidate;
        }
        return 0;
    }

    void init(limine_memmap_response* memmap, uint64_t hhdm_offset) {
        if (g_initialized) return;
        if (!memmap || memmap->entry_count == 0) return;
		g_hhdm_offset = hhdm_offset;

	// Find the highest usable address
		uint64_t max_addr = 0;
		for (uint64_t i = 0; i < memmap->entry_count; i++) {
			limine_memmap_entry* e = memmap->entries[i];
			if (!e) continue;
			if (e->type != LIMINE_MEMMAP_USABLE) continue;
			uint64_t end = e->base + e->length;
			if (end > max_addr) max_addr = end;
		}
        if (max_addr == 0) return;

        // Calculate page count
        g_total_pages = (max_addr + PAGE_SIZE - 1) / PAGE_SIZE;
        if (g_total_pages == 0) return;

        g_bitmap_size = (g_total_pages + 7) / 8;

        // Allocate space for the bitmap
        uint64_t bitmap_phys = reserve_bitmap_storage(memmap, g_bitmap_size);
        if (bitmap_phys == 0) return;

        // Map the bitmap through HHDM
        g_bitmap = (uint8_t*)(bitmap_phys + hhdm_offset);

        // Mark all pages as used
        memset(g_bitmap, 0xFF, g_bitmap_size);

        g_free_pages = 0;
        g_used_pages = g_total_pages;

        // Mark usable pages as free
        constexpr uint64_t LOW_RESERVED = 0x100000;
        uint64_t low_skip_page = (LOW_RESERVED + PAGE_SIZE - 1) / PAGE_SIZE;

        for (uint64_t i = 0; i < memmap->entry_count; i++) {
            limine_memmap_entry* e = memmap->entries[i];
            if (!e) continue;
            if (e->type != LIMINE_MEMMAP_USABLE) continue;

            uint64_t start_page = (e->base + PAGE_SIZE - 1) / PAGE_SIZE;
            uint64_t end_page   = (e->base + e->length) / PAGE_SIZE;

            if (start_page < low_skip_page) start_page = low_skip_page;
            if (end_page > g_total_pages)   end_page   = g_total_pages;
            if (start_page >= end_page) continue;

            for (uint64_t p = start_page; p < end_page; p++) {
                bit_clear(p);
                g_free_pages++;
                g_used_pages--;
            }
        }

		// Keep the bitmap pages reserved
        uint64_t bm_start = bitmap_phys / PAGE_SIZE;
        uint64_t bm_end   = (bitmap_phys + g_bitmap_size + PAGE_SIZE - 1) / PAGE_SIZE;
        for (uint64_t p = bm_start; p < bm_end && p < g_total_pages; p++) {
            if (!bit_test(p)) {
                bit_set(p);
                g_free_pages--;
                g_used_pages++;
            }
        }

        g_last_alloc_page = 0;
        g_initialized = true;
    }

    bool is_initialized() { return g_initialized; }
    uint64_t total_pages() { return g_total_pages; }
    uint64_t free_pages()  { return g_free_pages;  }
    uint64_t used_pages()  { return g_used_pages;  }

    uint64_t alloc_page() {
        return alloc_pages(1);
    }

    uint64_t alloc_pages(uint64_t count) {
        if (!g_initialized || count == 0) return 0;
        if (count > g_free_pages) return 0;

        uint64_t p = find_free_run(count, g_last_alloc_page);
        if (p == INVALID_INDEX) return 0;

        for (uint64_t i = 0; i < count; i++) bit_set(p + i);

        g_free_pages -= count;
        g_used_pages += count;
        g_last_alloc_page = p + count;

        return p * PAGE_SIZE;
    }

    void free_page(uint64_t phys) {
        free_pages(phys, 1);
    }

    void free_pages(uint64_t phys, uint64_t count) {
        if (!g_initialized || count == 0) return;
        if (phys % PAGE_SIZE) return;  // must be aligned

        uint64_t start = phys / PAGE_SIZE;
        for (uint64_t i = 0; i < count; i++) {
            uint64_t p = start + i;
            if (p >= g_total_pages) break;
            if (bit_test(p)) {
                bit_clear(p);
                g_free_pages++;
                g_used_pages--;
            }
        }
    }

    bool is_allocated(uint64_t phys) {
        if (!g_initialized) return true;
        uint64_t p = phys / PAGE_SIZE;
        if (p >= g_total_pages) return true;
        return bit_test(p);
    }

	uint64_t hhdm_offset() { return g_hhdm_offset; }
}
