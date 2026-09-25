/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "../model/keyboard.h"
#include <smooth_lvgl.hpp>
#include <uitk/short_namespace.hpp>
#include <array>
#include <memory>
#include <string>

// All hit tests take screen coordinates and assume the page is the one on screen.
namespace view {

enum class Page { Settings = 0, Touchpad = 1, Keyboard = 2 };
constexpr int PageCount = 3;

class SettingsPage {
public:
    enum class Item { None, Usb, Bluetooth, AirPointer, TwistScroll, Speed, InvertScroll };

    void init(lv_obj_t* parent);
    Item hitTest(int x, int y) const;
    void setPressed(Item item);

    void setTransport(bool usb);
    void setDetail(const std::string& text);
    void setValues(bool airPointer, bool twistScroll, int speed, bool invertScroll);

private:
    struct Row {
        std::unique_ptr<uitk::lvgl_cpp::Container> bg;
        std::unique_ptr<uitk::lvgl_cpp::Label> name;
        std::unique_ptr<uitk::lvgl_cpp::Label> value;
    };
    static constexpr int RowCount = 4;

    std::unique_ptr<uitk::lvgl_cpp::Container> _usb_pill;
    std::unique_ptr<uitk::lvgl_cpp::Label> _usb_label;
    std::unique_ptr<uitk::lvgl_cpp::Container> _ble_pill;
    std::unique_ptr<uitk::lvgl_cpp::Label> _ble_label;
    std::unique_ptr<uitk::lvgl_cpp::Label> _detail;
    std::array<Row, RowCount> _rows;
    Item _pressed = Item::None;
    bool _usb     = false;

    lv_obj_t* itemObject(Item item) const;
    void applyItemColor(Item item);
};

class TouchpadPage {
public:
    void init(lv_obj_t* parent);
    void update();
    bool hitModeChip(int x, int y) const;
    void setModeChipPressed(bool pressed);

    // progress 0..1 while zeroing, negative when not
    void setZeroing(float progress);
    void showZeroResult(bool ok);

    void setScrollMode(bool scroll);
    void setTouch(bool down, int x, int y);
    void setAirPointer(bool enabled, float velocityX, float velocityY);
    void setTwist(bool active, float angleDeg);

    // Called from the draw callback
    float getTwistAngle() const
    {
        return _twist_angle;
    }
    bool isTwistVisible() const
    {
        return _twist_visible;
    }
    float getZeroProgress() const
    {
        return _zero_progress;
    }

private:
    std::unique_ptr<uitk::lvgl_cpp::Container> _pad;
    std::unique_ptr<uitk::lvgl_cpp::Container> _rim;
    std::unique_ptr<uitk::lvgl_cpp::Container> _finger;
    std::unique_ptr<uitk::lvgl_cpp::Container> _air_ring;
    std::unique_ptr<uitk::lvgl_cpp::Container> _air_dot;
    std::unique_ptr<uitk::lvgl_cpp::Container> _zero_ring;
    std::unique_ptr<uitk::lvgl_cpp::Label> _hint;
    std::unique_ptr<uitk::lvgl_cpp::Container> _chip;
    std::unique_ptr<uitk::lvgl_cpp::Container> _chip_thumb;
    std::unique_ptr<uitk::lvgl_cpp::Label> _chip_move;
    std::unique_ptr<uitk::lvgl_cpp::Label> _chip_scroll;
    std::unique_ptr<uitk::lvgl_cpp::Image> _edge_left;
    std::unique_ptr<uitk::lvgl_cpp::Image> _edge_right;

    bool _scroll_mode      = false;
    bool _finger_down      = false;
    int _finger_x          = -1;
    int _finger_y          = -1;
    bool _air_enabled      = true;
    int _air_dot_x         = 0;
    int _air_dot_y         = 0;
    bool _twist_visible    = false;
    float _twist_angle     = 0.0f;
    uint32_t _twist_hide_at = 0;
    float _zero_progress    = -1.0f;
    const char* _message    = nullptr;
    uint32_t _message_until = 0;

    void applyHint();
};

class KeyboardPage {
public:
    enum class KeyStyle { Normal, Special, Active, Locked };

    void init(lv_obj_t* parent);
    int hitTest(int x, int y) const;  // key index or -1
    void setPressedKey(int index);
    void setKey(int index, model::Keyboard::Action action, uint8_t usage, const std::string& label,
                KeyStyle style);
    void setPreview(const std::string& text, bool lastIsPending);

private:
    struct Key {
        std::unique_ptr<uitk::lvgl_cpp::Container> bg;
        std::unique_ptr<uitk::lvgl_cpp::Label> label;
        KeyStyle style = KeyStyle::Normal;
        lv_area_t area = {};
    };

    std::array<Key, model::Keyboard::KeyCount> _keys;
    std::unique_ptr<uitk::lvgl_cpp::Container> _preview_row;
    std::unique_ptr<uitk::lvgl_cpp::Label> _preview_text;
    std::unique_ptr<uitk::lvgl_cpp::Label> _preview_pending;
    int _pressed = -1;

    void applyKeyColor(int index);
};

class AirMouseView {
public:
    void init(lv_obj_t* parent);

    void showPage(Page page, bool animate = true);
    Page getPage() const
    {
        return _page;
    }
    bool isAnimating() const;

    void setStatus(const std::string& text, uint32_t dotColor);

    SettingsPage& settings()
    {
        return _settings;
    }
    TouchpadPage& touchpad()
    {
        return _touchpad;
    }
    KeyboardPage& keyboard()
    {
        return _keyboard;
    }

private:
    std::unique_ptr<uitk::lvgl_cpp::Container> _panel;
    std::unique_ptr<uitk::lvgl_cpp::Container> _strip;
    std::array<std::unique_ptr<uitk::lvgl_cpp::Container>, PageCount> _pages;
    std::unique_ptr<uitk::lvgl_cpp::Container> _status_row;
    std::unique_ptr<uitk::lvgl_cpp::Container> _status_dot;
    std::unique_ptr<uitk::lvgl_cpp::Label> _status_label;
    std::array<std::unique_ptr<uitk::lvgl_cpp::Container>, PageCount> _page_dots;

    SettingsPage _settings;
    TouchpadPage _touchpad;
    KeyboardPage _keyboard;

    Page _page             = Page::Touchpad;
    uint32_t _anim_end_ms  = 0;
    std::string _status_text;
    uint32_t _status_color = 0;
};

}  // namespace view
