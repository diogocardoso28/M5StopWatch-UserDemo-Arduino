/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "../model/knob.h"
#include <smooth_lvgl.hpp>
#include <uitk/short_namespace.hpp>
#include <array>
#include <functional>
#include <memory>

namespace view {

class KnobView {
public:
    // Dial layout for one mode, in dial degrees clockwise from the zero mark
    struct DialStyle_t {
        float startDeg     = 0.0f;
        float endDeg       = 300.0f;
        float minorStepDeg = 15.0f;  // 0 to disable
        float majorStepDeg = 30.0f;
        bool dotMarks      = false;  // draw marks as dots instead of lines
        uint32_t accent    = 0xFFFFFF;
    };

    std::function<void()> onTapped;

    void init(lv_obj_t* parent);
    void update();

    void setMode(model::Knob::Mode_t mode);
    void setDialAngle(float degrees);
    void setValue(int value);
    void flashEndStop();

    const DialStyle_t& getDialStyle() const
    {
        return _dial_style;
    }
    float getDialAngle() const
    {
        return _dial_angle;
    }
    uint32_t getValueArcColor() const;

private:
    std::unique_ptr<uitk::lvgl_cpp::Container> _panel;
    std::unique_ptr<uitk::lvgl_cpp::Container> _dial;
    std::unique_ptr<uitk::lvgl_cpp::Container> _pointer;
    std::unique_ptr<uitk::lvgl_cpp::Label> _value_label;
    std::unique_ptr<uitk::lvgl_cpp::Container> _minus_sign;
    std::unique_ptr<uitk::lvgl_cpp::Label> _mode_label;
    std::unique_ptr<uitk::lvgl_cpp::Label> _hint_label;
    std::array<std::unique_ptr<uitk::lvgl_cpp::Container>, model::Knob::ModeCount> _mode_dots;

    DialStyle_t _dial_style;
    float _dial_angle        = 0.0f;
    int _value               = 0;
    bool _value_initialized  = false;
    uint32_t _flash_until_ms = 0;
    bool _is_flashing        = false;

    void applyValueLabel();
};

}  // namespace view
