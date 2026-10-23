#include "sntp_time.h"

#include <stdlib.h>
#include <sys/time.h>
#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "freertos/FreeRTOS.h"
#include "hw/rx8130.h"

static const char *TAG = "time";

#define NTP_SERVER "pool.ntp.org"

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static time_status_t s_st = { .source = CLOCK_SRC_NONE, .rtc = RTC_ST_UNKNOWN };

#define LOCKED(stmt) do { portENTER_CRITICAL(&s_lock); stmt; portEXIT_CRITICAL(&s_lock); } while (0)

void time_get_status(time_status_t *out)
{
    LOCKED(*out = s_st);
}

void time_init(void)
{
    setenv("TZ", WALLBOARD_TZ, 1);
    tzset();

    esp_err_t err = rx8130_init(bsp_i2c_get_handle());
    if (err == ESP_ERR_NOT_FOUND) {
        ESP_LOGE(TAG, "RTC not found on I2C");
        LOCKED(s_st.rtc = RTC_ST_MISSING);
        return;
    }

    struct tm utc;
    bool valid = false;
    if (err == ESP_OK) {
        err = rx8130_read_utc(&utc, &valid);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "RTC: %s", esp_err_to_name(err));
        LOCKED(s_st.rtc = RTC_ST_ERROR; s_st.rtc_err = err);
        return;
    }
    if (!valid) {
        ESP_LOGW(TAG, "RTC lost power or was never set; waiting for NTP");
        LOCKED(s_st.rtc = RTC_ST_INVALID);
        return;
    }

    /* timegm() is not in newlib; with TZ temporarily UTC, mktime() is the same thing. */
    setenv("TZ", "UTC0", 1);
    tzset();
    struct timeval tv = { .tv_sec = mktime(&utc) };
    setenv("TZ", WALLBOARD_TZ, 1);
    tzset();
    settimeofday(&tv, NULL);
    ESP_LOGI(TAG, "clock seeded from RTC");
    LOCKED(s_st.rtc = RTC_ST_OK; s_st.source = CLOCK_SRC_RTC);
}

/* Runs on the lwIP thread. The RTC write is one short I2C transaction. */
static void on_sync(struct timeval *tv)
{
    struct tm utc;
    gmtime_r(&tv->tv_sec, &utc);
    bool rtc_ok = rx8130_write_utc(&utc) == ESP_OK;
    ESP_LOGI(TAG, "NTP sync%s", rtc_ok ? ", RTC updated" : "");
    LOCKED(s_st.source = CLOCK_SRC_NTP; s_st.last_sync = tv->tv_sec;
           if (rtc_ok) { s_st.rtc_written = true; });
}

void time_sync_start(void)
{
    /* Not server_from_dhcp: without CONFIG_LWIP_DHCP_GET_NTP_SRV that makes
     * esp_netif_sntp_init fail outright and SNTP never starts (tab5-board skill). */
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG(NTP_SERVER);
    cfg.sync_cb = on_sync;
    esp_err_t err = esp_netif_sntp_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SNTP did not start: %s", esp_err_to_name(err));
    }
    LOCKED(s_st.sntp_err = err);
}
