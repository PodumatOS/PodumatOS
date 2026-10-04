// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Thinking Developer
//
// PodumatOS — a hobby operating system for x86_64.

#ifndef PCI_HPP
#define PCI_HPP

#include <cstdint>

namespace pci {

    struct Device {
        uint8_t  bus;
        uint8_t  slot;
        uint8_t  func;
        uint16_t vendor_id;
        uint16_t device_id;
        uint8_t  class_code;
        uint8_t  subclass;
        uint8_t  prog_if;
        uint8_t  revision;
        uint8_t  header_type;
        uint32_t bar[6];
        uint8_t  irq_line;
        uint8_t  irq_pin;
    };

    void init();

    int get_device_count();
    Device* get_device(int index);

    Device* find_by_class(uint8_t class_code, uint8_t subclass);

    uint32_t read_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
    void     write_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value);

    const char* class_name(uint8_t class_code, uint8_t subclass);

    void print_all();
}

#endif