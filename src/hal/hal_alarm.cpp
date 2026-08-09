/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"

#include <apps/common/audio/audio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mooncake_log.h>

#include <mutex>

namespace {

constexpr std::string_view tag = "HAL-Alarm";

class AlarmController {
public:
    void start()
    {
        ensureTask();
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _active = true;
        }
        if (_task) {
            xTaskNotifyGive(_task);
        }
    }

    void stop()
    {
        ensureTask();
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _active = false;
        }
        GetHAL().stopVibrate();
        std::vector<int16_t> empty;
        GetHAL().audioPlay(empty, true);
        if (_task) {
            xTaskNotifyGive(_task);
        }
    }

private:
    void ensureTask()
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (!_task) {
            const BaseType_t result =
                xTaskCreate([](void* context) { static_cast<AlarmController*>(context)->run(); }, "alarm", 4 * 1024,
                            this, 5, &_task);
            if (result != pdPASS) {
                _task = nullptr;
                mclog::tagError(tag, "failed to create alarm task");
            }
        }
    }

    void run()
    {
        std::size_t beepIndex = 0;
        for (;;) {
            bool active = false;
            {
                std::lock_guard<std::mutex> lock(_mutex);
                active = _active;
            }
            if (!active) {
                beepIndex = 0;
                ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
                continue;
            }

            audio::play_tone(1760, 0.07f);
            GetHAL().vibrate(70, 90);
            beepIndex = (beepIndex + 1) % 4;
            ulTaskNotifyTake(pdTRUE, beepIndex == 0 ? pdMS_TO_TICKS(720) : pdMS_TO_TICKS(180));
        }
    }

    std::mutex _mutex;
    TaskHandle_t _task = nullptr;
    bool _active = false;
};

AlarmController controller;

}  // namespace

void Hal::startAlarm()
{
    controller.start();
}

void Hal::stopAlarm()
{
    controller.stop();
}
