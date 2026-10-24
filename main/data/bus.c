#include "bus.h"

#include <stdio.h>
#include <string.h>
#include "esp_attr.h"
#include "config.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "net/http_json.h"
#include "net/wifi.h"
#include "wallboard_secrets.h"

static const char *TAG = "bus";

static SemaphoreHandle_t s_mutex;
static EXT_RAM_BSS_ATTR bus_board_t s_board[BUS_MAX_STOPS];
static bus_stop_meta_t s_stop[BUS_MAX_STOPS];
static bus_meta_t s_meta;

#define LOCK()   xSemaphoreTake(s_mutex, portMAX_DELAY)
#define UNLOCK() xSemaphoreGive(s_mutex)

bool bus_get_stop(int i, bus_board_t *out, bus_stop_meta_t *meta)
{
    if (s_mutex == NULL || i < 0 || i >= BUS_STOP_COUNT) {
        if (meta) {
            memset(meta, 0, sizeof(*meta));
        }
        return false;
    }
    LOCK();
    bool have = s_stop[i].have;
    if (have && out) {
        *out = s_board[i];
    }
    if (meta) {
        *meta = s_stop[i];
    }
    UNLOCK();
    return have;
}

void bus_get_meta(bus_meta_t *meta)
{
    if (s_mutex == NULL) {
        memset(meta, 0, sizeof(*meta));
        return;
    }
    LOCK();
    *meta = s_meta;
    int64_t now = esp_timer_get_time(), oldest = 0;
    for (int i = 0; i < BUS_STOP_COUNT; i++) {
        if (!s_stop[i].have) {
            oldest = -1;
            break;
        }
        if (now - s_stop[i].fetched_us > oldest) {
            oldest = now - s_stop[i].fetched_us;
        }
    }
    meta->oldest_s = oldest < 0 ? -1 : (int)(oldest / 1000000);
    UNLOCK();
}

static bool poll_stop(http_json_t *http, int i, bus_board_t *scratch, int *status, esp_err_t *err)
{
    /* The key rides in the query string, as TfL requires. Never log this URL. */
    char url[256];
#if TFL_APP_KEY_SET
    snprintf(url, sizeof(url), "https://api.tfl.gov.uk/StopPoint/%s/Arrivals?app_key=%s", BUS_STOPS[i].naptan, TFL_APP_KEY);
#else
    snprintf(url, sizeof(url), "https://api.tfl.gov.uk/StopPoint/%s/Arrivals", BUS_STOPS[i].naptan);
#endif
    const char *body;
    size_t len;
    *err = http_json_get(http, url, &body, &len, status);
    if (*err != ESP_OK || *status != 200) {
        return false;
    }
    if (!bus_parse(body, len, scratch)) {
        *err = ESP_ERR_INVALID_RESPONSE;
        return false;
    }
    LOCK();
    s_board[i] = *scratch;
    s_stop[i].fetched_us = esp_timer_get_time();
    s_stop[i].have = true;
    s_meta.last_ok = time(NULL);
    UNLOCK();
    return true;
}

static void bus_task(void *arg)
{
    /* A single stop's arrivals are a few KB; this leaves room for a busy one. */
    http_json_t *http = http_json_new(64 * 1024);
    static EXT_RAM_BSS_ATTR bus_board_t scratch;
    const unsigned step_ms = BUS_POLL_S * 1000 / BUS_STOP_COUNT;
    unsigned backoff_s = 0;
    int next = 0;

    for (;;) {
        wifi_status_t ws;
        wifi_get_status(&ws);
        if (ws.state != WIFI_ST_CONNECTED) {
            LOCK();
            s_meta.waiting_for_wifi = true;
            UNLOCK();
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        int status = 0;
        esp_err_t err = ESP_OK;
        unsigned wait_ms = step_ms;
        if (http != NULL && poll_stop(http, next, &scratch, &status, &err)) {
            backoff_s = 0;
            next = (next + 1) % BUS_STOP_COUNT;
        } else {
            if (http == NULL) {
                err = ESP_ERR_NO_MEM;
            }
            /* Back off as a whole: a failure is almost always TfL or the network, not one stop. */
            backoff_s = backoff_s ? (backoff_s * 2 > BUS_BACKOFF_MAX_S ? BUS_BACKOFF_MAX_S : backoff_s * 2) : 30;
            wait_ms = backoff_s * 1000;
            ESP_LOGW(TAG, "stop %s: HTTP %d, %s; retry in %u s", BUS_STOPS[next].letter, status,
                     esp_err_to_name(err), backoff_s);
            next = (next + 1) % BUS_STOP_COUNT;
        }
        LOCK();
        s_meta.http_status = status;
        s_meta.last_err = err;
        s_meta.backoff_s = backoff_s;
        s_meta.waiting_for_wifi = false;
        UNLOCK();
        vTaskDelay(pdMS_TO_TICKS(wait_ms));
    }
}

void bus_start(void)
{
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        ESP_LOGE(TAG, "out of memory; buses not started");
        return;
    }
    s_meta.keyless = !TFL_APP_KEY_SET;
    /* Stack in PSRAM: internal RAM is reserved for Wi-Fi and the display. This
     * task never writes flash, which a PSRAM-stacked task must not do. */
    xTaskCreateWithCaps(bus_task, "bus", 12288, NULL, 3, NULL, MALLOC_CAP_SPIRAM);
}
