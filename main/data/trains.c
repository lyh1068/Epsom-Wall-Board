#include "trains.h"

#include <stdio.h>
#include <string.h>
#include "config.h"
#include "data/trains_parse.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "net/http_json.h"
#include "net/wifi.h"
#include "wallboard_secrets.h"

static const char *TAG = "trains";

static SemaphoreHandle_t s_mutex;
static EXT_RAM_BSS_ATTR train_board_t s_board;
static bool s_have;
static trains_meta_t s_meta = { .no_key = !RAIL_DATA_KEY_SET };

bool trains_get(train_board_t *out, trains_meta_t *meta)
{
    if (s_mutex == NULL) {
        if (meta) {
            *meta = s_meta;
        }
        return false;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    bool have = s_have;
    if (have && out) {
        *out = s_board;
    }
    if (meta) {
        *meta = s_meta;
    }
    xSemaphoreGive(s_mutex);
    return have;
}

/* Weekday mornings poll twice as often (PLAN.md §4). */
static unsigned poll_interval_s(void)
{
    time_t now = time(NULL);
    struct tm lt;
    localtime_r(&now, &lt);
    int min = lt.tm_hour * 60 + lt.tm_min;
    bool weekday = lt.tm_wday >= 1 && lt.tm_wday <= 5;
    return weekday && min >= TRAINS_COMMUTE_FROM_MIN && min < TRAINS_COMMUTE_TO_MIN ? TRAINS_POLL_COMMUTE_S
                                                                                     : TRAINS_POLL_S;
}

static bool poll_once(http_json_t *http, train_board_t *scratch, int *status, esp_err_t *err)
{
    char url[192];
    snprintf(url, sizeof(url), "%s/GetDepBoardWithDetails/%s?numRows=%d&timeWindow=%d", RAIL_LDBWS_BASE,
             TRAINS_STATION_CRS, TRAINS_MAX, TRAINS_WINDOW_MIN);
    const char *body;
    size_t len;
    *err = http_json_get(http, url, &body, &len, status);
    if (*err != ESP_OK || *status != 200) {
        return false;
    }
    if (!trains_parse(body, len, TRAINS_LONDON_CRS, scratch)) {
        *err = ESP_ERR_INVALID_RESPONSE;
        return false;
    }
    scratch->updated = time(NULL);
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_board = *scratch;
    s_have = true;
    s_meta.last_ok = scratch->updated;
    xSemaphoreGive(s_mutex);
    ESP_LOGI(TAG, "%d services%s", scratch->count, scratch->nrcc[0] ? ", disruption message" : "");
    return true;
}

static void trains_task(void *arg)
{
    /* The key goes in a header, never the URL, so it is never in a log line. */
    http_json_t *http = http_json_new(64 * 1024);
    if (http != NULL) {
        http_json_set_header(http, "x-apikey", RAIL_DATA_KEY);
    }
    static EXT_RAM_BSS_ATTR train_board_t scratch;
    unsigned wait_s, backoff_s = 0;

    for (;;) {
        wifi_status_t ws;
        wifi_get_status(&ws);
        if (ws.state != WIFI_ST_CONNECTED) {
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            s_meta.waiting_for_wifi = true;
            xSemaphoreGive(s_mutex);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        int status = 0;
        esp_err_t err = ESP_OK;
        if (http != NULL && poll_once(http, &scratch, &status, &err)) {
            backoff_s = 0;
            wait_s = poll_interval_s();
        } else {
            if (http == NULL) {
                err = ESP_ERR_NO_MEM;
            }
            /* 401/403 mean a bad key: say so, but do not hammer. */
            backoff_s = backoff_s ? (backoff_s * 2 > TRAINS_BACKOFF_MAX_S ? TRAINS_BACKOFF_MAX_S : backoff_s * 2) : 30;
            wait_s = backoff_s;
            ESP_LOGW(TAG, "poll failed: HTTP %d, %s; retry in %u s", status, esp_err_to_name(err), wait_s);
        }
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        s_meta.http_status = status;
        s_meta.last_err = err;
        s_meta.backoff_s = backoff_s;
        s_meta.waiting_for_wifi = false;
        xSemaphoreGive(s_mutex);
        vTaskDelay(pdMS_TO_TICKS(wait_s * 1000));
    }
}

void trains_start(void)
{
    if (!RAIL_DATA_KEY_SET) {
        ESP_LOGW(TAG, "RAIL_DATA_KEY not set; no live trains");
        return;
    }
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        ESP_LOGE(TAG, "out of memory; trains not started");
        return;
    }
    /* Stack in PSRAM: internal RAM is reserved for Wi-Fi and the display. This
     * task never writes flash, which a PSRAM-stacked task must not do. */
    xTaskCreateWithCaps(trains_task, "trains", 12288, NULL, 3, NULL, MALLOC_CAP_SPIRAM);
}
