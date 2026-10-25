#include "http_json.h"

#include <stdlib.h>
#include <string.h>
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"

static const char *TAG = "http";

#define USER_AGENT "EpsomWallBoard/0.3 (ESP32-P4)"

struct http_json {
    esp_http_client_handle_t client;
    char *buf;
    size_t len, cap, max;
    bool overflow;
};

static esp_err_t on_event(esp_http_client_event_t *e)
{
    http_json_t *h = e->user_data;
    if (e->event_id != HTTP_EVENT_ON_DATA || h->overflow) {
        return ESP_OK;
    }
    size_t need = h->len + e->data_len + 1;
    if (need > h->max) {
        h->overflow = true;
        return ESP_OK;
    }
    if (need > h->cap) {
        size_t cap = h->cap ? h->cap : 16384;
        while (cap < need) {
            cap *= 2;
        }
        if (cap > h->max) {
            cap = h->max;
        }
        char *p = heap_caps_realloc(h->buf, cap, MALLOC_CAP_SPIRAM);
        if (p == NULL) {
            h->overflow = true;
            return ESP_OK;
        }
        h->buf = p;
        h->cap = cap;
    }
    memcpy(h->buf + h->len, e->data, e->data_len);
    h->len += e->data_len;
    return ESP_OK;
}

http_json_t *http_json_new(size_t max_body)
{
    http_json_t *h = calloc(1, sizeof(*h));
    if (h != NULL) {
        h->max = max_body;
    }
    return h;
}

static esp_err_t ensure_client(http_json_t *h, const char *url)
{
    if (h->client != NULL) {
        return esp_http_client_set_url(h->client, url);
    }
    esp_http_client_config_t cfg = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = on_event,
        .user_data = h,
        .timeout_ms = 15000,
        .keep_alive_enable = true,
        .user_agent = USER_AGENT,
        .buffer_size = 4096,
    };
    h->client = esp_http_client_init(&cfg);
    return h->client ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t http_json_get(http_json_t *h, const char *url, const char **body, size_t *len, int *status)
{
    *body = NULL;
    *len = 0;
    *status = 0;

    /* A kept-alive connection may have been closed by the server while idle
     * (adsbdb lookups can be minutes apart): the request then fails on write
     * (ESP_ERR_HTTP_WRITE_DATA). That is not a real failure, so on a reused
     * connection, reconnect and try once more. */
    esp_err_t err = ESP_FAIL;
    for (int attempt = 0; attempt < 2; attempt++) {
        bool reused = h->client != NULL;
        err = ensure_client(h, url);
        if (err != ESP_OK) {
            return err;
        }
        h->len = 0;
        h->overflow = false;
        err = esp_http_client_perform(h->client);
        if (err == ESP_OK) {
            break;
        }
        /* Start clean rather than reusing a broken connection. */
        esp_http_client_cleanup(h->client);
        h->client = NULL;
        if (!reused) {
            ESP_LOGW(TAG, "GET failed: %s", esp_err_to_name(err));
            return err;
        }
        ESP_LOGI(TAG, "stale connection (%s), reconnecting", esp_err_to_name(err));
    }
    if (err != ESP_OK) {
        return err;
    }
    *status = esp_http_client_get_status_code(h->client);
    if (h->overflow) {
        ESP_LOGW(TAG, "response over %u bytes, dropped", (unsigned)h->max);
        return ESP_ERR_INVALID_SIZE;
    }
    if (h->buf == NULL) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    h->buf[h->len] = '\0';
    *body = h->buf;
    *len = h->len;
    return ESP_OK;
}
