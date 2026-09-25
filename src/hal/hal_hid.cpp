/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"
#include "hal_hid_transport.h"

#include <Preferences.h>
#include <mooncake_log.h>

#include <algorithm>
#include <cstring>

namespace {

constexpr std::string_view tag = "HAL-HID";

bool hidActive                  = false;
Hal::HidTransport activeTransport = Hal::HidTransport::Bluetooth;

}  // namespace

bool Hal::hidStart(HidTransport transport)
{
    if (hidActive && transport == activeTransport) {
        return true;
    }
    hidStop();

    bool ok = false;
    if (transport == HidTransport::Usb) {
        ok = hal_hid::usbStart();
    } else {
        ok = hal_hid::bleStart(getBatteryLevel());
    }
    hidActive       = ok;
    activeTransport = transport;
    mclog::tagInfo(tag, "start {}: {}", transport == HidTransport::Usb ? "usb" : "ble", ok);
    return ok;
}

void Hal::hidStop()
{
    if (!hidActive) {
        return;
    }
    // Let go of anything still held so the host is not left with a stuck key or button
    hidSendMouse(0, 0, 0, 0, 0);
    hidSendKeyboard(0, {});

    if (activeTransport == HidTransport::Bluetooth) {
        hal_hid::bleStop();
    }
    // USB cannot be stopped, it just stays enumerated and idle
    hidActive = false;
}

Hal::HidState Hal::getHidState()
{
    if (!hidActive) {
        return HidState::Off;
    }
    const bool connected =
        activeTransport == HidTransport::Usb ? hal_hid::usbIsConnected() : hal_hid::bleIsConnected();
    return connected ? HidState::Connected : HidState::Waiting;
}

bool Hal::isUsbHidStarted()
{
    return hal_hid::usbIsStarted();
}

bool Hal::hidSendMouse(uint8_t buttons, int8_t x, int8_t y, int8_t wheel, int8_t pan)
{
    if (!hidActive) {
        return false;
    }
    const hal_hid::MouseReport report = {buttons, x, y, wheel, pan};
    if (activeTransport == HidTransport::Usb) {
        return hal_hid::usbSend(hal_hid::MouseReportId, &report, sizeof(report));
    }
    return hal_hid::bleSend(hal_hid::MouseReportId, &report, sizeof(report));
}

bool Hal::hidSendKeyboard(uint8_t modifiers, const std::array<uint8_t, 6>& keys)
{
    if (!hidActive) {
        return false;
    }
    hal_hid::KeyboardReport report = {modifiers, 0, {}};
    std::memcpy(report.keys, keys.data(), sizeof(report.keys));
    if (activeTransport == HidTransport::Usb) {
        return hal_hid::usbSend(hal_hid::KeyboardReportId, &report, sizeof(report));
    }
    return hal_hid::bleSend(hal_hid::KeyboardReportId, &report, sizeof(report));
}

void Hal::setHidConfig(HidConfig config, bool saveToSettings)
{
    config.pointerSpeed = std::min<uint8_t>(config.pointerSpeed, 2);
    _hidConfig          = config;
    if (saveToSettings) {
        Preferences settings;
        if (settings.begin(SettingsNs.data(), false)) {
            settings.putUChar("hid_trans", static_cast<uint8_t>(config.transport));
            settings.putBool("hid_air", config.airPointer);
            settings.putBool("hid_twist", config.twistScroll);
            settings.putBool("hid_invert", config.invertScroll);
            settings.putUChar("hid_speed", config.pointerSpeed);
            settings.end();
        }
    }
}

const Hal::HidConfig& Hal::getHidConfig(bool loadFromSettings)
{
    if (loadFromSettings) {
        Preferences settings;
        if (settings.begin(SettingsNs.data(), true)) {
            _hidConfig.transport = settings.getUChar("hid_trans", 1) == 0 ? HidTransport::Usb : HidTransport::Bluetooth;
            _hidConfig.airPointer   = settings.getBool("hid_air", true);
            _hidConfig.twistScroll  = settings.getBool("hid_twist", true);
            _hidConfig.invertScroll = settings.getBool("hid_invert", false);
            _hidConfig.pointerSpeed = std::min<uint8_t>(settings.getUChar("hid_speed", 1), 2);
            settings.end();
        }
    }
    return _hidConfig;
}
