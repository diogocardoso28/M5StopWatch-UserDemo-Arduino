/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <cstdint>

namespace model {

/**
 * @brief Classifies raw touch samples into taps, drags and horizontal swipes
 *
 * Works on raw points instead of LVGL events so a single touch can be a trackpad drag,
 * a key press or a page swipe depending on where it started and how it moved.
 */
class TouchGesture {
public:
    enum class Release {
        None,
        Tap,
        SwipeLeft,   // finger moved right to left
        SwipeRight,  // finger moved left to right
    };

    struct Frame {
        bool down          = false;  // a finger is on the screen
        bool pressed       = false;  // the touch started this frame
        bool dragging      = false;  // moved past the tap slop
        bool startedAtEdge = false;  // started in the left or right edge band
        bool longPress     = false;  // held still past LongPressMs, set on that one frame only
        int fingers        = 0;
        int x              = 0;
        int y              = 0;
        int startX         = 0;
        int startY         = 0;
        float dx           = 0.0f;  // movement this frame, only while dragging
        float dy           = 0.0f;
        Release release    = Release::None;  // set on the frame the finger lifts
    };

    static constexpr int ScreenSize       = 466;
    static constexpr int EdgeBand         = 58;   // px from the left/right edge
    static constexpr int TapSlop          = 12;   // px
    static constexpr uint32_t TapMax      = 350;  // ms
    static constexpr int SwipeMin         = 70;   // px
    static constexpr uint32_t LongPressMs = 600;  // ms

    const Frame& update(int fingers, int x, int y, uint32_t nowMs);
    const Frame& getFrame() const
    {
        return _frame;
    }

private:
    Frame _frame;
    uint32_t _start_ms = 0;
    int _last_x        = 0;
    int _last_y        = 0;
    int _last_fingers  = 0;
    bool _long_pressed = false;
};

}  // namespace model
