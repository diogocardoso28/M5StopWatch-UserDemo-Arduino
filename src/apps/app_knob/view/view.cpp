/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "view.h"
#include <assets/assets.h>
#include <hal/hal.h>
#include <algorithm>
#include <cmath>
#include <string>

using namespace view;
using namespace uitk;
using namespace uitk::lvgl_cpp;

namespace {

void dial_draw_event_cb(lv_event_t* e);

constexpr int _panel_size = 466;

constexpr int _track_radius    = 224;
constexpr int _track_width     = 12;
constexpr int _minor_tick_r1   = 190;
constexpr int _minor_tick_r2   = 202;
constexpr int _minor_tick_w    = 3;
constexpr int _major_tick_r1   = 180;
constexpr int _major_tick_r2   = 202;
constexpr int _major_tick_w    = 4;
constexpr int _zero_tick_r1    = 170;
constexpr int _dot_radius      = 192;
constexpr int _dot_size        = 12;
constexpr int _dot_size_active = 18;

constexpr int _pointer_width  = 10;
constexpr int _pointer_height = 44;

constexpr int _value_label_y  = -14;
constexpr int _digit_center_y = -3;  // digits sit slightly above the label box center
constexpr int _minus_width    = 36;
constexpr int _minus_height   = 10;
constexpr int _minus_gap      = 12;
constexpr int _mode_label_y   = 72;
constexpr int _mode_dots_y    = 112;
constexpr int _mode_dot_size  = 8;
constexpr int _mode_dot_gap   = 18;
constexpr int _hint_label_y   = 146;

constexpr uint32_t _bg_color         = 0x000000;
constexpr uint32_t _track_color      = 0x1E1E1E;
constexpr uint32_t _tick_color       = 0x4A4A4A;
constexpr uint32_t _pointer_color    = 0xFFFFFF;
constexpr uint32_t _value_color      = 0xFFFFFF;
constexpr uint32_t _hint_color       = 0x6B6B6B;
constexpr uint32_t _flash_color      = 0xFF4D4D;
constexpr uint32_t _half_limit_color = 0xFF77A0;
constexpr uint32_t _flash_duration   = 160;

constexpr float _redraw_threshold_deg = 0.2f;
constexpr float _pi                   = 3.14159265358979323846f;

constexpr std::array<const char*, model::Knob::ModeCount> _mode_names = {"Regular", "Detent", "Half Turn"};

KnobView::DialStyle_t dial_style_for(model::Knob::Mode_t mode)
{
    KnobView::DialStyle_t style;
    switch (mode) {
        case model::Knob::Mode_t::Regular:
            style.startDeg     = 0.0f;
            style.endDeg       = 300.0f;
            style.minorStepDeg = 15.0f;
            style.majorStepDeg = 30.0f;
            style.accent       = 0x7AC4F5;
            break;
        case model::Knob::Mode_t::Detent:
            style.startDeg     = 0.0f;
            style.endDeg       = 300.0f;
            style.minorStepDeg = 0.0f;
            style.majorStepDeg = 30.0f;
            style.dotMarks     = true;
            style.accent       = 0xF4CA63;
            break;
        case model::Knob::Mode_t::HalfTurn:
            style.startDeg     = -180.0f;
            style.endDeg       = 180.0f;
            style.minorStepDeg = 18.0f;
            style.majorStepDeg = 90.0f;
            style.accent       = 0x4AD78C;
            break;
    }
    return style;
}

uint32_t lerp_color(uint32_t from, uint32_t to, float t)
{
    t          = std::clamp(t, 0.0f, 1.0f);
    auto lerp8 = [t](uint32_t shift, uint32_t a, uint32_t b) {
        const float ca = static_cast<float>((a >> shift) & 0xFF);
        const float cb = static_cast<float>((b >> shift) & 0xFF);
        return static_cast<uint32_t>(std::lround(ca + (cb - ca) * t)) << shift;
    };
    return lerp8(16, from, to) | lerp8(8, from, to) | lerp8(0, from, to);
}

// Screen angle: degrees clockwise from 12 o'clock
lv_point_t polar_point(const lv_point_t& center, float screenDeg, int radius)
{
    float radians = screenDeg * _pi / 180.0f;
    return {
        static_cast<int32_t>(center.x + std::lround(radius * std::sin(radians))),
        static_cast<int32_t>(center.y - std::lround(radius * std::cos(radians))),
    };
}

void setup_plain(Container& obj)
{
    obj.setBorderWidth(0);
    obj.setOutlineWidth(0);
    obj.setShadowWidth(0);
    obj.setPaddingAll(0);
    obj.removeFlag(LV_OBJ_FLAG_SCROLLABLE);
}

}  // namespace

