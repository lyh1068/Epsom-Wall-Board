#pragma once

#include <stdbool.h>

/* A small control panel over every page, opened by swiping down: screen
 * brightness for now. Call under the LVGL lock. */
void controls_open(void);

bool controls_is_open(void);
