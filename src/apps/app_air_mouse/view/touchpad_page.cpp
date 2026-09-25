/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "view.h"
#include "view_style.h"
#include <assets/assets.h>
#include <hal/hal.h>
#include <algorithm>
#include <cmath>

using namespace view;
using namespace uitk::lvgl_cpp;

namespace {

constexpr int _pad_size        = 440;
constexpr uint32_t _pad_color  = 0x0E0E0E;
constexpr uint32_t _pad_border = 0x222222;
constexpr int _finger_size     = 64;
constexpr int _air_ring_size   = 84;
constexpr int _air_dot_size    = 20;
constexpr int _air_dot_travel  = 30;       // px at full indicator speed
constexpr float _air_full_rate = 1200.0f;  // counts/s that pushes the dot to the ring
constexpr int _hint_y          = 64;
constexpr int _edge_inset      = 6;

constexpr int _chip_width  = 200;
constexpr int _chip_height = 48;
constexpr int _chip_y      = 352;
constexpr int _chip_x      = (style::ScreenSize - _chip_width) / 2;
constexpr int _chip_pad    = 4;

constexpr int _twist_radius      = 222;
constexpr int _twist_width       = 8;
constexpr int _twist_span        = 50;  // deg
constexpr uint32_t _twist_track  = 0x1A1A1A;
constexpr uint32_t _twist_linger = 500;  // ms the indicator stays after scrolling stops

constexpr int _zero_ring_size      = 112;
constexpr int _zero_ring_width     = 6;
constexpr uint32_t _message_linger = 1500;  // ms

void zero_draw_event_cb(lv_event_t* e)
{
    auto* page           = static_cast<TouchpadPage*>(lv_event_get_user_data(e));
    const float progress = page->getZeroProgress();
    if (progress < 0.0f) {
        return;
    }

    lv_area_t coords;
    lv_obj_get_coords(static_cast<lv_obj_t*>(lv_event_get_target(e)), &coords);

    lv_draw_arc_dsc_t dsc;
    lv_draw_arc_dsc_init(&dsc);
    dsc.center = {coords.x1 + lv_area_get_width(&coords) / 2, coords.y1 + lv_area_get_height(&coords) / 2};
    dsc.radius = _zero_ring_size / 2;
    dsc.width  = _zero_ring_width;
    dsc.opa    = LV_OPA_COVER;

    dsc.color       = lv_color_hex(_twist_track);
    dsc.start_angle = 0;
    dsc.end_angle   = 360;
    lv_draw_arc(lv_event_get_layer(e), &dsc);

    // Fills clockwise from 12 o'clock
    const int sweep = static_cast<int>(std::lround(360.0f * progress));
    if (sweep > 0) {
        dsc.color       = lv_color_hex(style::Accent);
        dsc.rounded     = 1;
        dsc.start_angle = 270;
        dsc.end_angle   = 270 + sweep;
        lv_draw_arc(lv_event_get_layer(e), &dsc);
    }
}

void twist_draw_event_cb(lv_event_t* e)
{
    auto* page = static_cast<TouchpadPage*>(lv_event_get_user_data(e));
    if (!page->isTwistVisible()) {
        return;
    }

    lv_layer_t* layer = lv_event_get_layer(e);
    lv_obj_t* obj     = static_cast<lv_obj_t*>(lv_event_get_target(e));
    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);

    lv_draw_arc_dsc_t dsc;
    lv_draw_arc_dsc_init(&dsc);
    dsc.center = {coords.x1 + lv_area_get_width(&coords) / 2, coords.y1 + lv_area_get_height(&coords) / 2};
    dsc.radius = _twist_radius;
    dsc.width  = _twist_width;
    dsc.opa    = LV_OPA_COVER;

    dsc.color       = lv_color_hex(_twist_track);
    dsc.start_angle = 0;
    dsc.end_angle   = 360;
    lv_draw_arc(layer, &dsc);

    // LVGL arcs start at 3 o'clock, the twist angle starts at 12 o'clock
    int start = static_cast<int>(std::lround(page->getTwistAngle() - 90.0f - _twist_span / 2.0f)) % 360;
    if (start < 0) {
        start += 360;
    }
    dsc.color       = lv_color_hex(style::Accent);
    dsc.rounded     = 1;
    dsc.start_angle = start;
    dsc.end_angle   = start + _twist_span;
    lv_draw_arc(layer, &dsc);
}

void setup_circle(Container& obj, int size)
{
    obj.setSize(size, size);
    obj.setRadius(LV_RADIUS_CIRCLE);
    style::setup_plain(obj);
}

}  // namespace

