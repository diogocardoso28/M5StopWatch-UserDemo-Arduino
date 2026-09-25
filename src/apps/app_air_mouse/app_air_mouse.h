/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "model/air_pointer.h"
#include "model/keyboard.h"
#include "model/touch_gesture.h"
#include "view/view.h"
#include <apps/common/imu/gyro_calibrator.h>
#include <apps/common/imu/gyro_filter.h>
#include <apps/common/key_manager/key_manager.h>
#include <hal/hal.h>
#include <mooncake.h>
#include <memory>
#include <vector>

/**
 * @brief Derived App
 *
 * Turns the device into a USB or Bluetooth keyboard and mouse. Pointing the device moves the
 * pointer, twisting it scrolls, button A / B are the left / right mouse buttons and holding both
 * goes home. Swipe between three pages: settings, touchpad (drag to move or scroll, tap to click,
 * hold to zero the gyro) and a small multi-tap keyboard.
 */
class AppAirMouse : public mooncake::AppAbility {
public:
    AppAirMouse();

    // Override lifecycle callbacks
    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    enum class TouchTarget { None, Pad, PadEdge, ModeChip, Key, SettingsItem };

    std::unique_ptr<view::AirMouseView> _view;
    std::unique_ptr<input::KeyManager> _key_manager;
    imu::GyroFilter _gyro_filter;
    imu::GyroCalibrator _calibrator;
    model::AirPointer _pointer;
    model::TouchGesture _gesture;
    model::Keyboard _keyboard;
    Hal::HidConfig _config;

    uint32_t _last_update_tick = 0;
    bool _restart_hid          = false;
    bool _status_dirty         = true;
    Hal::HidState _hid_state   = Hal::HidState::Off;

    TouchTarget _touch_target               = TouchTarget::None;
    int _touch_key                          = -1;
    view::SettingsPage::Item _touch_item    = view::SettingsPage::Item::None;
    bool _pad_scroll_mode                   = false;
    float _pad_scroll_x                     = 0.0f;
    float _pad_scroll_y                     = 0.0f;

    // Pending mouse report, sent outside the LVGL lock
    float _move_x          = 0.0f;
    float _move_y          = 0.0f;
    int _wheel             = 0;
    int _pan               = 0;
    bool _click_pending    = false;
    uint8_t _buttons_sent  = 0;
    uint32_t _last_report  = 0;
    uint32_t _last_tick_haptic = 0;
    std::vector<model::KeyStroke> _pending_strokes;

    void handleTouch(const model::TouchGesture::Frame& frame, float dt, uint32_t now);
    void beginTouch(const model::TouchGesture::Frame& frame);
    void endTouch(const model::TouchGesture::Frame& frame, uint32_t now);
    void padDrag(const model::TouchGesture::Frame& frame, float dt);
    void startZeroing();
    void updateZeroing(const imu::GyroFilter::Rates& raw, float dt);
    void changePage(int direction);
    void activateSetting(view::SettingsPage::Item item);
    void pressKey(int index, uint32_t now);

    void applyConfig(bool save);
    void refreshKeyboard();
    void refreshStatus();
    void scrollTick();

    void sendMouseReport(uint32_t now);
    void sendPendingStrokes();
};
