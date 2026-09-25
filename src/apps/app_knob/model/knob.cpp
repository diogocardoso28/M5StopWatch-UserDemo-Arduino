/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "knob.h"
#include <algorithm>
#include <cmath>

using namespace model;

namespace {

// Regular: 0..100 over 300 deg of rotation
constexpr float _regular_range_deg     = 300.0f;
constexpr float _regular_max_value     = 100.0f;
constexpr float _regular_deg_per_unit  = _regular_range_deg / _regular_max_value;
constexpr float _regular_tick_deg      = 5 * _regular_deg_per_unit;
constexpr float _regular_tick_strength = 0.35f;

// Detent: 0..10 steps drawn 30 deg apart, but only 20 deg of physical rotation breaks out of a step
constexpr int _detent_steps           = 10;
constexpr float _detent_spacing_deg   = 30.0f;
constexpr float _detent_break_deg     = 20.0f;
constexpr float _detent_lean_deg      = 7.0f;   // how far the dial gives before snapping
constexpr float _detent_stop_give_deg = 4.0f;   // physical give when pushing into an end stop
constexpr float _detent_settle_rate   = 8.0f;   // deg/s, below this the step pulls back to center
constexpr float _detent_settle_tau    = 0.35f;  // s
constexpr float _detent_spring_omega  = 2.0f * 3.14159265f * 6.0f;
constexpr float _detent_spring_zeta   = 0.45f;  // under-damped, overshoots a little on snap
constexpr float _detent_spring_max_dt = 0.004f;

// Half turn: -100..100 over -180..180 deg
constexpr float _half_range_deg         = 180.0f;
constexpr float _half_max_value         = 100.0f;
constexpr float _half_deg_per_unit      = _half_range_deg / _half_max_value;
constexpr float _half_tick_deg          = 10 * _half_deg_per_unit;
constexpr float _half_notch_enter_deg   = 2.5f;
constexpr float _half_notch_exit_deg    = 6.0f;
constexpr float _half_notch_give_ratio  = 0.35f;  // dial gives a little while held in the notch
constexpr float _half_tick_min_strength = 0.25f;

constexpr float _stop_release_deg = 4.0f;

int tick_band(float angle, float tickDeg)
{
    return static_cast<int>(std::floor(angle / tickDeg));
}

}  // namespace

void Knob::update(float rotationDeg, float dt)
{
    switch (_mode) {
        case Mode_t::Regular:
            updateRegular(rotationDeg);
            break;
        case Mode_t::Detent:
            updateDetent(rotationDeg, dt);
            break;
        case Mode_t::HalfTurn:
            updateHalfTurn(rotationDeg);
            break;
    }
}

void Knob::setMode(Mode_t mode)
{
    _mode           = mode;
    _pending_haptic = {};
}

void Knob::nextMode(int direction)
{
    int index = (static_cast<int>(_mode) + direction) % ModeCount;
    if (index < 0) {
        index += ModeCount;
    }
    setMode(static_cast<Mode_t>(index));
}

void Knob::reset()
{
    switch (_mode) {
        case Mode_t::Regular:
            _regular_angle        = 0.0f;
            _regular_stop_latched = false;
            break;
        case Mode_t::Detent:
            // Leave the display where it is so it springs back to zero
            _detent_index        = 0;
            _detent_offset       = 0.0f;
            _detent_stop_latched = false;
            break;
        case Mode_t::HalfTurn:
            _half_angle        = 0.0f;
            _half_stop_latched = false;
            _half_in_notch     = true;
            break;
    }
    _pending_haptic = {};
}

int Knob::getValue() const
{
    switch (_mode) {
        case Mode_t::Regular:
            return static_cast<int>(std::lround(_regular_angle / _regular_deg_per_unit));
        case Mode_t::Detent:
            return _detent_index;
        case Mode_t::HalfTurn:
            return _half_in_notch ? 0 : static_cast<int>(std::lround(_half_angle / _half_deg_per_unit));
    }
    return 0;
}

float Knob::getDialAngle() const
{
    switch (_mode) {
        case Mode_t::Regular:
            return _regular_angle;
        case Mode_t::Detent:
            return _detent_display;
        case Mode_t::HalfTurn:
            return _half_in_notch ? _half_angle * _half_notch_give_ratio : _half_angle;
    }
    return 0.0f;
}

Knob::HapticEvent_t Knob::popHapticEvent()
{
    HapticEvent_t event = _pending_haptic;
    _pending_haptic     = {};
    return event;
}

void Knob::updateRegular(float rotationDeg)
{
    const float previous = _regular_angle;
    const float wanted   = previous + rotationDeg;
    _regular_angle       = std::clamp(wanted, 0.0f, _regular_range_deg);

    // Clamping the angle itself (instead of accumulating overshoot) means reversing responds immediately
    if (wanted != _regular_angle) {
        if (!_regular_stop_latched) {
            _regular_stop_latched = true;
            emitHaptic(HapticType_t::EndStop);
        }
    } else if (_regular_angle > _stop_release_deg && _regular_angle < _regular_range_deg - _stop_release_deg) {
        _regular_stop_latched = false;
    }

    if (tick_band(previous, _regular_tick_deg) != tick_band(_regular_angle, _regular_tick_deg)) {
        emitHaptic(HapticType_t::Tick, _regular_tick_strength);
    }
}

