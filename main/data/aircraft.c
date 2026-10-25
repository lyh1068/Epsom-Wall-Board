#include "aircraft.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "cJSON.h"
#include "config.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "net/http_json.h"
#include "data/home.h"
#include "net/wifi.h"

static const char *TAG = "aircraft";

#define MAX_BODY (512 * 1024)

static SemaphoreHandle_t s_mutex;
static adsb_aircraft_t *s_ac;           /* latest snapshot, PSRAM */
static adsb_aircraft_t *s_scratch;      /* parse target, PSRAM */
static aircraft_meta_t s_meta;
static volatile int s_range_nm = 20;

void aircraft_set_range_nm(int range_nm)
{
    s_range_nm = range_nm;
}

int aircraft_snapshot(adsb_aircraft_t *out, int max, aircraft_meta_t *meta)
{
    if (s_mutex == NULL) {
        if (meta) {
            memset(meta, 0, sizeof(*meta));
        }
        return 0;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    int n = s_meta.count < max ? s_meta.count : max;
    memcpy(out, s_ac, n * sizeof(*out));
    if (meta) {
        *meta = s_meta;
    }
    xSemaphoreGive(s_mutex);
    return n;
}

static void set_status(int http_status, esp_err_t err, uint32_t backoff_s, bool waiting)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_meta.http_status = http_status;
    s_meta.last_err = err;
    s_meta.backoff_s = backoff_s;
    s_meta.waiting_for_wifi = waiting;
    xSemaphoreGive(s_mutex);
}

/* cJSON makes many small allocations; keep them out of internal RAM. */
static void *psram_malloc(size_t size)
{
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
}

typedef enum { FEED_ADSB_FI, FEED_ADSB_LOL } feed_t;

static const char *const FEED_NAME[] = { "adsb.fi", "adsb.lol" };

/* Returns true on success. Both feeds serve the same readsb JSON. */
static bool poll_once(feed_t feed, http_json_t *http, int *http_status, esp_err_t *err)
{
    /* Only a rounded (~1 km) position leaves the board. */
    char url[128], where[32];
    int radius = (int)ceil(s_range_nm * RADAR_EDGE_FACTOR);
    if (feed == FEED_ADSB_FI) {
        home_query_coords(where, sizeof(where), "/lon/");
        snprintf(url, sizeof(url), "https://opendata.adsb.fi/api/v3/lat/%s/dist/%d", where, radius);
    } else {
        home_query_coords(where, sizeof(where), "/");
        snprintf(url, sizeof(url), "https://api.adsb.lol/v2/point/%s/%d", where, radius);
    }

    const char *body;
    size_t len;
    *err = http_json_get(http, url, &body, &len, http_status);
    if (*err != ESP_OK || *http_status != 200) {
        return false;
    }

    adsb_meta_t pm;
    int n = adsb_parse(body, len, s_scratch, ADSB_MAX_AIRCRAFT, &pm);
    if (n < 0) {
        *err = ESP_ERR_INVALID_RESPONSE;
        return false;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    adsb_aircraft_t *t = s_ac;
    s_ac = s_scratch;
    s_scratch = t;
    s_meta.count = n;
    s_meta.received = pm.total;
    s_meta.seq++;
    s_meta.fetched_us = esp_timer_get_time();
    s_meta.last_ok = time(NULL);
    s_meta.source = FEED_NAME[feed];
    s_meta.fallback = feed != FEED_ADSB_FI;
    xSemaphoreGive(s_mutex);
    ESP_LOGI(TAG, "%s: %d aircraft (%d received, %u bytes)", FEED_NAME[feed], n, pm.total, (unsigned)len);
    return true;
}

static void aircraft_task(void *arg)
{
    /* One kept-alive connection per feed. */
    http_json_t *fi = http_json_new(MAX_BODY), *lol = http_json_new(MAX_BODY);
    uint32_t backoff_s = ADSB_POLL_S;
    int64_t lol_rest_until = 0;

    for (;;) {
        wifi_status_t ws;
        wifi_get_status(&ws);
        if (ws.state != WIFI_ST_CONNECTED) {
            set_status(0, ESP_OK, 0, true);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        int http_status = 0;
        esp_err_t err = ESP_OK;
        bool ok = fi != NULL && poll_once(FEED_ADSB_FI, fi, &http_status, &err);
        if (!ok) {
            ESP_LOGW(TAG, "adsb.fi failed: HTTP %d, %s", http_status, esp_err_to_name(err));
            /* Fall back to adsb.lol, unless its daily allowance is used up. */
            if (lol != NULL && esp_timer_get_time() >= lol_rest_until) {
                int st = 0;
                esp_err_t e = ESP_OK;
                ok = poll_once(FEED_ADSB_LOL, lol, &st, &e);
                if (!ok && st == 429) {
                    lol_rest_until = esp_timer_get_time() + (int64_t)ADSB_LOL_REST_S * 1000000;
                }
            }
        }
        if (ok) {
            backoff_s = ADSB_POLL_S;
            http_status = 200;
            err = ESP_OK;
        } else {
            /* Back off on any failure: 429 and 5xx especially (PLAN.md §4). */
            if (fi == NULL) {
                err = ESP_ERR_NO_MEM;
            }
            backoff_s = backoff_s * 2 > ADSB_BACKOFF_MAX_S ? ADSB_BACKOFF_MAX_S : backoff_s * 2;
        }
        set_status(http_status, err, backoff_s, false);
        vTaskDelay(pdMS_TO_TICKS(backoff_s * 1000));
    }
}

void aircraft_start(void)
{
    home_origin();      /* initialise before any task reads it */

    cJSON_Hooks hooks = { .malloc_fn = psram_malloc, .free_fn = free };
    cJSON_InitHooks(&hooks);

    s_mutex = xSemaphoreCreateMutex();
    s_ac = heap_caps_calloc(ADSB_MAX_AIRCRAFT, sizeof(adsb_aircraft_t), MALLOC_CAP_SPIRAM);
    s_scratch = heap_caps_calloc(ADSB_MAX_AIRCRAFT, sizeof(adsb_aircraft_t), MALLOC_CAP_SPIRAM);
    if (s_mutex == NULL || s_ac == NULL || s_scratch == NULL) {
        ESP_LOGE(TAG, "out of memory; aircraft source not started");
        return;
    }
    /* TLS handshakes are stack-hungry. */
    /* Stack in PSRAM: internal RAM is reserved for Wi-Fi and the display. This
     * task never writes flash, which a PSRAM-stacked task must not do. */
    xTaskCreateWithCaps(aircraft_task, "aircraft", 12288, NULL, 4, NULL, MALLOC_CAP_SPIRAM);
}
