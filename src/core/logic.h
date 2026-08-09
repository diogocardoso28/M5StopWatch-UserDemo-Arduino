/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>

struct AlarmStorageEntry {
    uint8_t hour = 0;
    uint8_t minute = 0;
    uint8_t enabled = 0;
    uint8_t reserved = 0;

    bool isValid() const { return hour < 24 && minute < 60 && enabled <= 1; }
};

struct AlarmStorageSnapshot {
    static constexpr std::size_t maxAlarmCount = 16;

    uint8_t version = 1;
    uint8_t count = 0;
    uint16_t reserved = 0;
    std::array<AlarmStorageEntry, maxAlarmCount> alarms{};
};

struct TimeHms {
    uint8_t hour = 0;
    uint8_t minute = 0;
    uint8_t second = 0;

    bool isValid() const { return hour < 24 && minute < 60 && second < 60; }
};

struct DateYmd {
    uint16_t year = 2026;
    uint8_t month = 1;
    uint8_t day = 1;

    static bool isLeapYear(uint16_t value)
    {
        return (value % 4 == 0 && value % 100 != 0) || value % 400 == 0;
    }

    static uint8_t daysInMonth(uint16_t valueYear, uint8_t valueMonth)
    {
        static constexpr uint8_t monthDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        if (valueMonth < 1 || valueMonth > 12) {
            return 0;
        }
        return valueMonth == 2 && isLeapYear(valueYear) ? 29 : monthDays[valueMonth - 1];
    }

    bool isValid() const
    {
        const uint8_t maximumDay = daysInMonth(year, month);
        return year >= 2000 && year <= 2099 && maximumDay != 0 && day >= 1 && day <= maximumDay;
    }
};

namespace stopwatch_core {

inline uint8_t batteryMillivoltsToPercent(uint16_t millivolts)
{
    constexpr uint16_t emptyMillivolts = 3300;
    constexpr uint16_t fullMillivolts = 4200;
    if (millivolts <= emptyMillivolts) {
        return 0;
    }
    if (millivolts >= fullMillivolts) {
        return 100;
    }
    return static_cast<uint8_t>((static_cast<uint32_t>(millivolts - emptyMillivolts) * 100U) /
                                (fullMillivolts - emptyMillivolts));
}

inline uint16_t filterBatteryMillivolts(uint16_t previous, uint16_t current)
{
    return previous == 0 ? current
                         : static_cast<uint16_t>((static_cast<uint32_t>(previous) * 7U + current + 4U) / 8U);
}

inline AlarmStorageSnapshot makeDefaultAlarmSnapshot()
{
    AlarmStorageSnapshot snapshot;
    snapshot.version = 1;
    snapshot.count = 0;
    snapshot.reserved = 0;
    snapshot.alarms.fill({});
    return snapshot;
}

inline AlarmStorageSnapshot sanitizeAlarmSnapshot(const AlarmStorageSnapshot& source, bool& changed)
{
    AlarmStorageSnapshot result = makeDefaultAlarmSnapshot();
    if (source.version != 1) {
        changed = true;
        return result;
    }

    changed = source.reserved != 0;
    const std::size_t count = std::min<std::size_t>(source.count, AlarmStorageSnapshot::maxAlarmCount);
    changed = changed || count != source.count;
    for (std::size_t index = 0; index < count; ++index) {
        const auto& entry = source.alarms[index];
        if (!entry.isValid()) {
            changed = true;
            continue;
        }
        auto& output = result.alarms[result.count++];
        output.hour = entry.hour;
        output.minute = entry.minute;
        output.enabled = entry.enabled ? 1 : 0;
        output.reserved = 0;
        changed = changed || entry.reserved != 0;
    }
    return result;
}

template <std::size_t SlotCount>
std::size_t findAvailableBadgeSlot(const std::array<bool, SlotCount>& available, std::size_t current, int direction)
{
    if (direction == 0 || SlotCount == 0) {
        return SlotCount;
    }
    for (std::size_t step = 1; step <= SlotCount; ++step) {
        const int wrapped = (static_cast<int>(current) + direction * static_cast<int>(step) +
                             static_cast<int>(SlotCount)) %
                            static_cast<int>(SlotCount);
        if (available[static_cast<std::size_t>(wrapped)]) {
            return static_cast<std::size_t>(wrapped);
        }
    }
    return SlotCount;
}

template <std::size_t Size>
void radix2Fft(std::array<std::complex<float>, Size>& values)
{
    static_assert(Size > 1 && (Size & (Size - 1)) == 0, "FFT size must be a power of two");
    for (std::size_t i = 1, j = 0; i < Size; ++i) {
        std::size_t bit = Size >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            std::swap(values[i], values[j]);
        }
    }

    constexpr float pi = 3.14159265358979323846f;
    for (std::size_t length = 2; length <= Size; length <<= 1) {
        const float angle = -2.0f * pi / static_cast<float>(length);
        const std::complex<float> step(std::cos(angle), std::sin(angle));
        for (std::size_t start = 0; start < Size; start += length) {
            std::complex<float> rotation(1.0f, 0.0f);
            for (std::size_t offset = 0; offset < length / 2; ++offset) {
                const auto even = values[start + offset];
                const auto odd = values[start + offset + length / 2] * rotation;
                values[start + offset] = even + odd;
                values[start + offset + length / 2] = even - odd;
                rotation *= step;
            }
        }
    }
}

}  // namespace stopwatch_core
