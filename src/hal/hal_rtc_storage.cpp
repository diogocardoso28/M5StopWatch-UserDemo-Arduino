/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"

#include <Preferences.h>
#include <mooncake_log.h>
#include <nvs.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <sys/time.h>

namespace {

constexpr std::string_view rtcTag = "HAL-RTC";
constexpr std::string_view alarmTag = "HAL-AlarmStorage";
constexpr const char* alarmNamespace = "alarm";
constexpr const char* alarmStorageKey = "storage";
bool getLocalTimeValue(std::tm& value)
{
    const std::time_t now = std::time(nullptr);
    return localtime_r(&now, &value) != nullptr;
}

bool applyLocalTime(std::tm& value)
{
    const std::time_t timestamp = std::mktime(&value);
    if (timestamp < 0) {
        return false;
    }
    timeval tv{timestamp, 0};
    if (settimeofday(&tv, nullptr) != 0) {
        return false;
    }
    GetHAL().syncSystemTimeToRtc();
    return true;
}

}  // namespace

bool Hal::rtcInit()
{
    const std::string timezone = getTimezone();
    setenv("TZ", timezone.c_str(), 1);
    tzset();

    if (!M5.Rtc.isEnabled()) {
        mclog::tagError(rtcTag, "RX8130 is not available");
        return false;
    }
    m5::rtc_datetime_t rtc;
    if (!M5.Rtc.getDateTime(&rtc)) {
        mclog::tagError(rtcTag, "failed to read RX8130 during initialization");
        return false;
    }
    syncRtcTimeToSystem();
    return true;
}

void Hal::syncRtcTimeToSystem()
{
    if (!M5.Rtc.isEnabled()) {
        return;
    }

    m5::rtc_datetime_t rtc;
    if (!M5.Rtc.getDateTime(&rtc)) {
        mclog::tagError(rtcTag, "failed to read RTC");
        return;
    }

    std::tm utc{};
    rtc.set_tm(&utc);
    const char* previousTz = getenv("TZ");
    const std::string savedTz = previousTz ? previousTz : "";
    setenv("TZ", "UTC0", 1);
    tzset();
    const std::time_t timestamp = std::mktime(&utc);
    if (savedTz.empty()) {
        unsetenv("TZ");
    } else {
        setenv("TZ", savedTz.c_str(), 1);
    }
    tzset();

    if (timestamp >= 0) {
        timeval tv{timestamp, 0};
        settimeofday(&tv, nullptr);
    }
}

void Hal::syncSystemTimeToRtc()
{
    if (!M5.Rtc.isEnabled()) {
        return;
    }
    const std::time_t now = std::time(nullptr);
    std::tm utc{};
    if (gmtime_r(&now, &utc)) {
        M5.Rtc.setDateTime(&utc);
    }
}

void Hal::setTimezone(std::string_view timezone)
{
    const std::string owned(timezone);
    setenv("TZ", owned.c_str(), 1);
    tzset();

    Preferences settings;
    if (settings.begin(SettingsNs.data(), false)) {
        settings.putString("tz", owned.c_str());
        settings.end();
    }
}

std::string Hal::getTimezone()
{
    Preferences settings;
    if (!settings.begin(SettingsNs.data(), true)) {
        return "GMT0";
    }
    const String value = settings.getString("tz", "GMT0");
    settings.end();
    return std::string(value.c_str());
}

DateYmd Hal::getDateYmd()
{
    std::tm local{};
    if (!getLocalTimeValue(local)) {
        return {};
    }
    return DateYmd{static_cast<uint16_t>(local.tm_year + 1900), static_cast<uint8_t>(local.tm_mon + 1),
                   static_cast<uint8_t>(local.tm_mday)};
}

bool Hal::setDateYmd(const DateYmd& date)
{
    if (!date.isValid()) {
        return false;
    }
    std::tm local{};
    if (!getLocalTimeValue(local)) {
        return false;
    }
    local.tm_year = static_cast<int>(date.year) - 1900;
    local.tm_mon = static_cast<int>(date.month) - 1;
    local.tm_mday = static_cast<int>(date.day);
    local.tm_isdst = -1;
    return applyLocalTime(local);
}

