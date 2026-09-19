// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Thinking Developer
//
// PodumatOS — a hobby operating system for x86_64.

#include "drivers/ata.hpp"
#include "drivers/disk.hpp"
#include "io/io.hpp"

#define ATA_DATA      0x1F0
#define ATA_ERROR     0x1F1
#define ATA_FEATURES  0x1F1
#define ATA_SECTORS   0x1F2
#define ATA_LBA_LOW   0x1F3
#define ATA_LBA_MID   0x1F4
#define ATA_LBA_HIGH  0x1F5
#define ATA_DRIVE     0x1F6
#define ATA_STATUS    0x1F7
#define ATA_COMMAND   0x1F7
#define ATA_CONTROL   0x3F6

#define ATA_SR_BSY    0x80
#define ATA_SR_DRDY   0x40
#define ATA_SR_DF     0x20
#define ATA_SR_DSC    0x10
#define ATA_SR_DRQ    0x08
#define ATA_SR_CORR   0x04
#define ATA_SR_IDX    0x02
#define ATA_SR_ERR    0x01

namespace ata {

    static void delay_400ns() {
        for (int i = 0; i < 4; i++) inb(ATA_STATUS);
    }

    static void wait_bsy() {
        while (inb(ATA_STATUS) & ATA_SR_BSY);
    }

    static bool wait_drq() {
        while (inb(ATA_STATUS) & ATA_SR_BSY);
        return (inb(ATA_STATUS) & ATA_SR_DRQ) != 0;
    }

    bool init() {
        disk::info.present = false;

        outb(ATA_DRIVE, 0xA0);
        delay_400ns();

        outb(ATA_SECTORS, 0);
        outb(ATA_LBA_LOW, 0);
        outb(ATA_LBA_MID, 0);
        outb(ATA_LBA_HIGH, 0);

        outb(ATA_COMMAND, 0xEC);
        delay_400ns();

        uint8_t status = inb(ATA_STATUS);
        if (status == 0) return false;

        wait_bsy();

        uint8_t mid  = inb(ATA_LBA_MID);
        uint8_t high = inb(ATA_LBA_HIGH);
        if (mid != 0 || high != 0) return false;

        if (!wait_drq()) return false;

        uint16_t identify[256];
        for (int i = 0; i < 256; i++) identify[i] = inw(ATA_DATA);

        for (int i = 0; i < 20; i++) {
            uint16_t w = identify[27 + i];
            disk::info.model[i * 2]     = (char)((w >> 8) & 0xFF);
            disk::info.model[i * 2 + 1] = (char)(w & 0xFF);
        }
        disk::info.model[40] = '\0';
        for (int i = 39; i >= 0 && disk::info.model[i] == ' '; i--) disk::info.model[i] = '\0';

        for (int i = 0; i < 10; i++) {
            uint16_t w = identify[10 + i];
            disk::info.serial[i * 2]     = (char)((w >> 8) & 0xFF);
            disk::info.serial[i * 2 + 1] = (char)(w & 0xFF);
        }
        disk::info.serial[20] = '\0';
        for (int i = 19; i >= 0 && disk::info.serial[i] == ' '; i--) disk::info.serial[i] = '\0';

        for (int i = 0; i < 4; i++) {
            uint16_t w = identify[23 + i];
            disk::info.firmware[i * 2]     = (char)((w >> 8) & 0xFF);
            disk::info.firmware[i * 2 + 1] = (char)(w & 0xFF);
        }
        disk::info.firmware[8] = '\0';

        disk::info.sectors = identify[60] | ((uint32_t)identify[61] << 16);
        disk::info.lba48 = (identify[83] & (1u << 10)) != 0;
        if (disk::info.lba48) {
            disk::info.sectors48 =
                (uint64_t)identify[100] |
                ((uint64_t)identify[101] << 16) |
                ((uint64_t)identify[102] << 32) |
                ((uint64_t)identify[103] << 48);
        } else {
            disk::info.sectors48 = disk::info.sectors;
        }

        disk::info.present = true;
        return true;
    }

