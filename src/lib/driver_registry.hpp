// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Thinking Developer
//
// PodumatOS — a hobby operating system for x86_64.

#ifndef DRIVER_REGISTRY_HPP
#define DRIVER_REGISTRY_HPP

namespace driver_registry {

    struct Entry {
        const char* name;
        const char* description;
        bool        ok;
    };

    void clear();
    void add(const char* name, const char* description, bool ok);
    int  count();
    const Entry* get(int index);

}

#endif