#include "diag.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "esp_log.h"
#include "net/sntp_time.h"
#include "net/wifi.h"

#if CONFIG_LV_FONT_MONTSERRAT_28
#define DIAG_FONT (&lv_font_montserrat_28)
#else
#define DIAG_FONT LV_FONT_DEFAULT
#endif

#define DOT_SIZE 48

/*
 * The BSP decides the panel variant in a static function and only reports it in
 * a log line ("Discovered board version N (LCD ..., Touch ...)"). Rather than
 * re-probing the touch controller ourselves, catch that line on its way to the
 * console, so the screen shows exactly what the BSP chose.
 */
static vprintf_like_t s_prev_vprintf;
static char s_panel[96];

static int capture_vprintf(const char *fmt, va_list args)
{
    /* Other tasks (Wi-Fi) log during this window too: match on the format
     * string first, so only the BSP's line is ever formatted here. */
    if (s_panel[0] == '\0'
        && (strstr(fmt, "board version") != NULL || strstr(fmt, "Unsupported ST712x") != NULL)) {
        char line[160];
        va_list copy;
        va_copy(copy, args);
        vsnprintf(line, sizeof(line), fmt, copy);
        va_end(copy);

        const char *hit = strstr(line, "board version");
        if (hit == NULL) {
            hit = strstr(line, "Unsupported");
        }
        if (hit != NULL) {
            size_t n = strcspn(hit, "\r\n\033");
            if (n >= sizeof(s_panel)) {
                n = sizeof(s_panel) - 1;
            }
            memcpy(s_panel, hit, n);
            s_panel[n] = '\0';
        }
    }
    return s_prev_vprintf(fmt, args);
}

void diag_capture_begin(void)
{
    s_prev_vprintf = esp_log_set_vprintf(capture_vprintf);
}

void diag_capture_end(void)
{
    esp_log_set_vprintf(s_prev_vprintf);
}

static lv_obj_t *s_panel_label;
static lv_obj_t *s_clock_label;
static lv_obj_t *s_time_label;
static lv_obj_t *s_wifi_label;
static lv_obj_t *s_touch_label;
static lv_obj_t *s_dot;
static uint32_t s_presses;
static lv_point_t s_last;

static void show_touch(bool pressed)
{
    lv_label_set_text_fmt(s_touch_label, "Touch: x=%d y=%d  %s  presses: %lu",
                          (int)s_last.x, (int)s_last.y, pressed ? "DOWN" : "up",
                          (unsigned long)s_presses);
}

/* Discrete press/release, sent by LVGL for every press however short. */
static void touch_event_cb(lv_event_t *e)
{
    lv_indev_t *indev = lv_event_get_user_data(e);
    lv_indev_get_point(indev, &s_last);
    if (lv_event_get_code(e) == LV_EVENT_PRESSED) {
        s_presses++;
        show_touch(true);
    } else {
        lv_obj_set_hidden(s_dot, true);
        show_touch(false);
    }
}

/* LVGL does not send drag updates to the indev, so follow the finger on a timer. */
static void touch_poll_cb(lv_timer_t *t)
{
    lv_indev_t *indev = lv_timer_get_user_data(t);
    if (lv_indev_get_state(indev) != LV_INDEV_STATE_PRESSED) {
        return;
    }
    lv_indev_get_point(indev, &s_last);
    lv_obj_set_pos(s_dot, s_last.x - DOT_SIZE / 2, s_last.y - DOT_SIZE / 2);
    lv_obj_set_hidden(s_dot, false);
    show_touch(true);
}

static void show_clock(void)
{
    time_status_t ts;
    time_get_status(&ts);

    time_t now = time(NULL);
    struct tm lt;
    localtime_r(&now, &lt);
    char buf[48];
    strftime(buf, sizeof(buf), "%H:%M:%S  %a %d %b %Y %Z", &lt);
    if (ts.source == CLOCK_SRC_NONE) {
        lv_label_set_text_fmt(s_clock_label, "%s  (not set)", buf);
    } else {
        lv_label_set_text(s_clock_label, buf);
    }

    static const char *const rtc_txt[] = {
        [RTC_ST_UNKNOWN] = "?", [RTC_ST_OK] = "ok", [RTC_ST_INVALID] = "lost power",
        [RTC_ST_MISSING] = "not found", [RTC_ST_ERROR] = "I2C error",
    };
    const char *rtc = rtc_txt[ts.rtc];
    if (ts.rtc_written) {
        rtc = "updated from NTP";
    }

    if (ts.sntp_err != ESP_OK) {
        lv_label_set_text_fmt(s_time_label, "Time: SNTP did not start (%s)  RTC: %s",
                              esp_err_to_name(ts.sntp_err), rtc);
    } else if (ts.source == CLOCK_SRC_NTP) {
        struct tm st;
        localtime_r(&ts.last_sync, &st);
        strftime(buf, sizeof(buf), "%H:%M", &st);
        lv_label_set_text_fmt(s_time_label, "Time: NTP synced %s  RTC: %s", buf, rtc);
    } else {
        lv_label_set_text_fmt(s_time_label, "Time: %s, waiting for NTP  RTC: %s",
                              ts.source == CLOCK_SRC_RTC ? "from RTC" : "no source", rtc);
    }
}

