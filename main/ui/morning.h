#pragma once

#include "lvgl.h"

/*
 * Morning page (PLAN.md §5.1): departures, leave-by bar, clock, weather, tube
 * status and the overhead chip. Call under the LVGL lock.
 */
void morning_create(lv_obj_t *parent);
