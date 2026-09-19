// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Thinking Developer
//
// PodumatOS — a hobby operating system for x86_64.

#include "shell/shell.hpp"
#include "console/console.hpp"
#include "drivers/input.hpp"
#include "shell/commands.hpp"

namespace shell {
    void run() {
        console::puts("Welcome to PodumatOS v0.1\n");

        char buffer[256];
        int pos = 0;

        while (true) {
            console::puts("root@podumatos: ");
            pos = 0;
            buffer[0] = '\0';

            while (true) {
                char c = input::get_char();
                if (c == 0) continue;

                if (c == '\n') {
                    console::putc('\n');
                    buffer[pos] = '\0';
                    break;
                } else if (c == '\b') {
                    if (pos > 0) {
                        pos--;
                        console::backspace();
                    }
                } else if (pos < 255) {
                    buffer[pos++] = c;
                    console::putc(c);
                }
            }

            commands::execute(buffer);
        }
    }
}