#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "data/adsb_parse.h"
#include "esp_err.h"
#include "geo/geo.h"

/*
 * Aircraft source: polls adsb.lol on its own task and keeps the latest
 * snapshot. The UI copies it out with aircraft_snapshot() and never does
 * networking itself.
 */

typedef struct {
    uint32_t seq;           /* bumps on every successful poll */
    int64_t fetched_us;     /* esp_timer time of that poll */
    int count;              /* aircraft in the snapshot */
    int received;           /* aircraft in the response, before truncation */
    time_t last_ok;         /* wall-clock time of the last success, 0 if never */
    int http_status;        /* last HTTP status, 0 if the request itself failed */
    esp_err_t last_err;     /* last transport/parse error, ESP_OK if none */
    uint32_t backoff_s;     /* current wait before the next poll */
    bool waiting_for_wifi;
} aircraft_meta_t;

void aircraft_start(void);

/* The radius polled follows the radar range (range x edge factor). */
void aircraft_set_range_nm(int range_nm);

/* Copy up to max aircraft into out; returns how many. meta may be NULL. */
int aircraft_snapshot(adsb_aircraft_t *out, int max, aircraft_meta_t *meta);
