/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_air_mouse.h"
#include "view/view_style.h"
#include <apps/common/audio/audio.h>
#include <mooncake_log.h>
#include <assets/assets.h>
#include <algorithm>
#include <cmath>

using namespace mooncake;

namespace {

using Item      = view::SettingsPage::Item;
using KeyStyle  = view::KeyboardPage::KeyStyle;
using Release   = model::TouchGesture::Release;
using HidState  = Hal::HidState;
using Transport = Hal::HidTransport;

constexpr uint8_t _mouse_left  = 0x01;
constexpr uint8_t _mouse_right = 0x02;

constexpr float _max_dt              = 0.1f;  // s, ignore longer gaps instead of integrating a jump
constexpr uint32_t _report_interval  = 8;     // ms between movement reports, button changes go out at once

// Touchpad: slow drags are precise, quick swipes travel further
constexpr float _pad_slow_gain  = 1.2f;  // counts per touch px
constexpr float _pad_fast_gain  = 3.5f;
constexpr float _pad_slow_speed = 100.0f;  // touch px/s
constexpr float _pad_fast_speed = 1000.0f;
constexpr float _pad_scroll_px  = 22.0f;   // drag per wheel notch
constexpr float _pad_speed_scale[] = {0.7f, 1.0f, 1.4f};

// The motor is an ERM, only full strength pulses of 20 ms or more are felt rather than just heard
constexpr uint16_t _tick_ms               = 20;
constexpr uint32_t _tick_min_interval_ms  = 45;
constexpr uint16_t _press_ms              = 22;
constexpr uint16_t _page_ms               = 30;
constexpr uint16_t _connect_ms            = 60;
constexpr uint16_t _fail_ms               = 150;

int8_t take_counts(float& accumulated)
{
    const float whole   = std::clamp(std::trunc(accumulated), -127.0f, 127.0f);
    accumulated        -= whole;
    return static_cast<int8_t>(whole);
}

int8_t take_notches(int& accumulated)
{
    const int whole = std::clamp(accumulated, -127, 127);
    accumulated -= whole;
    return static_cast<int8_t>(whole);
}

void press_feedback()
{
    GetHAL().vibrate(_press_ms, 100);
    if (GetHAL().getButtonConfig().sfxEnabled) {
        audio::play_tone_from_midi(100, 0.012f, 0.25f);
    }
}

}  // namespace

AppAirMouse::AppAirMouse()
{
    setAppInfo().name = "Air Mouse";
    setAppInfo().icon = (void*)&icon_air_mouse;
}

void AppAirMouse::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");
}

void AppAirMouse::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    _key_manager      = std::make_unique<input::KeyManager>();
    _config           = GetHAL().getHidConfig(true);
    _last_update_tick = GetHAL().millis();
    _touch_target     = TouchTarget::None;
    _pad_scroll_mode  = false;
    _status_dirty     = true;
    _restart_hid      = false;
    _move_x = _move_y = 0.0f;
    _wheel = _pan     = 0;
    _click_pending    = false;
    _buttons_sent     = 0;
    _pending_strokes.clear();
    _pointer.reset();
    _calibrator.cancel();

    // Bringing up Bluetooth the first time takes a moment, keep it out of the LVGL lock
    GetHAL().hidStart(_config.transport);

    LvglLockGuard lock;

    _view = std::make_unique<view::AirMouseView>();
    _view->init(lv_screen_active());
    _view->touchpad().setScrollMode(_pad_scroll_mode);
    applyConfig(false);
    refreshKeyboard();
    refreshStatus();
}