    bool read_sector(uint32_t lba, uint8_t* buffer) {
        if (!disk::info.present) return false;

        wait_bsy();
        outb(ATA_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
        delay_400ns();
        outb(ATA_SECTORS, 1);
        outb(ATA_LBA_LOW,  (uint8_t)(lba & 0xFF));
        outb(ATA_LBA_MID,  (uint8_t)((lba >> 8) & 0xFF));
        outb(ATA_LBA_HIGH, (uint8_t)((lba >> 16) & 0xFF));
        outb(ATA_COMMAND, 0x20);
        delay_400ns();

        if (!wait_drq()) return false;

        uint16_t* ptr = (uint16_t*)buffer;
        for (int i = 0; i < 256; i++) ptr[i] = inw(ATA_DATA);
        return true;
    }

    bool write_sector(uint32_t lba, const uint8_t* buffer) {
        if (!disk::info.present) return false;

        wait_bsy();
        outb(ATA_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
        delay_400ns();
        outb(ATA_SECTORS, 1);
        outb(ATA_LBA_LOW,  (uint8_t)(lba & 0xFF));
        outb(ATA_LBA_MID,  (uint8_t)((lba >> 8) & 0xFF));
        outb(ATA_LBA_HIGH, (uint8_t)((lba >> 16) & 0xFF));
        outb(ATA_COMMAND, 0x30);
        delay_400ns();

        if (!wait_drq()) return false;

        const uint16_t* ptr = (const uint16_t*)buffer;
        for (int i = 0; i < 256; i++) outw(ATA_DATA, ptr[i]);

        outb(ATA_COMMAND, 0xE7);
        wait_bsy();
        return true;
    }

    bool read_sectors(uint32_t lba, uint8_t count, uint8_t* buffer) {
        if (!disk::info.present || count == 0) return false;
        wait_bsy();
        outb(ATA_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
        delay_400ns();
        outb(ATA_SECTORS, count);
        outb(ATA_LBA_LOW,  (uint8_t)(lba & 0xFF));
        outb(ATA_LBA_MID,  (uint8_t)((lba >> 8) & 0xFF));
        outb(ATA_LBA_HIGH, (uint8_t)((lba >> 16) & 0xFF));
        outb(ATA_COMMAND, 0x20);
        delay_400ns();

        for (int s = 0; s < count; s++) {
            if (!wait_drq()) return false;
            uint16_t* ptr = (uint16_t*)(buffer + s * 512);
            for (int i = 0; i < 256; i++) ptr[i] = inw(ATA_DATA);
            delay_400ns();
        }
        return true;
    }

    bool write_sectors(uint32_t lba, uint8_t count, const uint8_t* buffer) {
        if (!disk::info.present || count == 0) return false;
        wait_bsy();
        outb(ATA_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
        delay_400ns();
        outb(ATA_SECTORS, count);
        outb(ATA_LBA_LOW,  (uint8_t)(lba & 0xFF));
        outb(ATA_LBA_MID,  (uint8_t)((lba >> 8) & 0xFF));
        outb(ATA_LBA_HIGH, (uint8_t)((lba >> 16) & 0xFF));
        outb(ATA_COMMAND, 0x30);
        delay_400ns();

        for (int s = 0; s < count; s++) {
            if (!wait_drq()) return false;
            const uint16_t* ptr = (const uint16_t*)(buffer + s * 512);
            for (int i = 0; i < 256; i++) outw(ATA_DATA, ptr[i]);
            delay_400ns();
        }

        outb(ATA_COMMAND, 0xE7);
        wait_bsy();
        return true;
    }

    bool flush() {
        if (!disk::info.present) return false;
        outb(ATA_COMMAND, 0xE7);
        wait_bsy();
        return true;
    }
	
	void stop() {
        disk::info.present = false;
    }

}