TimeHms Hal::getTimeHms()
{
    std::tm local{};
    if (!getLocalTimeValue(local)) {
        return {};
    }
    return TimeHms{static_cast<uint8_t>(local.tm_hour), static_cast<uint8_t>(local.tm_min),
                   static_cast<uint8_t>(local.tm_sec)};
}

bool Hal::setTimeHms(const TimeHms& time)
{
    if (!time.isValid()) {
        return false;
    }
    std::tm local{};
    if (!getLocalTimeValue(local)) {
        return false;
    }
    local.tm_hour = time.hour;
    local.tm_min = time.minute;
    local.tm_sec = time.second;
    local.tm_isdst = -1;
    return applyLocalTime(local);
}

bool Hal::loadAlarmStorage(AlarmStorageSnapshot& snapshot)
{
    snapshot = stopwatch_core::makeDefaultAlarmSnapshot();

    nvs_handle_t handle = 0;
    esp_err_t result = nvs_open(alarmNamespace, NVS_READONLY, &handle);
    if (result == ESP_ERR_NVS_NOT_FOUND) {
        mclog::tagInfo(alarmTag, "alarm storage missing, using defaults");
        return true;
    }
    if (result != ESP_OK) {
        mclog::tagError(alarmTag, "failed to open alarm storage: {}", esp_err_to_name(result));
        return false;
    }

    std::size_t size = 0;
    result = nvs_get_blob(handle, alarmStorageKey, nullptr, &size);
    if (result == ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        mclog::tagInfo(alarmTag, "alarm storage missing, using defaults");
        return true;
    }
    if (result != ESP_OK) {
        nvs_close(handle);
        mclog::tagError(alarmTag, "failed to query alarm storage blob: {}", esp_err_to_name(result));
        return false;
    }
    if (size != sizeof(AlarmStorageSnapshot)) {
        nvs_close(handle);
        mclog::tagWarn(alarmTag, "invalid alarm storage size: {}, expected: {}", size,
                       sizeof(AlarmStorageSnapshot));
        return true;
    }

    AlarmStorageSnapshot raw{};
    size = sizeof(raw);
    result = nvs_get_blob(handle, alarmStorageKey, &raw, &size);
    nvs_close(handle);
    if (result != ESP_OK) {
        mclog::tagError(alarmTag, "failed to read alarm storage blob: {}", esp_err_to_name(result));
        return false;
    }

    bool changed = false;
    snapshot = stopwatch_core::sanitizeAlarmSnapshot(raw, changed);
    if (changed) {
        mclog::tagWarn(alarmTag, "alarm storage repaired during load, count: {}", snapshot.count);
    }
    return true;
}

bool Hal::saveAlarmStorage(const AlarmStorageSnapshot& snapshot)
{
    bool changed = false;
    const AlarmStorageSnapshot sanitized = stopwatch_core::sanitizeAlarmSnapshot(snapshot, changed);

    nvs_handle_t handle = 0;
    esp_err_t result = nvs_open(alarmNamespace, NVS_READWRITE, &handle);
    if (result != ESP_OK) {
        mclog::tagError(alarmTag, "failed to open alarm storage for write: {}", esp_err_to_name(result));
        return false;
    }

    result = nvs_set_blob(handle, alarmStorageKey, &sanitized, sizeof(sanitized));
    if (result == ESP_OK) {
        result = nvs_commit(handle);
    }
    nvs_close(handle);
    if (result != ESP_OK) {
        mclog::tagError(alarmTag, "failed to write alarm storage blob: {}", esp_err_to_name(result));
        return false;
    }

    if (changed) {
        mclog::tagWarn(alarmTag, "alarm storage sanitized before save, count: {}", sanitized.count);
    }
    return true;
}
