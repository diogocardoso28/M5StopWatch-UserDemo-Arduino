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

using Action = model::Keyboard::Action;

// Rows of 4, 4, 4 and 3 keys, sized so every corner stays inside the round screen
constexpr int _key_height    = 64;
constexpr int _key_gap       = 8;
constexpr int _wide_row_key  = 88;
constexpr int _short_row_key = 100;
constexpr int _first_row_y   = 118;
constexpr int _key_radius    = 16;
constexpr int _preview_y     = 66;
constexpr size_t _preview_max = 16;

constexpr int _row_sizes[] = {4, 4, 4, 3};

const char* symbol_for(Action action, uint8_t usage)
{
    switch (action) {
        case Action::Backspace:
            return LV_SYMBOL_BACKSPACE;
        case Action::Enter:
            return LV_SYMBOL_NEW_LINE;
        case Action::Key:
            switch (usage) {
                case model::hid_usage::Up:
                    return LV_SYMBOL_UP;
                case model::hid_usage::Down:
                    return LV_SYMBOL_DOWN;
                case model::hid_usage::Left:
                    return LV_SYMBOL_LEFT;
                case model::hid_usage::Right:
                    return LV_SYMBOL_RIGHT;
                default:
                    return nullptr;
            }
        default:
            return nullptr;
    }
}

}  // namespace

void KeyboardPage::init(lv_obj_t* parent)
{
    _preview_row = std::make_unique<Container>(parent);
    _preview_row->setSize(LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    _preview_row->align(LV_ALIGN_TOP_MID, 0, _preview_y);
    _preview_row->setBgOpa(LV_OPA_TRANSP);
    style::setup_plain(*_preview_row);
    _preview_row->setFlexFlow(LV_FLEX_FLOW_ROW);
    _preview_row->setFlexAlign(LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);

    _preview_text = std::make_unique<Label>(_preview_row->get());
    style::setup_label(*_preview_text, &lv_font_montserrat_22, style::Text);
    _preview_pending = std::make_unique<Label>(_preview_row->get());
    style::setup_label(*_preview_pending, &lv_font_montserrat_22, style::Accent);

    int index = 0;
    int y     = _first_row_y;
    for (int row_size : _row_sizes) {
        const int key_width = row_size == 4 ? _wide_row_key : _short_row_key;
        int x               = (style::ScreenSize - (row_size * key_width + (row_size - 1) * _key_gap)) / 2;
        for (int i = 0; i < row_size; ++i, ++index) {
            auto& key = _keys[index];
            key.area  = {x, y, x + key_width - 1, y + _key_height - 1};

            key.bg = std::make_unique<Container>(parent);
            key.bg->setPos(x, y);
            key.bg->setSize(key_width, _key_height);
            key.bg->setRadius(_key_radius);
            key.bg->setBgOpa(LV_OPA_COVER);
            style::setup_plain(*key.bg);
            key.bg->setBorderColor(lv_color_hex(style::Accent));

            key.label = std::make_unique<Label>(key.bg->get());
            key.label->removeFlag(LV_OBJ_FLAG_CLICKABLE);
            key.label->align(LV_ALIGN_CENTER, 0, 0);
            x += key_width + _key_gap;
        }
        y += _key_height + _key_gap;
    }

    setPreview("", false);
}

int KeyboardPage::hitTest(int x, int y) const
{
    // Grow every key by half the gap so taps between keys still land somewhere
    constexpr int grow = _key_gap / 2;
    for (int i = 0; i < model::Keyboard::KeyCount; ++i) {
        const auto& a = _keys[i].area;
        if (x >= a.x1 - grow && x <= a.x2 + grow && y >= a.y1 - grow && y <= a.y2 + grow) {
            return i;
        }
    }
    return -1;
}

void KeyboardPage::setPressedKey(int index)
{
    if (index == _pressed) {
        return;
    }
    const int previous = _pressed;
    _pressed           = index;
    applyKeyColor(previous);
    applyKeyColor(index);
}

void KeyboardPage::setKey(int index, model::Keyboard::Action action, uint8_t usage, const std::string& label,
                          KeyStyle keyStyle)
{
    if (index < 0 || index >= model::Keyboard::KeyCount) {
        return;
    }
    auto& key = _keys[index];

    const lv_font_t* font = &lv_font_montserrat_18;
    if (const char* symbol = symbol_for(action, usage)) {
        key.label->setText(symbol);
        font = &lv_font_montserrat_24;
    } else if (action == Action::Shift) {
        key.label->setText("aA");
    } else {
        key.label->setText(label);
        if (action == Action::Char) {
            font = &lv_font_montserrat_28;
        } else if (action == Action::MultiTap) {
            font = &lv_font_montserrat_24;
        }
    }
    key.label->setTextFont(font);
    key.style = keyStyle;
    applyKeyColor(index);
}

void KeyboardPage::setPreview(const std::string& text, bool lastIsPending)
{
    if (text.empty()) {
        _preview_text->setText("Type here");
        _preview_text->setTextColor(lv_color_hex(style::Hint));
        _preview_pending->setText("");
        return;
    }

    std::string shown = text.size() > _preview_max ? text.substr(text.size() - _preview_max) : text;
    std::string pending;
    if (lastIsPending) {
        pending = shown.substr(shown.size() - 1);
        shown.pop_back();
    }
    // A trailing space would collapse to nothing, show it as an underscore
    for (auto& c : shown) {
        if (c == ' ') {
            c = '_';
        }
    }
    _preview_text->setText(shown);
    _preview_text->setTextColor(lv_color_hex(style::Text));
    _preview_pending->setText(pending);
}

void KeyboardPage::applyKeyColor(int index)
{
    if (index < 0 || index >= model::Keyboard::KeyCount) {
        return;
    }
    auto& key          = _keys[index];
    const bool pressed = index == _pressed;

    uint32_t bg   = pressed ? style::SurfaceDown : style::Surface;
    uint32_t text = style::Text;
    int border    = 0;
    switch (key.style) {
        case KeyStyle::Normal:
            break;
        case KeyStyle::Special:
            text = style::Accent;
            break;
        case KeyStyle::Active:
            text   = style::Accent;
            border = 2;
            break;
        case KeyStyle::Locked:
            bg   = pressed ? style::Text : style::Accent;
            text = style::AccentDark;
            break;
    }
    key.bg->setBgColor(lv_color_hex(bg));
    key.bg->setBorderWidth(border);
    key.label->setTextColor(lv_color_hex(text));
}
