#include "geo.h"

#include <math.h>

#define DEG2RAD (M_PI / 180.0)
#define RAD2DEG (180.0 / M_PI)
#define EARTH_RADIUS_NM 3440.065

void geo_origin_init(geo_origin_t *o, double lat, double lon)
{
    o->lat = lat;
    o->lon = lon;
    o->cos_lat = cos(lat * DEG2RAD);
}

void geo_project(const geo_origin_t *o, double lat, double lon, double *x_nm, double *y_nm)
{
    *x_nm = (lon - o->lon) * o->cos_lat * 60.0;
    *y_nm = (lat - o->lat) * 60.0;
}

double geo_distance_nm(double lat1, double lon1, double lat2, double lon2)
{
    double dlat = (lat2 - lat1) * DEG2RAD;
    double dlon = (lon2 - lon1) * DEG2RAD;
    double a = sin(dlat / 2) * sin(dlat / 2)
               + cos(lat1 * DEG2RAD) * cos(lat2 * DEG2RAD) * sin(dlon / 2) * sin(dlon / 2);
    return 2.0 * EARTH_RADIUS_NM * atan2(sqrt(a), sqrt(1.0 - a));
}

double geo_bearing_deg(double lat1, double lon1, double lat2, double lon2)
{
    double p1 = lat1 * DEG2RAD, p2 = lat2 * DEG2RAD;
    double dlon = (lon2 - lon1) * DEG2RAD;
    double y = sin(dlon) * cos(p2);
    double x = cos(p1) * sin(p2) - sin(p1) * cos(p2) * cos(dlon);
    double b = atan2(y, x) * RAD2DEG;
    return fmod(b + 360.0, 360.0);
}

void geo_dead_reckon(double *x_nm, double *y_nm, double gs_kt, double track_deg, double dt_s)
{
    double d = gs_kt * dt_s / 3600.0;
    *x_nm += d * sin(track_deg * DEG2RAD);
    *y_nm += d * cos(track_deg * DEG2RAD);
}

const char *geo_compass16(double bearing_deg)
{
    static const char *const names[16] = {
        "N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
        "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW",
    };
    int i = (int)floor(fmod(bearing_deg + 360.0 + 11.25, 360.0) / 22.5);
    return names[i & 15];
}
