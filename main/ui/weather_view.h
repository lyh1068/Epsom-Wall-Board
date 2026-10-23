#pragma once

#include "lvgl.h"

/*
 * Weather block (PLAN.md §5.1): temperature and conditions, next rain, and a
 * 12-hour temperature sparkline. Lives on the radar panel until the Morning
 * page exists.
 */
void weather_view_create(lv_obj_t *parent, int32_t x, int32_t y, int32_t w);

/* Cheap to call often: only redraws on new data or a new minute. */
void weather_view_refresh(void);
