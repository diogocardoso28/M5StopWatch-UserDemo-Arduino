/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <M5Unified.h>
#include <apps/common/common.h>
#include <core/logic.h>
#include <lvgl.h>
#include <smooth_lvgl.hpp>
#include <smooth_ui_toolkit.hpp>
#include <uitk/short_namespace.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

class BootLogo {
public:
    BootLogo()
    {
        _panel = std::make_unique<uitk::lvgl_cpp::Container>(lv_screen_active());
        _panel->setSize(466, 466);
        _panel->setAlign(LV_ALIGN_CENTER);
        _panel->setBorderWidth(0);
        _panel->setBgOpa(0);
        _panel->setPaddingAll(0);
        _panel->setBgColor(lv_color_black());

        _labelLogo = std::make_unique<uitk::lvgl_cpp::Label>(_panel->get());
        _labelLogo->setTextFont(&lv_font_montserrat_28);
        _labelLogo->setTextColor(lv_color_hex(0xFFFFFF));
        _labelLogo->align(LV_ALIGN_CENTER, 0, -14);
        _labelLogo->setText("StopWatch");

        _labelMessage = std::make_unique<uitk::lvgl_cpp::Label>(_panel->get());
        _labelMessage->setTextFont(&lv_font_montserrat_16);
        _labelMessage->setTextColor(lv_color_hex(0xBFBFBF));
        _labelMessage->align(LV_ALIGN_CENTER, 0, 14);
        _labelMessage->setText("Starting up ...");

        _labelVersion = std::make_unique<uitk::lvgl_cpp::Label>(_panel->get());
        _labelVersion->setTextFont(&lv_font_montserrat_14);
        _labelVersion->setTextColor(lv_color_hex(0x8B8B8B));
        _labelVersion->align(LV_ALIGN_BOTTOM_MID, 0, -12);
        _labelVersion->setText(common::FirmwareVersion);
    }

private:
    std::unique_ptr<uitk::lvgl_cpp::Container> _panel;
    std::unique_ptr<uitk::lvgl_cpp::Label> _labelLogo;
    std::unique_ptr<uitk::lvgl_cpp::Label> _labelMessage;
    std::unique_ptr<uitk::lvgl_cpp::Label> _labelVersion;
};

class Hal {
public:
    void init();
    bool isReady() const { return _ready; }

    void delay(std::uint32_t ms);
    std::uint32_t millis();
    void feedTheDog();
    std::array<uint8_t, 6> getFactoryMac();
    std::string getFactoryMacString(std::string divider = "");
    void reboot();
    void factoryReset();

    uint8_t getBatteryLevel();
    bool isBatteryCharging(bool strict = false);

    void setBackLightBrightness(int brightness, bool saveToSettings = false);
    int getBackLightBrightness(bool loadFromSettings = false);
    LGFX_Device& getDisplay();
    LGFX_Sprite& getCanvas();
    void updateCanvas();

    lv_indev_t* lvTouchpad = nullptr;
    std::unique_ptr<BootLogo> bootLogo;
    bool lvglLock();
    void lvglUnlock();
    void startLvglUpdate();
    void stopLvglUpdate();

    struct TouchPoint {
        int num = 0;
        int x   = -1;
        int y   = -1;
    };
    TouchPoint getTouchPoint();

    void setSpeakerVolume(int volume, bool saveToSettings = false);
    int getSpeakerVolume(bool loadFromSettings = false);
    int getAudioSampleRate();
    void audioRecord(std::vector<int16_t>& data, uint16_t durationMs, float gain = 30.0f);
    void audioPlay(std::vector<int16_t>& data, bool async = true);

    struct AudioSpectrumFrame {
        static constexpr std::size_t bandCount = 20;
        std::array<float, bandCount> bands     = {};
        float peakFrequencyHz                  = 0.0f;
    };
    void updateAudioSpectrum();
    const AudioSpectrumFrame& getAudioSpectrum() const { return _audioSpectrum; }
    void playBootSfx();

