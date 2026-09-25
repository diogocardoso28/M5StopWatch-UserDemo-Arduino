/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "keyboard.h"
#include <array>
#include <cctype>
#include <cstring>

using namespace model;

namespace {

using Action = Keyboard::Action;
using Layer  = Keyboard::Layer;
using KeyDef = Keyboard::KeyDef;
using Layout = std::array<KeyDef, Keyboard::KeyCount>;

constexpr size_t _max_text_length = 64;

const Layout _letters = {{
    {".,?!", Action::MultiTap, ".,?!'-@"},
    {"abc", Action::MultiTap, "abc"},
    {"def", Action::MultiTap, "def"},
    {"Backspace", Action::Backspace},
    {"ghi", Action::MultiTap, "ghi"},
    {"jkl", Action::MultiTap, "jkl"},
    {"mno", Action::MultiTap, "mno"},
    {"Enter", Action::Enter},
    {"pqrs", Action::MultiTap, "pqrs"},
    {"tuv", Action::MultiTap, "tuv"},
    {"wxyz", Action::MultiTap, "wxyz"},
    {"Shift", Action::Shift},
    {"123", Action::Layer, "", 0, Layer::Numbers},
    {"Space", Action::Space},
    {"Nav", Action::Layer, "", 0, Layer::Navigation},
}};

const Layout _numbers = {{
    {"1", Action::Char, "1"},
    {"2", Action::Char, "2"},
    {"3", Action::Char, "3"},
    {"Backspace", Action::Backspace},
    {"4", Action::Char, "4"},
    {"5", Action::Char, "5"},
    {"6", Action::Char, "6"},
    {"Enter", Action::Enter},
    {"7", Action::Char, "7"},
    {"8", Action::Char, "8"},
    {"9", Action::Char, "9"},
    {"0", Action::Char, "0"},
    {"abc", Action::Layer, "", 0, Layer::Letters},
    {"Space", Action::Space},
    {"Nav", Action::Layer, "", 0, Layer::Navigation},
}};

const Layout _navigation = {{
    {"Esc", Action::Key, "", hid_usage::Escape},
    {"Up", Action::Key, "", hid_usage::Up},
    {"Tab", Action::Key, "", hid_usage::Tab},
    {"Backspace", Action::Backspace},
    {"Left", Action::Key, "", hid_usage::Left},
    {"Down", Action::Key, "", hid_usage::Down},
    {"Right", Action::Key, "", hid_usage::Right},
    {"Enter", Action::Enter},
    {"Home", Action::Key, "", hid_usage::Home},
    {"PgUp", Action::Key, "", hid_usage::PageUp},
    {"PgDn", Action::Key, "", hid_usage::PageDown},
    {"End", Action::Key, "", hid_usage::End},
    {"abc", Action::Layer, "", 0, Layer::Letters},
    {"Space", Action::Space},
    {"123", Action::Layer, "", 0, Layer::Numbers},
}};

const Layout& layout_of(Layer layer)
{
    switch (layer) {
        case Layer::Numbers:
            return _numbers;
        case Layer::Navigation:
            return _navigation;
        default:
            return _letters;
    }
}

}  // namespace

