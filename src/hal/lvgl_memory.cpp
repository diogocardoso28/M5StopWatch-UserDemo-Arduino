/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include <esp_heap_caps.h>
#include <lvgl.h>

#include <cstddef>

extern "C" void lv_mem_init() {}

extern "C" void lv_mem_deinit() {}

extern "C" lv_mem_pool_t lv_mem_add_pool(void*, std::size_t)
{
    return nullptr;
}

extern "C" void lv_mem_remove_pool(lv_mem_pool_t) {}

extern "C" void* lv_malloc_core(std::size_t size)
{
    void* memory = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return memory != nullptr ? memory : heap_caps_malloc(size, MALLOC_CAP_8BIT);
}

extern "C" void lv_free_core(void* memory)
{
    heap_caps_free(memory);
}

extern "C" void* lv_realloc_core(void* memory, std::size_t newSize)
{
    if (newSize == 0) {
        heap_caps_free(memory);
        return nullptr;
    }
    void* resized = heap_caps_realloc(memory, newSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return resized != nullptr ? resized : heap_caps_realloc(memory, newSize, MALLOC_CAP_8BIT);
}

extern "C" void lv_mem_monitor_core(lv_mem_monitor_t*) {}

extern "C" lv_result_t lv_mem_test_core()
{
    return LV_RESULT_OK;
}
