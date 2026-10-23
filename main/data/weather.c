#include "weather.h"

#include <stdio.h>
#include <string.h>
#include "config.h"
#include "data/home.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "net/http_json.h"
#include "net/wifi.h"

static const char *TAG = "weather";

static SemaphoreHandle_t s_mutex;
static weather_t s_wx;
static bool s_have;
static weather_meta_t s_meta;

bool weather_get(weather_t *out, weather_meta_t *meta)
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
        *out = s_wx;
    }
    if (meta) {
        *meta = s_meta;
    }
    xSemaphoreGive(s_mutex);
    return have;
}

static bool poll_once(http_json_t *http, int *status, esp_err_t *err)
{
    /* Unix times and GMT: no local-time strings to misread across BST. */
    char url[320], where[48];
    home_query_coords(where, sizeof(where), "&longitude=");
    snprintf(url, sizeof(url),
             "https://api.open-meteo.com/v1/forecast?latitude=%s"
             "&current=temperature_2m,weather_code,is_day"
             "&hourly=temperature_2m,precipitation_probability,weather_code"
             "&forecast_hours=%d&timeformat=unixtime&timezone=GMT", where, WX_HOURS);

    const char *body;
    size_t len;
    *err = http_json_get(http, url, &body, &len, status);
    if (*err != ESP_OK || *status != 200) {
        return false;
    }
    weather_t w;
    if (!weather_parse(body, len, &w)) {
        *err = ESP_ERR_INVALID_RESPONSE;
        return false;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_wx = w;
    s_have = true;
    s_meta.seq++;
    s_meta.last_ok = time(NULL);
    xSemaphoreGive(s_mutex);
    ESP_LOGI(TAG, "%.1f C, code %d", w.temp_c, w.code);
    return true;
}

static void weather_task(void *arg)
{
    http_json_t *http = http_json_new(16 * 1024);
    uint32_t wait_s = WEATHER_POLL_S, backoff_s = 0;

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
        if (http != NULL && poll_once(http, &status, &err)) {
            backoff_s = 0;
            wait_s = WEATHER_POLL_S;
        } else {
            if (http == NULL) {
                err = ESP_ERR_NO_MEM;
            }
            backoff_s = backoff_s ? (backoff_s * 2 > WEATHER_BACKOFF_MAX_S ? WEATHER_BACKOFF_MAX_S : backoff_s * 2) : 30;
            wait_s = backoff_s;
            ESP_LOGW(TAG, "poll failed: HTTP %d, %s; retry in %lu s", status, esp_err_to_name(err), (unsigned long)wait_s);
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

void weather_start(void)
{
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        ESP_LOGE(TAG, "out of memory; weather not started");
        return;
    }
    xTaskCreate(weather_task, "weather", 12288, NULL, 3, NULL);
}
