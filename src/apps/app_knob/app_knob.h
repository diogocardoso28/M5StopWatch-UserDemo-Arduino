/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "model/knob.h"
#include "view/view.h"
#include <apps/common/imu/gyro_filter.h>
#include <apps/common/key_manager/key_manager.h>
#include <mooncake.h>
#include <memory>

/**
 * @brief Derived App
 *
 * Turns the whole device into a knob: twist it around the screen axis and the IMU gyro drives
 * a virtual knob with haptic feedback. Button A / B cycle through the knob modes, tapping the
 * screen zeroes the current mode.
 */
class AppKnob : public mooncake::AppAbility {
public:
    AppKnob();

    // Override lifecycle callbacks
    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    std::unique_ptr<view::KnobView> _view;
    std::unique_ptr<input::KeyManager> _key_manager;
    model::Knob _knob;
    uint32_t _last_update_tick = 0;
    uint32_t _last_tick_haptic = 0;
    imu::GyroFilter _gyro_filter{1.5f};  // deg/s deadband, slower rates are treated as still
    bool _reset_requested      = false;

    float readRotation(float dt);
    void changeMode(int direction);
    void playHaptic(const model::Knob::HapticEvent_t& event);
    void syncView();
};
