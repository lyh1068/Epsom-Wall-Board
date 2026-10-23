#pragma once

#include "lvgl.h"

/*
 * Radar page (PLAN.md §5.2): a 720x720 scope on the left, detail panel and
 * status corner on the right. Call under the LVGL lock, after aircraft_start().
 */
void radar_create(lv_obj_t *screen);
