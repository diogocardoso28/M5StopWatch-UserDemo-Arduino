/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal_hid_transport.h"

#include <BLEDevice.h>
#include <BLEHIDDevice.h>
#include <BLESecurity.h>
#include <BLEServer.h>
#include <mooncake_log.h>

#include <atomic>

namespace {

constexpr std::string_view tag = "HAL-HID-BLE";

// Ask for a 7.5-15 ms connection interval so the pointer feels as smooth as a real mouse
constexpr uint16_t minConnInterval = 6;   // 1.25 ms units
constexpr uint16_t maxConnInterval = 12;  // 1.25 ms units
constexpr uint16_t connTimeout     = 400; // 10 ms units

std::atomic<bool> advertisingWanted{false};
std::atomic<bool> connected{false};
std::atomic<uint16_t> connectionId{0};

BLEServer* server              = nullptr;
BLEHIDDevice* hid              = nullptr;
BLECharacteristic* keyboardIn  = nullptr;
BLECharacteristic* mouseIn     = nullptr;

class ServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer, esp_ble_gatts_cb_param_t* param) override
    {
        connectionId = param->connect.conn_id;
        connected    = true;
        pServer->updateConnParams(param->connect.remote_bda, minConnInterval, maxConnInterval, 0, connTimeout);
        mclog::tagInfo(tag, "host connected");
    }

    void onDisconnect(BLEServer* pServer) override
    {
        connected = false;
        mclog::tagInfo(tag, "host disconnected");
        if (advertisingWanted) {
            pServer->startAdvertising();
        }
    }
};

bool initStack()
{
    if (server) {
        return true;
    }

    // Bluedroid does not survive being torn down and brought back up in this core, so the stack
    // is started once and later sessions only toggle advertising.
    BLEDevice::init(hal_hid::DeviceName);
    server = BLEDevice::createServer();
    if (!server) {
        mclog::tagError(tag, "failed to create the GATT server");
        return false;
    }
    server->setCallbacks(new ServerCallbacks());

    hid        = new BLEHIDDevice(server);
    keyboardIn = hid->inputReport(hal_hid::KeyboardReportId);
    hid->outputReport(hal_hid::KeyboardReportId);  // LED state from the host, accepted and ignored
    mouseIn = hid->inputReport(hal_hid::MouseReportId);

    hid->manufacturer()->setValue(hal_hid::Manufacturer);
    hid->pnp(0x02, 0x303A, 0x8200, 0x0100);  // USB vendor id source, Espressif VID
    hid->hidInfo(0x00, 0x02);                // not localized, normally connectable
    hid->reportMap(const_cast<uint8_t*>(hal_hid::ReportMap), sizeof(hal_hid::ReportMap));
    hid->startServices();

    auto* security = new BLESecurity();
    security->setAuthenticationMode(ESP_LE_AUTH_BOND);
    security->setCapability(ESP_IO_CAP_NONE);

    auto* advertising = server->getAdvertising();
    advertising->setAppearance(HID_MOUSE);
    advertising->addServiceUUID(hid->hidService()->getUUID());
    advertising->setScanResponse(true);
    return true;
}

}  // namespace

namespace hal_hid {

bool bleStart(uint8_t batteryLevel)
{
    if (!initStack()) {
        return false;
    }
    hid->setBatteryLevel(batteryLevel);
    advertisingWanted = true;
    if (!connected) {
        server->startAdvertising();
    }
    return true;
}

void bleStop()
{
    if (!server) {
        return;
    }
    advertisingWanted = false;
    server->getAdvertising()->stop();
    if (connected) {
        server->disconnect(connectionId);
    }
}

bool bleIsConnected()
{
    return connected;
}

bool bleSend(uint8_t reportId, const void* report, size_t length)
{
    if (!connected) {
        return false;
    }
    auto* characteristic = reportId == KeyboardReportId ? keyboardIn : mouseIn;
    characteristic->setValue(static_cast<uint8_t*>(const_cast<void*>(report)), length);
    characteristic->notify();
    return true;
}

}  // namespace hal_hid
