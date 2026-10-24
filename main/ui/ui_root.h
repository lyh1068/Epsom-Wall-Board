#pragma once

#include "lvgl.h"

/*
 * Top level: two full-screen pages in a horizontal tileview (Morning, Radar),
 * swiped between, with page dots at the bottom (PLAN.md §5). Call under the
 * LVGL lock, after the data sources have started.
 */
void ui_root_create(lv_obj_t *screen);

void ui_root_show_radar(void);
