/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <cstdint>

namespace model {

/**
 * @brief Virtual knob driven by device rotation around the screen axis
 *
 * Pure logic: feed it the clockwise rotation of the device for each frame and it produces
 * the knob value, the angle the dial should be drawn at, and haptic events to play.
 */
class Knob {
public:
    enum class Mode_t {
        Regular,   // 1:1 rotation, 0..100 over 300 deg, hard stops at both ends
        Detent,    // 0..10 steps, sticky resistance then a snap into the next step
        HalfTurn,  // -100..100 over -180..180 deg, zero in the middle with a center notch
    };

    enum class HapticType_t {
        None,
        Tick,         // light texture tick, intensity scales strength
        Detent,       // crisp click when snapping into a step
        CenterNotch,  // click when passing through zero
        EndStop,      // heavy thud when pushing against a hard stop
    };

    struct HapticEvent_t {
        HapticType_t type = HapticType_t::None;
        float intensity   = 0.0f;  // 0..1
    };

    static constexpr int ModeCount = 3;

    /**
     * @param rotationDeg device rotation since last update, clockwise positive
     * @param dt seconds since last update
     */
    void update(float rotationDeg, float dt);

    void setMode(Mode_t mode);
    Mode_t getMode() const
    {
        return _mode;
    }
    void nextMode(int direction);

    // Zero the value of the current mode
    void reset();

    // Integer value shown to the user, in the current mode's units
    int getValue() const;

    /**
     * @brief Knob position on the dial in degrees, clockwise from the dial's zero mark
     *
     * The view rotates the scale by the negative of this so the scale stays still in the
     * world while the device (the knob) turns. In Detent mode this is not 1:1 with the
     * physical rotation: it resists, then snaps.
     */
    float getDialAngle() const;

    // Returns the strongest haptic event since the last call and clears it
    HapticEvent_t popHapticEvent();

private:
    Mode_t _mode = Mode_t::Regular;
    HapticEvent_t _pending_haptic;

    // Regular
    float _regular_angle       = 0.0f;
    bool _regular_stop_latched = false;

    // Detent
    int _detent_index         = 0;
    float _detent_offset      = 0.0f;  // physical degrees away from the current step's center
    bool _detent_stop_latched = false;
    float _detent_display     = 0.0f;  // spring-follower output, dial degrees
    float _detent_display_vel = 0.0f;

    // Half turn
    float _half_angle       = 0.0f;
    bool _half_stop_latched = false;
    bool _half_in_notch     = true;

    void updateRegular(float rotationDeg);
    void updateDetent(float rotationDeg, float dt);
    void updateHalfTurn(float rotationDeg);
    float detentTargetAngle() const;
    void emitHaptic(HapticType_t type, float intensity = 1.0f);
};

}  // namespace model
