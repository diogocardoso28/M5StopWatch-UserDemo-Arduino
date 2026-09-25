/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "view.h"
#include "view_style.h"

using namespace view;
using namespace uitk::lvgl_cpp;

namespace {

using Item = SettingsPage::Item;

// Laid out to stay inside the round screen
constexpr int _pill_y      = 62;
constexpr int _pill_width  = 150;
constexpr int _pill_height = 60;
constexpr int _pill_gap    = 10;
constexpr int _detail_y    = 130;
constexpr int _row_y       = 172;
constexpr int _row_width   = 320;
constexpr int _row_height  = 52;
constexpr int _row_gap     = 6;
constexpr int _row_inset   = 22;

constexpr int _usb_x = style::ScreenSize / 2 - _pill_gap / 2 - _pill_width;
constexpr int _ble_x = style::ScreenSize / 2 + _pill_gap / 2;
constexpr int _row_x = (style::ScreenSize - _row_width) / 2;

constexpr Item _row_items[]        = {Item::AirPointer, Item::TwistScroll, Item::Speed, Item::InvertScroll};
constexpr const char* _row_names[] = {"Air pointer", "Twist scroll", "Speed", "Invert scroll"};
constexpr const char* _speed_names[] = {"Slow", "Medium", "Fast"};

lv_area_t rect(int x, int y, int w, int h)
{
    return {x, y, x + w - 1, y + h - 1};
}

lv_area_t row_area(int index)
{
    return rect(_row_x, _row_y + index * (_row_height + _row_gap), _row_width, _row_height);
}

void setup_pill(Container& pill, int x, int y, int w, int h)
{
    pill.setPos(x, y);
    pill.setSize(w, h);
    pill.setRadius(h / 2);
    pill.setBgOpa(LV_OPA_COVER);
    style::setup_plain(pill);
}

}  // namespace

void SettingsPage::init(lv_obj_t* parent)
{
    _usb_pill = std::make_unique<Container>(parent);
    setup_pill(*_usb_pill, _usb_x, _pill_y, _pill_width, _pill_height);
    _usb_label = std::make_unique<Label>(_usb_pill->get());
    style::setup_label(*_usb_label, &lv_font_montserrat_20, style::Text);
    _usb_label->setText(LV_SYMBOL_USB " USB");
    _usb_label->align(LV_ALIGN_CENTER, 0, 0);

    _ble_pill = std::make_unique<Container>(parent);
    setup_pill(*_ble_pill, _ble_x, _pill_y, _pill_width, _pill_height);
    _ble_label = std::make_unique<Label>(_ble_pill->get());
    style::setup_label(*_ble_label, &lv_font_montserrat_20, style::Text);
    _ble_label->setText(LV_SYMBOL_BLUETOOTH " Bluetooth");
    _ble_label->align(LV_ALIGN_CENTER, 0, 0);

    _detail = std::make_unique<Label>(parent);
    style::setup_label(*_detail, &lv_font_montserrat_14, style::TextDim);
    _detail->setWidth(_row_width);
    _detail->setTextAlign(LV_TEXT_ALIGN_CENTER);
    _detail->align(LV_ALIGN_TOP_MID, 0, _detail_y);

    for (int i = 0; i < RowCount; ++i) {
        auto& row       = _rows[i];
        const auto area = row_area(i);
        row.bg          = std::make_unique<Container>(parent);
        setup_pill(*row.bg, area.x1, area.y1, _row_width, _row_height);
        row.bg->setBgColor(lv_color_hex(style::Surface));

        row.name = std::make_unique<Label>(row.bg->get());
        style::setup_label(*row.name, &lv_font_montserrat_20, style::Text);
        row.name->setText(_row_names[i]);
        row.name->align(LV_ALIGN_LEFT_MID, _row_inset, 0);

        row.value = std::make_unique<Label>(row.bg->get());
        style::setup_label(*row.value, &lv_font_montserrat_20, style::Accent);
        row.value->align(LV_ALIGN_RIGHT_MID, -_row_inset, 0);
    }

    setTransport(false);
    setValues(true, true, 1, false);
}

SettingsPage::Item SettingsPage::hitTest(int x, int y) const
{
    if (style::area_contains(rect(_usb_x, _pill_y, _pill_width, _pill_height), x, y)) {
        return Item::Usb;
    }
    if (style::area_contains(rect(_ble_x, _pill_y, _pill_width, _pill_height), x, y)) {
        return Item::Bluetooth;
    }
    for (int i = 0; i < RowCount; ++i) {
        // Rows are close together, so split the gaps between neighbours
        auto area = row_area(i);
        area.y1 -= _row_gap / 2;
        area.y2 += _row_gap / 2;
        if (style::area_contains(area, x, y)) {
            return _row_items[i];
        }
    }
    return Item::None;
}

void SettingsPage::setPressed(Item item)
{
    if (item == _pressed) {
        return;
    }
    const Item previous = _pressed;
    _pressed            = item;
    applyItemColor(previous);
    applyItemColor(item);
}

void SettingsPage::setTransport(bool usb)
{
    _usb = usb;
    applyItemColor(Item::Usb);
    applyItemColor(Item::Bluetooth);
}

void SettingsPage::setDetail(const std::string& text)
{
    _detail->setText(text);
}

void SettingsPage::setValues(bool airPointer, bool twistScroll, int speed, bool invertScroll)
{
    auto set_toggle = [](Label& label, bool on) {
        label.setText(on ? "On" : "Off");
        label.setTextColor(lv_color_hex(on ? style::Accent : style::TextDim));
    };
    set_toggle(*_rows[0].value, airPointer);
    set_toggle(*_rows[1].value, twistScroll);
    _rows[2].value->setText(_speed_names[speed >= 0 && speed <= 2 ? speed : 1]);
    set_toggle(*_rows[3].value, invertScroll);
}

lv_obj_t* SettingsPage::itemObject(Item item) const
{
    switch (item) {
        case Item::Usb:
            return _usb_pill->get();
        case Item::Bluetooth:
            return _ble_pill->get();
        case Item::None:
            return nullptr;
        default:
            for (int i = 0; i < RowCount; ++i) {
                if (_row_items[i] == item) {
                    return _rows[i].bg->get();
                }
            }
            return nullptr;
    }
}

void SettingsPage::applyItemColor(Item item)
{
    lv_obj_t* obj = itemObject(item);
    if (!obj) {
        return;
    }

    const bool pressed = item == _pressed;
    uint32_t bg        = pressed ? style::SurfaceDown : style::Surface;
    uint32_t text      = style::Text;
    const bool selected = (item == Item::Usb && _usb) || (item == Item::Bluetooth && !_usb);
    if (selected) {
        bg   = pressed ? style::Text : style::Accent;
        text = style::AccentDark;
    }
    lv_obj_set_style_bg_color(obj, lv_color_hex(bg), LV_PART_MAIN);

    if (item == Item::Usb) {
        _usb_label->setTextColor(lv_color_hex(text));
    } else if (item == Item::Bluetooth) {
        _ble_label->setTextColor(lv_color_hex(text));
    }
}
