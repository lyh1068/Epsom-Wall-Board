#include "wifi.h"

#include <string.h>
#include "bsp/esp-bsp.h"
#include "esp_event.h"
#include "esp_hosted_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "wallboard_secrets.h"

static const char *TAG = "wifi";

#define BACKOFF_MIN_MS   1000
#define BACKOFF_MAX_MS   60000
#define RSSI_POLL_MS     5000

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static wifi_status_t s_st = { .state = WIFI_ST_OFF };
static esp_timer_handle_t s_retry_timer;
static uint32_t s_backoff_ms = BACKOFF_MIN_MS;

#define LOCKED(stmt) do { portENTER_CRITICAL(&s_lock); stmt; portEXIT_CRITICAL(&s_lock); } while (0)

void wifi_get_status(wifi_status_t *out)
{
    LOCKED(*out = s_st);
}

const char *wifi_reason_str(uint8_t reason)
{
    switch (reason) {
    case WIFI_REASON_NO_AP_FOUND:
        return "no AP found (2.4 GHz only - check SSID)";
    case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
        return "no AP with compatible security";
    case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
        return "AP security below threshold";
    case WIFI_REASON_AUTH_FAIL:
        return "auth failed (password?)";
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
        return "handshake timeout (password?)";
    case WIFI_REASON_AUTH_EXPIRE:
        return "auth expired";
    case WIFI_REASON_ASSOC_FAIL:
        return "association failed";
    case WIFI_REASON_CONNECTION_FAIL:
        return "connection failed";
    case WIFI_REASON_BEACON_TIMEOUT:
        return "beacon timeout (signal?)";
    case WIFI_REASON_ASSOC_LEAVE:
        return "left network";
    default:
        return "see wifi_err_reason_t";
    }
}

void wifi_power_on(void)
{
    esp_err_t err = bsp_feature_enable(BSP_FEATURE_WIFI, true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "C6 power-on failed: %s", esp_err_to_name(err));
        LOCKED(s_st.init_err = err; s_st.init_step = "C6 power");
    }
}

static void retry_cb(void *arg)
{
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_connect: %s", esp_err_to_name(err));
    }
}

static void schedule_retry(void)
{
    ESP_LOGW(TAG, "reconnecting in %lu ms", (unsigned long)s_backoff_ms);
    esp_timer_stop(s_retry_timer);
    esp_timer_start_once(s_retry_timer, (uint64_t)s_backoff_ms * 1000);
    s_backoff_ms = s_backoff_ms * 2 > BACKOFF_MAX_MS ? BACKOFF_MAX_MS : s_backoff_ms * 2;
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == ESP_HOSTED_EVENT) {
        if (id == ESP_HOSTED_EVENT_TRANSPORT_UP) {
            LOCKED(s_st.link_up = true);
        } else {
            LOCKED(s_st.link_up = false; s_st.link_seen_down = true);
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        if (WIFI_SSID_SET) {
            LOCKED(s_st.state = WIFI_ST_CONNECTING);
            esp_wifi_connect();
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *d = data;
        ESP_LOGW(TAG, "disconnected, reason %u (%s)", d->reason, wifi_reason_str(d->reason));
        LOCKED(s_st.state = WIFI_ST_CONNECTING; s_st.last_reason = d->reason;
               s_st.retries++; s_st.ip[0] = '\0'; s_st.rssi = 0);
        schedule_retry();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *g = data;
        char ip[16];
        snprintf(ip, sizeof(ip), IPSTR, IP2STR(&g->ip_info.ip));
        ESP_LOGI(TAG, "got IP %s", ip);
        s_backoff_ms = BACKOFF_MIN_MS;
        LOCKED(s_st.state = WIFI_ST_CONNECTED; strlcpy(s_st.ip, ip, sizeof(s_st.ip)));
    }
}

/* Log and record the first failure, then carry on: an init failure must show
 * on screen, not just stop things quietly. */
static bool step(const char *name, esp_err_t err)
{
    if (err == ESP_OK) {
        return true;
    }
    ESP_LOGE(TAG, "%s: %s", name, esp_err_to_name(err));
    LOCKED(if (s_st.init_err == ESP_OK) { s_st.init_err = err; s_st.init_step = name; }
           s_st.state = WIFI_ST_FAILED);
    return false;
}

static void wifi_task(void *arg)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (!step("nvs_flash_init", err)) {
        vTaskDelete(NULL);
    }

    esp_netif_create_default_wifi_sta();

    /* RPC to the C6: blocks until the SDIO transport is up. */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (!step("esp_wifi_init", esp_wifi_init(&cfg))) {
        vTaskDelete(NULL);
    }
    uint8_t mac[6] = { 0 };
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    LOCKED(s_st.link_up = true; memcpy(s_st.mac, mac, sizeof(mac)));

    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL);

    wifi_config_t wc = { 0 };
    strlcpy((char *)wc.sta.ssid, WIFI_SSID, sizeof(wc.sta.ssid));
    strlcpy((char *)wc.sta.password, WIFI_PASSWORD, sizeof(wc.sta.password));
    wc.sta.threshold.authmode = WIFI_PASSWORD[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    wc.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

    if (WIFI_SSID_SET) {
        LOCKED(strlcpy(s_st.ssid, WIFI_SSID, sizeof(s_st.ssid)); s_st.state = WIFI_ST_CONNECTING);
    } else {
        ESP_LOGW(TAG, "WIFI_SSID is the placeholder; set it in .env");
        LOCKED(s_st.state = WIFI_ST_NOT_CONFIGURED);
    }

    if (!step("esp_wifi_set_mode", esp_wifi_set_mode(WIFI_MODE_STA))
        || !step("esp_wifi_set_config", esp_wifi_set_config(WIFI_IF_STA, &wc))
        || !step("esp_wifi_start", esp_wifi_start())) {
        vTaskDelete(NULL);
    }

    /* RSSI needs an RPC to the C6, so poll it here rather than from the UI. */
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(RSSI_POLL_MS));
        wifi_ap_record_t ap;
        bool connected;
        LOCKED(connected = (s_st.state == WIFI_ST_CONNECTED));
        if (connected && esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            LOCKED(s_st.rssi = ap.rssi);
        }
    }
}

void wifi_start(void)
{
    LOCKED(if (s_st.state == WIFI_ST_OFF) { s_st.state = WIFI_ST_STARTING; });

    esp_event_handler_register(ESP_HOSTED_EVENT, ESP_EVENT_ANY_ID, on_event, NULL);

    const esp_timer_create_args_t targs = { .callback = retry_cb, .name = "wifi_retry" };
    esp_timer_create(&targs, &s_retry_timer);

    xTaskCreate(wifi_task, "wifi", 6144, NULL, 5, NULL);
}
