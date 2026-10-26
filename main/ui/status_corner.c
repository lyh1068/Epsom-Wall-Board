#include "status_corner.h"

#include <stdio.h>
#include <time.h>
#include "config.h"
#include "esp_timer.h"
#include "data/aircraft.h"
#include "data/routes.h"
#include "data/bus.h"
#include "data/trains.h"
#include "hw/battery.h"
#include "data/tube.h"
#include "data/weather.h"
#include "net/sntp_time.h"
#include "net/wifi.h"
#include "sys/health.h"
#include "ui/ui_util.h"

#define COLOR_TEXT   lv_color_hex(0xB0BEC5)
#define COLOR_OK     lv_color_hex(0x81C784)
#define COLOR_WARN   lv_color_hex(0xFFB74D)
#define COLOR_BAD    lv_color_hex(0xFF5252)

static lv_obj_t *s_clock, *s_wifi, *s_time, *s_feed, *s_routes, *s_weather, *s_trains, *s_tube, *s_bus, *s_batt, *s_sys, *s_disp, *s_crash;

void status_corner_create(lv_obj_t *parent, int32_t width)
{
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 4, 0);
    s_clock = ui_fixed_label(parent, &lv_font_montserrat_28, width, lv_color_white());
    s_wifi = ui_fixed_label(parent, &lv_font_montserrat_14, width, COLOR_TEXT);
    s_time = ui_fixed_label(parent, &lv_font_montserrat_14, width, COLOR_TEXT);
    s_feed = ui_fixed_label(parent, &lv_font_montserrat_14, width, COLOR_TEXT);
    s_routes = ui_fixed_label(parent, &lv_font_montserrat_14, width, COLOR_TEXT);
    s_weather = ui_fixed_label(parent, &lv_font_montserrat_14, width, COLOR_TEXT);
    s_trains = ui_fixed_label(parent, &lv_font_montserrat_14, width, COLOR_TEXT);
    s_tube = ui_fixed_label(parent, &lv_font_montserrat_14, width, COLOR_TEXT);
    s_bus = ui_fixed_label(parent, &lv_font_montserrat_14, width, COLOR_TEXT);
    s_batt = ui_fixed_label(parent, &lv_font_montserrat_14, width, COLOR_TEXT);
    s_sys = ui_fixed_label(parent, &lv_font_montserrat_14, width, COLOR_TEXT);
    s_disp = ui_fixed_label(parent, &lv_font_montserrat_14, width, COLOR_TEXT);
    s_crash = ui_fixed_label(parent, &lv_font_montserrat_14, width, COLOR_BAD);
    lv_obj_set_hidden(s_crash, true);
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
        snprintf(line, sizeof(line), "Aircraft: %s%s, %d received, %ld s ago", m.source ? m.source : "?",
                 m.fallback ? " (adsb.fi failed)" : "", m.received, age);
        if (m.fallback) {
            c = COLOR_WARN;
        }
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
    long wait_s = (long)((m.retry_at_us - esp_timer_get_time()) / 1000000);
    if (!m.fs_mounted && m.fs_err != ESP_OK) {
        snprintf(line, sizeof(line), "Routes: %d cached, storage failed (%s)", m.cached, esp_err_to_name(m.fs_err));
        c = COLOR_WARN;
    } else if (m.rate_limited) {
        /* Busy, not broken: amber with a countdown, and back to normal once the
         * wait is over, rather than red until the next lookup happens to succeed. */
        if (wait_s > 0) {
            snprintf(line, sizeof(line), "Routes: adsbdb busy (rate-limited), retrying in %ld s", wait_s);
            c = COLOR_WARN;
        } else {
            snprintf(line, sizeof(line), "Routes: %d cached%s", m.cached, m.loaded ? ", restored from flash" : "");
        }
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

static void refresh_weather(void)
{
    weather_meta_t m;
    weather_get(NULL, &m);
    char line[96];
    lv_color_t c = COLOR_TEXT;
    long age = m.last_ok ? (long)(time(NULL) - m.last_ok) : -1;
    if (m.last_err != ESP_OK || (m.http_status != 0 && m.http_status != 200)) {
        if (m.last_err == ESP_OK) {
            snprintf(line, sizeof(line), "Weather: HTTP %d, retry in %lu s", m.http_status, (unsigned long)m.backoff_s);
        } else {
            snprintf(line, sizeof(line), "Weather: %s, retry in %lu s", esp_err_to_name(m.last_err), (unsigned long)m.backoff_s);
        }
        c = COLOR_BAD;
    } else if (age < 0) {
        snprintf(line, sizeof(line), m.waiting_for_wifi ? "Weather: waiting for Wi-Fi" : "Weather: first poll...");
        c = COLOR_WARN;
    } else {
        snprintf(line, sizeof(line), "Weather: Open-Meteo, %ld min ago", age / 60);
    }
    if (age > 3 * WEATHER_POLL_S) {
        c = COLOR_BAD;      /* stale */
    }
    ui_set_text(s_weather, line);
    ui_set_text_color(s_weather, c);
}

static bool trains_bad(const trains_meta_t *m, time_t now)
{
    return !m->no_key && (m->last_err != ESP_OK || (m->http_status && m->http_status != 200)
                          || (m->last_ok && now - m->last_ok > 3 * TRAINS_POLL_S));
}

static void refresh_trains(void)
{
    trains_meta_t m;
    trains_get(NULL, &m);
    char line[96];
    lv_color_t c = COLOR_TEXT;
    long age = m.last_ok ? (long)(time(NULL) - m.last_ok) : -1;
    if (m.no_key) {
        snprintf(line, sizeof(line), "Trains: no RAIL_DATA_KEY in .env");
        c = COLOR_WARN;
    } else if (m.http_status == 401 || m.http_status == 403) {
        snprintf(line, sizeof(line), "Trains: Rail Data rejected the key (HTTP %d) - check RAIL_DATA_KEY", m.http_status);
        c = COLOR_BAD;
    } else if (m.last_err != ESP_OK || (m.http_status != 0 && m.http_status != 200)) {
        if (m.last_err == ESP_OK) {
            snprintf(line, sizeof(line), "Trains: Rail Data HTTP %d, retry in %u s", m.http_status, m.backoff_s);
        } else {
            snprintf(line, sizeof(line), "Trains: %s, retry in %u s", esp_err_to_name(m.last_err), m.backoff_s);
        }
        c = COLOR_BAD;
    } else if (age < 0) {
        snprintf(line, sizeof(line), m.waiting_for_wifi ? "Trains: waiting for Wi-Fi" : "Trains: first poll...");
        c = COLOR_WARN;
    } else {
        snprintf(line, sizeof(line), "Trains: National Rail (Darwin), %ld min ago", age / 60);
    }
    if (trains_bad(&m, time(NULL))) {
        c = COLOR_BAD;
    }
    ui_set_text(s_trains, line);
    ui_set_text_color(s_trains, c);
}

static bool tube_bad(const tube_meta_t *m, time_t now)
{
    return m->last_err != ESP_OK || (m->http_status && m->http_status != 200)
           || (m->last_ok && now - m->last_ok > 3 * TUBE_POLL_S);
}

static void refresh_tube(void)
{
    tube_meta_t m;
    tube_get(NULL, &m);
    char line[96];
    lv_color_t c = COLOR_TEXT;
    long age = m.last_ok ? (long)(time(NULL) - m.last_ok) : -1;
    if (m.http_status == 401 || m.http_status == 403) {
        snprintf(line, sizeof(line), "Tube: TfL rejected the key (HTTP %d) - check TFL_APP_KEY", m.http_status);
        c = COLOR_BAD;
    } else if (m.last_err != ESP_OK || (m.http_status != 0 && m.http_status != 200)) {
        if (m.last_err == ESP_OK) {
            snprintf(line, sizeof(line), "Tube: TfL HTTP %d, retry in %u s", m.http_status, m.backoff_s);
        } else {
            snprintf(line, sizeof(line), "Tube: %s, retry in %u s", esp_err_to_name(m.last_err), m.backoff_s);
        }
        c = COLOR_BAD;
    } else if (age < 0) {
        snprintf(line, sizeof(line), m.waiting_for_wifi ? "Tube: waiting for Wi-Fi" : "Tube: first poll...");
        c = COLOR_WARN;
    } else {
        snprintf(line, sizeof(line), "Tube: TfL, %ld min ago%s", age / 60, m.keyless ? "  (no app key)" : "");
        if (m.keyless) {
            c = COLOR_WARN;
        }
    }
    if (tube_bad(&m, time(NULL))) {
        c = COLOR_BAD;
    }
    ui_set_text(s_tube, line);
    ui_set_text_color(s_tube, c);
}

static bool bus_bad(const bus_meta_t *m)
{
    return m->last_err != ESP_OK || (m->http_status && m->http_status != 200)
           || m->oldest_s > 3 * BUS_POLL_S;
}

static void refresh_bus(void)
{
    bus_meta_t m;
    bus_get_meta(&m);
    char line[96];
    lv_color_t c = COLOR_TEXT;
    if (m.last_err != ESP_OK || (m.http_status != 0 && m.http_status != 200)) {
        if (m.last_err == ESP_OK) {
            snprintf(line, sizeof(line), "Buses: TfL HTTP %d, retry in %u s", m.http_status, m.backoff_s);
        } else {
            snprintf(line, sizeof(line), "Buses: %s, retry in %u s", esp_err_to_name(m.last_err), m.backoff_s);
        }
    } else if (m.waiting_for_wifi && m.oldest_s < 0) {
        snprintf(line, sizeof(line), "Buses: waiting for Wi-Fi");
        c = COLOR_WARN;
    } else if (m.oldest_s < 0) {
        snprintf(line, sizeof(line), "Buses: loading %d stops...", BUS_STOP_COUNT);
        c = COLOR_WARN;
    } else {
        snprintf(line, sizeof(line), "Buses: %d stops, oldest %d s%s", BUS_STOP_COUNT, m.oldest_s,
                 m.keyless ? "  (no app key)" : "");
    }
    if (bus_bad(&m)) {
        c = COLOR_BAD;
    }
    ui_set_text(s_bus, line);
    ui_set_text_color(s_bus, c);
}

static void refresh_battery(void)
{
    battery_t b;
    battery_read(&b);
    char line[96];
    lv_color_t c = COLOR_TEXT;
    if (!b.monitor_ok) {
        snprintf(line, sizeof(line), "Battery: monitor not found (%s)", esp_err_to_name(b.err));
        c = COLOR_BAD;
    } else if (b.charging) {
        /* No percentage while charging: the charging voltage inflates it. */
        snprintf(line, sizeof(line), "Battery: %.2f V  " "\xE2\x80\xA2" "  charging %.2f A  " "\xE2\x80\xA2" "  +%.0f mAh so far",
                 b.volts, b.amps, b.session_mah);
        c = COLOR_OK;
    } else {
        char level[24];
        if (b.level_valid) {
            snprintf(level, sizeof(level), "%d%%", b.level);
        } else {
            snprintf(level, sizeof(level), "level in %d s", b.level_in_s);
        }
        if (b.amps < -0.03f) {
            snprintf(line, sizeof(line), "Battery: %.2f V  %s  " "\xE2\x80\xA2" "  on battery, %.2f A", b.volts, level, -b.amps);
            c = b.level_valid && b.level < 20 ? COLOR_WARN : COLOR_TEXT;
        } else {
            snprintf(line, sizeof(line), "Battery: %.2f V  %s  " "\xE2\x80\xA2" "  not charging", b.volts, level);
        }
    }
    ui_set_text(s_batt, line);
    ui_set_text_color(s_batt, c);
}

/* Uptime, last restart, and headroom: the numbers that explain a restart. */
static void refresh_system(void)
{
    health_sample_lvgl();
    const health_boot_t *hb = health_boot();
    uint32_t up = health_uptime_s();
    char line[128];
    snprintf(line, sizeof(line), "System: up %luh%02lum  " "\xE2\x80\xA2" "  last reset: %s  " "\xE2\x80\xA2" "  RAM min %u KB  "
             "\xE2\x80\xA2" "  UI stack %lu B free",
             (unsigned long)(up / 3600), (unsigned long)(up / 60 % 60), hb->reason,
             (unsigned)(health_internal_min_free() / 1024), (unsigned long)health_lvgl_stack_free());
    ui_set_text(s_sys, line);
    ui_set_text_color(s_sys, hb->crashed ? COLOR_BAD : COLOR_TEXT);

    uint32_t ago;
    uint32_t stalls = health_display_underruns(&ago);
    if (stalls == 0) {
        snprintf(line, sizeof(line), "Display: no stalls (blue frames) since boot");
    } else if (ago < 3600) {
        snprintf(line, sizeof(line), "Display: %lu stall(s) (blue frames) since boot, last %lu min ago",
                 (unsigned long)stalls, (unsigned long)(ago / 60));
    } else {
        snprintf(line, sizeof(line), "Display: %lu stall(s) (blue frames) since boot, last %luh%02lum ago",
                 (unsigned long)stalls, (unsigned long)(ago / 3600), (unsigned long)(ago / 60 % 60));
    }
    ui_set_text(s_disp, line);
    ui_set_text_color(s_disp, stalls ? COLOR_WARN : COLOR_TEXT);

    if (hb->dump) {
        snprintf(line, sizeof(line), "Crash dump: task %s at 0x%08lx%s  " "\xE2\x80\xA2" "  %lu crash(es) since power-on",
                 hb->dump_task, (unsigned long)hb->dump_pc, hb->dump_this_fw ? "" : " (older firmware)",
                 (unsigned long)hb->crashes);
        ui_set_text(s_crash, line);
    }
    ui_set_hidden(s_crash, !hb->dump);
}

void status_compact(char *buf, size_t size, bool *problem)
{
    wifi_status_t ws;
    time_status_t ts;
    aircraft_meta_t am;
    weather_meta_t wm;
    adsb_aircraft_t unused;
    wifi_get_status(&ws);
    time_get_status(&ts);
    aircraft_snapshot(&unused, 0, &am);
    weather_get(NULL, &wm);

    time_t now = time(NULL);
    bool ac_bad = am.last_err != ESP_OK || (am.http_status && am.http_status != 200)
                  || (am.last_ok && now - am.last_ok > 3 * ADSB_POLL_S);
    bool wx_bad = wm.last_err != ESP_OK || (wm.http_status && wm.http_status != 200)
                  || (wm.last_ok && now - wm.last_ok > 3 * WEATHER_POLL_S);
    tube_meta_t tm;
    tube_get(NULL, &tm);
    bus_meta_t bm;
    bus_get_meta(&bm);
    trains_meta_t rm;
    trains_get(NULL, &rm);
    wx_bad = wx_bad || tube_bad(&tm, now) || bus_bad(&bm) || trains_bad(&rm, now);
    *problem = ws.state != WIFI_ST_CONNECTED || ts.source != CLOCK_SRC_NTP || ac_bad || wx_bad;

    char wifi[32], sync[24] = "no NTP";
    if (ws.state == WIFI_ST_CONNECTED) {
        snprintf(wifi, sizeof(wifi), "Wi-Fi %d dBm", ws.rssi);
    } else {
        snprintf(wifi, sizeof(wifi), "Wi-Fi down");
    }
    if (ts.source == CLOCK_SRC_NTP) {
        struct tm st;
        localtime_r(&ts.last_sync, &st);
        strftime(sync, sizeof(sync), "synced %H:%M", &st);
    }
    /* A crash restart is a problem until power is cycled: worth seeing at a glance. */
    const health_boot_t *hb = health_boot();
    *problem = *problem || hb->crashed;
    snprintf(buf, size, "%s  " "\xE2\x80\xA2" "  %s%s%s", wifi, sync, (ac_bad || wx_bad) ? "  " "\xE2\x80\xA2" "  data stale" : "",
             hb->crashed ? "  " "\xE2\x80\xA2" "  restarted after a crash" : "");
}

void status_corner_refresh(void)
{
    refresh_clock();
    refresh_wifi();
    refresh_feed();
    refresh_routes();
    refresh_weather();
    refresh_trains();
    refresh_tube();
    refresh_bus();
    refresh_battery();
    refresh_system();
}
