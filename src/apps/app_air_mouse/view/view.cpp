/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "view.h"
#include "view_style.h"
#include <hal/hal.h>

using namespace view;
using namespace uitk::lvgl_cpp;

namespace {

constexpr uint32_t _slide_duration = 220;  // ms

constexpr int _status_y        = 22;
constexpr int _status_dot_size = 10;
constexpr int _page_dots_y     = -18;  // from the bottom
constexpr int _page_dot_size   = 8;
constexpr int _page_dot_active = 22;
constexpr int _page_dot_gap    = 18;

void set_x_cb(void* obj, int32_t value)
{
    lv_obj_set_x(static_cast<lv_obj_t*>(obj), value);
}

}  // namespace

void AirMouseView::init(lv_obj_t* parent)
{
    _panel = std::make_unique<Container>(parent);
    _panel->align(LV_ALIGN_CENTER, 0, 0);
    _panel->setSize(style::ScreenSize, style::ScreenSize);
    _panel->setBgColor(lv_color_hex(style::Background));
    _panel->setBgOpa(LV_OPA_COVER);
    _panel->setRadius(0);
    style::setup_plain(*_panel);

    // Pages sit side by side on a strip that slides; input is handled from raw touch points
    _strip = std::make_unique<Container>(_panel->get());
    _strip->setSize(style::ScreenSize * PageCount, style::ScreenSize);
    _strip->setPos(0, 0);
    _strip->setBgOpa(LV_OPA_TRANSP);
    _strip->setRadius(0);
    style::setup_plain(*_strip);

    for (int i = 0; i < PageCount; ++i) {
        auto& page = _pages[i];
        page       = std::make_unique<Container>(_strip->get());
        page->setSize(style::ScreenSize, style::ScreenSize);
        page->setPos(i * style::ScreenSize, 0);
        page->setBgOpa(LV_OPA_TRANSP);
        page->setRadius(0);
        style::setup_plain(*page);
    }

    _settings.init(_pages[static_cast<int>(Page::Settings)]->get());
    _touchpad.init(_pages[static_cast<int>(Page::Touchpad)]->get());
    _keyboard.init(_pages[static_cast<int>(Page::Keyboard)]->get());

    _status_row = std::make_unique<Container>(_panel->get());
    _status_row->setSize(LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    _status_row->align(LV_ALIGN_TOP_MID, 0, _status_y - 10);
    _status_row->setBgOpa(LV_OPA_TRANSP);
    style::setup_plain(*_status_row);
    _status_row->setFlexFlow(LV_FLEX_FLOW_ROW);
    _status_row->setFlexAlign(LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    _status_row->setPadColumn(8);

    _status_dot = std::make_unique<Container>(_status_row->get());
    _status_dot->setSize(_status_dot_size, _status_dot_size);
    _status_dot->setRadius(LV_RADIUS_CIRCLE);
    _status_dot->setBgOpa(LV_OPA_COVER);
    style::setup_plain(*_status_dot);

    _status_label = std::make_unique<Label>(_status_row->get());
    style::setup_label(*_status_label, &lv_font_montserrat_16, style::TextDim);

    for (int i = 0; i < PageCount; ++i) {
        auto& dot = _page_dots[i];
        dot       = std::make_unique<Container>(_panel->get());
        dot->setRadius(LV_RADIUS_CIRCLE);
        dot->setBgOpa(LV_OPA_COVER);
        style::setup_plain(*dot);
    }

    setStatus("", style::Idle);
    showPage(Page::Touchpad, false);
}

void AirMouseView::showPage(Page page, bool animate)
{
    _page            = page;
    const int target = -static_cast<int>(page) * style::ScreenSize;

    lv_anim_delete(_strip->get(), set_x_cb);
    if (animate) {
        lv_anim_t anim;
        lv_anim_init(&anim);
        lv_anim_set_var(&anim, _strip->get());
        lv_anim_set_exec_cb(&anim, set_x_cb);
        lv_anim_set_values(&anim, lv_obj_get_x(_strip->get()), target);
        lv_anim_set_duration(&anim, _slide_duration);
        lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
        lv_anim_start(&anim);
        _anim_end_ms = GetHAL().millis() + _slide_duration;
    } else {
        _strip->setX(target);
        _anim_end_ms = GetHAL().millis();
    }

    // Active page gets a wider pill, like the knob's mode dots
    const int active = static_cast<int>(page);
    int x            = -((PageCount - 1) * _page_dot_gap + _page_dot_active) / 2;
    for (int i = 0; i < PageCount; ++i) {
        const int width = i == active ? _page_dot_active : _page_dot_size;
        _page_dots[i]->setSize(width, _page_dot_size);
        _page_dots[i]->align(LV_ALIGN_BOTTOM_MID, x + width / 2, _page_dots_y);
        _page_dots[i]->setBgColor(lv_color_hex(i == active ? style::Accent : style::Idle));
        x += width + (_page_dot_gap - _page_dot_size);
    }
}

bool AirMouseView::isAnimating() const
{
    return static_cast<int32_t>(GetHAL().millis() - _anim_end_ms) < 0;
}

void AirMouseView::setStatus(const std::string& text, uint32_t dotColor)
{
    if (text == _status_text && dotColor == _status_color) {
        return;
    }
    _status_text  = text;
    _status_color = dotColor;
    _status_label->setText(text);
    _status_dot->setBgColor(lv_color_hex(dotColor));
}
