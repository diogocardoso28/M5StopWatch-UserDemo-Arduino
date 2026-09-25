/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "touch_gesture.h"
#include <cstdlib>

using namespace model;

const TouchGesture::Frame& TouchGesture::update(int fingers, int x, int y, uint32_t nowMs)
{
    const bool was_down = _frame.down;
    _frame.pressed      = false;
    _frame.longPress    = false;
    _frame.release      = Release::None;
    _frame.dx           = 0.0f;
    _frame.dy           = 0.0f;

    if (fingers <= 0) {
        if (was_down) {
            const int total_dx = _frame.x - _frame.startX;
            const int total_dy = _frame.y - _frame.startY;
            if (!_frame.dragging && nowMs - _start_ms <= TapMax) {
                _frame.release = Release::Tap;
            } else if (std::abs(total_dx) >= SwipeMin && std::abs(total_dx) * 2 > std::abs(total_dy) * 3) {
                _frame.release = total_dx < 0 ? Release::SwipeLeft : Release::SwipeRight;
            }
        }
        _frame.down     = false;
        _frame.dragging = false;
        _frame.fingers  = 0;
        _last_fingers   = 0;
        return _frame;
    }

    if (!was_down) {
        _frame               = {};
        _frame.down          = true;
        _frame.pressed       = true;
        _frame.startX        = x;
        _frame.startY        = y;
        _frame.startedAtEdge = x < EdgeBand || x >= ScreenSize - EdgeBand;
        _start_ms            = nowMs;
        _long_pressed        = false;
        _last_x              = x;
        _last_y              = y;
    }

    _frame.x       = x;
    _frame.y       = y;
    _frame.fingers = fingers;

    // When a second finger lands or lifts the reported point jumps to another finger,
    // so skip that frame's movement instead of turning it into a leap
    if (fingers != _last_fingers) {
        _last_fingers = fingers;
        _last_x       = x;
        _last_y       = y;
        if (!was_down) {
            return _frame;
        }
        _frame.dragging = true;
        return _frame;
    }

    if (!_frame.dragging) {
        const int from_start_x = x - _frame.startX;
        const int from_start_y = y - _frame.startY;
        if (from_start_x * from_start_x + from_start_y * from_start_y > TapSlop * TapSlop) {
            // Start moving from here so crossing the slop doesn't jump
            _frame.dragging = true;
            _last_x         = x;
            _last_y         = y;
        } else if (!_long_pressed && nowMs - _start_ms >= LongPressMs) {
            _long_pressed    = true;
            _frame.longPress = true;
        }
        return _frame;
    }

    _frame.dx = static_cast<float>(x - _last_x);
    _frame.dy = static_cast<float>(y - _last_y);
    _last_x   = x;
    _last_y   = y;
    return _frame;
}
