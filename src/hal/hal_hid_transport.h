/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
// Internal to the HAL: the USB and BLE HID backends live in separate translation units because
// the TinyUSB and Bluedroid HID headers define clashing macros.
#include <cstddef>
#include <cstdint>

namespace hal_hid {

constexpr uint8_t KeyboardReportId = 1;
constexpr uint8_t MouseReportId    = 2;
constexpr const char* DeviceName   = "StopWatch Air Mouse";
constexpr const char* Manufacturer = "M5Stack";

struct __attribute__((packed)) KeyboardReport {
    uint8_t modifiers;
    uint8_t reserved;
    uint8_t keys[6];
};

struct __attribute__((packed)) MouseReport {
    uint8_t buttons;
    int8_t x;
    int8_t y;
    int8_t wheel;
    int8_t pan;
};

// One boot-style keyboard and one 5-button mouse with wheel and horizontal pan, shared by both transports
inline constexpr uint8_t ReportMap[] = {
    // Keyboard
    0x05, 0x01,              // Usage Page (Generic Desktop)
    0x09, 0x06,              // Usage (Keyboard)
    0xA1, 0x01,              // Collection (Application)
    0x85, KeyboardReportId,  //   Report ID
    0x05, 0x07,              //   Usage Page (Keyboard)
    0x19, 0xE0,              //   Usage Minimum (Left Control)
    0x29, 0xE7,              //   Usage Maximum (Right GUI)
    0x15, 0x00,              //   Logical Minimum (0)
    0x25, 0x01,              //   Logical Maximum (1)
    0x75, 0x01,              //   Report Size (1)
    0x95, 0x08,              //   Report Count (8)
    0x81, 0x02,              //   Input (Data, Variable, Absolute): modifiers
    0x95, 0x01,              //   Report Count (1)
    0x75, 0x08,              //   Report Size (8)
    0x81, 0x01,              //   Input (Constant): reserved
    0x05, 0x08,              //   Usage Page (LEDs)
    0x19, 0x01,              //   Usage Minimum (Num Lock)
    0x29, 0x05,              //   Usage Maximum (Kana)
    0x95, 0x05,              //   Report Count (5)
    0x75, 0x01,              //   Report Size (1)
    0x91, 0x02,              //   Output (Data, Variable, Absolute): LEDs
    0x95, 0x01,              //   Report Count (1)
    0x75, 0x03,              //   Report Size (3)
    0x91, 0x01,              //   Output (Constant): LED padding
    0x05, 0x07,              //   Usage Page (Keyboard)
    0x19, 0x00,              //   Usage Minimum (0)
    0x2A, 0xFF, 0x00,        //   Usage Maximum (255)
    0x15, 0x00,              //   Logical Minimum (0)
    0x26, 0xFF, 0x00,        //   Logical Maximum (255)
    0x95, 0x06,              //   Report Count (6)
    0x75, 0x08,              //   Report Size (8)
    0x81, 0x00,              //   Input (Data, Array): key codes
    0xC0,                    // End Collection

    // Mouse
    0x05, 0x01,              // Usage Page (Generic Desktop)
    0x09, 0x02,              // Usage (Mouse)
    0xA1, 0x01,              // Collection (Application)
    0x85, MouseReportId,     //   Report ID
    0x09, 0x01,              //   Usage (Pointer)
    0xA1, 0x00,              //   Collection (Physical)
    0x05, 0x09,              //     Usage Page (Buttons)
    0x19, 0x01,              //     Usage Minimum (1)
    0x29, 0x05,              //     Usage Maximum (5)
    0x15, 0x00,              //     Logical Minimum (0)
    0x25, 0x01,              //     Logical Maximum (1)
    0x95, 0x05,              //     Report Count (5)
    0x75, 0x01,              //     Report Size (1)
    0x81, 0x02,              //     Input (Data, Variable, Absolute): buttons
    0x95, 0x01,              //     Report Count (1)
    0x75, 0x03,              //     Report Size (3)
    0x81, 0x01,              //     Input (Constant): padding
    0x05, 0x01,              //     Usage Page (Generic Desktop)
    0x09, 0x30,              //     Usage (X)
    0x09, 0x31,              //     Usage (Y)
    0x09, 0x38,              //     Usage (Wheel)
    0x15, 0x81,              //     Logical Minimum (-127)
    0x25, 0x7F,              //     Logical Maximum (127)
    0x75, 0x08,              //     Report Size (8)
    0x95, 0x03,              //     Report Count (3)
    0x81, 0x06,              //     Input (Data, Variable, Relative)
    0x05, 0x0C,              //     Usage Page (Consumer)
    0x0A, 0x38, 0x02,        //     Usage (AC Pan)
    0x15, 0x81,              //     Logical Minimum (-127)
    0x25, 0x7F,              //     Logical Maximum (127)
    0x75, 0x08,              //     Report Size (8)
    0x95, 0x01,              //     Report Count (1)
    0x81, 0x06,              //     Input (Data, Variable, Relative)
    0xC0,                    //   End Collection
    0xC0,                    // End Collection
};

// USB backend (hal_hid_usb.cpp)
bool usbStart();
bool usbIsStarted();
bool usbIsConnected();
bool usbSend(uint8_t reportId, const void* report, size_t length);

// Bluetooth LE backend (hal_hid_ble.cpp)
bool bleStart(uint8_t batteryLevel);
void bleStop();
bool bleIsConnected();
bool bleSend(uint8_t reportId, const void* report, size_t length);

}  // namespace hal_hid
