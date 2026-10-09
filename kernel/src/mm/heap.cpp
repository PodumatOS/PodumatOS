// SPDX-License-Identifier: BSD-2-Clause-Patent
// Copyright (c) 2026 Thinking Developer
//
// PodumatOS — a hobby operating system for x86_64.
//
// Kernel heap allocator (kmalloc / kfree)
//
// Allocations are stored in page-backed regions.
// Freed blocks are reused when possible.
// Regions are returned to the physical allocator when empty.
// Not thread-safe; the kernel is currently single-core.

#include "mm/heap.hpp"
#include "mm/phys.hpp"
#include "lib/memory.hpp"
#include "console/console.hpp"

namespace heap {
	
	// Returned pointers are 16-byte aligned.
	static constexpr uint64_t ALIGNMENT = 16;

	// Minimum region size: 16 pages (64 KB).
	static constexpr uint32_t MIN_REGION_PAGES = 16;

    // Magic values used to detect invalid or already freed blocks.
	static constexpr uint32_t BLOCK_MAGIC = 0xC0DEC0DEu;
	static constexpr uint32_t BLOCK_DEAD  = 0xDEADDEADu;
	
	struct BlockHeader;

	struct RegionHeader {
		RegionHeader* prev;       // offset  0, 8 bytes
        RegionHeader* next;       // offset  8, 8 bytes
        BlockHeader*  first;      // offset 16, 8 bytes
        uint32_t      pages;      // offset 24, 4 bytes
        uint32_t      size;       // offset 28, 4 bytes
        uint32_t      usage;      // offset 32, 4 bytes
        uint32_t      reserved;   // offset 36, 4 bytes
        uint64_t      padding;    // offset 40, 8 bytes
    };                            // total 48 bytes

    struct BlockHeader {
        BlockHeader*  prev;       // offset  0, 8 bytes
        BlockHeader*  next;       // offset  8, 8 bytes
        RegionHeader* region;     // offset 16, 8 bytes
        uint64_t      reserved;   // offset 24, 8 bytes
        uint32_t      magic;      // offset 32, 4 bytes
        uint32_t      size;       // offset 36, 4 bytes
        uint32_t      req_size;   // offset 40, 4 bytes
        uint32_t      padding;    // offset 44, 4 bytes
    };                            // total 48 bytes

    static_assert(sizeof(RegionHeader) % ALIGNMENT == 0,
                  "RegionHeader must be a multiple of ALIGNMENT");
    static_assert(sizeof(BlockHeader) % ALIGNMENT == 0,
                  "BlockHeader must be a multiple of ALIGNMENT");

    static RegionHeader* g_root        = nullptr;
    static RegionHeader* g_best        = nullptr;
    static bool          g_initialized = false;

    static uint64_t g_total_region_bytes = 0;
    static uint64_t g_in_use_bytes       = 0;
    static uint64_t g_peak_in_use_bytes  = 0;
    static uint64_t g_region_count       = 0;
    static uint64_t g_live_allocations   = 0;

    static inline uint64_t align_up(uint64_t value, uint64_t alignment) {
        return (value + alignment - 1) & ~(alignment - 1);
    }

    // Allocate pages and return their HHDM address.
    static void* region_alloc(uint32_t pages) {
        uint64_t phys = phys::alloc_pages(pages);
        if (phys == 0) return nullptr;
        return (void*)(phys + phys::hhdm_offset());
    }

    // Return a region's pages to phys::.
    static void region_free(void* virt, uint32_t pages) {
        if (!virt) return;
        uint64_t v    = (uint64_t)virt;
        uint64_t hhdm = phys::hhdm_offset();
        if (hhdm == 0 || v < hhdm) return;
        phys::free_pages(v - hhdm, pages);
    }

    // Recompute the best-fit hint
    static void refresh_best_hint() {
        uint64_t best_free = 0;
        RegionHeader* best = nullptr;
        for (RegionHeader* r = g_root; r; r = r->next) {
            uint64_t free_bytes = (uint64_t)r->size - r->usage;
            if (free_bytes > best_free) {
                best_free = free_bytes;
                best      = r;
            }
        }
        g_best = best;
    }

    // Region management

    static RegionHeader* create_region(uint64_t payload) {
        uint64_t want = payload
                      + sizeof(RegionHeader)
                      + sizeof(BlockHeader);

        uint64_t pages = (want + phys::PAGE_SIZE - 1) / phys::PAGE_SIZE;
        if (pages < MIN_REGION_PAGES) pages = MIN_REGION_PAGES;

		// Check for uint32_t overflow.
        if (pages > (uint64_t)0xFFFFFFFFull / phys::PAGE_SIZE) {
            return nullptr;
        }

        void* mem = region_alloc((uint32_t)pages);
        if (!mem) return nullptr;

        RegionHeader* r = (RegionHeader*)mem;
        r->prev     = nullptr;
        r->next     = nullptr;
        r->first    = nullptr;
        r->pages    = (uint32_t)pages;
        r->size     = (uint32_t)(pages * phys::PAGE_SIZE);
        r->usage    = (uint32_t)sizeof(RegionHeader);
        r->reserved = 0;
        r->padding  = 0;

        // Append to the global list.
        if (!g_root) {
            g_root = r;
        } else {
            RegionHeader* tail = g_root;
            while (tail->next) tail = tail->next;
            tail->next = r;
            r->prev    = tail;
        }

        g_total_region_bytes += r->size;
        g_region_count       += 1;
        return r;
    }

