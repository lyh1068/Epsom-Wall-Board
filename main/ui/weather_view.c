#include "weather_view.h"

#include <math.h>
#include <stdio.h>
#include <time.h>
#include "config.h"
#include "data/weather.h"
#include "ui/ui_util.h"

#define C_TEXT   lv_color_hex(0xECEFF1)
#define C_DIM    lv_color_hex(0x90A4AE)
#define C_RAIN   lv_color_hex(0x4FC3F7)
#define C_LINE   lv_color_hex(0xFFB300)
#define SPARK_H  44

static lv_obj_t *s_now, *s_rain, *s_chart, *s_caption;
static lv_chart_series_t *s_series;
static uint32_t s_seq = UINT32_MAX;
static int s_chart_hour = -1;
static time_t s_last_minute;

void weather_view_create(lv_obj_t *parent, int32_t x, int32_t y, int32_t w)
{
    s_now = ui_fixed_label(parent, &lv_font_montserrat_28, w, C_TEXT);
    lv_obj_set_pos(s_now, x, y);
    s_rain = ui_fixed_label(parent, &lv_font_montserrat_20, w, C_RAIN);
    lv_obj_set_pos(s_rain, x, y + 36);

    s_chart = lv_chart_create(parent);
    lv_obj_set_pos(s_chart, x, y + 66);
    lv_obj_set_size(s_chart, w, SPARK_H);
    lv_chart_set_type(s_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_div_line_count(s_chart, 0, 0);
    lv_chart_set_point_count(s_chart, WEATHER_SPARK_HOURS);
    lv_obj_set_style_bg_opa(s_chart, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_chart, 0, 0);
    lv_obj_set_style_pad_all(s_chart, 4, 0);
    lv_obj_set_style_line_width(s_chart, 3, LV_PART_ITEMS);
    lv_obj_set_style_width(s_chart, 0, LV_PART_INDICATOR);     /* no point markers */
    lv_obj_set_style_height(s_chart, 0, LV_PART_INDICATOR);
    lv_obj_set_clickable(s_chart, false);
    s_series = lv_chart_add_series(s_chart, C_LINE, LV_CHART_AXIS_PRIMARY_Y);
    lv_obj_set_hidden(s_chart, true);

    s_caption = ui_fixed_label(parent, &lv_font_montserrat_14, w, C_DIM);
    lv_obj_set_pos(s_caption, x, y + 66 + SPARK_H + 2);

    ui_set_text(s_now, "Weather: waiting...");
}

static void set_chart(const weather_t *w, int start)
{
    int n = w->hours - start;
    n = n > WEATHER_SPARK_HOURS ? WEATHER_SPARK_HOURS : n;
    if (n < 2) {
        lv_obj_set_hidden(s_chart, true);
        ui_set_text(s_caption, "");
        return;
    }
    float lo = w->h_temp[start], hi = lo;
    for (int i = start; i < start + n; i++) {
        lo = fminf(lo, w->h_temp[i]);
        hi = fmaxf(hi, w->h_temp[i]);
    }
    /* Tenths of a degree, with a little headroom so the line never sits on the edge. */
    lv_chart_set_axis_range(s_chart, LV_CHART_AXIS_PRIMARY_Y, (int32_t)floorf(lo * 10) - 5, (int32_t)ceilf(hi * 10) + 5);
    lv_chart_set_point_count(s_chart, n);
    for (int i = 0; i < n; i++) {
        lv_chart_set_series_value_by_id(s_chart, s_series, i, (int32_t)lroundf(w->h_temp[start + i] * 10));
    }
    lv_chart_refresh(s_chart);
    lv_obj_set_hidden(s_chart, false);

    char cap[64];
    snprintf(cap, sizeof(cap), "Next %d h: %d to %d\xC2\xB0" "C", n, (int)lroundf(lo), (int)lroundf(hi));
    ui_set_text(s_caption, cap);
}

void weather_view_refresh(void)
{
    weather_t w;
    weather_meta_t m;
    if (!weather_get(&w, &m)) {
        return;
    }
    time_t now = time(NULL);
    int hour = weather_hour_index(&w, now);
    if (m.seq == s_seq && now / 60 == s_last_minute) {
        return;
    }
    s_last_minute = now / 60;

    char buf[64];
    snprintf(buf, sizeof(buf), "%d\xC2\xB0" "C  %s", (int)lroundf(w.temp_c), weather_code_text(w.code));
    ui_set_text(s_now, buf);

    int r = weather_next_rain(&w, now, WEATHER_RAIN_PCT, WX_HOURS);
    if (hour < 0) {
        ui_set_text(s_rain, "Forecast out of date");
    } else if (r < 0) {
        ui_set_text(s_rain, "No rain forecast");
    } else if (r == hour) {
        snprintf(buf, sizeof(buf), "Rain now (%d%%)", w.h_pop[r]);
        ui_set_text(s_rain, buf);
    } else {
        struct tm at, today;
        localtime_r(&w.h_time[r], &at);
        localtime_r(&now, &today);
        char hhmm[8];
        strftime(hhmm, sizeof(hhmm), "%H:%M", &at);
        snprintf(buf, sizeof(buf), at.tm_yday == today.tm_yday ? "Rain from %s (%d%%)" : "Rain tomorrow %s (%d%%)",
                 hhmm, w.h_pop[r]);
        ui_set_text(s_rain, buf);
    }

    if (hour >= 0 && (m.seq != s_seq || hour != s_chart_hour)) {
        set_chart(&w, hour);
        s_chart_hour = hour;
    } else if (hour < 0) {
        lv_obj_set_hidden(s_chart, true);
    }
    s_seq = m.seq;
}
