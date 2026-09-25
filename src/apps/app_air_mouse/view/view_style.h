/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
// Shared look for the Air Mouse pages
#include <smooth_lvgl.hpp>
#include <uitk/short_namespace.hpp>
#include <cstdint>

namespace view::style {

constexpr int ScreenSize = 466;

constexpr uint32_t Background  = 0x000000;
constexpr uint32_t Accent      = 0x9B8CFF;
constexpr uint32_t AccentDark  = 0x221B4D;
constexpr uint32_t Surface     = 0x262626;
constexpr uint32_t SurfaceDown = 0x4C4C4C;
constexpr uint32_t Text        = 0xFFFFFF;
constexpr uint32_t TextDim     = 0x8B8B8B;
constexpr uint32_t Hint        = 0x6B6B6B;
constexpr uint32_t Idle        = 0x3A3A3A;
constexpr uint32_t Waiting     = 0xF4CA63;
constexpr uint32_t Connected   = 0x4AD78C;

inline void setup_plain(uitk::lvgl_cpp::Container& obj)
{
    obj.setBorderWidth(0);
    obj.setOutlineWidth(0);
    obj.setShadowWidth(0);
    obj.setPaddingAll(0);
    obj.removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    obj.removeFlag(LV_OBJ_FLAG_CLICKABLE);
}

inline void setup_label(uitk::lvgl_cpp::Label& label, const lv_font_t* font, uint32_t color)
{
    label.setTextFont(font);
    label.setTextColor(lv_color_hex(color));
    label.removeFlag(LV_OBJ_FLAG_CLICKABLE);
}

inline bool area_contains(const lv_area_t& area, int x, int y)
{
    return x >= area.x1 && x <= area.x2 && y >= area.y1 && y <= area.y2;
}

}  // namespace view::style