void AppAirMouse::onRunning()
{
    const auto key_event = _key_manager->update();
    if (key_event == input::KeyEvent::GoHome) {
        close();
        return;
    }

    if (_restart_hid) {
        _restart_hid = false;
        GetHAL().hidStart(_config.transport);
        _status_dirty = true;
    }

    const uint32_t now     = GetHAL().millis();
    const uint32_t elapsed = now - _last_update_tick;
    float dt               = 0.0f;
    imu::GyroFilter::Rates raw;
    imu::GyroFilter::Rates rates;
    model::AirPointer::Vec3 accel;
    // Same-millisecond loops are skipped so the elapsed time accumulates instead of being lost
    if (elapsed > 0) {
        _last_update_tick = now;
        dt                = std::min(static_cast<float>(elapsed) / 1000.0f, _max_dt);
        GetHAL().updateImuData();
        const auto& imu = GetHAL().getImuData();
        raw             = {imu.gyroX, imu.gyroY, imu.gyroZ};
        rates           = _gyro_filter.update(raw, dt);
        accel           = {imu.accelX, imu.accelY, imu.accelZ};
    }
    const auto touch = GetHAL().getTouchPoint();

    {
        LvglLockGuard lock;

        if (!_view) {
            return;
        }

        const auto& frame = _gesture.update(touch.num, touch.x, touch.y, now);
        handleTouch(frame, dt, now);

        updateZeroing(raw, dt);
        _view->touchpad().update();

        // The pointer holds still while zeroing, the device is supposed to be still anyway
        if (_view->getPage() == view::Page::Touchpad && dt > 0.0f && !_calibrator.isRunning()) {
            const auto motion = _pointer.update({rates.x, rates.y, rates.z}, accel, dt);
            _move_x += motion.dx;
            _move_y += motion.dy;
            if (motion.wheel != 0) {
                _wheel += motion.wheel;
                scrollTick();
            }
            _view->touchpad().setAirPointer(_config.airPointer, _pointer.getVelocityX(), _pointer.getVelocityY());
            _view->touchpad().setTwist(_pointer.isScrolling(), _pointer.getTwistAngle());
        }

        if (_keyboard.update(now)) {
            _view->keyboard().setPreview(_keyboard.getText(), _keyboard.hasPendingLetter());
        }

        const auto state = GetHAL().getHidState();
        if (state != _hid_state || _status_dirty) {
            if (state == HidState::Connected && _hid_state != HidState::Connected) {
                GetHAL().vibrate(_connect_ms, 100);
            }
            _hid_state = state;
            refreshStatus();
        }
    }

    // USB reports wait for the host to pick them up, so they go out after the LVGL lock is released
    sendPendingStrokes();
    sendMouseReport(now);
}

void AppAirMouse::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

    _key_manager.reset();
    GetHAL().hidStop();
    GetHAL().stopVibrate();

    LvglLockGuard lock;

    _view.reset();
}

void AppAirMouse::handleTouch(const model::TouchGesture::Frame& frame, float dt, uint32_t now)
{
    if (frame.pressed) {
        beginTouch(frame);
    }

    if (frame.longPress && (_touch_target == TouchTarget::Pad || _touch_target == TouchTarget::PadEdge)) {
        startZeroing();
    }

    if (frame.down && frame.dragging) {
        switch (_touch_target) {
            case TouchTarget::Pad:
                padDrag(frame, dt);
                break;
            case TouchTarget::Key:
            case TouchTarget::SettingsItem:
            case TouchTarget::ModeChip:
                // Moving off a key cancels it, the touch may still turn into a swipe
                _view->keyboard().setPressedKey(-1);
                _view->settings().setPressed(Item::None);
                _view->touchpad().setModeChipPressed(false);
                break;
            default:
                break;
        }
    }
    _view->touchpad().setTouch(frame.down && _touch_target == TouchTarget::Pad, frame.x, frame.y);

    if (frame.release != Release::None) {
        endTouch(frame, now);
    }

    if (!frame.down) {
        if (_touch_target == TouchTarget::Key) {
            _view->keyboard().setPressedKey(-1);
        } else if (_touch_target == TouchTarget::SettingsItem) {
            _view->settings().setPressed(Item::None);
        } else if (_touch_target == TouchTarget::ModeChip) {
            _view->touchpad().setModeChipPressed(false);
        }
        _touch_target = TouchTarget::None;
    }
}