static void show_wifi(void)
{
    wifi_status_t ws;
    wifi_get_status(&ws);
    const char *link = ws.link_up ? "C6 link up" : (ws.link_seen_down ? "C6 link DOWN" : "C6 link ?");

    switch (ws.state) {
    case WIFI_ST_OFF:
    case WIFI_ST_STARTING:
        lv_label_set_text_fmt(s_wifi_label, "Wi-Fi: starting...  %s", link);
        break;
    case WIFI_ST_FAILED:
        lv_label_set_text_fmt(s_wifi_label, "Wi-Fi: %s failed: %s  %s",
                              ws.init_step, esp_err_to_name(ws.init_err), link);
        break;
    case WIFI_ST_NOT_CONFIGURED:
        lv_label_set_text_fmt(s_wifi_label,
                              "Wi-Fi: not configured - set WIFI_SSID in .env  %s, MAC %02x:%02x:%02x:%02x:%02x:%02x",
                              link, ws.mac[0], ws.mac[1], ws.mac[2], ws.mac[3], ws.mac[4], ws.mac[5]);
        break;
    case WIFI_ST_CONNECTING:
        if (ws.last_reason != 0) {
            lv_label_set_text_fmt(s_wifi_label, "Wi-Fi: retrying \"%s\" (%lu): reason %u, %s",
                                  ws.ssid, (unsigned long)ws.retries, ws.last_reason,
                                  wifi_reason_str(ws.last_reason));
        } else {
            lv_label_set_text_fmt(s_wifi_label, "Wi-Fi: connecting to \"%s\"...  %s", ws.ssid, link);
        }
        break;
    case WIFI_ST_CONNECTED:
        lv_label_set_text_fmt(s_wifi_label, "Wi-Fi: \"%s\"  %s  %d dBm", ws.ssid, ws.ip, ws.rssi);
        break;
    }
}

/* The UI only reads module status here; all networking stays in its own tasks. */
static void status_poll_cb(lv_timer_t *t)
{
    show_clock();
    show_wifi();
}

static lv_obj_t *make_label(lv_obj_t *parent)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_width(label, lv_pct(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(label, DIAG_FONT, 0);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    return label;
}

void diag_overlay_create(lv_indev_t *touch)
{
    /* The top layer stays above whatever the screen draws. Nothing here is
     * clickable, so touches still reach the UI underneath. */
    lv_obj_t *box = lv_obj_create(lv_layer_top());
    lv_obj_set_clickable(box, false);
    lv_obj_set_scrollable(box, false);
    lv_obj_set_width(box, lv_pct(100));
    lv_obj_set_height(box, LV_SIZE_CONTENT);
    lv_obj_align(box, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_color(box, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_70, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_radius(box, 0, 0);
    lv_obj_set_style_pad_all(box, 16, 0);
    lv_obj_set_style_pad_row(box, 8, 0);

    s_panel_label = make_label(box);
    if (s_panel[0] != '\0') {
        lv_label_set_text_fmt(s_panel_label, "Panel: %s", s_panel);
    } else {
        /* No evidence either way: the BSP's log line was not seen. */
        lv_label_set_text(s_panel_label, "Panel: detection line not captured");
    }

    s_clock_label = make_label(box);
    s_time_label = make_label(box);
    s_wifi_label = make_label(box);
    status_poll_cb(NULL);
    lv_timer_create(status_poll_cb, 500, NULL);

    s_touch_label = make_label(box);
    if (touch == NULL) {
        lv_label_set_text(s_touch_label, "Touch: no input device");
        return;
    }
    lv_label_set_text(s_touch_label, "Touch: none yet - tap the screen");

    s_dot = lv_obj_create(lv_layer_top());
    lv_obj_set_clickable(s_dot, false);
    lv_obj_set_scrollable(s_dot, false);
    lv_obj_set_hidden(s_dot, true);
    lv_obj_set_size(s_dot, DOT_SIZE, DOT_SIZE);
    lv_obj_set_style_radius(s_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_dot, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_set_style_bg_opa(s_dot, LV_OPA_80, 0);
    lv_obj_set_style_border_width(s_dot, 0, 0);

    lv_indev_add_event_cb(touch, touch_event_cb, LV_EVENT_PRESSED, touch);
    lv_indev_add_event_cb(touch, touch_event_cb, LV_EVENT_RELEASED, touch);
    lv_timer_create(touch_poll_cb, 20, touch);
}
