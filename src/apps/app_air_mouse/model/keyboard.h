/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace model {

namespace hid_usage {

constexpr uint8_t ModLeftShift = 0x02;

constexpr uint8_t Enter     = 0x28;
constexpr uint8_t Escape    = 0x29;
constexpr uint8_t Backspace = 0x2A;
constexpr uint8_t Tab       = 0x2B;
constexpr uint8_t Space     = 0x2C;
constexpr uint8_t Home      = 0x4A;
constexpr uint8_t PageUp    = 0x4B;
constexpr uint8_t End       = 0x4D;
constexpr uint8_t PageDown  = 0x4E;
constexpr uint8_t Right     = 0x4F;
constexpr uint8_t Left      = 0x50;
constexpr uint8_t Down      = 0x51;
constexpr uint8_t Up        = 0x52;

}  // namespace hid_usage

struct KeyStroke {
    uint8_t modifiers = 0;
    uint8_t usage     = 0;
};

// Printable ASCII to a key stroke on a US layout host. Returns false for anything unmapped.
bool ascii_to_key_stroke(char c, KeyStroke& out);

/**
 * @brief A 15-key keyboard sized for a small round screen
 *
 * Letters use phone-style multi-tap: tapping the same key again within a moment replaces the
 * letter just typed with the next one on that key (sent to the host as backspace + letter).
 * Rows hold 4, 4, 4 and 3 keys.
 */
class Keyboard {
public:
    enum class Layer { Letters, Numbers, Navigation };
    enum class Shift { Off, Once, Locked };
    enum class Action { MultiTap, Char, Key, Backspace, Enter, Space, Shift, Layer };

    struct KeyDef {
        const char* label;
        Action action;
        const char* chars = "";  // MultiTap letters or the Char to type
        uint8_t usage     = 0;   // Key
        Layer target      = Layer::Letters;
    };

    static constexpr int KeyCount        = 15;
    static constexpr uint32_t MultiTapMs = 900;

    const KeyDef& getKey(int index) const;
    // Label with the current shift state applied
    std::string getKeyLabel(int index) const;
    Layer getLayer() const
    {
        return _layer;
    }
    Shift getShift() const
    {
        return _shift;
    }

    // Returns the strokes to send to the host, in order
    std::vector<KeyStroke> press(int index, uint32_t nowMs);
    // Commits a pending multi-tap letter once it times out. Returns true if the preview changed.
    bool update(uint32_t nowMs);

    // Local echo of what has been typed, for the preview line
    const std::string& getText() const
    {
        return _text;
    }
    bool hasPendingLetter() const
    {
        return _pending_key >= 0;
    }

private:
    Layer _layer          = Layer::Letters;
    Shift _shift          = Shift::Off;
    std::string _text;
    int _pending_key      = -1;
    int _pending_index    = 0;
    bool _pending_upper   = false;
    uint32_t _pending_ms  = 0;

    void commitPending();
    void typeChar(char c, std::vector<KeyStroke>& out);
    void eraseChar(std::vector<KeyStroke>& out);
};

}  // namespace model
