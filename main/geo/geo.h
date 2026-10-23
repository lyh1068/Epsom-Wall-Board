#pragma once

/*
 * Flat-earth geometry around home. Pure C, no ESP-IDF, so it host-tests.
 * Accurate to well under 1% within the radar's 40 nm (PLAN.md §5.2).
 */

typedef struct {
    double lat, lon;        /* degrees */
    double cos_lat;         /* cached cos(lat) */
} geo_origin_t;

void geo_origin_init(geo_origin_t *o, double lat, double lon);

/* Equirectangular projection: nautical miles east (x) and north (y) of the origin. */
void geo_project(const geo_origin_t *o, double lat, double lon, double *x_nm, double *y_nm);

/* Great-circle distance in nautical miles (haversine). */
double geo_distance_nm(double lat1, double lon1, double lat2, double lon2);

/* Initial great-circle bearing from point 1 to point 2, degrees true, 0..360. */
double geo_bearing_deg(double lat1, double lon1, double lat2, double lon2);

/* Advance a projected position by ground speed (knots) along a track (degrees true). */
void geo_dead_reckon(double *x_nm, double *y_nm, double gs_kt, double track_deg, double dt_s);

/* "N", "NNE", ... for a bearing in degrees. */
const char *geo_compass16(double bearing_deg);
