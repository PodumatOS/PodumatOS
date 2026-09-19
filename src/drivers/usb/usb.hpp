// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Thinking Developer
//
// PodumatOS — a hobby operating system for x86_64.

#ifndef USB_HPP
#define USB_HPP

#include <cstdint>
#include <cstddef>

namespace usb {

    struct __attribute__((packed)) SetupPacket {
        uint8_t  bmRequestType;
        uint8_t  bRequest;
        uint16_t wValue;
        uint16_t wIndex;
        uint16_t wLength;
    };

    struct __attribute__((packed)) DeviceDescriptor {
        uint8_t  bLength;
        uint8_t  bDescriptorType;
        uint16_t bcdUSB;
        uint8_t  bDeviceClass;
        uint8_t  bDeviceSubClass;
        uint8_t  bDeviceProtocol;
        uint8_t  bMaxPacketSize0;
        uint16_t idVendor;
        uint16_t idProduct;
        uint16_t bcdDevice;
        uint8_t  iManufacturer;
        uint8_t  iProduct;
        uint8_t  iSerialNumber;
        uint8_t  bNumConfigurations;
    };

    struct __attribute__((packed)) ConfigDescriptor {
        uint8_t  bLength;
        uint8_t  bDescriptorType;
        uint16_t wTotalLength;
        uint8_t  bNumInterfaces;
        uint8_t  bConfigurationValue;
        uint8_t  iConfiguration;
        uint8_t  bmAttributes;
        uint8_t  bMaxPower;
    };

    struct __attribute__((packed)) InterfaceDescriptor {
        uint8_t bLength;
        uint8_t bDescriptorType;
        uint8_t bInterfaceNumber;
        uint8_t bAlternateSetting;
        uint8_t bNumEndpoints;
        uint8_t bInterfaceClass;
        uint8_t bInterfaceSubClass;
        uint8_t bInterfaceProtocol;
        uint8_t iInterface;
    };

    struct __attribute__((packed)) EndpointDescriptor {
        uint8_t  bLength;
        uint8_t  bDescriptorType;
        uint8_t  bEndpointAddress;
        uint8_t  bmAttributes;
        uint16_t wMaxPacketSize;
        uint8_t  bInterval;
    };

    constexpr uint8_t REQ_DIR_DEV_TO_HOST = 0x80;
    constexpr uint8_t REQ_DIR_HOST_TO_DEV = 0x00;
    constexpr uint8_t REQ_TYPE_STANDARD   = 0x00;
    constexpr uint8_t REQ_TYPE_CLASS      = 0x20;
    constexpr uint8_t REQ_TYPE_VENDOR     = 0x40;
    constexpr uint8_t REQ_RECIP_DEVICE    = 0x00;
    constexpr uint8_t REQ_RECIP_INTERFACE = 0x01;
    constexpr uint8_t REQ_RECIP_ENDPOINT  = 0x02;

    constexpr uint8_t REQ_GET_DESCRIPTOR      = 0x06;
    constexpr uint8_t REQ_SET_ADDRESS         = 0x05;
    constexpr uint8_t REQ_SET_CONFIGURATION   = 0x09;

    constexpr uint8_t DESC_DEVICE        = 0x01;
    constexpr uint8_t DESC_CONFIGURATION = 0x02;
    constexpr uint8_t DESC_STRING        = 0x03;
    constexpr uint8_t DESC_INTERFACE     = 0x04;
    constexpr uint8_t DESC_ENDPOINT      = 0x05;
    constexpr uint8_t DESC_HID           = 0x21;

    bool send_setup(uint8_t addr, uint16_t mps,
                    uint8_t bmRequestType, uint8_t bRequest,
                    uint16_t wValue, uint16_t wIndex, uint16_t wLength,
                    void* data);

    bool get_descriptor(uint8_t addr, uint16_t mps, uint8_t type, uint8_t index,
                        uint16_t langid, void* buf, uint16_t len);

    bool set_address(uint8_t new_addr);

    bool set_configuration(uint8_t addr, uint16_t mps, uint8_t config);

    bool enumerate_device(uint8_t new_addr);

    uint16_t get_device_mps();
    uint8_t  get_device_addr();
    bool     is_enumerated();

}

#endif