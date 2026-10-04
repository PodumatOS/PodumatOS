// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Thinking Developer
//
// PodumatOS — a hobby operating system for x86_64.

#ifndef CONSOLE_HPP
#define CONSOLE_HPP

#include <cstdint>
#include <cstddef>
#include <limine.h>

namespace console {


    void init(limine_framebuffer* framebuffer);


    void putc(char c);
    void puts(const char* str);
    void clear();
    void backspace();
    void set_color(std::uint32_t fg, std::uint32_t bg);


    void draw_pixel(int x, int y, std::uint32_t color);
    void draw_rect(int x, int y, int w, int h, std::uint32_t color);
    std::uint32_t read_pixel(int x, int y);


    void show_cursor();
    void hide_cursor();
    void move_cursor(int x, int y);
    void move_cursor_rel(int dx, int dy);
    int  get_cursor_x();
    int  get_cursor_y();


    int  get_width();
    int  get_height();
}

#endif