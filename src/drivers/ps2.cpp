// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Thinking Developer
//
// PodumatOS — a hobby operating system for x86_64.

#include "drivers/ps2.hpp"
#include "io/io.hpp"

namespace ps2 {
    static bool shift_pressed = false;
    static bool caps_lock = false;

    static const char scancode_ascii[128] = {
        0,    27,   '1',  '2',  '3',  '4',  '5',  '6',
        '7',  '8',  '9',  '0',  '-',  '=',  '\b', '\t',
        'q',  'w',  'e',  'r',  't',  'y',  'u',  'i',
        'o',  'p',  '[',  ']',  '\n', 0,    'a',  's',
        'd',  'f',  'g',  'h',  'j',  'k',  'l',  ';',
        '\'', '`',  0,    '\\', 'z',  'x',  'c',  'v',
        'b',  'n',  'm',  ',',  '.',  '/',  0,    '*',
        0,    ' ',  0,    0,    0,    0,    0,    0,
        0, 0, 0, 0, 0, 0, 0, '7', '8', '9', '-', '4', '5', '6', '+', '1',
        '2', '3', '0', '.', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    };

    static const char scancode_ascii_shift[128] = {
        0,    27,   '!',  '@',  '#',  '$',  '%',  '^',
        '&',  '*',  '(',  ')',  '_',  '+',  '\b', '\t',
        'Q',  'W',  'E',  'R',  'T',  'Y',  'U',  'I',
        'O',  'P',  '{',  '}',  '\n', 0,    'A',  'S',
        'D',  'F',  'G',  'H',  'J',  'K',  'L',  ':',
        '\"', '~',  0,    '|',  'Z',  'X',  'C',  'V',
        'B',  'N',  'M',  '<',  '>',  '?',  0,    '*',
        0,    ' ',  0,    0,    0,    0,    0,    0,
        0, 0, 0, 0, 0, 0, 0, '7', '8', '9', '-', '4', '5', '6', '+', '1',
        '2', '3', '0', '.', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    };

    void init() {
        // clear output buffer
        while (inb(0x64) & 1) inb(0x60);

        // read cfg
        outb(0x64, 0x20);
        while (!(inb(0x64) & 1));
        std::uint8_t config = inb(0x60);

        config |= 0x01;
        config |= 0x40;

        // write cfg
        outb(0x64, 0x60);
        while (inb(0x64) & 2);
        outb(0x60, config);

        // turn on keyboard interface
        outb(0x64, 0xAE);
        while (inb(0x64) & 2);

        // clear buffer
        while (inb(0x64) & 1) inb(0x60);
    }

    bool has_data() {
        return (inb(0x64) & 0x01) != 0;
    }


    std::uint8_t read_data() {
        return inb(0x60);
    }

    char get_char() {
        if (!has_data()) return 0;

        std::uint8_t scancode = read_data();

        bool released = (scancode & 0x80) != 0;
        std::uint8_t code = scancode & 0x7F;

        // shift
        if (code == 0x2A || code == 0x36) {
            shift_pressed = !released;
            return 0;
        }

        if (code == 0x3A && !released) {
            caps_lock = !caps_lock;
            return 0;
        }

        if (released) return 0;

        char c = 0;
        if (code < 128) {
            c = shift_pressed ? scancode_ascii_shift[code] : scancode_ascii[code];
            if (caps_lock) {
                if (c >= 'a' && c <= 'z') c -= 32;
                else if (c >= 'A' && c <= 'Z') c += 32;
            }
        }
        return c;
    }
	
	void stop() {
        outb(0x64, 0xAD);
        while (inb(0x64) & 2) { }
        while (inb(0x64) & 1) inb(0x60);
    }
}