/*
 * lv_mem_p4.h — аллокатор LVGL для JC8012P4A1C.
 * Объекты/стили/слои LVGL живут в PSRAM: на 1280×800 буферы полупрозрачных слоёв
 * в разы больше, чем на 4.3", а фиксированный пул 48 КБ во внутренней RAM
 * исчерпывался → LV_ASSERT → зависание экрана.
 */
#pragma once
#include <stddef.h>
#include "esp_heap_caps.h"

static inline void* lv_p4_malloc(size_t size)
{
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

static inline void* lv_p4_realloc(void* p, size_t size)
{
    return heap_caps_realloc(p, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

static inline void lv_p4_free(void* p)
{
    heap_caps_free(p);
}
