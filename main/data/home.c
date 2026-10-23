#include "home.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include "config.h"
#include "wallboard_secrets.h"

const geo_origin_t *home_origin(void)
{
    static geo_origin_t home;
    static bool init;
    if (!init) {
#if HOME_LAT_SET && HOME_LON_SET
        geo_origin_init(&home, atof(HOME_LAT), atof(HOME_LON));
#else
        geo_origin_init(&home, HOME_LAT_DEFAULT, HOME_LON_DEFAULT);
#endif
        init = true;
    }
    return &home;
}

void home_query_coords(char *buf, size_t size, const char *sep)
{
    const geo_origin_t *h = home_origin();
    snprintf(buf, size, "%.2f%s%.2f", h->lat, sep, h->lon);
}
