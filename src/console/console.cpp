// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Thinking Developer
//
// PodumatOS — a hobby operating system for x86_64.


#include "console/console.hpp"
#include "console/font.hpp"
#include "io/io.hpp"

namespace console {

    static limine_framebuffer* fb = nullptr;

    static std::size_t cursor_x = 0;
    static std::size_t cursor_y = 0;


    static std::uint32_t fg_color = 0xFFFFFFFF;
    static std::uint32_t bg_color = 0x000000;


    static int  mouse_x = 100;
    static int  mouse_y = 100;
    static bool mouse_visible = false;

    static constexpr int CURSOR_W = 12;
    static constexpr int CURSOR_H = 12;
    static std::uint32_t cursor_bg[CURSOR_W * CURSOR_H];


    void init(limine_framebuffer* framebuffer) {
        fb = framebuffer;
        clear();
    }


    void set_color(std::uint32_t fg, std::uint32_t bg) {
        fg_color = fg;
        bg_color = bg;
    }


    int get_width()  { return fb ? (int)fb->width  : 0; }
    int get_height() { return fb ? (int)fb->height : 0; }


    void draw_pixel(int x, int y, std::uint32_t color) {
        if (!fb) return;
        if (x < 0 || y < 0) return;
        if ((std::uint32_t)x >= fb->width) return;
        if ((std::uint32_t)y >= fb->height) return;

        std::uint32_t* fb_ptr = (std::uint32_t*)fb->address;
        std::size_t pitch = fb->pitch / 4;
        fb_ptr[(std::size_t)y * pitch + (std::size_t)x] = color;
    }


    std::uint32_t read_pixel(int x, int y) {
        if (!fb) return 0;
        if (x < 0 || y < 0) return 0;
        if ((std::uint32_t)x >= fb->width) return 0;
        if ((std::uint32_t)y >= fb->height) return 0;

        std::uint32_t* fb_ptr = (std::uint32_t*)fb->address;
        std::size_t pitch = fb->pitch / 4;
        return fb_ptr[(std::size_t)y * pitch + (std::size_t)x];
    }


    void draw_rect(int x, int y, int w, int h, std::uint32_t color) {
        for (int j = 0; j < h; j++) {
            for (int i = 0; i < w; i++) {
                draw_pixel(x + i, y + j, color);
            }
        }
    }


    static void scroll() {
        std::uint32_t* fb_ptr = (std::uint32_t*)fb->address;
        std::size_t pitch = fb->pitch / 4;
        std::size_t row_size = fb->width;

        for (std::size_t y = 16; y < fb->height; y++) {
            for (std::size_t x = 0; x < row_size; x++) {
                fb_ptr[(y - 16) * pitch + x] = fb_ptr[y * pitch + x];
            }
        }
        for (std::size_t y = fb->height - 16; y < fb->height; y++) {
            for (std::size_t x = 0; x < row_size; x++) {
                fb_ptr[y * pitch + x] = bg_color;
            }
        }
        if (cursor_y >= 16) cursor_y -= 16;
    }


    static void draw_char(char c, std::size_t cx, std::size_t cy) {
        std::uint32_t* fb_ptr = (std::uint32_t*)fb->address;
        std::size_t pitch = fb->pitch / 4;
        const std::uint8_t* glyph = font[(std::uint8_t)c];

        for (int y = 0; y < 16; y++) {
            for (int x = 0; x < 8; x++) {
                std::size_t px = cx + x;
                std::size_t py = cy + y;
                if (px >= fb->width || py >= fb->height) continue;
                if (glyph[y] & (1 << (7 - x))) {
                    fb_ptr[py * pitch + px] = fg_color;
                } else {
                    fb_ptr[py * pitch + px] = bg_color;
                }
            }
        }
    }


    void putc(char c) {
        if (c == '\n') {
            cursor_x = 0;
            cursor_y += 16;
        } else if (c == '\b') {
            backspace();
        } else {
            draw_char(c, cursor_x, cursor_y);
            cursor_x += 8;
            if (cursor_x + 8 > fb->width) {
                cursor_x = 0;
                cursor_y += 16;
            }
        }
        if (cursor_y + 16 > fb->height) {
            scroll();
        }
    }


    void backspace() {
        if (cursor_x >= 8) {
            cursor_x -= 8;
        } else if (cursor_y >= 16) {
            cursor_y -= 16;
            cursor_x = (fb->width / 8) * 8 - 8;
        }
        draw_char(' ', cursor_x, cursor_y);
    }


    void puts(const char* str) {
        while (*str) putc(*str++);
    }


    void clear() {
        std::uint32_t* fb_ptr = (std::uint32_t*)fb->address;
        for (std::size_t i = 0; i < (fb->pitch / 4) * fb->height; i++) {
            fb_ptr[i] = bg_color;
        }
        cursor_x = 0;
        cursor_y = 0;
    }

    static void save_bg() {
        for (int j = 0; j < CURSOR_H; j++) {
            for (int i = 0; i < CURSOR_W; i++) {
                cursor_bg[j * CURSOR_W + i] = read_pixel(mouse_x + i, mouse_y + j);
            }
        }
    }


    static void restore_bg() {
        for (int j = 0; j < CURSOR_H; j++) {
            for (int i = 0; i < CURSOR_W; i++) {
                draw_pixel(mouse_x + i, mouse_y + j, cursor_bg[j * CURSOR_W + i]);
            }
        }
    }


    static void draw_cursor_shape() {
        int cx = CURSOR_W / 2;
        int cy = CURSOR_H / 2;


        draw_pixel(mouse_x + cx, mouse_y + cy, 0xFFFFFF);


        for (int i = 1; i <= 3; i++) {
            draw_pixel(mouse_x + cx + i, mouse_y + cy, 0xFFFFFF);
            draw_pixel(mouse_x + cx - i, mouse_y + cy, 0xFFFFFF);
            draw_pixel(mouse_x + cx, mouse_y + cy + i, 0xFFFFFF);
            draw_pixel(mouse_x + cx, mouse_y + cy - i, 0xFFFFFF);
        }


        for (int i = 1; i <= 4; i++) {
            draw_pixel(mouse_x + cx + i, mouse_y + cy + 1, 0x000000);
            draw_pixel(mouse_x + cx - i, mouse_y + cy - 1, 0x000000);
            draw_pixel(mouse_x + cx + 1, mouse_y + cy + i, 0x000000);
            draw_pixel(mouse_x + cx - 1, mouse_y + cy - i, 0x000000);
        }
    }


    void show_cursor() {
        if (mouse_visible) return;
        save_bg();
        draw_cursor_shape();
        mouse_visible = true;
    }


    void hide_cursor() {
        if (!mouse_visible) return;
        restore_bg();
        mouse_visible = false;
    }


    void move_cursor(int x, int y) {
        if (!fb) return;

        if (x < 0) x = 0;
        if (y < 0) y = 0;
        if ((std::uint32_t)x > fb->width - CURSOR_W)  x = fb->width - CURSOR_W;
        if ((std::uint32_t)y > fb->height - CURSOR_H) y = fb->height - CURSOR_H;

        hide_cursor();
        mouse_x = x;
        mouse_y = y;
        show_cursor();
    }


    void move_cursor_rel(int dx, int dy) {
        move_cursor(mouse_x + dx, mouse_y + dy);
    }


    int get_cursor_x() { return mouse_x; }
    int get_cursor_y() { return mouse_y; }
}