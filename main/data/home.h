#pragma once

#include <stddef.h>
#include "geo/geo.h"

/* Home position: HOME_LAT/HOME_LON from .env, else Epsom station (config.h). */
const geo_origin_t *home_origin(void);

/* "51.33/-0.27": rounded to ~1 km, the only form of home sent to any API. */
void home_query_coords(char *buf, size_t size, const char *sep);