void Knob::updateDetent(float rotationDeg, float dt)
{
    _detent_offset += rotationDeg;

    // Break out into the neighbouring step; the remainder carries over so fast spins don't lose steps
    while (_detent_offset >= _detent_break_deg && _detent_index < _detent_steps) {
        _detent_index++;
        _detent_offset -= _detent_break_deg;
        emitHaptic(HapticType_t::Detent);
    }
    while (_detent_offset <= -_detent_break_deg && _detent_index > 0) {
        _detent_index--;
        _detent_offset += _detent_break_deg;
        emitHaptic(HapticType_t::Detent);
    }

    // Hard stops at both ends only give a little, so backing off steps down again right away
    const bool pushing_max = _detent_index == _detent_steps && _detent_offset > _detent_stop_give_deg;
    const bool pushing_min = _detent_index == 0 && _detent_offset < -_detent_stop_give_deg;
    if (pushing_max || pushing_min) {
        _detent_offset = std::clamp(_detent_offset, -_detent_stop_give_deg, _detent_stop_give_deg);
        if (!_detent_stop_latched) {
            _detent_stop_latched = true;
            emitHaptic(HapticType_t::EndStop);
        }
    } else if (std::fabs(_detent_offset) < _detent_stop_give_deg * 0.5f) {
        _detent_stop_latched = false;
    }

    // When the knob is held still the step pulls it back to center, like a real detent spring.
    // This also stops gyro drift from slowly accumulating into an unwanted step.
    if (dt > 0.0f && std::fabs(rotationDeg) / dt < _detent_settle_rate) {
        _detent_offset *= std::exp(-dt / _detent_settle_tau);
    }

    // Spring-follow the target so the snap into a step has a little overshoot
    const float target = detentTargetAngle();
    float remaining    = std::min(dt, 0.05f);
    while (remaining > 0.0f) {
        const float h = std::min(remaining, _detent_spring_max_dt);
        const float accel =
            _detent_spring_omega * _detent_spring_omega * (target - _detent_display) -
            2.0f * _detent_spring_zeta * _detent_spring_omega * _detent_display_vel;
        _detent_display_vel += accel * h;
        _detent_display += _detent_display_vel * h;
        remaining -= h;
    }
}

float Knob::detentTargetAngle() const
{
    // Stiffening spring: gives easily at first, then resists harder the closer it gets to breaking out
    const float u    = std::min(std::fabs(_detent_offset) / _detent_break_deg, 1.0f);
    const float lean = _detent_lean_deg * (1.0f - (1.0f - u) * (1.0f - u));
    return _detent_index * _detent_spacing_deg + std::copysign(lean, _detent_offset);
}

void Knob::updateHalfTurn(float rotationDeg)
{
    const float previous = _half_angle;
    const float wanted   = previous + rotationDeg;
    _half_angle          = std::clamp(wanted, -_half_range_deg, _half_range_deg);

    if (wanted != _half_angle) {
        if (!_half_stop_latched) {
            _half_stop_latched = true;
            emitHaptic(HapticType_t::EndStop);
        }
    } else if (std::fabs(_half_angle) < _half_range_deg - _stop_release_deg) {
        _half_stop_latched = false;
    }

    // Center notch, with hysteresis so jitter around zero doesn't chatter
    const float magnitude = std::fabs(_half_angle);
    if (!_half_in_notch) {
        const bool crossed_zero = (previous > 0.0f) != (_half_angle > 0.0f);
        if (magnitude < _half_notch_enter_deg || crossed_zero) {
            _half_in_notch = magnitude < _half_notch_exit_deg;
            emitHaptic(HapticType_t::CenterNotch);
        }
    } else if (magnitude > _half_notch_exit_deg) {
        _half_in_notch = false;
    }

    // Texture ticks that get stronger toward the ends, so resistance seems to build up
    const int previous_band = tick_band(std::fabs(previous), _half_tick_deg);
    const int band          = tick_band(magnitude, _half_tick_deg);
    if (band != previous_band) {
        const float strength =
            _half_tick_min_strength + (1.0f - _half_tick_min_strength) * (magnitude / _half_range_deg);
        emitHaptic(HapticType_t::Tick, strength);
    }
}

void Knob::emitHaptic(HapticType_t type, float intensity)
{
    // Keep only the most important event per frame; the enum is ordered by priority
    if (type > _pending_haptic.type ||
        (type == _pending_haptic.type && intensity > _pending_haptic.intensity)) {
        _pending_haptic.type      = type;
        _pending_haptic.intensity = std::clamp(intensity, 0.0f, 1.0f);
    }
}
