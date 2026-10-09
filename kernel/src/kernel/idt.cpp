// SPDX-License-Identifier: BSD-2-Clause-Patent
// Copyright (c) 2026 Thinking Developer
//
// PodumatOS — a hobby operating system for x86_64.

#include "kernel/idt.hpp"
#include <cstdint>

struct idt_entry {
    std::uint16_t isr_low;
    std::uint16_t kernel_cs;
    std::uint8_t  ist;
    std::uint8_t  attributes;
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
extern "C" void isr34();
extern "C" void isr35();
extern "C" void isr36();
extern "C" void isr37();
extern "C" void isr38();
extern "C" void isr39();
extern "C" void isr40();
extern "C" void isr41();
extern "C" void isr42();
extern "C" void isr43();
extern "C" void isr44();
extern "C" void isr45();
extern "C" void isr46();
extern "C" void isr47();

namespace idt {

    void set_gate(std::uint8_t num, std::uint64_t base, std::uint16_t sel, std::uint8_t flags) {
        idt_table[num].isr_low    = base & 0xFFFF;
        idt_table[num].kernel_cs  = sel;
        idt_table[num].ist        = 0;
        idt_table[num].attributes = flags;
        idt_table[num].isr_mid    = (base >> 16) & 0xFFFF;
        idt_table[num].isr_high   = (base >> 32) & 0xFFFFFFFF;
        idt_table[num].reserved   = 0;
    }

    void init() {
        idtr_reg.base  = (std::uint64_t)&idt_table[0];
        idtr_reg.limit = (std::uint16_t)sizeof(idt_entry) * 256 - 1;

        std::uint16_t current_cs;
        asm volatile("mov %%cs, %0" : "=r"(current_cs));

        // Vectors 0..31 (CPU exceptions) and 48..255 go to isr_default
        for (int i = 0; i < 32; i++)
            set_gate(i, (std::uint64_t)isr_default, current_cs, 0x8E);
        for (int i = 48; i < 256; i++)
            set_gate(i, (std::uint64_t)isr_default, current_cs, 0x8E);

        // IRQs 0..15 mapped to vectors 32..47
        set_gate(32, (std::uint64_t)isr32, current_cs, 0x8E);
        set_gate(33, (std::uint64_t)isr33, current_cs, 0x8E);
        set_gate(34, (std::uint64_t)isr34, current_cs, 0x8E);
        set_gate(35, (std::uint64_t)isr35, current_cs, 0x8E);
        set_gate(36, (std::uint64_t)isr36, current_cs, 0x8E);
        set_gate(37, (std::uint64_t)isr37, current_cs, 0x8E);
        set_gate(38, (std::uint64_t)isr38, current_cs, 0x8E);
        set_gate(39, (std::uint64_t)isr39, current_cs, 0x8E);
        set_gate(40, (std::uint64_t)isr40, current_cs, 0x8E);
        set_gate(41, (std::uint64_t)isr41, current_cs, 0x8E);
        set_gate(42, (std::uint64_t)isr42, current_cs, 0x8E);
        set_gate(43, (std::uint64_t)isr43, current_cs, 0x8E);
        set_gate(44, (std::uint64_t)isr44, current_cs, 0x8E);
        set_gate(45, (std::uint64_t)isr45, current_cs, 0x8E);
        set_gate(46, (std::uint64_t)isr46, current_cs, 0x8E);
        set_gate(47, (std::uint64_t)isr47, current_cs, 0x8E);

        asm volatile("lidt %0" : : "m"(idtr_reg));
    }

    void stop() {
        struct { std::uint16_t limit; std::uint64_t base; }
            __attribute__((packed)) null_idt = { 0, 0 };
        asm volatile("lidt %0" : : "m"(null_idt));
    }
}