void KnobView::init(lv_obj_t* parent)
{
    _panel = std::make_unique<Container>(parent);
    _panel->align(LV_ALIGN_CENTER, 0, 0);
    _panel->setSize(_panel_size, _panel_size);
    _panel->setBgColor(lv_color_hex(_bg_color));
    _panel->setBgOpa(LV_OPA_COVER);
    _panel->setRadius(0);
    setup_plain(*_panel);
    _panel->onClick().connect([this]() {
        if (onTapped) {
            onTapped();
        }
    });

    _dial = std::make_unique<Container>(_panel->get());
    _dial->align(LV_ALIGN_CENTER, 0, 0);
    _dial->setSize(_panel_size, _panel_size);
    _dial->setBgOpa(LV_OPA_TRANSP);
    _dial->setRadius(0);
    setup_plain(*_dial);
    _dial->removeFlag(LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(_dial->get(), dial_draw_event_cb, LV_EVENT_DRAW_MAIN, this);

    // The pointer is fixed to the device: it reads the dial, which turns underneath it
    _pointer = std::make_unique<Container>(_panel->get());
    _pointer->setSize(_pointer_width, _pointer_height);
    _pointer->align(LV_ALIGN_TOP_MID, 0, 0);
    _pointer->setBgColor(lv_color_hex(_pointer_color));
    _pointer->setBgOpa(LV_OPA_COVER);
    _pointer->setRadius(LV_RADIUS_CIRCLE);
    setup_plain(*_pointer);
    _pointer->setShadowWidth(24);
    _pointer->setShadowOpa(LV_OPA_70);
    _pointer->removeFlag(LV_OBJ_FLAG_CLICKABLE);

    _value_label = std::make_unique<Label>(_panel->get());
    _value_label->setTextFont(&CommissionerMedium108);
    _value_label->setTextColor(lv_color_hex(_value_color));
    _value_label->align(LV_ALIGN_CENTER, 0, _value_label_y);
    _value_label->removeFlag(LV_OBJ_FLAG_CLICKABLE);

    // The big font only has digits, so the minus sign is drawn
    _minus_sign = std::make_unique<Container>(_panel->get());
    _minus_sign->setSize(_minus_width, _minus_height);
    _minus_sign->setBgColor(lv_color_hex(_value_color));
    _minus_sign->setBgOpa(LV_OPA_COVER);
    _minus_sign->setRadius(LV_RADIUS_CIRCLE);
    setup_plain(*_minus_sign);
    _minus_sign->removeFlag(LV_OBJ_FLAG_CLICKABLE);
    _minus_sign->setHidden(true);

    _mode_label = std::make_unique<Label>(_panel->get());
    _mode_label->setTextFont(&MontserratSemiBold26);
    _mode_label->align(LV_ALIGN_CENTER, 0, _mode_label_y);
    _mode_label->removeFlag(LV_OBJ_FLAG_CLICKABLE);

    for (int i = 0; i < model::Knob::ModeCount; ++i) {
        auto& dot = _mode_dots[i];
        dot       = std::make_unique<Container>(_panel->get());
        dot->setSize(_mode_dot_size, _mode_dot_size);
        dot->align(LV_ALIGN_CENTER, (2 * i - (model::Knob::ModeCount - 1)) * _mode_dot_gap / 2, _mode_dots_y);
        dot->setRadius(LV_RADIUS_CIRCLE);
        dot->setBgOpa(LV_OPA_COVER);
        setup_plain(*dot);
        dot->removeFlag(LV_OBJ_FLAG_CLICKABLE);
    }

    _hint_label = std::make_unique<Label>(_panel->get());
    _hint_label->setTextFont(&lv_font_montserrat_16);
    _hint_label->setTextColor(lv_color_hex(_hint_color));
    _hint_label->align(LV_ALIGN_CENTER, 0, _hint_label_y);
    _hint_label->setText("tap to zero");
    _hint_label->removeFlag(LV_OBJ_FLAG_CLICKABLE);

    setMode(model::Knob::Mode_t::Regular);
}

void KnobView::update()
{
    if (_is_flashing && static_cast<int32_t>(GetHAL().millis() - _flash_until_ms) >= 0) {
        _is_flashing = false;
        lv_obj_invalidate(_dial->get());
    }
}

void KnobView::setMode(model::Knob::Mode_t mode)
{
    _dial_style = dial_style_for(mode);

    const int index = static_cast<int>(mode);
    _mode_label->setText(_mode_names[index]);
    _mode_label->setTextColor(lv_color_hex(_dial_style.accent));
    _pointer->setShadowColor(lv_color_hex(_dial_style.accent));

    for (int i = 0; i < model::Knob::ModeCount; ++i) {
        _mode_dots[i]->setBgColor(lv_color_hex(i == index ? _dial_style.accent : _tick_color));
    }

    _value_initialized = false;
    lv_obj_invalidate(_dial->get());
}

void KnobView::setDialAngle(float degrees)
{
    if (std::fabs(degrees - _dial_angle) < _redraw_threshold_deg) {
        return;
    }
    _dial_angle = degrees;
    lv_obj_invalidate(_dial->get());
}

void KnobView::setValue(int value)
{
    if (_value_initialized && value == _value) {
        return;
    }
    _value             = value;
    _value_initialized = true;
    applyValueLabel();
}

void KnobView::flashEndStop()
{
    _flash_until_ms = GetHAL().millis() + _flash_duration;
    _is_flashing    = true;
    lv_obj_invalidate(_dial->get());
}

uint32_t KnobView::getValueArcColor() const
{
    if (_is_flashing) {
        return _flash_color;
    }
    if (_dial_style.startDeg < 0.0f) {
        // Warm up toward the ends in Half Turn mode, matching the stronger haptic ticks
        return lerp_color(_dial_style.accent, _half_limit_color, std::fabs(_dial_angle) / _dial_style.endDeg);
    }
    return _dial_style.accent;
}

void KnobView::applyValueLabel()
{
    _value_label->setText(std::to_string(std::abs(_value)));

    if (_value >= 0) {
        _minus_sign->setHidden(true);
        return;
    }

    lv_obj_update_layout(_value_label->get());
    const int label_width = _value_label->getWidth();
    _minus_sign->align(LV_ALIGN_CENTER, -(label_width / 2) - _minus_gap - _minus_width / 2,
                       _value_label_y + _digit_center_y);
    _minus_sign->setHidden(false);
}

namespace {

void draw_arc(lv_layer_t* layer, const lv_point_t& center, float fromScreenDeg, float toScreenDeg, uint32_t color,
              bool rounded)
{
    float span = toScreenDeg - fromScreenDeg;
    if (span < 1.0f) {
        return;
    }

    lv_draw_arc_dsc_t dsc;
    lv_draw_arc_dsc_init(&dsc);
    dsc.center  = center;
    dsc.radius  = _track_radius;
    dsc.width   = _track_width;
    dsc.color   = lv_color_hex(color);
    dsc.opa     = LV_OPA_COVER;
    dsc.rounded = rounded ? 1 : 0;

    if (span >= 359.5f) {
        dsc.start_angle = 0;
        dsc.end_angle   = 360;
    } else {
        // LVGL arcs start at 3 o'clock, screen angles start at 12 o'clock
        int start = static_cast<int>(std::lround(fromScreenDeg - 90.0f)) % 360;
        if (start < 0) {
            start += 360;
        }
        dsc.start_angle = start;
        dsc.end_angle   = start + static_cast<int>(std::lround(span));
    }
    lv_draw_arc(layer, &dsc);
}

void draw_tick(lv_layer_t* layer, const lv_point_t& center, float screenDeg, int r1, int r2, int width,
               uint32_t color)
{
    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    const lv_point_t p1 = polar_point(center, screenDeg, r1);
    const lv_point_t p2 = polar_point(center, screenDeg, r2);
    dsc.p1              = {p1.x, p1.y};
    dsc.p2              = {p2.x, p2.y};
    dsc.width           = width;
    dsc.color           = lv_color_hex(color);
    dsc.opa             = LV_OPA_COVER;
    dsc.round_start     = 1;
    dsc.round_end       = 1;
    lv_draw_line(layer, &dsc);
}

void draw_dot(lv_layer_t* layer, const lv_point_t& center, float screenDeg, int size, uint32_t color)
{
    const lv_point_t p = polar_point(center, screenDeg, _dot_radius);
    lv_area_t area     = {p.x - size / 2, p.y - size / 2, p.x - size / 2 + size - 1, p.y - size / 2 + size - 1};

    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.radius   = LV_RADIUS_CIRCLE;
    dsc.bg_color = lv_color_hex(color);
    dsc.bg_opa   = LV_OPA_COVER;
    lv_draw_rect(layer, &dsc, &area);
}

bool is_major(float dialDeg, float majorStep)
{
    if (majorStep <= 0.0f) {
        return false;
    }
    const float r = std::fmod(std::fabs(dialDeg), majorStep);
    return r < 0.01f || majorStep - r < 0.01f;
}

void dial_draw_event_cb(lv_event_t* e)
{
    auto* view = static_cast<KnobView*>(lv_event_get_user_data(e));
    if (view == nullptr) {
        return;
    }

    lv_obj_t* obj     = lv_event_get_target_obj(e);
    lv_layer_t* layer = lv_event_get_layer(e);
    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);
    const lv_point_t center = {
        static_cast<int32_t>(coords.x1 + lv_obj_get_width(obj) / 2),
        static_cast<int32_t>(coords.y1 + lv_obj_get_height(obj) / 2),
    };

    const auto& style        = view->getDialStyle();
    const float knob         = view->getDialAngle();
    const uint32_t arc_color = view->getValueArcColor();

    // A mark at dial angle d is drawn at screen angle (d - knob): the scale turns against the device
    // so it appears to stay still while the pointer, fixed to the device, sweeps across it.
    draw_arc(layer, center, style.startDeg - knob, style.endDeg - knob, _track_color, true);
    if (knob >= 0.0f) {
        draw_arc(layer, center, -knob, 0.0f, arc_color, true);
    } else {
        draw_arc(layer, center, 0.0f, -knob, arc_color, true);
    }

    auto is_lit = [knob](float dialDeg) {
        constexpr float epsilon = 0.5f;
        return knob >= 0.0f ? (dialDeg >= -epsilon && dialDeg <= knob + epsilon)
                            : (dialDeg <= epsilon && dialDeg >= knob - epsilon);
    };

    if (style.dotMarks) {
        const int nearest = static_cast<int>(std::lround(knob / style.majorStepDeg));
        const int count   = static_cast<int>(std::lround((style.endDeg - style.startDeg) / style.majorStepDeg));
        for (int i = 0; i <= count; ++i) {
            const float dial_deg = style.startDeg + i * style.majorStepDeg;
            const bool active    = static_cast<int>(std::lround(dial_deg / style.majorStepDeg)) == nearest;
            draw_dot(layer, center, dial_deg - knob, active ? _dot_size_active : _dot_size,
                     is_lit(dial_deg) ? arc_color : _tick_color);
        }
        return;
    }

    const float step       = style.minorStepDeg > 0.0f ? style.minorStepDeg : style.majorStepDeg;
    const int count        = static_cast<int>(std::lround((style.endDeg - style.startDeg) / step));
    const bool full_circle = style.endDeg - style.startDeg >= 359.5f;
    for (int i = full_circle ? 1 : 0; i <= count; ++i) {
        const float dial_deg = style.startDeg + i * step;
        // On a full circle the first and last marks coincide, so the last one stands for both
        const bool lit       = is_lit(dial_deg) || (full_circle && i == count && is_lit(style.startDeg));
        const uint32_t color = lit ? arc_color : _tick_color;
        if (std::fabs(dial_deg) < 0.01f && style.startDeg < 0.0f) {
            // Center notch mark
            draw_tick(layer, center, dial_deg - knob, _zero_tick_r1, _major_tick_r2, _major_tick_w + 2, color);
        } else if (is_major(dial_deg, style.majorStepDeg)) {
            draw_tick(layer, center, dial_deg - knob, _major_tick_r1, _major_tick_r2, _major_tick_w, color);
        } else {
            draw_tick(layer, center, dial_deg - knob, _minor_tick_r1, _minor_tick_r2, _minor_tick_w, color);
        }
    }
}

}  // namespace
