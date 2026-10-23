#pragma once

#include "lvgl.h"

/*
 * Radar glyphs, rasterised once at boot into PSRAM: white with antialiased
 * alpha, pointing north, so they can be recoloured by altitude and rotated
 * to the track. All are ICON_SIZE x ICON_SIZE.
 */
#define ICON_SIZE 32

typedef struct {
    const lv_image_dsc_t *plane;    /* aircraft with a known track */
    const lv_image_dsc_t *blip;     /* aircraft with no track: nothing to rotate */
    const lv_image_dsc_t *arrow;    /* out of range, at the scope edge */
} radar_icons_t;

/* NULL members if PSRAM allocation failed. */
radar_icons_t icons_create(void);
