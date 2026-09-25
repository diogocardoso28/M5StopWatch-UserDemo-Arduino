/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

namespace imu {

/**
 * @brief Cleans up raw gyro rates for motion input
 *
 * Slowly learns each axis' zero-rate offset whenever that axis is close to still, otherwise the
 * offset shows up as things creeping on their own. Optionally zeroes rates inside a deadband.
 */
class GyroFilter {
public:
    struct Rates {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    explicit GyroFilter(float deadband = 0.0f) : _deadband(deadband)
    {
    }

    // Rates in deg/s, dt in seconds. Returns the bias-corrected rates.
    Rates update(const Rates& raw, float dt);
    void reset()
    {
        _bias = {};
    }
    void setBias(const Rates& bias)
    {
        _bias = bias;
    }

private:
    float _deadband;
    Rates _bias;

    float filterAxis(float raw, float& bias, float dt) const;
};

}  // namespace imu
