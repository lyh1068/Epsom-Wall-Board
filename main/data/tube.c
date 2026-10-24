#include "tube.h"

#include <stdio.h>
#include <string.h>
#include "esp_attr.h"
#include "config.h"
#include "data/tube_parse.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "net/http_json.h"
#include "net/wifi.h"
#include "wallboard_secrets.h"

static const char *TAG = "tube";

static SemaphoreHandle_t s_mutex;
static EXT_RAM_BSS_ATTR tube_status_t s_status;
static bool s_have;
static tube_meta_t s_meta = { .keyless = !TFL_APP_KEY_SET };

bool tube_get(tube_status_t *out, tube_meta_t *meta)
{
    if (s_mutex == NULL) {
        if (meta) {
            memset(meta, 0, sizeof(*meta));
        }
        return false;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    bool have = s_have;
    if (have && out) {
        *out = s_status;
    }
    if (meta) {
        *meta = s_meta;
    }
    xSemaphoreGive(s_mutex);
    return have;
}

static bool poll_once(http_json_t *http, tube_status_t *scratch, int *status, esp_err_t *err)
{
    /* The key rides in the query string, as TfL requires. Never log this URL. */
    char url[256];
#if TFL_APP_KEY_SET
    snprintf(url, sizeof(url), "https://api.tfl.gov.uk/Line/%s/Status?app_key=%s", TUBE_LINES, TFL_APP_KEY);
#else
    snprintf(url, sizeof(url), "https://api.tfl.gov.uk/Line/%s/Status", TUBE_LINES);
#endif
    const char *body;
    size_t len;
    *err = http_json_get(http, url, &body, &len, status);
    if (*err != ESP_OK || *status != 200) {
        return false;
    }
    if (!tube_parse(body, len, TUBE_LINES, scratch)) {
        *err = ESP_ERR_INVALID_RESPONSE;
        return false;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_status = *scratch;
    s_have = true;
    s_meta.last_ok = time(NULL);
    xSemaphoreGive(s_mutex);
    ESP_LOGI(TAG, "%d lines", scratch->count);
    return true;
}

static void tube_task(void *arg)
{
    http_json_t *http = http_json_new(32 * 1024);
    static EXT_RAM_BSS_ATTR tube_status_t scratch;
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
            wait_s = TUBE_POLL_S;
        } else {
            if (http == NULL) {
                err = ESP_ERR_NO_MEM;
            }
            /* 401/403 mean a bad key: say so, but do not hammer. */
            backoff_s = backoff_s ? (backoff_s * 2 > TUBE_BACKOFF_MAX_S ? TUBE_BACKOFF_MAX_S : backoff_s * 2) : 30;
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

void tube_start(void)
{
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        ESP_LOGE(TAG, "out of memory; tube status not started");
        return;
    }
    /* Stack in PSRAM: internal RAM is reserved for Wi-Fi and the display. This
     * task never writes flash, which a PSRAM-stacked task must not do. */
    xTaskCreateWithCaps(tube_task, "tube", 12288, NULL, 3, NULL, MALLOC_CAP_SPIRAM);
}
