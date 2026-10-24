#pragma once

#include "lvgl.h"

/* Weather page: now, next rain, and the next 24 hours of temperature and rain
 * chance. Call under the LVGL lock. */
void weather_page_create(lv_obj_t *parent);
