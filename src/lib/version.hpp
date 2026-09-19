// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Thinking Developer
//
// PodumatOS — a hobby operating system for x86_64.

#ifndef VERSION_HPP
#define VERSION_HPP

namespace version {

    constexpr const char* OS_NAME    = "PodumatOS";
    constexpr const char* OS_VERSION = "0.1.0";

#if defined(__x86_64__)
    constexpr const char* ARCH = "x86_64";
#elif defined(__i386__)
    constexpr const char* ARCH = "i386";
#elif defined(__aarch64__)
    constexpr const char* ARCH = "aarch64";
#elif defined(__riscv) && (__riscv_xlen == 64)
    constexpr const char* ARCH = "riscv64";
#else
    constexpr const char* ARCH = "unknown";
#endif

}

#endif