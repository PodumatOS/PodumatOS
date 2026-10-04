// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Thinking Developer
//
// PodumatOS — a hobby operating system for x86_64.

#include "lib/driver_registry.hpp"

namespace driver_registry {

    static constexpr int MAX_DRIVERS = 32;

    static Entry g_entries[MAX_DRIVERS];
    static int   g_count = 0;

    void clear() { g_count = 0; }

    void add(const char* name, const char* description, bool ok) {
        if (g_count >= MAX_DRIVERS) return;
        g_entries[g_count].name        = name;
        g_entries[g_count].description = description;
        g_entries[g_count].ok          = ok;
        g_count++;
    }

    int count() { return g_count; }

    const Entry* get(int index) {
        if (index < 0 || index >= g_count) return nullptr;
        return &g_entries[index];
    }

}