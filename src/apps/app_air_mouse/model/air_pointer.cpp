/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "air_pointer.h"
#include <algorithm>
#include <cmath>

using namespace model;

namespace {

using Vec3 = AirPointer::Vec3;

// Direction of each motion, flip one if it moves the wrong way on your unit
constexpr float _yaw_sign   = -1.0f;  // turning right moves the pointer right
constexpr float _pitch_sign = -1.0f;  // tilting the top away moves the pointer up
constexpr float _twist_sign = 1.0f;   // twisting clockwise scrolls down

constexpr float _gravity_tau = 0.15f;  // s

// Pointer: slow motion is precise, fast flicks cover the screen
constexpr float _deadband_rate   = 2.0f;  // deg/s, soft fade-in above this to swallow hand tremor
constexpr float _slow_gain       = 6.0f;  // counts per degree
constexpr float _fast_gain       = 22.0f;
constexpr float _slow_rate       = 10.0f;  // deg/s
constexpr float _fast_rate       = 150.0f;
constexpr float _speed_scale[]   = {0.6f, 1.0f, 1.6f};
constexpr float _velocity_smooth = 0.08f;  // s

// Twist scroll: only when twisting clearly dominates, so pointing doesn't leak into the wheel
constexpr float _scroll_notch_deg  = 15.0f;
constexpr float _scroll_enter_rate = 25.0f;  // deg/s
constexpr float _scroll_exit_rate  = 8.0f;   // deg/s
constexpr float _scroll_dominance  = 1.5f;   // twist rate must beat pointing rate by this much
constexpr float _scroll_hold       = 0.15f;  // s of quiet before pointing takes over again

float dot(const Vec3& a, const Vec3& b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 cross(const Vec3& a, const Vec3& b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

Vec3 scale(const Vec3& v, float s)
{
    return {v.x * s, v.y * s, v.z * s};
}

Vec3 sub(const Vec3& a, const Vec3& b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

bool normalize(Vec3& v)
{
    const float length = std::sqrt(dot(v, v));
    if (length < 1e-4f) {
        return false;
    }
    v = scale(v, 1.0f / length);
    return true;
}

float smoothstep(float edge0, float edge1, float x)
{
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

}  // namespace

void AirPointer::setSettings(const Settings& settings)
{
    _settings       = settings;
    _settings.speed = std::clamp(_settings.speed, 0, 2);
    if (!_settings.scroll) {
        _scrolling    = false;
        _scroll_accum = 0.0f;
    }
}

void AirPointer::reset()
{
    _gravity_valid = false;
    _scrolling     = false;
    _scroll_quiet  = 0.0f;
    _scroll_accum  = 0.0f;
    _twist_angle   = 0.0f;
    _velocity_x    = 0.0f;
    _velocity_y    = 0.0f;
}

AirPointer::Output AirPointer::update(const Vec3& gyro, const Vec3& accel, float dt)
{
    Output output;
    if (dt <= 0.0f) {
        return output;
    }

    // Low-passed accelerometer = which way is up, in the device frame
    if (!_gravity_valid) {
        _gravity       = accel;
        _gravity_valid = true;
    } else {
        const float k = std::min(1.0f, dt / _gravity_tau);
        _gravity      = {_gravity.x + (accel.x - _gravity.x) * k, _gravity.y + (accel.y - _gravity.y) * k,
                         _gravity.z + (accel.z - _gravity.z) * k};
    }

    Vec3 up = _gravity;
    if (!normalize(up)) {
        up = {0.0f, 0.0f, 1.0f};
    }
    // The screen's horizontal axis laid flat, falling back to its vertical axis if the device is on its side
    Vec3 horizontal = sub({1.0f, 0.0f, 0.0f}, scale(up, up.x));
    if (!normalize(horizontal)) {
        horizontal = sub({0.0f, 1.0f, 0.0f}, scale(up, up.y));
        normalize(horizontal);
    }
    const Vec3 forward = cross(up, horizontal);

    const float yaw   = dot(gyro, up);
    const float pitch = dot(gyro, horizontal);
    const float twist = dot(gyro, forward) * _twist_sign;
    const float point = std::hypot(yaw, pitch);

    // Twist scroll with hysteresis
    if (_settings.scroll) {
        if (!_scrolling) {
            if (std::fabs(twist) > _scroll_enter_rate && std::fabs(twist) > point * _scroll_dominance) {
                _scrolling    = true;
                _scroll_quiet = 0.0f;
            }
        } else {
            _scroll_quiet = std::fabs(twist) > _scroll_exit_rate ? 0.0f : _scroll_quiet + dt;
            if (_scroll_quiet > _scroll_hold) {
                _scrolling    = false;
                _scroll_accum = 0.0f;
            }
        }
    }

    if (_scrolling) {
        const float delta = twist * dt;
        _twist_angle += delta;
        _scroll_accum += delta;
        int notches = 0;
        while (_scroll_accum >= _scroll_notch_deg) {
            _scroll_accum -= _scroll_notch_deg;
            notches++;
        }
        while (_scroll_accum <= -_scroll_notch_deg) {
            _scroll_accum += _scroll_notch_deg;
            notches--;
        }
        // Clockwise scrolls down, which is a negative wheel value
        output.wheel = _settings.invertScroll ? notches : -notches;
    } else if (_settings.pointer) {
        const float fade = smoothstep(_deadband_rate, 2.0f * _deadband_rate, point);
        const float t    = smoothstep(_slow_rate, _fast_rate, point);
        const float gain = (_slow_gain + (_fast_gain - _slow_gain) * t) * _speed_scale[_settings.speed] * fade;
        output.dx        = _yaw_sign * yaw * gain * dt;
        output.dy        = _pitch_sign * pitch * gain * dt;
    }

    const float k = std::min(1.0f, dt / _velocity_smooth);
    _velocity_x += (output.dx / dt - _velocity_x) * k;
    _velocity_y += (output.dy / dt - _velocity_y) * k;
    return output;
}
