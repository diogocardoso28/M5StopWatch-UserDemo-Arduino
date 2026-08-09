/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"

#include <Preferences.h>
#include <apps/common/audio/audio.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mooncake_log.h>
#include <utility/M5IOE1_Class.hpp>

#include <algorithm>
#include <mutex>

namespace {

constexpr std::string_view powerTag = "HAL-Power";
constexpr std::string_view vibrationTag = "HAL-Vibration";
constexpr gpio_num_t buttonAPin = GPIO_NUM_2;
constexpr gpio_num_t buttonBPin = GPIO_NUM_1;
constexpr gpio_num_t speakerPaPin = GPIO_NUM_14;
constexpr uint8_t pmicAddress = 0x6E;
constexpr uint8_t ioeAddress = 0x4F;
constexpr uint32_t pmicI2cFrequency = 100000;
constexpr uint8_t ioeI2cConfigRegister = 0x23;
std::mutex powerMutex;

class Vibrator {
public:
    bool init()
    {
        M5.Power.setVibration(0);
        if (xTaskCreate([](void* context) { static_cast<Vibrator*>(context)->task(); }, "vibrator", 4 * 1024,
                        this, 5, &_taskHandle) != pdPASS) {
            mclog::tagError(vibrationTag, "failed to create vibrator task");
            return false;
        }
        return true;
    }

    void vibrate(uint16_t durationMs, uint8_t strength)
    {
        if (_taskHandle == nullptr) {
            return;
        }
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _enabled = strength != 0;
            _strength = std::min<uint8_t>(strength, 100);
            _endTick = xTaskGetTickCount() + std::max<TickType_t>(pdMS_TO_TICKS(durationMs), 1);
        }
        xTaskNotifyGive(_taskHandle);
    }

    void stop()
    {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _enabled = false;
        }
        if (_taskHandle != nullptr) {
            xTaskNotifyGive(_taskHandle);
        } else {
            M5.Power.setVibration(0);
        }
    }

private:
    static uint8_t motorLevel(uint8_t strength)
    {
        if (strength == 0) {
            return 0;
        }
        const uint8_t dutyPercent =
            25 + static_cast<uint8_t>((static_cast<uint16_t>(strength) * (100 - 25)) / 100);
        return static_cast<uint8_t>((static_cast<uint16_t>(dutyPercent) * 255U) / 100U);
    }

    void task()
    {
        mclog::tagInfo(vibrationTag, "vibrator task started");
        uint8_t currentStrength = 0;
        for (;;) {
            const TickType_t now = xTaskGetTickCount();
            TickType_t waitTicks = portMAX_DELAY;
            bool shouldVibrate = false;
            uint8_t targetStrength = 0;
            {
                std::lock_guard<std::mutex> lock(_mutex);
                if (_enabled && static_cast<int32_t>(_endTick - now) > 0) {
                    shouldVibrate = true;
                    targetStrength = _strength;
                    waitTicks = _endTick - now;
                } else {
                    _enabled = false;
                }
            }

            if (shouldVibrate) {
                if (currentStrength != targetStrength) {
                    M5.Power.setVibration(motorLevel(targetStrength));
                    currentStrength = targetStrength;
                }
            } else if (currentStrength != 0) {
                M5.Power.setVibration(0);
                currentStrength = 0;
            }
            ulTaskNotifyTake(pdTRUE, waitTicks);
        }
    }

    std::mutex _mutex;
    TaskHandle_t _taskHandle = nullptr;
    bool _enabled = false;
    TickType_t _endTick = 0;
    uint8_t _strength = 0;
};

Vibrator vibrator;