    void vibrate(uint16_t durationMs, uint8_t strength = 100);
    void stopVibrate();

    struct ImuData {
        float accelX = 0.0f;
        float accelY = 0.0f;
        float accelZ = 0.0f;
        float gyroX  = 0.0f;
        float gyroY  = 0.0f;
        float gyroZ  = 0.0f;
    };
    void updateImuData();
    const ImuData& getImuData() const { return _imuData; }

    void syncRtcTimeToSystem();
    void syncSystemTimeToRtc();
    DateYmd getDateYmd();
    bool setDateYmd(const DateYmd& date);
    TimeHms getTimeHms();
    bool setTimeHms(const TimeHms& time);
    void setTimezone(std::string_view tz);
    std::string getTimezone();
    bool loadAlarmStorage(AlarmStorageSnapshot& snapshot);
    bool saveAlarmStorage(const AlarmStorageSnapshot& snapshot);
    void startAlarm();
    void stopAlarm();

    m5::Button_Class btnA;
    m5::Button_Class btnB;
    m5::Button_Class btnPwr;

    struct ButtonConfig {
        bool sfxEnabled     = true;
        bool vibrateEnabled = true;
    };
    void updateButtonStates();
    void setButtonConfig(ButtonConfig config, bool saveToSettings = false);
    const ButtonConfig& getButtonConfig(bool loadFromSettings = false);

    enum class HidTransport : uint8_t { Usb = 0, Bluetooth = 1 };
    enum class HidState : uint8_t { Off, Waiting, Connected };
    struct HidConfig {
        HidTransport transport = HidTransport::Bluetooth;
        bool airPointer        = true;
        bool twistScroll       = true;
        bool invertScroll      = false;
        uint8_t pointerSpeed   = 1;  // 0 slow, 1 medium, 2 fast
    };
    // Starting USB takes the USB port over from the serial console until the next reboot
    bool hidStart(HidTransport transport);
    void hidStop();
    HidState getHidState();
    bool isUsbHidStarted();
    bool hidSendMouse(uint8_t buttons, int8_t x, int8_t y, int8_t wheel, int8_t pan);
    bool hidSendKeyboard(uint8_t modifiers, const std::array<uint8_t, 6>& keys);
    void setHidConfig(HidConfig config, bool saveToSettings = false);
    const HidConfig& getHidConfig(bool loadFromSettings = false);

    bool loadBadgeImage(lv_obj_t* image);
    bool loadNextBadgeImage(lv_obj_t* image);
    bool loadPreviousBadgeImage(lv_obj_t* image);
    void startBadgeEditModeViaAp(std::function<void(std::string_view)> onLog);

    bool shouldShowGuide();

private:
    static constexpr std::string_view SettingsNs = "system";

    bool _ready = false;
    ImuData _imuData;
    ButtonConfig _buttonConfig;
    HidConfig _hidConfig;
    AudioSpectrumFrame _audioSpectrum;
    int _brightness = 80;
    int _speakerVolume = 80;
    uint8_t _batteryLevel = 0;
    bool _externalPower = false;
    bool _activelyCharging = false;
    uint16_t _filteredBatteryMv = 0;
    uint32_t _lastPowerUpdateMs = 0;

    bool deviceInit();
    bool displayInit();
    bool lvglInit();
    bool audioInit();
    bool rtcInit();
    bool fsInit();
    void updateM5State();
    void updatePowerState(bool force = false);
};

Hal& GetHAL();

class LvglLockGuard {
public:
    LvglLockGuard() { GetHAL().lvglLock(); }
    ~LvglLockGuard() { GetHAL().lvglUnlock(); }

    LvglLockGuard(const LvglLockGuard&) = delete;
    LvglLockGuard& operator=(const LvglLockGuard&) = delete;
};
