#pragma once

#include "lvgl.h"

/* Buses page: every TfL stop within 300 m of Epsom station, nearest first,
 * with its next arrivals counting down. Call under the LVGL lock. */
void bus_page_create(lv_obj_t *parent);
