// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Thinking Developer
//
// PodumatOS — a hobby operating system for x86_64.

#include "drivers/disk.hpp"
#include "drivers/ata.hpp"
#include "drivers/ahci.hpp"

namespace disk {

    Info   info = {false, 0, 0, {0}, {0}, {0}, false};
    Driver active_driver = Driver::NONE;

    bool init(uint64_t hhdm_offset) {
        info.present  = false;
        active_driver = Driver::NONE;

        if (ahci::init(hhdm_offset)) {
            active_driver = Driver::AHCI;
            return true;
        }

        if (ata::init()) {
            active_driver = Driver::ATA;
            return true;
        }

        active_driver = Driver::NONE;
        info.present = false;
        return false;
    }

    bool read_sector(uint32_t lba, uint8_t* buffer) {
        switch (active_driver) {
            case Driver::AHCI: return ahci::read_sector(lba, buffer);
            case Driver::ATA:  return ata::read_sector(lba, buffer);
            default:           return false;
        }
    }

    bool write_sector(uint32_t lba, const uint8_t* buffer) {
        switch (active_driver) {
            case Driver::AHCI: return ahci::write_sector(lba, buffer);
            case Driver::ATA:  return ata::write_sector(lba, buffer);
            default:           return false;
        }
    }

    bool read_sectors(uint32_t lba, uint8_t count, uint8_t* buffer) {
        switch (active_driver) {
            case Driver::AHCI: return ahci::read_sectors(lba, count, buffer);
            case Driver::ATA:  return ata::read_sectors(lba, count, buffer);
            default:           return false;
        }
    }

    bool write_sectors(uint32_t lba, uint8_t count, const uint8_t* buffer) {
        switch (active_driver) {
            case Driver::AHCI: return ahci::write_sectors(lba, count, buffer);
            case Driver::ATA:  return ata::write_sectors(lba, count, buffer);
            default:           return false;
        }
    }

    bool flush() {
        switch (active_driver) {
            case Driver::AHCI: return ahci::flush();
            case Driver::ATA:  return ata::flush();
            default:           return false;
        }
    }

    const char* active_driver_name() {
        switch (active_driver) {
            case Driver::AHCI: return "AHCI";
            case Driver::ATA:  return "ATA PIO";
            default:           return "none";
        }
    }

    bool is_ata()  { return active_driver == Driver::ATA;  }
    bool is_ahci() { return active_driver == Driver::AHCI; }

}