/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"

#include <Preferences.h>
#include <atomic>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <mooncake_log.h>

#include <algorithm>
#include <mutex>

namespace {

constexpr std::string_view tag = "HAL-Display";
constexpr int logicalDisplayWidth = 468;
constexpr int logicalDisplayHeight = 466;
constexpr int drawBufferLines = 60;
SemaphoreHandle_t guiMutex = nullptr;
std::atomic<bool> lvglUpdatesEnabled{false};
LGFX_Sprite canvas(&M5.Display);
TaskHandle_t lvglTaskHandle = nullptr;
std::mutex inputMutex;
Hal::TouchPoint cachedTouch;

void flushDisplay(lv_display_t* display, const lv_area_t* area, uint8_t* pixels)
{
    const uint32_t width = area->x2 - area->x1 + 1;
    const uint32_t height = area->y2 - area->y1 + 1;
    M5.Display.startWrite();
    M5.Display.setAddrWindow(area->x1, area->y1, width, height);
    // Preserve the upstream workaround for M5GFX's large RGB565 fast-copy
    // path: feed bounded chunks even though LVGL's buffer can be larger.
    constexpr uint32_t safeChunkPixels = 8192;
    const auto* source = reinterpret_cast<const lgfx::rgb565_t*>(pixels);
    uint32_t remaining = width * height;
    uint32_t offset = 0;
    while (remaining > 0) {
        const uint32_t chunk = std::min(remaining, safeChunkPixels);
        M5.Display.writePixels(source + offset, chunk);
        offset += chunk;
        remaining -= chunk;
    }
    M5.Display.endWrite();
    lv_display_flush_ready(display);
}

void readTouch(lv_indev_t*, lv_indev_data_t* data)
{
    const auto point = GetHAL().getTouchPoint();
    if (point.num <= 0) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }
    data->state = LV_INDEV_STATE_PRESSED;
    data->point.x = point.x;
    data->point.y = point.y;
}

void lvglTask(void*)
{
    for (;;) {
        if (lvglUpdatesEnabled.load() && xSemaphoreTake(guiMutex, portMAX_DELAY) == pdTRUE) {
            lv_timer_handler();
            xSemaphoreGive(guiMutex);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void* allocateDrawBuffer(std::size_t bytes)
{
    return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

}  // namespace

bool Hal::displayInit()
{
    if (M5.Display.width() < logicalDisplayWidth || M5.Display.height() < logicalDisplayHeight) {
        mclog::tagError(tag, "unexpected AMOLED geometry: {}x{}", M5.Display.width(), M5.Display.height());
        return false;
    }
    if (ESP.getPsramSize() == 0) {
        mclog::tagError(tag, "OPI PSRAM is not available");
        return false;
    }
    const int brightness = getBackLightBrightness(true);
    setBackLightBrightness(brightness, false);
    M5.Display.fillScreen(TFT_BLACK);
    return true;
}

LGFX_Device& Hal::getDisplay()
{
    return M5.Display;
}

LGFX_Sprite& Hal::getCanvas()
{
    return canvas;
}

void Hal::updateCanvas()
{
    canvas.pushSprite(0, 0);
}

void Hal::setBackLightBrightness(int brightness, bool saveToSettings)
{
    _brightness = std::clamp(brightness, 0, 100);
    M5.Display.setBrightness(static_cast<uint8_t>((_brightness * 255) / 100));
    if (saveToSettings) {
        Preferences settings;
        if (settings.begin(SettingsNs.data(), false)) {
            settings.putInt("bl_lev", _brightness);
            settings.end();
        }
    }
}

int Hal::getBackLightBrightness(bool loadFromSettings)
{
    if (loadFromSettings) {
        Preferences settings;
        if (settings.begin(SettingsNs.data(), true)) {
            _brightness = std::clamp(settings.getInt("bl_lev", 80), 10, 100);
            settings.end();
        }
    }
    return _brightness;
}

Hal::TouchPoint Hal::getTouchPoint()
{
    std::lock_guard<std::mutex> lock(inputMutex);
    return cachedTouch;
}

void Hal::updateM5State()
{
    std::lock_guard<std::mutex> lock(inputMutex);
    M5.update();
    cachedTouch = {};
    cachedTouch.num = M5.Touch.getCount();
    if (cachedTouch.num > 0) {
        const auto& detail = M5.Touch.getDetail(0);
        cachedTouch.x = std::clamp<int>(detail.x, 0, logicalDisplayWidth - 1);
        cachedTouch.y = std::clamp<int>(detail.y, 0, logicalDisplayHeight - 1);
    }
}

bool Hal::lvglInit()
{
    lv_init();
    lv_tick_set_cb([]() -> uint32_t { return ::millis(); });

    auto* display = lv_display_create(logicalDisplayWidth, logicalDisplayHeight);
    if (!display) {
        mclog::tagError(tag, "lv_display_create failed");
        return false;
    }
    lv_display_set_flush_cb(display, flushDisplay);

    const std::size_t bufferBytes = logicalDisplayWidth * drawBufferLines * sizeof(lv_color_t);
    void* buffer1 = allocateDrawBuffer(bufferBytes);
    void* buffer2 = allocateDrawBuffer(bufferBytes);
    if (!buffer1 || !buffer2) {
        mclog::tagError(tag, "LVGL draw buffer allocation failed: {} bytes each", bufferBytes);
        if (buffer1) {
            heap_caps_free(buffer1);
        }
        if (buffer2) {
            heap_caps_free(buffer2);
        }
        return false;
    }
    lv_display_set_buffers(display, buffer1, buffer2, bufferBytes, LV_DISPLAY_RENDER_MODE_PARTIAL);

    lvTouchpad = lv_indev_create();
    if (!lvTouchpad) {
        mclog::tagError(tag, "lv_indev_create failed");
        return false;
    }
    lv_indev_set_type(lvTouchpad, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(lvTouchpad, readTouch);
    lv_indev_set_display(lvTouchpad, display);

    guiMutex = xSemaphoreCreateMutex();
    if (!guiMutex) {
        mclog::tagError(tag, "GUI mutex creation failed");
        lvTouchpad = nullptr;
        return false;
    }

    if (xTaskCreate(lvglTask, "lvgl", 16 * 1024, nullptr, 2, &lvglTaskHandle) != pdPASS) {
        mclog::tagError(tag, "LVGL task creation failed");
        lvTouchpad = nullptr;
        return false;
    }
    startLvglUpdate();

    {
        LvglLockGuard lock;
        uitk::lvgl_cpp::ScreenActive screen;
        screen.setBgColor(lv_color_black());
        bootLogo = std::make_unique<BootLogo>();
    }
    return true;
}

bool Hal::lvglLock()
{
    return guiMutex && xSemaphoreTake(guiMutex, portMAX_DELAY) == pdTRUE;
}

void Hal::lvglUnlock()
{
    if (guiMutex) {
        xSemaphoreGive(guiMutex);
    }
}

void Hal::startLvglUpdate()
{
    lvglUpdatesEnabled.store(true);
}

void Hal::stopLvglUpdate()
{
    lvglUpdatesEnabled.store(false);
}
