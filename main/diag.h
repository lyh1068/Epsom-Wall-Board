#pragma once

#include "lvgl.h"

/*
 * On-screen diagnostics. The Tab5's serial port cannot be opened without
 * stopping the app (see the tab5-board skill), so the screen is the readout.
 */

/* Call around bsp_display_start(): records the BSP's panel detection log line. */
void diag_capture_begin(void);
void diag_capture_end(void);

/* Draws the panel type, clock, time-sync and Wi-Fi status, and a live touch
 * readout on the top layer. Call under the LVGL lock. */
void diag_overlay_create(lv_indev_t *touch);
