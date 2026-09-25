/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "gyro_filter.h"
#include <algorithm>
#include <cmath>

using namespace imu;

namespace {

constexpr float _bias_track_window = 2.5f;  // deg/s, only learn the zero-rate offset this close to it
constexpr float _bias_track_tau    = 3.0f;  // s

}  // namespace

GyroFilter::Rates GyroFilter::update(const Rates& raw, float dt)
{
    return {
        filterAxis(raw.x, _bias.x, dt),
        filterAxis(raw.y, _bias.y, dt),
        filterAxis(raw.z, _bias.z, dt),
    };
}

float GyroFilter::filterAxis(float raw, float& bias, float dt) const
{
    if (std::fabs(raw - bias) < _bias_track_window) {
        bias += (raw - bias) * std::min(1.0f, dt / _bias_track_tau);
    }

    const float corrected = raw - bias;
    return std::fabs(corrected) < _deadband ? 0.0f : corrected;
}