void TouchpadPage::init(lv_obj_t* parent)
{
    _pad = std::make_unique<Container>(parent);
    setup_circle(*_pad, _pad_size);
    _pad->align(LV_ALIGN_CENTER, 0, 0);
    _pad->setBgColor(lv_color_hex(_pad_color));
    _pad->setBgOpa(LV_OPA_COVER);
    _pad->setBorderWidth(2);
    _pad->setBorderColor(lv_color_hex(_pad_border));

    _rim = std::make_unique<Container>(parent);
    _rim->setSize(style::ScreenSize, style::ScreenSize);
    _rim->setPos(0, 0);
    _rim->setBgOpa(LV_OPA_TRANSP);
    _rim->setRadius(0);
    style::setup_plain(*_rim);
    lv_obj_add_event_cb(_rim->get(), twist_draw_event_cb, LV_EVENT_DRAW_MAIN, this);

    _air_ring = std::make_unique<Container>(parent);
    setup_circle(*_air_ring, _air_ring_size);
    _air_ring->align(LV_ALIGN_CENTER, 0, 0);
    _air_ring->setBgOpa(LV_OPA_TRANSP);
    _air_ring->setBorderWidth(2);
    _air_ring->setBorderColor(lv_color_hex(style::Idle));

    _air_dot = std::make_unique<Container>(parent);
    setup_circle(*_air_dot, _air_dot_size);
    _air_dot->align(LV_ALIGN_CENTER, 0, 0);
    _air_dot->setBgColor(lv_color_hex(style::Accent));
    _air_dot->setBgOpa(LV_OPA_COVER);

    // Only the ring's own area is redrawn while zeroing, not the whole screen
    _zero_ring = std::make_unique<Container>(parent);
    _zero_ring->setSize(_zero_ring_size + _zero_ring_width, _zero_ring_size + _zero_ring_width);
    _zero_ring->align(LV_ALIGN_CENTER, 0, 0);
    _zero_ring->setBgOpa(LV_OPA_TRANSP);
    _zero_ring->setRadius(0);
    style::setup_plain(*_zero_ring);
    lv_obj_add_event_cb(_zero_ring->get(), zero_draw_event_cb, LV_EVENT_DRAW_MAIN, this);

    _finger = std::make_unique<Container>(parent);
    setup_circle(*_finger, _finger_size);
    _finger->setBgColor(lv_color_hex(style::Accent));
    _finger->setBgOpa(LV_OPA_40);
    _finger->setHidden(true);

    _hint = std::make_unique<Label>(parent);
    style::setup_label(*_hint, &lv_font_montserrat_16, style::Hint);
    _hint->align(LV_ALIGN_TOP_MID, 0, _hint_y);

    // Swipe from either edge to change page, the middle of the pad is all trackpad
    _edge_left = std::make_unique<Image>(parent);
    _edge_left->setSrc(&icon_indicator_left);
    _edge_left->align(LV_ALIGN_LEFT_MID, _edge_inset, 0);
    _edge_left->setOpa(LV_OPA_60);
    _edge_left->removeFlag(LV_OBJ_FLAG_CLICKABLE);

    _edge_right = std::make_unique<Image>(parent);
    _edge_right->setSrc(&icon_indicator_right);
    _edge_right->align(LV_ALIGN_RIGHT_MID, -_edge_inset, 0);
    _edge_right->setOpa(LV_OPA_60);
    _edge_right->removeFlag(LV_OBJ_FLAG_CLICKABLE);

    _chip = std::make_unique<Container>(parent);
    _chip->setPos(_chip_x, _chip_y);
    _chip->setSize(_chip_width, _chip_height);
    _chip->setRadius(_chip_height / 2);
    _chip->setBgColor(lv_color_hex(style::Surface));
    _chip->setBgOpa(LV_OPA_COVER);
    style::setup_plain(*_chip);

    _chip_thumb = std::make_unique<Container>(_chip->get());
    _chip_thumb->setSize(_chip_width / 2 - _chip_pad, _chip_height - 2 * _chip_pad);
    _chip_thumb->setRadius((_chip_height - 2 * _chip_pad) / 2);
    _chip_thumb->setBgColor(lv_color_hex(style::Accent));
    _chip_thumb->setBgOpa(LV_OPA_COVER);
    style::setup_plain(*_chip_thumb);

    _chip_move = std::make_unique<Label>(_chip->get());
    style::setup_label(*_chip_move, &lv_font_montserrat_18, style::Text);
    _chip_move->setText("Move");
    _chip_move->align(LV_ALIGN_CENTER, -_chip_width / 4, 0);

    _chip_scroll = std::make_unique<Label>(_chip->get());
    style::setup_label(*_chip_scroll, &lv_font_montserrat_18, style::Text);
    _chip_scroll->setText("Scroll");
    _chip_scroll->align(LV_ALIGN_CENTER, _chip_width / 4, 0);

    _scroll_mode = true;  // force the first apply
    setScrollMode(false);
}

