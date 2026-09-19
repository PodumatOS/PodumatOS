// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Thinking Developer
//
// PodumatOS — a hobby operating system for x86_64.

#include "drivers/usb/usb.hpp"
#include "drivers/usb/ehci.hpp"
#include "lib/memory.hpp"
#include "io/io.hpp"

namespace usb {

    static bool     g_enumerated  = false;
    static uint16_t g_device_mps  = 8;
    static uint8_t  g_device_addr = 0;

    static void serial_puts(const char* s) {
        while (*s) {
            if (*s == '\n') { while ((inb(0x3F8+5) & 0x20)==0){} outb(0x3F8,'\r'); }
            while ((inb(0x3F8+5) & 0x20)==0){}
            outb(0x3F8, *s++);
        }
    }
    static void serial_dec(uint32_t v) {
        char buf[12]; int i = 0;
        if (v == 0) { serial_puts("0"); return; }
        while (v > 0) { buf[i++] = '0' + (v % 10); v /= 10; }
        char out[12]; int k = 0;
        for (int j = i - 1; j >= 0; j--) out[k++] = buf[j];
        out[k] = 0;
        serial_puts(out);
    }
    static void serial_hex8(uint8_t v) {
        static const char hex[] = "0123456789ABCDEF";
        char b[5] = {'0','x', hex[(v>>4)&0xF], hex[v&0xF], 0};
        serial_puts(b);
    }
    static void serial_hex16(uint16_t v) {
        static const char hex[] = "0123456789ABCDEF";
        char b[7] = {'0','x', hex[(v>>12)&0xF], hex[(v>>8)&0xF], hex[(v>>4)&0xF], hex[v&0xF], 0};
        serial_puts(b);
    }

    uint16_t get_device_mps()  { return g_device_mps; }
    uint8_t  get_device_addr() { return g_device_addr; }
    bool     is_enumerated()   { return g_enumerated; }

    bool send_setup(uint8_t addr, uint16_t mps,
                    uint8_t bmRequestType, uint8_t bRequest,
                    uint16_t wValue, uint16_t wIndex, uint16_t wLength,
                    void* data) {
        SetupPacket sp;
        sp.bmRequestType = bmRequestType;
        sp.bRequest      = bRequest;
        sp.wValue        = wValue;
        sp.wIndex        = wIndex;
        sp.wLength       = wLength;

        return ehci::control(addr, mps, (const uint8_t*)&sp, data, wLength);
    }

    bool get_descriptor(uint8_t addr, uint16_t mps, uint8_t type, uint8_t index,
                        uint16_t langid, void* buf, uint16_t len) {
        uint16_t wValue = ((uint16_t)type << 8) | index;
        uint8_t bmRequestType = REQ_DIR_DEV_TO_HOST | REQ_TYPE_STANDARD | REQ_RECIP_DEVICE;
        return send_setup(addr, mps, bmRequestType, REQ_GET_DESCRIPTOR,
                          wValue, langid, len, buf);
    }

    bool set_address(uint8_t new_addr) {
        uint8_t bmRequestType = REQ_DIR_HOST_TO_DEV | REQ_TYPE_STANDARD | REQ_RECIP_DEVICE;
        return send_setup(0, 8, bmRequestType, REQ_SET_ADDRESS,
                          new_addr, 0, 0, nullptr);
    }

    bool set_configuration(uint8_t addr, uint16_t mps, uint8_t config) {
        uint8_t bmRequestType = REQ_DIR_HOST_TO_DEV | REQ_TYPE_STANDARD | REQ_RECIP_DEVICE;
        return send_setup(addr, mps, bmRequestType, REQ_SET_CONFIGURATION,
                          config, 0, 0, nullptr);
    }

    bool enumerate_device(uint8_t new_addr) {
        serial_puts("[usb] enumerate addr=");
        serial_dec(new_addr);
        serial_puts("\n");

        if (g_enumerated) {
            serial_puts("[usb] already enumerated\n");
            return false;
        }
        if (new_addr == 0 || new_addr > 127) return false;

        DeviceDescriptor dd;
        memset(&dd, 0, sizeof(dd));
        if (!get_descriptor(0, 8, DESC_DEVICE, 0, 0, &dd, 8)) {
            serial_puts("[usb] GET_DESC(DEVICE,8) failed\n");
            return false;
        }

        uint8_t mps0 = dd.bMaxPacketSize0;
        if (mps0 == 0) mps0 = 8;
        serial_puts("[usb] bMaxPacketSize0=");
        serial_hex8(mps0);
        serial_puts("\n");

        if (!set_address(new_addr)) {
            serial_puts("[usb] SET_ADDRESS failed\n");
            return false;
        }
        serial_puts("[usb] SET_ADDRESS ok\n");

        // delay ~20ms
        for (volatile uint32_t i = 0; i < 20000000; i++) {
            asm volatile("pause");
        }

        memset(&dd, 0, sizeof(dd));
        if (!get_descriptor(new_addr, mps0, DESC_DEVICE, 0, 0, &dd, sizeof(dd))) {
            serial_puts("[usb] GET_DESC(DEVICE,18) failed\n");
            return false;
        }

        serial_puts("[usb] bLength=");   serial_hex8(dd.bLength);
        serial_puts(" idVendor=");       serial_hex16(dd.idVendor);
        serial_puts(" idProduct=");      serial_hex16(dd.idProduct);
        serial_puts(" bcdUSB=");         serial_hex16(dd.bcdUSB);
        serial_puts("\n");

        if (dd.bLength != 18) {
            serial_puts("[usb] invalid device descriptor length\n");
            return false;
        }

        if (dd.bMaxPacketSize0 != 0) mps0 = dd.bMaxPacketSize0;

        ConfigDescriptor cd;
        memset(&cd, 0, sizeof(cd));
        if (!get_descriptor(new_addr, mps0, DESC_CONFIGURATION, 0, 0, &cd, 9)) {
            serial_puts("[usb] GET_DESC(CONFIG,9) failed\n");
            return false;
        }

        uint16_t total_len = cd.wTotalLength;
        serial_puts("[usb] wTotalLength=");
        serial_dec(total_len);
        serial_puts("\n");
        if (total_len == 0 || total_len > 4096) return false;
		
        uint8_t cfg_buf[4096];
        memset(cfg_buf, 0, sizeof(cfg_buf));
        if (!get_descriptor(new_addr, mps0, DESC_CONFIGURATION, 0, 0, cfg_buf, total_len)) {
            serial_puts("[usb] GET_DESC(CONFIG,full) failed\n");
            return false;
        }

        ConfigDescriptor* full_cd = (ConfigDescriptor*)cfg_buf;
        serial_puts("[usb] bConfigurationValue=");
        serial_hex8(full_cd->bConfigurationValue);
        serial_puts(" bNumInterfaces=");
        serial_dec(full_cd->bNumInterfaces);
        serial_puts("\n");

        if (full_cd->bConfigurationValue == 0) return false;


        if (!set_configuration(new_addr, mps0, full_cd->bConfigurationValue)) {
            serial_puts("[usb] SET_CONFIGURATION failed\n");
            return false;
        }
        serial_puts("[usb] SET_CONFIGURATION ok\n");

        g_device_mps  = mps0;
        g_device_addr = new_addr;
        g_enumerated  = true;
        return true;
    }

}