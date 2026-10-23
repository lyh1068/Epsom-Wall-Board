#include "status_corner.h"

#include <stdio.h>
#include <time.h>
#include "config.h"
#include "data/aircraft.h"
#include "data/routes.h"
#include "net/sntp_time.h"
#include "net/wifi.h"
#include "ui/ui_util.h"

#define COLOR_TEXT   lv_color_hex(0xB0BEC5)
#define COLOR_OK     lv_color_hex(0x81C784)
#define COLOR_WARN   lv_color_hex(0xFFB74D)
#define COLOR_BAD    lv_color_hex(0xFF5252)

static lv_obj_t *s_clock, *s_wifi, *s_time, *s_feed, *s_routes;

void status_corner_create(lv_obj_t *parent, int32_t width)
{
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 4, 0);
    s_clock = ui_fixed_label(parent, &lv_font_montserrat_28, width, lv_color_white());
    s_wifi = ui_fixed_label(parent, &lv_font_montserrat_14, width, COLOR_TEXT);
    s_time = ui_fixed_label(parent, &lv_font_montserrat_14, width, COLOR_TEXT);
    s_feed = ui_fixed_label(parent, &lv_font_montserrat_14, width, COLOR_TEXT);
    s_routes = ui_fixed_label(parent, &lv_font_montserrat_14, width, COLOR_TEXT);
    status_corner_refresh();
}

static void refresh_clock(void)
{
    time_status_t ts;
    time_get_status(&ts);
    char buf[48];
    if (ts.source == CLOCK_SRC_NONE) {
        ui_set_text(s_clock, "--:--:--  clock not set");
    } else {
        time_t now = time(NULL);
        struct tm lt;
        localtime_r(&now, &lt);
        strftime(buf, sizeof(buf), "%H:%M:%S  %a %d %b", &lt);
        ui_set_text(s_clock, buf);
    }

    static const char *const rtc_txt[] = {
        [RTC_ST_UNKNOWN] = "?", [RTC_ST_OK] = "ok", [RTC_ST_INVALID] = "no valid time",
        [RTC_ST_MISSING] = "not found", [RTC_ST_ERROR] = "I2C error",
    };
    const char *rtc = ts.rtc_written ? "updated" : rtc_txt[ts.rtc];
    char line[96];
    if (ts.sntp_err != ESP_OK) {
        snprintf(line, sizeof(line), "Time: SNTP did not start (%s)  RTC %s", esp_err_to_name(ts.sntp_err), rtc);
        ui_set_text_color(s_time, COLOR_BAD);
    } else if (ts.source == CLOCK_SRC_NTP) {
        struct tm st;
        localtime_r(&ts.last_sync, &st);
        strftime(buf, sizeof(buf), "%H:%M", &st);
        snprintf(line, sizeof(line), "Time: NTP %s  " "\xE2\x80\xA2" "  RTC %s", buf, rtc);
        ui_set_text_color(s_time, COLOR_TEXT);
    } else {
        snprintf(line, sizeof(line), "Time: %s, no NTP yet  " "\xE2\x80\xA2" "  RTC %s",
                 ts.source == CLOCK_SRC_RTC ? "from RTC" : "not set", rtc);
        ui_set_text_color(s_time, COLOR_WARN);
    }
    ui_set_text(s_time, line);
}

static void refresh_wifi(void)
{
    wifi_status_t ws;
    wifi_get_status(&ws);
    char line[96];
    lv_color_t c = COLOR_WARN;
    switch (ws.state) {
    case WIFI_ST_OFF:
    case WIFI_ST_STARTING:
        snprintf(line, sizeof(line), "Wi-Fi: starting%s", ws.link_seen_down ? ", C6 link DOWN" : "");
        break;
    case WIFI_ST_FAILED:
        snprintf(line, sizeof(line), "Wi-Fi: %s failed: %s", ws.init_step, esp_err_to_name(ws.init_err));
        c = COLOR_BAD;
        break;
    case WIFI_ST_NOT_CONFIGURED:
        snprintf(line, sizeof(line), "Wi-Fi: not configured (set WIFI_SSID in .env)");
        c = COLOR_BAD;
        break;
    case WIFI_ST_CONNECTING:
        if (ws.last_reason) {
            snprintf(line, sizeof(line), "Wi-Fi: retrying, reason %u: %s", ws.last_reason, wifi_reason_str(ws.last_reason));
        } else {
            snprintf(line, sizeof(line), "Wi-Fi: connecting to %s", ws.ssid);
        }
        break;
    case WIFI_ST_CONNECTED:
        snprintf(line, sizeof(line), "Wi-Fi: %s  %d dBm  %s", ws.ssid, ws.rssi, ws.ip);
        c = COLOR_TEXT;
        break;
    }
    ui_set_text(s_wifi, line);
    ui_set_text_color(s_wifi, c);
}

static void refresh_feed(void)
{
    aircraft_meta_t m;
    adsb_aircraft_t unused;
    aircraft_snapshot(&unused, 0, &m);

    char line[96];
    lv_color_t c = COLOR_TEXT;
    long age = m.last_ok ? (long)(time(NULL) - m.last_ok) : -1;
    if (m.last_err != ESP_OK || (m.http_status != 0 && m.http_status != 200)) {
        if (m.http_status != 0 && m.http_status != 200) {
            snprintf(line, sizeof(line), "Aircraft: HTTP %d, retry in %lu s", m.http_status, (unsigned long)m.backoff_s);
        } else {
            snprintf(line, sizeof(line), "Aircraft: %s, retry in %lu s", esp_err_to_name(m.last_err), (unsigned long)m.backoff_s);
        }
        c = COLOR_BAD;
    } else if (m.waiting_for_wifi && age < 0) {
        snprintf(line, sizeof(line), "Aircraft: waiting for Wi-Fi");
        c = COLOR_WARN;
    } else if (age < 0) {
        snprintf(line, sizeof(line), "Aircraft: first poll...");
        c = COLOR_WARN;
    } else {
        snprintf(line, sizeof(line), "Aircraft: adsb.lol, %d received, %ld s ago", m.received, age);
    }
    /* Stale badge: no fresh data for three poll intervals. */
    if (age > 3 * ADSB_POLL_S) {
        c = COLOR_BAD;
    }
    ui_set_text(s_feed, line);
    ui_set_text_color(s_feed, c);
}

static void refresh_routes(void)
{
    routes_meta_t m;
    routes_get_meta(&m);
    char line[96];
    lv_color_t c = COLOR_TEXT;
    if (!m.fs_mounted && m.fs_err != ESP_OK) {
        snprintf(line, sizeof(line), "Routes: %d cached, storage failed (%s)", m.cached, esp_err_to_name(m.fs_err));
        c = COLOR_WARN;
    } else if (m.last_err != ESP_OK || (m.http_status != 0 && m.http_status != 200 && m.http_status != 404)) {
        if (m.last_err == ESP_OK) {
            snprintf(line, sizeof(line), "Routes: adsbdb HTTP %d, backoff %lu s", m.http_status, (unsigned long)m.backoff_s);
        } else {
            snprintf(line, sizeof(line), "Routes: adsbdb %s, backoff %lu s", esp_err_to_name(m.last_err), (unsigned long)m.backoff_s);
        }
        c = COLOR_BAD;
    } else {
        snprintf(line, sizeof(line), "Routes: %d cached%s", m.cached, m.loaded ? ", restored from flash" : "");
    }
    ui_set_text(s_routes, line);
    ui_set_text_color(s_routes, c);
}

void status_corner_refresh(void)
{
    refresh_clock();
    refresh_wifi();
    refresh_feed();
    refresh_routes();
}
