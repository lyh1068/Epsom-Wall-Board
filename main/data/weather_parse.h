#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

/*
 * Parser for Open-Meteo /v1/forecast, requested with timeformat=unixtime so
 * no local-time strings (or BST changeovers) are involved. Pure C + cJSON,
 * host-tested against test/fixtures/open_meteo_forecast.json.
 */

#define WX_HOURS 24

typedef struct {
    time_t time;            /* current conditions, epoch seconds */
    float temp_c;
    int code;               /* WMO weather code */
    bool is_day;
    int cloud;              /* current cloud cover %, -1 if not reported */
    int hours;              /* valid entries below */
    time_t h_time[WX_HOURS];
    float h_temp[WX_HOURS];
    int8_t h_pop[WX_HOURS]; /* precipitation probability %, -1 if null */
    int16_t h_code[WX_HOURS];
} weather_t;

bool weather_parse(const char *json, size_t len, weather_t *out);

/*
 * Index of the first hour, starting with the one containing now, whose
 * precipitation probability is at least threshold_pct, looking at most
 * horizon_h hours ahead. -1 if none.
 */
int weather_next_rain(const weather_t *w, time_t now, int threshold_pct, int horizon_h);

/* Index of the hour containing now, or -1 if the forecast does not cover it. */
int weather_hour_index(const weather_t *w, time_t now);

/* Short description of a WMO weather code, e.g. "Partly cloudy". */
const char *weather_code_text(int code);
