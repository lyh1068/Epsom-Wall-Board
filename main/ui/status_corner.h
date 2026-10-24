#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "lvgl.h"

/*
 * The status corner (PLAN.md §5.3, pulled forward because the screen is this
 * board's only log): clock, Wi-Fi, time sync and aircraft-feed health. Every
 * failure to start or to poll shows here.
 */
void status_corner_create(lv_obj_t *parent, int32_t width);

/* Call periodically from an LVGL timer. Only touches labels whose text changed. */
void status_corner_refresh(void);

/*
 * One-line summary for the Morning page's corner (PLAN.md §5.3): Wi-Fi
 * signal and last time sync. *problem is set if Wi-Fi is down, the clock is
 * unsynced, or any live source is failing or stale; the radar page's status
 * corner has the detail.
 */
void status_compact(char *buf, size_t size, bool *problem);