    static void destroy_region(RegionHeader* r) {
        if (!r) return;

        if (r->prev) r->prev->next = r->next;
        if (r->next) r->next->prev = r->prev;
        if (g_root == r) g_root = r->next;
        if (g_best == r) g_best = nullptr;

        g_total_region_bytes -= r->size;
        g_region_count       -= 1;

        region_free(r, r->pages);
    }


	// Try to place a block in the region.
    static void* place_block(RegionHeader* r, uint64_t payload, uint64_t req) {
        const uint64_t needed = sizeof(BlockHeader) + payload;

        // Empty region.
        if (!r->first) {
            uint8_t* base = (uint8_t*)r + sizeof(RegionHeader);
            BlockHeader* b = (BlockHeader*)base;

            b->prev     = nullptr;
            b->next     = nullptr;
            b->region   = r;
            b->reserved = 0;
            b->magic    = BLOCK_MAGIC;
            b->size     = (uint32_t)payload;
            b->req_size = (uint32_t)req;
            b->padding  = 0;

            r->first  = b;
            r->usage += (uint32_t)needed;

            g_in_use_bytes += req;
            if (g_in_use_bytes > g_peak_in_use_bytes)
                g_peak_in_use_bytes = g_in_use_bytes;
            g_live_allocations += 1;

            return (uint8_t*)b + sizeof(BlockHeader);
        }

        // Gap before the first block.
        {
            uint8_t* region_data = (uint8_t*)r + sizeof(RegionHeader);
            uint8_t* first_block = (uint8_t*)r->first;
            uint64_t gap         = (uint64_t)(first_block - region_data);

            if (gap >= needed) {
                BlockHeader* b = (BlockHeader*)region_data;
                b->prev     = nullptr;
                b->next     = r->first;
                b->region   = r;
                b->reserved = 0;
                b->magic    = BLOCK_MAGIC;
                b->size     = (uint32_t)payload;
                b->req_size = (uint32_t)req;
                b->padding  = 0;

                r->first->prev = b;
                r->first       = b;
                r->usage      += (uint32_t)needed;

                g_in_use_bytes += req;
                if (g_in_use_bytes > g_peak_in_use_bytes)
                    g_peak_in_use_bytes = g_in_use_bytes;
                g_live_allocations += 1;

                return (uint8_t*)b + sizeof(BlockHeader);
            }
        }

        // Gap between blocks.
        for (BlockHeader* cur = r->first; cur; cur = cur->next) {
            if (!cur->next) break;

            uint8_t* after_cur  = (uint8_t*)cur + sizeof(BlockHeader) + cur->size;
            uint8_t* next_start = (uint8_t*)cur->next;
            uint64_t gap        = (uint64_t)(next_start - after_cur);

            if (gap >= needed) {
                BlockHeader* b = (BlockHeader*)after_cur;
                b->prev     = cur;
                b->next     = cur->next;
                b->region   = r;
                b->reserved = 0;
                b->magic    = BLOCK_MAGIC;
                b->size     = (uint32_t)payload;
                b->req_size = (uint32_t)req;
                b->padding  = 0;

                cur->next->prev = b;
                cur->next       = b;
                r->usage       += (uint32_t)needed;

                g_in_use_bytes += req;
                if (g_in_use_bytes > g_peak_in_use_bytes)
                    g_peak_in_use_bytes = g_in_use_bytes;
                g_live_allocations += 1;

                return (uint8_t*)b + sizeof(BlockHeader);
            }
        }

        // Gap after the last block.
        {
            BlockHeader* last = r->first;
            while (last && last->next) last = last->next;
            if (!last) return nullptr;

            uint8_t* after_last = (uint8_t*)last + sizeof(BlockHeader) + last->size;
            uint8_t* region_end = (uint8_t*)r + r->size;
            uint64_t gap        = (uint64_t)(region_end - after_last);

            if (gap >= needed) {
                BlockHeader* b = (BlockHeader*)after_last;
                b->prev     = last;
                b->next     = nullptr;
                b->region   = r;
                b->reserved = 0;
                b->magic    = BLOCK_MAGIC;
                b->size     = (uint32_t)payload;
                b->req_size = (uint32_t)req;
                b->padding  = 0;

                last->next = b;
                r->usage  += (uint32_t)needed;

                g_in_use_bytes += req;
                if (g_in_use_bytes > g_peak_in_use_bytes)
                    g_peak_in_use_bytes = g_in_use_bytes;
                g_live_allocations += 1;

                return (uint8_t*)b + sizeof(BlockHeader);
            }
        }

        return nullptr;
    }

