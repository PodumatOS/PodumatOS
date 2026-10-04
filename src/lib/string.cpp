// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Thinking Developer
//
// PodumatOS — a hobby operating system for x86_64.

#include "lib/string.hpp"
std::size_t strlen(const char* str) {
    std::size_t len = 0;
    while (str[len]) len++;
    return len;
}
int strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}
int strncmp(const char* s1, const char* s2, std::size_t n) {
    while (n && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    return n == 0 ? 0 : *(const unsigned char*)s1 - *(const unsigned char*)s2;
}