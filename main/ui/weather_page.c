#include "weather_page.h"

#include <math.h>
#include <stdio.h>
#include <time.h>
#include "esp_attr.h"
#include "config.h"
#include "data/weather.h"
#include "ui/ui_util.h"

/*
 * 1280x720: title; temperature and conditions; next rain; a 24 h temperature
 * line; 24 h rain-chance bars; a 3-hourly strip of time, temperature, rain %.
 * Everything changes at most hourly, so it redraws only on new data or a new hour.
 */
#define PAD         24
#define W           (1280 - 2 * PAD)
#define STRIP_N     8           /* every 3 h across 24 h */
#define REFRESH_MS  2000

#define C_TEXT      lv_color_hex(0xECEFF1)
#define C_DIM       lv_color_hex(0x90A4AE)
#define C_RAIN      lv_color_hex(0x4FC3F7)
#define C_TEMP      lv_color_hex(0xFFB300)

static lv_obj_t *s_updated, *s_temp, *s_desc, *s_rain, *s_temp_range;
static lv_obj_t *s_tchart, *s_pchart;
static lv_chart_series_t *s_tser, *s_pser;
static lv_obj_t *s_st_time[STRIP_N], *s_st_temp[STRIP_N], *s_st_pop[STRIP_N];
static uint32_t s_seq = UINT32_MAX;
static int s_hour = -2;
static long s_minute = -1;

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, lv_color_t c, int32_t x, int32_t y, int32_t w)
{
    lv_obj_t *l = ui_fixed_label(parent, font, w, c);
    lv_obj_set_pos(l, x, y);
    return l;
}

static lv_obj_t *chart(lv_obj_t *parent, int32_t y, int32_t h, lv_chart_type_t type)
{
    lv_obj_t *c = lv_chart_create(parent);
    lv_obj_set_pos(c, PAD, y);
    lv_obj_set_size(c, W, h);
    lv_chart_set_type(c, type);
    lv_chart_set_div_line_count(c, 0, 0);
    lv_chart_set_point_count(c, WX_HOURS);
    lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_pad_all(c, 4, 0);
    lv_obj_set_clickable(c, false);
    return c;
}

