/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "gyro_filter.h"

namespace imu {

/**
 * @brief Measures the gyro's zero-rate offset while the device is held still
 *
 * GyroFilter already tracks the offset slowly on its own, this is the explicit "zero it now"
 * version: let the hand settle, average raw rates for a moment and fail if the device moved.
 */
class GyroCalibrator {
public:
    enum class State { Idle, Settling, Measuring, Done, Failed };

    void start();
    void cancel()
    {
        _state = State::Idle;
    }

    // Raw rates in deg/s, dt in seconds
    State update(const GyroFilter::Rates& raw, float dt);

    State getState() const
    {
        return _state;
    }
    bool isRunning() const
    {
        return _state == State::Settling || _state == State::Measuring;
    }
    // 0..1 over the whole run
    float getProgress() const;
    // Valid once Done
    const GyroFilter::Rates& getBias() const
    {
        return _bias;
    }

private:
    State _state   = State::Idle;
    float _elapsed = 0.0f;
    int _samples   = 0;
    GyroFilter::Rates _sum;
    GyroFilter::Rates _min;
    GyroFilter::Rates _max;
    GyroFilter::Rates _bias;
};

}  // namespace imu