    // API

    void init() {
        if (g_initialized) return;

        g_root               = nullptr;
        g_best               = nullptr;
        g_total_region_bytes = 0;
        g_in_use_bytes       = 0;
        g_peak_in_use_bytes  = 0;
        g_region_count       = 0;
        g_live_allocations   = 0;

        g_initialized = true;
    }

    void* kmalloc(size_t size) {
        if (!g_initialized || size == 0) return nullptr;

        const uint64_t payload = align_up((uint64_t)size, ALIGNMENT);
        if (payload > 0xFFFFFFFFull) return nullptr;

        // Try the current best-fit region.
        if (g_best) {
            void* p = place_block(g_best, payload, (uint64_t)size);
            if (p) return p;
        }

        // Try the remaining regions.
        for (RegionHeader* r = g_root; r; r = r->next) {
            if (r == g_best) continue;
            void* p = place_block(r, payload, (uint64_t)size);
            if (p) {
                refresh_best_hint();
                return p;
            }
        }

        // Allocate a new region if none fit.
        RegionHeader* r = create_region(payload);
        if (!r) return nullptr;

        void* p = place_block(r, payload, (uint64_t)size);
        if (!p) {
            destroy_region(r);
            return nullptr;
        }

        refresh_best_hint();
        return p;
    }

    void* kcalloc(size_t count, size_t size) {
        if (count == 0 || size == 0) return nullptr;

        // Overflow check
        if (count > (size_t)-1 / size) return nullptr;

        size_t total = count * size;
        void*  p     = kmalloc(total);
        if (!p) return nullptr;

        memset(p, 0, total);
        return p;
    }

    void* krealloc(void* ptr, size_t newsize) {
        if (!ptr) return kmalloc(newsize);
        if (newsize == 0) { kfree(ptr); return nullptr; }

        BlockHeader* b = (BlockHeader*)((uint8_t*)ptr - sizeof(BlockHeader));
        if (b->magic != BLOCK_MAGIC) return nullptr;

        if (newsize <= b->req_size) {
            g_in_use_bytes -= b->req_size;
            b->req_size = (uint32_t)newsize;
            g_in_use_bytes += newsize;
            return ptr;
        }

        // Growing: allocate, copy, free.
        void* np = kmalloc(newsize);
        if (!np) return nullptr;

        memcpy(np, ptr, b->req_size);
        kfree(ptr);
        return np;
    }

    void kfree(void* ptr) {
        if (!ptr) return;

        BlockHeader* b = (BlockHeader*)((uint8_t*)ptr - sizeof(BlockHeader));

        // Reject invalid or already freed blocks.
        if (b->magic != BLOCK_MAGIC) return;

        RegionHeader* r = b->region;

        g_in_use_bytes     -= b->req_size;
        g_live_allocations -= 1;

        if (b->prev) b->prev->next = b->next;
        else         r->first      = b->next;
        if (b->next) b->next->prev = b->prev;

        b->magic = BLOCK_DEAD;

        r->usage -= (uint32_t)(sizeof(BlockHeader) + b->size);

        if (r->first == nullptr) {
            destroy_region(r);
            refresh_best_hint();
            return;
        }

		// The freed block becomes a hole that can be reused.
        refresh_best_hint();
    }

	// Statictics
    uint64_t total_region_bytes()   { return g_total_region_bytes; }
    uint64_t current_in_use_bytes() { return g_in_use_bytes;       }
    uint64_t peak_in_use_bytes()    { return g_peak_in_use_bytes;  }
    uint64_t region_count()         { return g_region_count;       }
    uint64_t allocation_count()     { return g_live_allocations;   }

    static void print_u64(uint64_t v) {
        if (v == 0) { console::putc('0'); return; }
        char buf[24]; int i = 0;
        while (v > 0) { buf[i++] = '0' + (v % 10); v /= 10; }
        for (int j = i - 1; j >= 0; j--) console::putc(buf[j]);
    }

    void print_stats() {
        console::puts("Heap statistics:\n");

        console::puts("  Regions:       ");
        print_u64(g_region_count);
        console::puts("  (");
        print_u64(g_total_region_bytes / 1024);
        console::puts(" KB reserved from phys)\n");

        console::puts("  In use:        ");
        print_u64(g_in_use_bytes);
        console::puts(" bytes (");
        print_u64(g_in_use_bytes / 1024);
        console::puts(" KB)\n");

        console::puts("  Peak in use:   ");
        print_u64(g_peak_in_use_bytes);
        console::puts(" bytes\n");

        console::puts("  Live blocks:   ");
        print_u64(g_live_allocations);
        console::putc('\n');
    }

}
