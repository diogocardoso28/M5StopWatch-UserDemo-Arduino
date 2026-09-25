/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal_hid_transport.h"

#include <USB.h>
#include <USBHID.h>
#include <mooncake_log.h>

#include <cstring>
#include <memory>

namespace {

constexpr std::string_view tag = "HAL-HID-USB";

class UsbComboHid : public USBHIDDevice {
public:
    UsbComboHid()
    {
        // Must be registered before USB.begin(), the descriptors are built once when the stack starts
        USBHID::addDevice(this, sizeof(hal_hid::ReportMap));
    }

    void begin()
    {
        _hid.begin();
    }

    bool ready()
    {
        return _hid.ready();
    }

    bool send(uint8_t reportId, const void* report, size_t length)
    {
        return _hid.SendReport(reportId, report, length, 20);
    }

    uint16_t _onGetDescriptor(uint8_t* buffer) override
    {
        std::memcpy(buffer, hal_hid::ReportMap, sizeof(hal_hid::ReportMap));
        return sizeof(hal_hid::ReportMap);
    }

private:
    USBHID _hid;
};

std::unique_ptr<UsbComboHid> usbHid;
bool usbStarted = false;

}  // namespace

namespace hal_hid {

bool usbStart()
{
    if (usbStarted) {
        return true;
    }

    // The board runs the USB port as the built-in serial/JTAG. Starting TinyUSB moves the port
    // over to the OTG controller and there is no way back short of a reboot, so this only happens
    // once and stays up even after the app closes.
    mclog::tagInfo(tag, "switching the USB port to HID, the serial console is gone until reboot");
    usbHid = std::make_unique<UsbComboHid>();
    usbHid->begin();
    USB.productName(DeviceName);
    USB.manufacturerName(Manufacturer);
    usbStarted = USB.begin();
    if (!usbStarted) {
        mclog::tagError(tag, "USB.begin failed");
    }
    return usbStarted;
}

bool usbIsStarted()
{
    return usbStarted;
}

bool usbIsConnected()
{
    return usbStarted && static_cast<bool>(USB);
}

bool usbSend(uint8_t reportId, const void* report, size_t length)
{
    if (!usbIsConnected() || !usbHid->ready()) {
        return false;
    }
    return usbHid->send(reportId, report, length);
}

}  // namespace hal_hid