static void redraw(const weather_t *w, int start)
{
    int n = w->hours - start;
    n = n > WX_HOURS ? WX_HOURS : n;

    float lo = w->h_temp[start], hi = lo;
    for (int i = start; i < start + n; i++) {
        lo = fminf(lo, w->h_temp[i]);
        hi = fmaxf(hi, w->h_temp[i]);
    }
    lv_chart_set_point_count(s_tchart, n);
    lv_chart_set_point_count(s_pchart, n);
    lv_chart_set_axis_range(s_tchart, LV_CHART_AXIS_PRIMARY_Y, (int32_t)floorf(lo * 10) - 5, (int32_t)ceilf(hi * 10) + 5);
    lv_chart_set_axis_range(s_pchart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    for (int i = 0; i < n; i++) {
        lv_chart_set_series_value_by_id(s_tchart, s_tser, i, (int32_t)lroundf(w->h_temp[start + i] * 10));
        int p = w->h_pop[start + i];
        lv_chart_set_series_value_by_id(s_pchart, s_pser, i, p < 0 ? 0 : p);
    }
    lv_chart_refresh(s_tchart);
    lv_chart_refresh(s_pchart);

    char buf[48];
    snprintf(buf, sizeof(buf), "Temperature, next %d h:  %d to %d\xC2\xB0" "C", n, (int)lroundf(lo), (int)lroundf(hi));
    ui_set_text(s_temp_range, buf);

    for (int k = 0; k < STRIP_N; k++) {
        int i = start + k * 3;
        if (i >= w->hours) {
            ui_set_text(s_st_time[k], "");
            ui_set_text(s_st_temp[k], "");
            ui_set_text(s_st_pop[k], "");
            continue;
        }
        struct tm lt;
        localtime_r(&w->h_time[i], &lt);
        strftime(buf, sizeof(buf), "%H:%M", &lt);
        ui_set_text(s_st_time[k], buf);
        snprintf(buf, sizeof(buf), "%d\xC2\xB0", (int)lroundf(w->h_temp[i]));
        ui_set_text(s_st_temp[k], buf);
        if (w->h_pop[i] < 0) {
            snprintf(buf, sizeof(buf), "rain ?");
        } else {
            snprintf(buf, sizeof(buf), "rain %d%%", w->h_pop[i]);
        }
        ui_set_text(s_st_pop[k], buf);
    }
}

static void refresh_cb(lv_timer_t *t)
{
    static EXT_RAM_BSS_ATTR weather_t w;     /* off the LVGL stack, and out of internal RAM */
    weather_meta_t m;
    if (!weather_get(&w, &m)) {
        ui_set_text(s_desc, m.waiting_for_wifi ? "waiting for Wi-Fi" : "waiting for the first forecast...");
        return;
    }
    time_t now = time(NULL);
    int hour = weather_hour_index(&w, now);
    if (m.seq == s_seq && hour == s_hour && now / 60 == s_minute) {
        return;
    }
    s_minute = now / 60;

    char buf[64];
    struct tm lt;
    localtime_r(&m.last_ok, &lt);
    strftime(buf, sizeof(buf), "Open-Meteo  " "\xE2\x80\xA2" "  updated %H:%M", &lt);
    ui_set_text(s_updated, buf);
    snprintf(buf, sizeof(buf), "%d\xC2\xB0" "C", (int)lroundf(w.temp_c));
    ui_set_text(s_temp, buf);
    ui_set_text(s_desc, weather_code_text(w.code));

    int r = weather_next_rain(&w, now, WEATHER_RAIN_PCT, WX_HOURS);
    if (hour < 0) {
        snprintf(buf, sizeof(buf), "Forecast out of date");
    } else if (r < 0) {
        snprintf(buf, sizeof(buf), "No rain forecast in the next 24 h");
    } else if (r == hour) {
        snprintf(buf, sizeof(buf), "Raining now (%d%%)", w.h_pop[r]);
    } else {
        struct tm at, today;
        localtime_r(&w.h_time[r], &at);
        localtime_r(&now, &today);
        char hhmm[8];
        strftime(hhmm, sizeof(hhmm), "%H:%M", &at);
        snprintf(buf, sizeof(buf), "Rain %s%s (%d%%)", at.tm_yday == today.tm_yday ? "from " : "tomorrow from ", hhmm, w.h_pop[r]);
    }
    ui_set_text(s_rain, buf);

    if (hour >= 0 && (m.seq != s_seq || hour != s_hour)) {
        redraw(&w, hour);
    }
    s_seq = m.seq;
    s_hour = hour;
}

void weather_page_create(lv_obj_t *page)
{
    lv_obj_set_style_bg_color(page, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(page, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(page, false);

    lv_obj_t *title = label(page, &lv_font_montserrat_28, C_TEMP, PAD, 12, 600);
    lv_label_set_text(title, "Weather  " "\xE2\x80\xA2" "  Epsom");
    s_updated = label(page, &lv_font_montserrat_14, C_DIM, 700, 22, 1280 - 700 - PAD);
    lv_obj_set_style_text_align(s_updated, LV_TEXT_ALIGN_RIGHT, 0);

    s_temp = label(page, &lv_font_montserrat_48, lv_color_white(), PAD, 56, 180);
    s_desc = label(page, &lv_font_montserrat_36, C_TEXT, PAD + 190, 64, 800);
    s_rain = label(page, &lv_font_montserrat_28, C_RAIN, PAD, 126, W);

    s_temp_range = label(page, &lv_font_montserrat_14, C_DIM, PAD, 178, W);
    s_tchart = chart(page, 196, 170, LV_CHART_TYPE_LINE);
    lv_obj_set_style_line_width(s_tchart, 4, LV_PART_ITEMS);
    lv_obj_set_style_width(s_tchart, 0, LV_PART_INDICATOR);
    lv_obj_set_style_height(s_tchart, 0, LV_PART_INDICATOR);
    s_tser = lv_chart_add_series(s_tchart, C_TEMP, LV_CHART_AXIS_PRIMARY_Y);

    lv_obj_t *pl = label(page, &lv_font_montserrat_14, C_DIM, PAD, 378, W);
    lv_label_set_text(pl, "Chance of rain, next 24 h");
    s_pchart = chart(page, 396, 110, LV_CHART_TYPE_BAR);
    lv_obj_set_style_pad_column(s_pchart, 4, 0);
    s_pser = lv_chart_add_series(s_pchart, C_RAIN, LV_CHART_AXIS_PRIMARY_Y);

    const int32_t col = W / STRIP_N;
    for (int k = 0; k < STRIP_N; k++) {
        int32_t x = PAD + k * col;
        s_st_time[k] = label(page, &lv_font_montserrat_20, C_DIM, x, 528, col - 8);
        s_st_temp[k] = label(page, &lv_font_montserrat_36, lv_color_white(), x, 556, col - 8);
        s_st_pop[k] = label(page, &lv_font_montserrat_20, C_RAIN, x, 602, col - 8);
    }

    refresh_cb(NULL);
    lv_timer_create(refresh_cb, REFRESH_MS, NULL);
}