void AppAirMouse::beginTouch(const model::TouchGesture::Frame& frame)
{
    _touch_target = TouchTarget::None;
    _pad_scroll_x = 0.0f;
    _pad_scroll_y = 0.0f;
    if (_view->isAnimating()) {
        return;
    }

    switch (_view->getPage()) {
        case view::Page::Settings:
            _touch_item   = _view->settings().hitTest(frame.x, frame.y);
            _touch_target = TouchTarget::SettingsItem;
            _view->settings().setPressed(_touch_item);
            break;
        case view::Page::Keyboard:
            _touch_key    = _view->keyboard().hitTest(frame.x, frame.y);
            _touch_target = TouchTarget::Key;
            _view->keyboard().setPressedKey(_touch_key);
            break;
        case view::Page::Touchpad:
            if (_view->touchpad().hitModeChip(frame.x, frame.y)) {
                _touch_target = TouchTarget::ModeChip;
                _view->touchpad().setModeChipPressed(true);
            } else {
                // Touches from the edge are reserved for page swipes so they never move the pointer
                _touch_target = frame.startedAtEdge ? TouchTarget::PadEdge : TouchTarget::Pad;
            }
            break;
    }
}

void AppAirMouse::endTouch(const model::TouchGesture::Frame& frame, uint32_t now)
{
    if (frame.release == Release::Tap) {
        switch (_touch_target) {
            case TouchTarget::Pad:
            case TouchTarget::PadEdge:
                _click_pending = true;
                GetHAL().vibrate(_tick_ms, 100);
                break;
            case TouchTarget::ModeChip:
                _pad_scroll_mode = !_pad_scroll_mode;
                _view->touchpad().setScrollMode(_pad_scroll_mode);
                press_feedback();
                break;
            case TouchTarget::Key:
                pressKey(_touch_key, now);
                break;
            case TouchTarget::SettingsItem:
                activateSetting(_touch_item);
                break;
            case TouchTarget::None:
                break;
        }
        return;
    }

    // On the touchpad only edge swipes change page, everywhere else any swipe does
    const bool can_swipe = _touch_target != TouchTarget::None &&
                           (_view->getPage() != view::Page::Touchpad || _touch_target == TouchTarget::PadEdge);
    if (can_swipe && frame.release == Release::SwipeLeft) {
        changePage(1);
    } else if (can_swipe && frame.release == Release::SwipeRight) {
        changePage(-1);
    }
}

void AppAirMouse::padDrag(const model::TouchGesture::Frame& frame, float dt)
{
    const bool scroll = _pad_scroll_mode || frame.fingers >= 2;
    if (!scroll) {
        const float speed = dt > 0.0f ? std::hypot(frame.dx, frame.dy) / dt : 0.0f;
        const float t     = std::clamp((speed - _pad_slow_speed) / (_pad_fast_speed - _pad_slow_speed), 0.0f, 1.0f);
        const float gain  = (_pad_slow_gain + (_pad_fast_gain - _pad_slow_gain) * t) *
                           _pad_speed_scale[std::min<uint8_t>(_config.pointerSpeed, 2)];
        _move_x += frame.dx * gain;
        _move_y += frame.dy * gain;
        return;
    }

    // Lock to whichever axis moved more this frame so vertical scrolling doesn't drift sideways
    if (std::fabs(frame.dy) >= std::fabs(frame.dx)) {
        _pad_scroll_y += frame.dy;
    } else {
        _pad_scroll_x += frame.dx;
    }

    // Content follows the finger: dragging up scrolls down (negative wheel), dragging left scrolls right
    const int sign = _config.invertScroll ? -1 : 1;
    const int rows = static_cast<int>(_pad_scroll_y / _pad_scroll_px);
    const int cols = static_cast<int>(_pad_scroll_x / _pad_scroll_px);
    if (rows != 0) {
        _pad_scroll_y -= rows * _pad_scroll_px;
        _wheel += sign * rows;
        scrollTick();
    }
    if (cols != 0) {
        _pad_scroll_x -= cols * _pad_scroll_px;
        _pan -= sign * cols;
        scrollTick();
    }
}

