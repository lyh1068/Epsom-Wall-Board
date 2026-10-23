#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "data/weather_parse.h"
#include "esp_err.h"

/* Weather source: polls Open-Meteo on its own task; the UI copies it out. */

typedef struct {
    uint32_t seq;           /* bumps on every successful poll */
    time_t last_ok;         /* 0 if never */
    int http_status;
    esp_err_t last_err;
    uint32_t backoff_s;
    bool waiting_for_wifi;
} weather_meta_t;

void weather_start(void);

/* Returns false (and leaves *out alone) until the first successful poll. */
bool weather_get(weather_t *out, weather_meta_t *meta);
