/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_knob.h"
#include <apps/common/audio/audio.h>
#include <hal/hal.h>
#include <mooncake.h>
#include <mooncake_log.h>
#include <assets/assets.h>
#include <smooth_lvgl.hpp>
#include <algorithm>
#include <cmath>

using namespace mooncake;

namespace {

// The BMI270 Z axis points out of the screen, so turning the device clockwise (as seen by the
// user) reads as a negative Z rate. Flip this if the knob turns the wrong way on your unit.
constexpr float _clockwise_sign = -1.0f;

constexpr float _max_dt = 0.1f;  // s, ignore longer gaps instead of integrating a jump

// The motor is an ERM: short or low-duty pulses never spin it up enough to be felt, only heard.
// So ticks always kick at full strength and their intensity is expressed through pulse length.
constexpr uint16_t _tick_min_ms          = 20;
constexpr uint16_t _tick_max_ms          = 34;
constexpr uint32_t _tick_min_interval_ms = 55;  // keep texture ticks distinct instead of a buzz
constexpr uint16_t _detent_click_ms      = 40;  // longer than any tick so steps still stand out

}  // namespace

AppKnob::AppKnob()
{
    setAppInfo().name = "Knob";
    setAppInfo().icon = (void*)&icon_knob;
}

void AppKnob::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");
}

void AppKnob::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    _key_manager      = std::make_unique<input::KeyManager>();
    _last_update_tick = GetHAL().millis();
    _last_tick_haptic = 0;
    _reset_requested  = false;

    LvglLockGuard lock;

    _view = std::make_unique<view::KnobView>();
    _view->init(lv_screen_active());
    _view->onTapped = [this]() { _reset_requested = true; };
    _view->setMode(_knob.getMode());
    syncView();
}

void AppKnob::onRunning()
{
    const auto key_event = _key_manager->update();
    if (key_event == input::KeyEvent::GoHome) {
        close();
        return;
    }

    const uint32_t now     = GetHAL().millis();
    const uint32_t elapsed = now - _last_update_tick;
    float rotation         = 0.0f;
    float dt               = 0.0f;
    // Same-millisecond loops are skipped so the elapsed time accumulates instead of being lost
    if (elapsed > 0) {
        _last_update_tick = now;
        dt                = std::min(static_cast<float>(elapsed) / 1000.0f, _max_dt);
        rotation          = readRotation(dt);
    }

    LvglLockGuard lock;

    if (!_view) {
        return;
    }

    if (key_event == input::KeyEvent::GoPrevious) {
        changeMode(-1);
    } else if (key_event == input::KeyEvent::GoNext) {
        changeMode(1);
    }

    if (_reset_requested) {
        _reset_requested = false;
        _knob.reset();
        GetHAL().vibrate(20, 60);
    }

    if (dt > 0.0f) {
        _knob.update(rotation, dt);
        playHaptic(_knob.popHapticEvent());
    }

    syncView();
    _view->update();
}

void AppKnob::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

    _key_manager.reset();
    GetHAL().stopVibrate();

    LvglLockGuard lock;

    _view.reset();
}

float AppKnob::readRotation(float dt)
{
    GetHAL().updateImuData();
    const auto& imu   = GetHAL().getImuData();
    const auto rates = _gyro_filter.update({imu.gyroX, imu.gyroY, imu.gyroZ}, dt);
    return _clockwise_sign * rates.z * dt;
}

void AppKnob::changeMode(int direction)
{
    _knob.nextMode(direction);
    _view->setMode(_knob.getMode());
    mclog::tagInfo(getAppInfo().name, "mode: {}", static_cast<int>(_knob.getMode()));
}

void AppKnob::playHaptic(const model::Knob::HapticEvent_t& event)
{
    using HapticType_t = model::Knob::HapticType_t;

    const uint32_t now = GetHAL().millis();
    const bool sfx     = GetHAL().getButtonConfig().sfxEnabled;

    switch (event.type) {
        case HapticType_t::None:
            break;
        case HapticType_t::Tick:
            if (now - _last_tick_haptic < _tick_min_interval_ms) {
                break;
            }
            _last_tick_haptic = now;
            GetHAL().vibrate(
                static_cast<uint16_t>(_tick_min_ms + std::lround((_tick_max_ms - _tick_min_ms) * event.intensity)),
                100);
            break;
        case HapticType_t::Detent:
            GetHAL().vibrate(_detent_click_ms, 100);
            if (sfx) {
                audio::play_tone_from_midi(100, 0.012f, 0.3f);
            }
            break;
        case HapticType_t::CenterNotch:
            GetHAL().vibrate(36, 90);
            if (sfx) {
                audio::play_tone_from_midi(88, 0.02f, 0.3f);
            }
            break;
        case HapticType_t::EndStop:
            GetHAL().vibrate(70, 100);
            if (sfx) {
                audio::play_tone_from_midi(64, 0.04f, 0.4f);
            }
            _view->flashEndStop();
            break;
    }
}

void AppKnob::syncView()
{
    _view->setDialAngle(_knob.getDialAngle());
    _view->setValue(_knob.getValue());
}