void AppAirMouse::startZeroing()
{
    if (_calibrator.isRunning()) {
        return;
    }
    _calibrator.start();
    _view->touchpad().setZeroing(0.0f);
    GetHAL().vibrate(_page_ms, 100);
}

void AppAirMouse::updateZeroing(const imu::GyroFilter::Rates& raw, float dt)
{
    if (!_calibrator.isRunning()) {
        return;
    }

    switch (_calibrator.update(raw, dt)) {
        case imu::GyroCalibrator::State::Done:
            _gyro_filter.setBias(_calibrator.getBias());
            _pointer.reset();
            _view->touchpad().setZeroing(-1.0f);
            _view->touchpad().showZeroResult(true);
            GetHAL().vibrate(_connect_ms, 100);
            mclog::tagInfo(getAppInfo().name, "gyro zeroed: {:.2f} {:.2f} {:.2f}", _calibrator.getBias().x,
                           _calibrator.getBias().y, _calibrator.getBias().z);
            break;
        case imu::GyroCalibrator::State::Failed:
            _view->touchpad().setZeroing(-1.0f);
            _view->touchpad().showZeroResult(false);
            GetHAL().vibrate(_fail_ms, 100);
            break;
        default:
            _view->touchpad().setZeroing(_calibrator.getProgress());
            break;
    }
}

void AppAirMouse::changePage(int direction)
{
    const int page = static_cast<int>(_view->getPage()) + direction;
    if (page < 0 || page >= view::PageCount) {
        return;
    }
    if (_view->getPage() == view::Page::Touchpad) {
        _pointer.reset();
        _calibrator.cancel();
        _view->touchpad().setZeroing(-1.0f);
        _view->touchpad().setTwist(false, 0.0f);
    }
    _view->showPage(static_cast<view::Page>(page));
    GetHAL().vibrate(_page_ms, 100);
}

void AppAirMouse::activateSetting(Item item)
{
    switch (item) {
        case Item::Usb:
        case Item::Bluetooth: {
            const auto transport = item == Item::Usb ? Transport::Usb : Transport::Bluetooth;
            if (transport != _config.transport) {
                _config.transport = transport;
                _restart_hid      = true;
            }
            break;
        }
        case Item::AirPointer:
            _config.airPointer = !_config.airPointer;
            break;
        case Item::TwistScroll:
            _config.twistScroll = !_config.twistScroll;
            break;
        case Item::Speed:
            _config.pointerSpeed = (_config.pointerSpeed + 1) % 3;
            break;
        case Item::InvertScroll:
            _config.invertScroll = !_config.invertScroll;
            break;
        case Item::None:
            return;
    }
    press_feedback();
    applyConfig(true);
    _status_dirty = true;
}

void AppAirMouse::pressKey(int index, uint32_t now)
{
    if (index < 0) {
        return;
    }
    const auto strokes = _keyboard.press(index, now);
    _pending_strokes.insert(_pending_strokes.end(), strokes.begin(), strokes.end());
    press_feedback();
    refreshKeyboard();
}

void AppAirMouse::applyConfig(bool save)
{
    GetHAL().setHidConfig(_config, save);
    _config = GetHAL().getHidConfig();

    model::AirPointer::Settings settings;
    settings.pointer      = _config.airPointer;
    settings.scroll       = _config.twistScroll;
    settings.invertScroll = _config.invertScroll;
    settings.speed        = _config.pointerSpeed;
    _pointer.setSettings(settings);

    _view->settings().setTransport(_config.transport == Transport::Usb);
    _view->settings().setValues(_config.airPointer, _config.twistScroll, _config.pointerSpeed, _config.invertScroll);
}