bool model::ascii_to_key_stroke(char c, KeyStroke& out)
{
    constexpr uint8_t shift = hid_usage::ModLeftShift;
    out                     = {};

    if (c >= 'a' && c <= 'z') {
        out.usage = 0x04 + (c - 'a');
        return true;
    }
    if (c >= 'A' && c <= 'Z') {
        out = {shift, static_cast<uint8_t>(0x04 + (c - 'A'))};
        return true;
    }
    if (c >= '1' && c <= '9') {
        out.usage = 0x1E + (c - '1');
        return true;
    }

    // Shifted digit row, in key order 1..0
    static constexpr const char* digit_shifted = "!@#$%^&*()";
    if (const char* found = c ? std::strchr(digit_shifted, c) : nullptr) {
        out = {shift, static_cast<uint8_t>(0x1E + (found - digit_shifted))};
        return true;
    }

    struct Symbol {
        char plain;
        char shifted;
        uint8_t usage;
    };
    static constexpr Symbol symbols[] = {
        {'0', ')', 0x27}, {' ', ' ', hid_usage::Space}, {'-', '_', 0x2D}, {'=', '+', 0x2E}, {'[', '{', 0x2F},
        {']', '}', 0x30}, {'\\', '|', 0x31},             {';', ':', 0x33}, {'\'', '"', 0x34}, {'`', '~', 0x35},
        {',', '<', 0x36}, {'.', '>', 0x37},              {'/', '?', 0x38},
    };
    for (const auto& symbol : symbols) {
        if (c == symbol.plain) {
            out.usage = symbol.usage;
            return true;
        }
        if (c == symbol.shifted) {
            out = {shift, symbol.usage};
            return true;
        }
    }
    return false;
}

const Keyboard::KeyDef& Keyboard::getKey(int index) const
{
    const auto& layout = layout_of(_layer);
    return layout[index >= 0 && index < KeyCount ? index : 0];
}

std::string Keyboard::getKeyLabel(int index) const
{
    const auto& key   = getKey(index);
    std::string label = key.label;
    if (key.action == Action::MultiTap && _shift != Shift::Off) {
        for (auto& c : label) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
    }
    return label;
}

std::vector<KeyStroke> Keyboard::press(int index, uint32_t nowMs)
{
    std::vector<KeyStroke> out;
    if (index < 0 || index >= KeyCount) {
        return out;
    }
    const auto& key = getKey(index);

    if (key.action == Action::MultiTap) {
        const int count = static_cast<int>(std::strlen(key.chars));
        if (index == _pending_key && nowMs - _pending_ms < MultiTapMs) {
            // Same key again: swap the letter just typed for the next one
            _pending_index = (_pending_index + 1) % count;
            eraseChar(out);
        } else {
            commitPending();
            _pending_key   = index;
            _pending_index = 0;
            _pending_upper = _shift != Shift::Off;
            if (_shift == Shift::Once) {
                _shift = Shift::Off;
            }
        }
        _pending_ms = nowMs;

        char c = key.chars[_pending_index];
        if (_pending_upper) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
        typeChar(c, out);
        return out;
    }

    commitPending();
    switch (key.action) {
        case Action::Char:
            typeChar(key.chars[0], out);
            break;
        case Action::Space:
            typeChar(' ', out);
            break;
        case Action::Key:
            out.push_back({0, key.usage});
            break;
        case Action::Backspace:
            eraseChar(out);
            break;
        case Action::Enter:
            out.push_back({0, hid_usage::Enter});
            _text.clear();
            break;
        case Action::Shift:
            _shift = _shift == Shift::Off ? Shift::Once : (_shift == Shift::Once ? Shift::Locked : Shift::Off);
            break;
        case Action::Layer:
            _layer = key.target;
            break;
        case Action::MultiTap:
            break;
    }
    return out;
}

bool Keyboard::update(uint32_t nowMs)
{
    if (_pending_key >= 0 && nowMs - _pending_ms >= MultiTapMs) {
        commitPending();
        return true;
    }
    return false;
}

void Keyboard::commitPending()
{
    _pending_key   = -1;
    _pending_index = 0;
}

void Keyboard::typeChar(char c, std::vector<KeyStroke>& out)
{
    KeyStroke stroke;
    if (!ascii_to_key_stroke(c, stroke)) {
        return;
    }
    out.push_back(stroke);
    _text.push_back(c);
    if (_text.size() > _max_text_length) {
        _text.erase(0, _text.size() - _max_text_length);
    }
}

void Keyboard::eraseChar(std::vector<KeyStroke>& out)
{
    out.push_back({0, hid_usage::Backspace});
    if (!_text.empty()) {
        _text.pop_back();
    }
}
