/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "gyro_calibrator.h"
#include <algorithm>

using namespace imu;

namespace {

constexpr float _settle_time  = 0.15f;  // s, lets the jolt of pressing the screen die down
constexpr float _measure_time = 0.8f;   // s
constexpr float _max_spread   = 30.0f;  // deg/s between the lowest and highest sample on any axis
constexpr int _min_samples    = 10;

}  // namespace

void GyroCalibrator::start()
{
    _state   = State::Settling;
    _elapsed = 0.0f;
    _samples = 0;
    _sum     = {};
}

GyroCalibrator::State GyroCalibrator::update(const GyroFilter::Rates& raw, float dt)
{
    if (!isRunning() || dt <= 0.0f) {
        return _state;
    }
    _elapsed += dt;

    if (_state == State::Settling) {
        if (_elapsed < _settle_time) {
            return _state;
        }
        _state = State::Measuring;
        _min   = raw;
        _max   = raw;
    }

    _sum.x += raw.x;
    _sum.y += raw.y;
    _sum.z += raw.z;
    _min = {std::min(_min.x, raw.x), std::min(_min.y, raw.y), std::min(_min.z, raw.z)};
    _max = {std::max(_max.x, raw.x), std::max(_max.y, raw.y), std::max(_max.z, raw.z)};
    _samples++;

    const float spread = std::max({_max.x - _min.x, _max.y - _min.y, _max.z - _min.z});
    if (spread > _max_spread) {
        _state = State::Failed;
        return _state;
    }

    if (_elapsed >= _settle_time + _measure_time && _samples >= _min_samples) {
        const float n = static_cast<float>(_samples);
        _bias         = {_sum.x / n, _sum.y / n, _sum.z / n};
        _state        = State::Done;
    }
    return _state;
}

float GyroCalibrator::getProgress() const
{
    switch (_state) {
        case State::Idle:
        case State::Failed:
            return 0.0f;
        case State::Done:
            return 1.0f;
        default:
            return std::clamp(_elapsed / (_settle_time + _measure_time), 0.0f, 1.0f);
    }
}