void AppAirMouse::refreshKeyboard()
{
    using Action = model::Keyboard::Action;
    using Shift  = model::Keyboard::Shift;

    for (int i = 0; i < model::Keyboard::KeyCount; ++i) {
        const auto& key = _keyboard.getKey(i);
        KeyStyle style  = KeyStyle::Normal;
        switch (key.action) {
            case Action::Backspace:
            case Action::Enter:
            case Action::Layer:
                style = KeyStyle::Special;
                break;
            case Action::Shift:
                style = _keyboard.getShift() == Shift::Off
                            ? KeyStyle::Special
                            : (_keyboard.getShift() == Shift::Once ? KeyStyle::Active : KeyStyle::Locked);
                break;
            default:
                break;
        }
        _view->keyboard().setKey(i, key.action, key.usage, _keyboard.getKeyLabel(i), style);
    }
    _view->keyboard().setPreview(_keyboard.getText(), _keyboard.hasPendingLetter());
}

void AppAirMouse::refreshStatus()
{
    _status_dirty  = false;
    const bool usb = _config.transport == Transport::Usb;
    const std::string icon = usb ? LV_SYMBOL_USB " " : LV_SYMBOL_BLUETOOTH " ";
    const char* serial_note = "\nSerial console is off until restart";

    // A transport switch is applied on the next loop, show it as waiting until then
    const auto state = _restart_hid ? HidState::Waiting : _hid_state;
    switch (state) {
        case HidState::Connected:
            _view->setStatus(icon + "Connected", view::style::Connected);
            _view->settings().setDetail(usb ? std::string("Connected over USB") + serial_note
                                            : std::string("Connected over Bluetooth"));
            break;
        case HidState::Waiting:
            _view->setStatus(icon + (usb ? "Waiting for host" : "Pairing"), view::style::Waiting);
            _view->settings().setDetail(usb ? std::string("Plug into a computer") + serial_note
                                            : std::string("Pair \"StopWatch Air Mouse\"\nin your computer's Bluetooth settings"));
            break;
        case HidState::Off:
            _view->setStatus(icon + "Off", view::style::Idle);
            _view->settings().setDetail(usb ? "USB could not start" : "Bluetooth could not start");
            break;
    }
}

void AppAirMouse::scrollTick()
{
    const uint32_t now = GetHAL().millis();
    if (now - _last_tick_haptic >= _tick_min_interval_ms) {
        _last_tick_haptic = now;
        GetHAL().vibrate(_tick_ms, 100);
    }
}

void AppAirMouse::sendMouseReport(uint32_t now)
{
    uint8_t buttons = 0;
    if (GetHAL().btnA.isPressed()) {
        buttons |= _mouse_left;
    }
    if (GetHAL().btnB.isPressed()) {
        buttons |= _mouse_right;
    }

    const bool buttons_changed = buttons != _buttons_sent;
    const bool has_motion = std::fabs(_move_x) >= 1.0f || std::fabs(_move_y) >= 1.0f || _wheel != 0 || _pan != 0;
    if (!buttons_changed && !has_motion && !_click_pending) {
        return;
    }
    if (!buttons_changed && !_click_pending && now - _last_report < _report_interval) {
        return;  // keep accumulating, the next report carries it all
    }

    _last_report  = now;
    _buttons_sent = buttons;
    const int8_t x     = take_counts(_move_x);
    const int8_t y     = take_counts(_move_y);
    const int8_t wheel = take_notches(_wheel);
    const int8_t pan   = take_notches(_pan);

    if (_hid_state != HidState::Connected) {
        // Nobody is listening, don't let movement pile up for when a host shows up
        _move_x = _move_y = 0.0f;
        _wheel = _pan     = 0;
        _click_pending    = false;
        return;
    }

    if (_click_pending) {
        _click_pending = false;
        GetHAL().hidSendMouse(buttons | _mouse_left, x, y, wheel, pan);
        GetHAL().hidSendMouse(buttons, 0, 0, 0, 0);
        return;
    }
    GetHAL().hidSendMouse(buttons, x, y, wheel, pan);
}

void AppAirMouse::sendPendingStrokes()
{
    if (_pending_strokes.empty()) {
        return;
    }
    if (_hid_state == HidState::Connected) {
        for (const auto& stroke : _pending_strokes) {
            GetHAL().hidSendKeyboard(stroke.modifiers, {stroke.usage, 0, 0, 0, 0, 0});
            GetHAL().hidSendKeyboard(0, {});
        }
    }
    _pending_strokes.clear();
}
