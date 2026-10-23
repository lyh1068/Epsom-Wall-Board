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

/* Returns true on success. */
static bool poll_once(http_json_t *http, int *http_status, esp_err_t *err)
{
    /* Only a rounded (~1 km) position leaves the board. */
    char url[128], where[32];
    int radius = (int)ceil(s_range_nm * RADAR_EDGE_FACTOR);
    home_query_coords(where, sizeof(where), "/");
    snprintf(url, sizeof(url), "https://api.adsb.lol/v2/point/%s/%d", where, radius);

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
    xSemaphoreGive(s_mutex);
    ESP_LOGI(TAG, "%d aircraft (%d received, %u bytes)", n, pm.total, (unsigned)len);
    return true;
}

static void aircraft_task(void *arg)
{
    http_json_t *http = http_json_new(MAX_BODY);
    uint32_t backoff_s = ADSB_POLL_S;

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
        if (http != NULL && poll_once(http, &http_status, &err)) {
            backoff_s = ADSB_POLL_S;
        } else {
            /* Back off on any failure: 429 and 5xx especially (PLAN.md §4). */
            if (http == NULL) {
                err = ESP_ERR_NO_MEM;
            }
            ESP_LOGW(TAG, "poll failed: HTTP %d, %s", http_status, esp_err_to_name(err));
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
    xTaskCreate(aircraft_task, "aircraft", 12288, NULL, 4, NULL);
}