bool configurePmic()
{
    auto& pmic = M5.Power.M5pm1;
    bool ok = true;

    // Match the official M5PM1 1.0.6 configuration while retaining the
    // M5Unified instance that owns the shared I2C bus.
    ok = M5.In_I2C.writeRegister8(pmicAddress, 0x09, 0x00, pmicI2cFrequency) && ok;
    ok = M5.In_I2C.writeRegister8(pmicAddress, 0x09, 0x00, pmicI2cFrequency) && ok;
    ok = M5.In_I2C.writeRegister8(pmicAddress, 0x0A, 0x00, pmicI2cFrequency) && ok;
    ok = M5.In_I2C.bitOn(pmicAddress, 0x07, 1U << 5, pmicI2cFrequency) && ok;
    ok = pmic.setBatteryCharge(true) && ok;

    ok = pmic.setGPIOFunction(m5::M5PM1_Class::gpio3, m5::M5PM1_Class::gpio) && ok;
    ok = pmic.setGPIOMode(m5::M5PM1_Class::gpio3, m5::M5PM1_Class::output) && ok;
    ok = pmic.setGPIOOutput(m5::M5PM1_Class::gpio3, false) && ok;
    ok = pmic.setGPIOPull(m5::M5PM1_Class::gpio3, m5::M5PM1_Class::pull_none) && ok;
    ok = pmic.setGPIODrive(m5::M5PM1_Class::gpio3, m5::M5PM1_Class::push_pull) && ok;

    ok = pmic.setGPIOFunction(m5::M5PM1_Class::gpio2, m5::M5PM1_Class::gpio) && ok;
    ok = pmic.setGPIOMode(m5::M5PM1_Class::gpio2, m5::M5PM1_Class::input) && ok;
    ok = pmic.setGPIOPull(m5::M5PM1_Class::gpio2, m5::M5PM1_Class::pull_none) && ok;

    uint8_t buttonConfig = 0;
    if (!M5.In_I2C.readRegister(pmicAddress, 0x49, &buttonConfig, 1, pmicI2cFrequency)) {
        ok = false;
    } else {
        // Bits 2:1 = 1 s click delay; bit 0 disables single-click reset.
        buttonConfig = static_cast<uint8_t>((buttonConfig & ~0x07U) | 0x07U);
        ok = M5.In_I2C.writeRegister8(pmicAddress, 0x49, buttonConfig, pmicI2cFrequency) && ok;
    }
    return ok;
}

}  // namespace

bool Hal::deviceInit()
{
    gpio_reset_pin(buttonAPin);
    gpio_set_direction(buttonAPin, GPIO_MODE_INPUT);
    gpio_set_pull_mode(buttonAPin, GPIO_PULLUP_ONLY);
    gpio_reset_pin(buttonBPin);
    gpio_set_direction(buttonBPin, GPIO_MODE_INPUT);
    gpio_set_pull_mode(buttonBPin, GPIO_PULLUP_ONLY);

    const bool pmicReady = configurePmic();
    if (!pmicReady) {
        mclog::tagError(powerTag, "failed to apply official M5PM1 configuration");
    }

    // Match the official IOE bring-up order.  L3B powers the board's 3.3 V
    // peripherals, including ES8311, and must be high before codec access.
    bool ioeReady = M5.In_I2C.writeRegister8(ioeAddress, ioeI2cConfigRegister, 0x00, pmicI2cFrequency);
    ioeReady = M5.In_I2C.writeRegister8(ioeAddress, ioeI2cConfigRegister, 0x00, pmicI2cFrequency) && ioeReady;
    if (!ioeReady) {
        mclog::tagError(powerTag, "failed to disable M5IOE1 I2C sleep");
        return false;
    }

    auto& ioe = static_cast<m5::M5IOE1_Class&>(M5.getIOExpander(0));
    static constexpr m5::M5IOE1_Class::gpio_t outputPins[] = {
        m5::M5IOE1_Class::gpio9,  m5::M5IOE1_Class::gpio8, m5::M5IOE1_Class::gpio10,
        m5::M5IOE1_Class::gpio4,  m5::M5IOE1_Class::gpio5, m5::M5IOE1_Class::gpio1,
        m5::M5IOE1_Class::gpio3,
    };
    for (const auto pin : outputPins) {
        ioe.setHighImpedance(pin, false);
        ioe.setDirection(pin, true);
    }
    ioe.digitalWrite(m5::M5IOE1_Class::gpio8, true);
    ioe.digitalWrite(m5::M5IOE1_Class::gpio4, true);
    ioe.digitalWrite(m5::M5IOE1_Class::gpio10, false);
    ioe.digitalWrite(m5::M5IOE1_Class::gpio5, true);
    ioe.digitalWrite(m5::M5IOE1_Class::gpio1, false);
    ioe.digitalWrite(m5::M5IOE1_Class::gpio3, true);
    ioe.setPwmFrequency(5000);

    gpio_set_direction(speakerPaPin, GPIO_MODE_OUTPUT);
    gpio_set_level(speakerPaPin, 0);

    const bool vibratorReady = vibrator.init();

    uint32_t l3bRetryCount = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(80));
        if (ioe.digitalRead(m5::M5IOE1_Class::gpio8)) {
            break;
        }
        ioe.digitalWrite(m5::M5IOE1_Class::gpio8, true);
        ++l3bRetryCount;
        mclog::tagInfo(powerTag, "set L3B_EN HIGH, retry count: {}", l3bRetryCount);
    }

    return pmicReady && vibratorReady;
}

