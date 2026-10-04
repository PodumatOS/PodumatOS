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
        console::puts("Welcome to PodumatOS v0.2\n");
        console::puts("Type 'help' for a list of commands.\n\n");

        char buffer[256];
        int pos = 0;

        while (true) {
            commands::shell_print_prompt();

            pos = 0;
            buffer[0] = '\0';

            while (true) {
                char c = input::get_char();
                if (c == 0) {
                    asm volatile("pause");
                    continue;
                }

                if (c == '\n') {
                    console::putc('\n');
                    buffer[pos] = '\0';
                    break;
                } else if (c == '\b') {
                    if (pos > 0) {
                        pos--;
                        console::backspace();
                    }
                } else if (c >= 32 && pos < 255) {
                    buffer[pos++] = c;
                    console::putc(c);
                }
            }

            commands::execute(buffer);
        }
    }
}
