// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Thinking Developer
//
// PodumatOS — a hobby operating system for x86_64.

#include "kernel/idt.hpp"
#include <cstdint>

struct idt_entry {
    std::uint16_t isr_low;
    std::uint16_t kernel_cs;
    std::uint8_t ist;
    std::uint8_t attributes;
    std::uint16_t isr_mid;
    std::uint32_t isr_high;
    std::uint32_t reserved;
} __attribute__((packed));

struct idtr {
    std::uint16_t limit;
    std::uint64_t base;
} __attribute__((packed));

__attribute__((aligned(0x10))) idt_entry idt_table[256];
idtr idtr_reg;

extern "C" void isr_default();
extern "C" void isr32();
extern "C" void isr33();

namespace idt {
    void set_gate(std::uint8_t num, std::uint64_t base, std::uint16_t sel, std::uint8_t flags) {
        idt_table[num].isr_low = base & 0xFFFF;
        idt_table[num].kernel_cs = sel;
        idt_table[num].ist = 0;
        idt_table[num].attributes = flags;
        idt_table[num].isr_mid = (base >> 16) & 0xFFFF;
        idt_table[num].isr_high = (base >> 32) & 0xFFFFFFFF;
        idt_table[num].reserved = 0;
    }

    void init() {
        idtr_reg.base = (std::uint64_t)&idt_table[0];
        idtr_reg.limit = (std::uint16_t)sizeof(idt_entry) * 256 - 1;

        std::uint16_t current_cs;
        asm volatile("mov %%cs, %0" : "=r"(current_cs));

        for (int i = 34; i < 256; i++) {
            set_gate(i, (std::uint64_t)isr_default, current_cs, 0x8E);
        }

        set_gate(32, (std::uint64_t)isr32, current_cs, 0x8E);
        set_gate(33, (std::uint64_t)isr33, current_cs, 0x8E);

        asm volatile("lidt %0" : : "m"(idtr_reg));
    }
	
	void stop() {
        struct { std::uint16_t limit; std::uint64_t base; }
            __attribute__((packed)) null_idt = { 0, 0 };
        asm volatile("lidt %0" : : "m"(null_idt));
    }
}