void Hal::updatePowerState(bool force)
{
    const uint32_t now = ::millis();
    if (!force && now - _lastPowerUpdateMs < 1000) {
        return;
    }
    _lastPowerUpdateMs = now;

    const int batteryMv = M5.Power.getBatteryVoltage();
    const int vbusMv = M5.Power.getVBUSVoltage();
    const bool activelyCharging = M5.Power.isCharging() == m5::Power_Class::is_charging;

    std::lock_guard<std::mutex> lock(powerMutex);
    if (batteryMv > 0) {
        _filteredBatteryMv =
            stopwatch_core::filterBatteryMillivolts(_filteredBatteryMv, static_cast<uint16_t>(batteryMv));
        _batteryLevel = stopwatch_core::batteryMillivoltsToPercent(_filteredBatteryMv);
    }
    _externalPower = vbusMv > 4000;
    _activelyCharging = activelyCharging;
}

uint8_t Hal::getBatteryLevel()
{
    std::lock_guard<std::mutex> lock(powerMutex);
    return _batteryLevel;
}

bool Hal::isBatteryCharging(bool strict)
{
    std::lock_guard<std::mutex> lock(powerMutex);
    return strict ? (_externalPower && _activelyCharging) : _externalPower;
}

void Hal::vibrate(uint16_t durationMs, uint8_t strength)
{
    vibrator.vibrate(durationMs, strength);
}

void Hal::stopVibrate()
{
    vibrator.stop();
}

void Hal::updateButtonStates()
{
    const uint32_t now = millis();
    btnA.setRawState(now, gpio_get_level(buttonAPin) == 0);
    btnB.setRawState(now, gpio_get_level(buttonBPin) == 0);
    btnPwr.setRawState(now, M5.BtnPWR.isPressed());

    const auto& config = getButtonConfig();
    if (btnA.wasPressed()) {
        if (config.sfxEnabled) {
            audio::play_tone_from_midi(94, 0.02f);
        }
        if (config.vibrateEnabled) {
            vibrate(20, 60);
        }
    } else if (btnB.wasPressed()) {
        if (config.sfxEnabled) {
            audio::play_tone_from_midi(96, 0.02f);
        }
        if (config.vibrateEnabled) {
            vibrate(20, 60);
        }
    }
}

void Hal::setButtonConfig(ButtonConfig config, bool saveToSettings)
{
    _buttonConfig = config;
    if (saveToSettings) {
        Preferences settings;
        if (settings.begin(SettingsNs.data(), false)) {
            settings.putBool("btn_sfx", config.sfxEnabled);
            settings.putBool("btn_vibrate", config.vibrateEnabled);
            settings.end();
        }
    }
}

const Hal::ButtonConfig& Hal::getButtonConfig(bool loadFromSettings)
{
    if (loadFromSettings) {
        Preferences settings;
        if (settings.begin(SettingsNs.data(), true)) {
            _buttonConfig.sfxEnabled = settings.getBool("btn_sfx", true);
            _buttonConfig.vibrateEnabled = settings.getBool("btn_vibrate", true);
            settings.end();
        }
    }
    return _buttonConfig;
}

void Hal::updateImuData()
{
    if (!M5.Imu.isEnabled()) {
        return;
    }

    M5.Imu.update();
    float sourceX = 0.0f;
    float sourceY = 0.0f;
    float sourceZ = 0.0f;
    if (M5.Imu.getAccel(&sourceX, &sourceY, &sourceZ)) {
        _imuData.accelX = sourceY;
        _imuData.accelY = sourceX;
        _imuData.accelZ = sourceZ;
    }
    if (M5.Imu.getGyro(&sourceX, &sourceY, &sourceZ)) {
        _imuData.gyroX = sourceY;
        _imuData.gyroY = sourceX;
        _imuData.gyroZ = sourceZ;
    }
}
