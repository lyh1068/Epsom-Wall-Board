#pragma once

#include "lvgl.h"

/* Sky page: the whole sky over home right now, day or night, with the Sun,
 * Moon, planets, bright stars and constellation figures. Call under the LVGL lock. */
void sky_page_create(lv_obj_t *parent);
