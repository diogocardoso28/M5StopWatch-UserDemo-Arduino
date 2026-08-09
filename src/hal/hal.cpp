/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"

#include <FFat.h>
#include <Preferences.h>
#include <esp_mac.h>
#include <esp_system.h>
#include <mooncake_log.h>
#include <nvs_flash.h>

#include <cstdio>

namespace {

std::unique_ptr<Hal> halInstance;
constexpr std::string_view tag = "HAL-Arduino";

}  // namespace

Hal& GetHAL()
{
    if (!halInstance) {
        halInstance = std::make_unique<Hal>();
    }
    return *halInstance;
}

void Hal::init()
{
    _ready = false;
    auto config = M5.config();
    config.clear_display = false;
    config.internal_imu = true;
    config.internal_rtc = true;
    // ES8311 is a shared full-duplex codec on M5StopWatch.  It is initialized
    // by hal_audio.cpp instead of the independent M5Unified Mic/Speaker paths.
    config.internal_mic = false;
    config.internal_spk = false;
    config.pmic_button = true;
    M5.begin(config);

    if (M5.getBoard() != m5::board_t::board_M5StopWatch) {
        mclog::tagError(tag, "unexpected board id: {}", static_cast<int>(M5.getBoard()));
        return;
    }

    mclog::tagInfo(tag, "M5StopWatch detected, display={}x{}, psram={} bytes", M5.Display.width(),
                   M5.Display.height(), ESP.getPsramSize());

    const bool deviceReady = deviceInit();
    const bool fsReady = fsInit();
    const bool displayReady = displayInit();
    const bool audioReady = audioInit();
    const bool rtcReady = rtcInit();
    const bool imuReady = M5.Imu.isEnabled();
    if (!imuReady) {
        mclog::tagError(tag, "BMI270 is not available");
    }
    getButtonConfig(true);
    updatePowerState(true);
    const bool lvglReady = displayReady && lvglInit();

    _ready = displayReady && lvglReady;
    if (!_ready) {
        mclog::tagError(tag, "critical HAL init failed: display={} lvgl={}", displayReady, lvglReady);
    } else if (!(deviceReady && fsReady && audioReady && rtcReady && imuReady)) {
        mclog::tagWarn(tag,
                       "HAL started with unavailable services: device={} fs={} audio={} rtc={} imu={}",
                       deviceReady, fsReady, audioReady, rtcReady, imuReady);
    }
}

void Hal::delay(std::uint32_t ms)
{
    ::delay(ms);
}

std::uint32_t Hal::millis()
{
    return ::millis();
}

void Hal::feedTheDog()
{
    updateM5State();
    updatePowerState();
    ::delay(1);
}

std::array<uint8_t, 6> Hal::getFactoryMac()
{
    std::array<uint8_t, 6> mac{};
    esp_efuse_mac_get_default(mac.data());
    return mac;
}

std::string Hal::getFactoryMacString(std::string divider)
{
    const auto mac = getFactoryMac();
    char buffer[32] = {};
    std::snprintf(buffer, sizeof(buffer), "%02X%s%02X%s%02X%s%02X%s%02X%s%02X", mac[0], divider.c_str(), mac[1],
                  divider.c_str(), mac[2], divider.c_str(), mac[3], divider.c_str(), mac[4], divider.c_str(), mac[5]);
    return buffer;
}

void Hal::reboot()
{
    ESP.restart();
}

void Hal::factoryReset()
{
    stopLvglUpdate();
    nvs_flash_erase();
    ESP.restart();
}

bool Hal::fsInit()
{
    if (!FFat.begin(true, "/spiflash", 10, "storage")) {
        mclog::tagError(tag, "failed to mount or format storage FAT partition");
        return false;
    }
    mclog::tagInfo(tag, "FFat mounted: used={} total={}", FFat.usedBytes(), FFat.totalBytes());
    return true;
}

bool Hal::shouldShowGuide()
{
    Preferences settings;
    if (!settings.begin(SettingsNs.data(), false)) {
        return true;
    }
    const int count = settings.getInt("launch_count", 0);
    if (count < 5) {
        settings.putInt("launch_count", count + 1);
    }
    settings.end();
    return count < 5;
}