void TouchpadPage::update()
{
    if (_message && static_cast<int32_t>(GetHAL().millis() - _message_until) >= 0) {
        _message = nullptr;
        applyHint();
    }
}

void TouchpadPage::setZeroing(float progress)
{
    // Quantize so the ring redraws in visible steps instead of every frame
    const float stepped = progress < 0.0f ? -1.0f : std::round(progress * 60.0f) / 60.0f;
    if (stepped == _zero_progress) {
        return;
    }
    const bool started_or_stopped = (stepped < 0.0f) != (_zero_progress < 0.0f);
    _zero_progress                = stepped;
    lv_obj_invalidate(_zero_ring->get());
    if (started_or_stopped) {
        _message = nullptr;
        applyHint();
    }
}

void TouchpadPage::showZeroResult(bool ok)
{
    _message       = ok ? "Zeroed" : "Moved too much, try again";
    _message_until = GetHAL().millis() + _message_linger;
    applyHint();
}

void TouchpadPage::applyHint()
{
    if (_zero_progress >= 0.0f) {
        _hint->setText("Hold still...");
        _hint->setTextColor(lv_color_hex(style::Accent));
    } else if (_message) {
        _hint->setText(_message);
        _hint->setTextColor(lv_color_hex(style::Accent));
    } else {
        _hint->setText(_scroll_mode ? "Drag to scroll, hold to zero" : "Tap to click, hold to zero");
        _hint->setTextColor(lv_color_hex(style::Hint));
    }
}

bool TouchpadPage::hitModeChip(int x, int y) const
{
    // A little larger than drawn, it is the only button on this page
    constexpr int slop = 10;
    return x >= _chip_x - slop && x < _chip_x + _chip_width + slop && y >= _chip_y - slop &&
           y < _chip_y + _chip_height + slop;
}

void TouchpadPage::setModeChipPressed(bool pressed)
{
    _chip->setBgColor(lv_color_hex(pressed ? style::SurfaceDown : style::Surface));
}

void TouchpadPage::setScrollMode(bool scroll)
{
    if (scroll == _scroll_mode) {
        return;
    }
    _scroll_mode = scroll;
    _chip_thumb->align(LV_ALIGN_CENTER, (scroll ? 1 : -1) * _chip_width / 4, 0);
    _chip_move->setTextColor(lv_color_hex(scroll ? style::TextDim : style::AccentDark));
    _chip_scroll->setTextColor(lv_color_hex(scroll ? style::AccentDark : style::TextDim));
    applyHint();
}

void TouchpadPage::setTouch(bool down, int x, int y)
{
    if (down == _finger_down && (!down || (x == _finger_x && y == _finger_y))) {
        return;
    }
    _finger_down = down;
    _finger_x    = x;
    _finger_y    = y;
    _finger->setHidden(!down);
    if (down) {
        _finger->setPos(x - _finger_size / 2, y - _finger_size / 2);
    }
}

void TouchpadPage::setAirPointer(bool enabled, float velocityX, float velocityY)
{
    if (enabled != _air_enabled) {
        _air_enabled = enabled;
        _air_ring->setHidden(!enabled);
        _air_dot->setHidden(!enabled);
    }
    if (!enabled) {
        return;
    }

    float nx           = velocityX / _air_full_rate;
    float ny           = velocityY / _air_full_rate;
    const float length = std::hypot(nx, ny);
    if (length > 1.0f) {
        nx /= length;
        ny /= length;
    }
    const int x = static_cast<int>(std::lround(nx * _air_dot_travel));
    const int y = static_cast<int>(std::lround(ny * _air_dot_travel));
    if (x != _air_dot_x || y != _air_dot_y) {
        _air_dot_x = x;
        _air_dot_y = y;
        _air_dot->align(LV_ALIGN_CENTER, x, y);
    }
}

void TouchpadPage::setTwist(bool active, float angleDeg)
{
    const uint32_t now = GetHAL().millis();
    if (active) {
        _twist_hide_at = now + _twist_linger;
        if (!_twist_visible || std::fabs(angleDeg - _twist_angle) > 0.5f) {
            _twist_visible = true;
            _twist_angle   = angleDeg;
            lv_obj_invalidate(_rim->get());
        }
    } else if (_twist_visible && static_cast<int32_t>(now - _twist_hide_at) >= 0) {
        _twist_visible = false;
        lv_obj_invalidate(_rim->get());
    }
}
