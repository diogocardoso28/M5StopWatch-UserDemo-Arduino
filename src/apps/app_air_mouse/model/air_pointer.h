/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

namespace model {

/**
 * @brief Turns device motion into pointer movement and scroll wheel notches
 *
 * Rotation is split against gravity instead of the raw device axes, so it behaves the same
 * whether the device is held upright, tilted like a watch or lying flat:
 *  - turning left/right around the vertical axis moves the pointer horizontally
 *  - tilting up/down around the screen's horizontal axis moves it vertically
 *  - twisting around the remaining horizontal axis (the forearm, like a knob) scrolls
 */
class AirPointer {
public:
    struct Vec3 {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    struct Settings {
        bool pointer      = true;
        bool scroll       = true;
        bool invertScroll = false;
        int speed         = 1;  // 0 slow, 1 medium, 2 fast
    };

    struct Output {
        float dx  = 0.0f;  // pointer counts, fractional
        float dy  = 0.0f;
        int wheel = 0;     // scroll notches, positive scrolls up
    };

    void setSettings(const Settings& settings);
    const Settings& getSettings() const
    {
        return _settings;
    }

    // gyro in deg/s with the bias removed, accel in g, both in the device frame
    Output update(const Vec3& gyro, const Vec3& accel, float dt);
    void reset();

    // Smoothed pointer velocity in counts/s, for the on-screen indicator
    float getVelocityX() const
    {
        return _velocity_x;
    }
    float getVelocityY() const
    {
        return _velocity_y;
    }
    // Accumulated twist in degrees while scrolling, for the on-screen indicator
    float getTwistAngle() const
    {
        return _twist_angle;
    }
    bool isScrolling() const
    {
        return _scrolling;
    }

private:
    Settings _settings;
    Vec3 _gravity;
    bool _gravity_valid  = false;
    bool _scrolling      = false;
    float _scroll_quiet  = 0.0f;
    float _scroll_accum  = 0.0f;
    float _twist_angle   = 0.0f;
    float _velocity_x    = 0.0f;
    float _velocity_y    = 0.0f;
};

}  // namespace model
