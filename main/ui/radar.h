#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

/*
 * Radar page (PLAN.md §5.2): a 720x720 scope on the left, detail panel and
 * status corner on the right. Call under the LVGL lock, after aircraft_start().
 */
void radar_create(lv_obj_t *parent);

/* The nearest airborne aircraft within OVERHEAD_NM, dead-reckoned to now. */
typedef struct {
    char hex[8];
    char flight[9];
    char type[5];
    bool has_alt, has_rate;
    int32_t alt_baro, baro_rate;
    double dist_nm, bearing_deg;
} radar_target_t;

/* False when nothing is overhead. Call under the LVGL lock. */
bool radar_overhead(radar_target_t *out);

/* Select an aircraft by hex, as if tapped. Call under the LVGL lock. */
void radar_select_hex(const char *hex);
