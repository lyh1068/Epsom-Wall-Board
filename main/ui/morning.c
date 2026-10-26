#include "morning.h"

#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "esp_attr.h"
#include "config.h"
#include "data/trains.h"
#include "data/tube.h"
#include "geo/geo.h"
#include "net/sntp_time.h"
#include "ui/radar.h"
#include "ui/route_text.h"
#include "ui/status_corner.h"
#include "ui/ui_root.h"
#include "ui/ui_util.h"
#include "data/weather.h"

/*
 * Layout, 1280x720 landscape:
 *   left  (0..860):     header + toggle, disruption banner, 6 departure rows
 *   right (880..1260):  status line, clock, date, one-line weather
 *   full width below:   leave-by bar, tube pills, overhead chip
 * Departures use 36 px so they read from 3 m (PLAN.md §5).
 */
#define LEFT_W      860
#define PAD         24
#define RIGHT_X     880
#define RIGHT_W     380
#define ROW_Y0      118
#define ROW_H       54
#define BAR_Y       476
#define TUBE_Y      566
#define CHIP_Y      626
#define REFRESH_MS  500

#define C_AMBER     lv_color_hex(0xFFB000)
#define C_RED       lv_color_hex(0xFF5252)
#define C_GONE      lv_color_hex(0x5F5F5F)
#define C_TEXT      lv_color_hex(0xECEFF1)
#define C_DIM       lv_color_hex(0x90A4AE)
#define C_OK        lv_color_hex(0x81C784)

typedef struct {
    lv_obj_t *time, *dest, *plat, *status;
} row_t;

static row_t s_rows[TRAINS_ROWS];
static lv_obj_t *s_banner, *s_toggle[2];
static lv_obj_t *s_row_hit[TRAINS_ROWS];
static int s_row_train[TRAINS_ROWS];        /* row -> index into s_board, -1 if empty */
static EXT_RAM_BSS_ATTR train_board_t s_board;  /* what the rows show; out of internal RAM */
static lv_obj_t *s_status, *s_clock, *s_date;
static lv_obj_t *s_bar, *s_bar_text;
static lv_obj_t *s_pill[TUBE_MAX_LINES], *s_pill_text[TUBE_MAX_LINES], *s_tube_demo;
static lv_obj_t *s_chip, *s_chip_text;
static lv_obj_t *s_wx_now, *s_wx_rain;
static bool s_london_only;
static char s_chip_hex[8];

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, lv_color_t c, int32_t x, int32_t y, int32_t w)
{
    lv_obj_t *l = ui_fixed_label(parent, font, w, c);
    lv_obj_set_pos(l, x, y);
    return l;
}

static lv_obj_t *demo_badge(lv_obj_t *parent, int32_t x, int32_t y)
{
    lv_obj_t *b = lv_label_create(parent);
    lv_label_set_text(b, "DEMO");
    lv_obj_set_style_text_font(b, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(b, lv_color_white(), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(0xC62828), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(b, 6, 0);
    lv_obj_set_style_pad_ver(b, 2, 0);
    lv_obj_set_style_radius(b, 4, 0);
    lv_obj_set_pos(b, x, y);
    return b;
}

static const char *short_dest(const char *dest)
{
    return strncmp(dest, "London ", 7) == 0 ? dest + 7 : dest;
}

/* ---- departures ------------------------------------------------------------------- */

static void refresh_trains(void)
{
    trains_meta_t m;
    bool have = trains_get(&s_board, &m);

    time_t now = time(NULL);
    struct tm lt;
    localtime_r(&now, &lt);
    int now_min = lt.tm_hour * 60 + lt.tm_min;

    int r = 0;
    for (int i = 0; have && i < s_board.count && r < TRAINS_ROWS; i++) {
        const train_t *t = &s_board.t[i];
        if (s_london_only && !t->london) {
            continue;
        }
        int exp;
        etd_kind_t k = train_etd(t, &exp);
        int until = exp >= 0 ? minutes_until(now_min, exp) : minutes_until(now_min, hhmm_to_min(t->std));
        if (until < 0 && k != ETD_CANCELLED) {
            continue;   /* gone */
        }

        char status[16];
        lv_color_t c = C_AMBER;
        switch (k) {
        case ETD_ON_TIME:
            snprintf(status, sizeof(status), "On time");
            break;
        case ETD_EXPECTED:
            snprintf(status, sizeof(status), "Exp %s", t->etd);
            if (train_delay_min(t) > 5) {
                c = C_RED;
            }
            break;
        case ETD_DELAYED:
            snprintf(status, sizeof(status), "Delayed");
            c = C_RED;
            break;
        case ETD_CANCELLED:
            snprintf(status, sizeof(status), "Cancelled");
            c = C_RED;
            break;
        default:
            snprintf(status, sizeof(status), "%.15s", t->etd);
            break;
        }
        /* Can't make it on foot: dimmed, not hidden, so the board still reads true. */
        if (k != ETD_CANCELLED && until < WALK_MIN) {
            c = C_GONE;
        }

        char plat[8] = "";
        if (t->platform[0]) {
            snprintf(plat, sizeof(plat), "P%s", t->platform);
        }
        s_row_train[r] = i;
        ui_set_hidden(s_row_hit[r], false);
        row_t *row = &s_rows[r++];
        ui_set_text(row->time, t->std);
        ui_set_text(row->dest, t->dest);
        ui_set_text(row->plat, plat);
        ui_set_text(row->status, status);
        ui_set_text_color(row->time, c);
        ui_set_text_color(row->dest, c);
        ui_set_text_color(row->plat, c);
        ui_set_text_color(row->status, c);
    }
    /* Empty rows: say why there are no trains, instead of a blank board. */
    const char *why[3] = { "", "", "" };
    if (have) {
        why[0] = r == 0 ? (s_london_only ? "No London trains in the next 2 hours" : "No departures in the next 2 hours") : "";
    } else if (m.no_key) {
        why[0] = "No live departures";
        why[1] = "needs RAIL_DATA_KEY";
        why[2] = "(see .env.example)";
    } else if (m.waiting_for_wifi) {
        why[0] = "Waiting for Wi-Fi...";
    } else if (m.last_err != ESP_OK || (m.http_status && m.http_status != 200)) {
        why[0] = "National Rail not answering";
        why[1] = "(details on the radar page)";
    } else {
        why[0] = "Fetching departures...";
    }
    for (int e = 0; r < TRAINS_ROWS; r++, e++) {
        s_row_train[r] = -1;
        ui_set_hidden(s_row_hit[r], true);
        ui_set_text(s_rows[r].time, "");
        ui_set_text(s_rows[r].dest, e < 3 ? why[e] : "");
        ui_set_text_color(s_rows[r].dest, C_DIM);
        ui_set_text(s_rows[r].plat, "");
        ui_set_text(s_rows[r].status, "");
    }

    ui_set_hidden(s_banner, !have || s_board.nrcc[0] == '\0');
    if (have && s_board.nrcc[0]) {
        ui_set_text(s_banner, s_board.nrcc);
    }

    /* Leave-by bar (PLAN.md §5.1): green at 0-2 min, grey with nothing in the next hour. */
    leave_by_t lb;
    char bar[128];
    uint32_t bg;
    if (have && leave_by(s_board.t, s_board.count, now_min, WALK_MIN, LEAVE_BY_HORIZON_MIN, &lb)) {
        const train_t *t = &s_board.t[lb.index];
        char exp[24] = "";
        if (strcmp(t->etd, "On time") != 0 && hhmm_to_min(t->etd) >= 0) {
            snprintf(exp, sizeof(exp), " (exp %s)", t->etd);
        }
        if (lb.leave_in <= 2) {
            snprintf(bar, sizeof(bar), "GO NOW  " LV_SYMBOL_RIGHT "  %s to %s%s", t->std, short_dest(t->dest), exp);
            bg = 0x2E7D32;
        } else {
            snprintf(bar, sizeof(bar), "LEAVE IN %d MIN  " LV_SYMBOL_RIGHT "  %s to %s%s", lb.leave_in, t->std,
                     short_dest(t->dest), exp);
            bg = 0x0D47A1;
        }
    } else {
        if (have) {
            snprintf(bar, sizeof(bar), "No London trains to catch in the next %d min", LEAVE_BY_HORIZON_MIN);
        } else {
            snprintf(bar, sizeof(bar), "Leave-by countdown starts once live trains arrive");
        }
        bg = 0x37474F;
    }
    ui_set_text(s_bar_text, bar);
    if (!lv_color_eq(lv_obj_get_style_bg_color(s_bar, LV_PART_MAIN), lv_color_hex(bg))) {
        lv_obj_set_style_bg_color(s_bar, lv_color_hex(bg), 0);
    }
}

static void on_toggle(lv_event_t *e)
{
    s_london_only = (intptr_t)lv_event_get_user_data(e) == 1;
    lv_obj_set_state(s_toggle[0], LV_STATE_CHECKED, !s_london_only);
    lv_obj_set_state(s_toggle[1], LV_STATE_CHECKED, s_london_only);
    refresh_trains();
}

/* ---- right column, tube, overhead -------------------------------------------------- */

static void refresh_clock(void)
{
    time_status_t ts;
    time_get_status(&ts);
    char buf[32];
    if (ts.source == CLOCK_SRC_NONE) {
        ui_set_text(s_clock, "--:--");
        ui_set_text(s_date, "clock not set");
    } else {
        time_t now = time(NULL);
        struct tm lt;
        localtime_r(&now, &lt);
        strftime(buf, sizeof(buf), "%H:%M", &lt);
        ui_set_text(s_clock, buf);
        strftime(buf, sizeof(buf), "%a %d %b", &lt);
        ui_set_text(s_date, buf);
    }

    bool problem;
    char line[96];
    status_compact(line, sizeof(line), &problem);
    ui_set_text(s_status, line);
    ui_set_text_color(s_status, problem ? C_RED : C_DIM);
}

static const uint32_t TUBE_COLOR[] = {
    [TUBE_GOOD] = 0x00E676, [TUBE_MINOR] = 0xFFB300, [TUBE_SEVERE] = 0xFF1744,
    [TUBE_CLOSED] = 0x78909C, [TUBE_UNKNOWN] = 0x78909C,
};

static EXT_RAM_BSS_ATTR tube_status_t s_tube;
static lv_obj_t *s_detail;     /* detail overlay, NULL when closed */

static void close_detail(lv_event_t *e)
{
    if (s_detail) {
        lv_obj_delete_async(s_detail);
        s_detail = NULL;
    }
}

/* A dimmed full-screen overlay with a titled text box; any tap closes it. */
static void open_detail(const char *title, const char *text, uint32_t accent)
{
    if (s_detail != NULL) {
        return;
    }
    s_detail = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_detail);
    lv_obj_set_size(s_detail, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(s_detail, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_detail, LV_OPA_70, 0);
    lv_obj_set_clickable(s_detail, true);       /* swallow taps meant for the page */
    lv_obj_add_event_cb(s_detail, close_detail, LV_EVENT_CLICKED, NULL);

    lv_obj_t *box = lv_obj_create(s_detail);
    lv_obj_remove_style_all(box);
    lv_obj_set_clickable(box, false);
    lv_obj_set_scrollable(box, false);
    lv_obj_set_size(box, 960, LV_SIZE_CONTENT);
    lv_obj_center(box);
    lv_obj_set_style_bg_color(box, lv_color_hex(0x1E272E), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(box, 12, 0);
    lv_obj_set_style_pad_all(box, 28, 0);
    lv_obj_set_style_border_width(box, 4, 0);
    lv_obj_set_style_border_side(box, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_border_color(box, lv_color_hex(accent), 0);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(box, 14, 0);

    lv_obj_t *t = lv_label_create(box);
    lv_obj_set_width(t, lv_pct(100));
    lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(t, lv_color_white(), 0);
    lv_label_set_text(t, title);

    lv_obj_t *body = lv_label_create(box);
    lv_obj_set_width(body, lv_pct(100));
    lv_label_set_long_mode(body, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_font(body, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(body, C_TEXT, 0);
    lv_label_set_text(body, text);

    lv_obj_t *hint = lv_label_create(box);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hint, C_DIM, 0);
    lv_label_set_text(hint, "Tap anywhere to close");
}

/* Tap a pill: the line's status and TfL's reason text (PLAN.md §5.1). */
static void on_pill_clicked(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i >= s_tube.count) {
        return;
    }
    const tube_line_t *l = &s_tube.line[i];
    char title[64];
    snprintf(title, sizeof(title), "%s: %s", l->name, l->status);
    open_detail(title, l->reason[0] ? l->reason : "No further information from TfL.", TUBE_COLOR[l->severity]);
}

/* Tap a departure: platform, operator, length and where it calls (PLAN.md §5.1). */
static void on_row_clicked(lv_event_t *e)
{
    int r = (int)(intptr_t)lv_event_get_user_data(e);
    if (r < 0 || r >= TRAINS_ROWS || s_row_train[r] < 0 || s_row_train[r] >= s_board.count) {
        return;
    }
    const train_t *t = &s_board.t[s_row_train[r]];
    static EXT_RAM_BSS_ATTR char text[TRAINS_CALLS_MAX * 64 + 160];

    char title[64], coaches[32] = "", plat[32] = "platform not yet announced";
    snprintf(title, sizeof(title), "%s to %s", t->std, t->dest);
    if (t->platform[0]) {
        snprintf(plat, sizeof(plat), "Platform %s", t->platform);
    }
    if (t->coaches > 0) {
        snprintf(coaches, sizeof(coaches), "  " "\xE2\x80\xA2" "  %d coaches", t->coaches);
    }
    int n = snprintf(text, sizeof(text), "%s  " "\xE2\x80\xA2" "  %s%s  " "\xE2\x80\xA2" "  %s\n\n%s", plat,
                     t->operator_name, coaches, t->etd, t->ncalls ? "Calling at:" : "No calling points given.");
    for (int i = 0; i < t->ncalls && n > 0 && n < (int)sizeof(text); i++) {
        const train_call_t *c = &t->calls[i];
        char when[24] = "";
        if (strcmp(c->et, "Cancelled") == 0) {
            snprintf(when, sizeof(when), "  (cancelled)");
        } else if (c->et[0] && strcmp(c->et, "On time") != 0) {
            snprintf(when, sizeof(when), "  (%s%s)", hhmm_to_min(c->et) >= 0 ? "exp " : "", c->et);
        }
        n += snprintf(text + n, sizeof(text) - n, "\n%s   %s%s", c->st, c->name, when);
    }
    int exp;
    etd_kind_t k = train_etd(t, &exp);
    bool bad = k == ETD_CANCELLED || k == ETD_DELAYED || train_delay_min(t) > 5;
    open_detail(title, text, bad ? 0xFF5252 : 0xFFB000);
}

/* Tap the banner: the whole disruption message, which one line cuts short. */
static void on_banner_clicked(lv_event_t *e)
{
    if (s_board.nrcc[0]) {
        open_detail("National Rail disruption", s_board.nrcc, 0xFFD54F);
    }
}

static void refresh_tube(void)
{
    static EXT_RAM_BSS_ATTR tube_status_t ts;  /* off the LVGL stack, and out of internal RAM */
    tube_meta_t m;
    bool have = tube_get(&ts, &m);
    if (have) {
        s_tube = ts;
    }
    ui_set_hidden(s_tube_demo, !(have && ts.demo));
    for (int i = 0; i < TUBE_MAX_LINES; i++) {
        if (!have || i >= ts.count) {
            ui_set_hidden(s_pill[i], true);
            continue;
        }
        const tube_line_t *l = &ts.line[i];
        const uint32_t *dot = TUBE_COLOR;
        char buf[128];
        /* Good service is just the dot; anything else says what is wrong. */
        if (l->severity == TUBE_GOOD) {
            snprintf(buf, sizeof(buf), "#%06lX \xE2\x80\xA2# %s", (unsigned long)dot[l->severity], l->name);
        } else {
            snprintf(buf, sizeof(buf), "#%06lX \xE2\x80\xA2# %s  #%06lX %s#", (unsigned long)dot[l->severity], l->name,
                     (unsigned long)dot[l->severity], l->status);
        }
        ui_set_text(s_pill_text[i], buf);
        ui_set_hidden(s_pill[i], false);
    }
}

static void refresh_weather(void)
{
    static EXT_RAM_BSS_ATTR weather_t w;      /* off the LVGL stack, and out of internal RAM */
    if (!weather_get(&w, NULL)) {
        ui_set_text(s_wx_now, "Weather: waiting...");
        return;
    }
    char buf[64];
    snprintf(buf, sizeof(buf), "%d\xC2\xB0" "C  %s", (int)lroundf(w.temp_c), weather_code_text(w.code));
    ui_set_text(s_wx_now, buf);

    time_t now = time(NULL);
    int hour = weather_hour_index(&w, now);
    int r = weather_next_rain(&w, now, WEATHER_RAIN_PCT, WX_HOURS);
    if (hour < 0) {
        snprintf(buf, sizeof(buf), "Forecast out of date");
    } else if (r < 0) {
        snprintf(buf, sizeof(buf), "No rain forecast");
    } else if (r == hour) {
        snprintf(buf, sizeof(buf), "Rain now (%d%%)", w.h_pop[r]);
    } else {
        struct tm at, today;
        localtime_r(&w.h_time[r], &at);
        localtime_r(&now, &today);
        char hhmm[8];
        strftime(hhmm, sizeof(hhmm), "%H:%M", &at);
        snprintf(buf, sizeof(buf), "Rain %s%s (%d%%)", at.tm_yday == today.tm_yday ? "from " : "tomorrow ", hhmm, w.h_pop[r]);
    }
    ui_set_text(s_wx_rain, buf);
}

static void refresh_chip(void)
{
    radar_target_t t;
    if (!radar_overhead(&t)) {
        ui_set_hidden(s_chip, true);
        s_chip_hex[0] = '\0';
        return;
    }
    strlcpy(s_chip_hex, t.hex, sizeof(s_chip_hex));
    char alt[16], rt[40], buf[160];
    ui_fmt_thousands(alt, sizeof(alt), t.alt_baro);
    route_short(t.flight, rt, sizeof(rt));
    const char *trend = !t.has_rate || abs(t.baro_rate) < 200 ? "" : (t.baro_rate > 0 ? " " LV_SYMBOL_UP : " " LV_SYMBOL_DOWN);
    snprintf(buf, sizeof(buf), "Overhead:  %s  %s  " "\xE2\x80\xA2" "  %s ft%s  " "\xE2\x80\xA2" "  %s  " "\xE2\x80\xA2" "  %.1f nm %s",
             t.flight[0] ? t.flight : t.hex, t.type, t.has_alt ? alt : "?", trend, rt, t.dist_nm,
             geo_compass16(t.bearing_deg));
    ui_set_text(s_chip_text, buf);
    ui_set_hidden(s_chip, false);
}

/* Tap the chip: jump to the radar with that aircraft selected (PLAN.md §5.1). */
static void on_chip_clicked(lv_event_t *e)
{
    if (s_chip_hex[0]) {
        radar_select_hex(s_chip_hex);
        ui_root_show_radar();
    }
}

static void refresh_cb(lv_timer_t *t)
{
    refresh_clock();
    refresh_trains();
    refresh_tube();
    refresh_chip();
    refresh_weather();
}

/* ---- build ------------------------------------------------------------------------ */

static lv_obj_t *toggle_button(lv_obj_t *parent, const char *text, int32_t x, intptr_t id)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_size(b, 120, 44);
    lv_obj_set_pos(b, x, 18);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x263238), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x8D6E00), LV_STATE_CHECKED);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_add_event_cb(b, on_toggle, LV_EVENT_CLICKED, (void *)id);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(l, C_TEXT, 0);
    lv_obj_center(l);
    return b;
}

void morning_create(lv_obj_t *page)
{
    lv_obj_set_style_bg_color(page, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(page, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(page, false);

    /* Header, demo badge, All | London toggle. */
    lv_obj_t *h = label(page, &lv_font_montserrat_36, C_AMBER, PAD, 18, 420);
    lv_label_set_text(h, "EPSOM " LV_SYMBOL_RIGHT " London");
    s_toggle[0] = toggle_button(page, "All", LEFT_W - 260, 0);
    s_toggle[1] = toggle_button(page, "London", LEFT_W - 136, 1);
    lv_obj_set_state(s_toggle[0], LV_STATE_CHECKED, true);

    s_banner = label(page, &lv_font_montserrat_20, lv_color_black(), PAD - 8, 72, LEFT_W - 2 * PAD + 16);
    lv_obj_set_style_bg_color(s_banner, lv_color_hex(0xFFD54F), 0);
    lv_obj_set_style_bg_opa(s_banner, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_banner, 8, 0);
    lv_obj_set_style_radius(s_banner, 6, 0);
    /* One line, cut with dots; tapping shows the whole message. */
    lv_label_set_long_mode(s_banner, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_height(s_banner, 24 + 2 * 8);
    lv_obj_set_clickable(s_banner, true);
    lv_obj_add_event_cb(s_banner, on_banner_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_set_hidden(s_banner, true);

    for (int i = 0; i < TRAINS_ROWS; i++) {
        int32_t y = ROW_Y0 + i * ROW_H;
        /* Tap target under the row's labels (labels are not clickable, so taps
         * fall through to it): opens the calling points. */
        s_row_hit[i] = lv_obj_create(page);
        lv_obj_remove_style_all(s_row_hit[i]);
        lv_obj_set_scrollable(s_row_hit[i], false);
        lv_obj_set_pos(s_row_hit[i], PAD - 8, y - 4);
        lv_obj_set_size(s_row_hit[i], LEFT_W - 2 * PAD + 16, ROW_H);
        lv_obj_set_clickable(s_row_hit[i], true);
        lv_obj_add_event_cb(s_row_hit[i], on_row_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_set_hidden(s_row_hit[i], true);
        s_row_train[i] = -1;
        s_rows[i].time = label(page, &lv_font_montserrat_36, C_AMBER, PAD, y, 120);
        s_rows[i].dest = label(page, &lv_font_montserrat_36, C_AMBER, PAD + 130, y, 420);
        s_rows[i].plat = label(page, &lv_font_montserrat_36, C_AMBER, PAD + 560, y, 76);
        s_rows[i].status = label(page, &lv_font_montserrat_36, C_AMBER, PAD + 640, y, LEFT_W - PAD - 640 - PAD + 24);
        lv_obj_set_style_text_align(s_rows[i].status, LV_TEXT_ALIGN_RIGHT, 0);
    }
    lv_obj_t *hint = label(page, &lv_font_montserrat_14, C_DIM, PAD, ROW_Y0 + TRAINS_ROWS * ROW_H + 4, 600);
    lv_label_set_text_fmt(hint, "Dimmed: under the %d min walk to the station  " "\xE2\x80\xA2" "  tap a train for its stops", WALK_MIN);

    /* Right column. */
    s_status = label(page, &lv_font_montserrat_14, C_DIM, RIGHT_X, 18, RIGHT_W);
    s_clock = label(page, &lv_font_montserrat_48, lv_color_white(), RIGHT_X, 40, RIGHT_W);
    s_date = label(page, &lv_font_montserrat_28, C_TEXT, RIGHT_X, 100, RIGHT_W);
    /* Weather in one glance; the Weather page has the rest. */
    s_wx_now = label(page, &lv_font_montserrat_28, C_TEXT, RIGHT_X, 156, RIGHT_W);
    s_wx_rain = label(page, &lv_font_montserrat_20, lv_color_hex(0x4FC3F7), RIGHT_X, 194, RIGHT_W);

    /* Leave-by bar. */
    s_bar = lv_obj_create(page);
    lv_obj_remove_style_all(s_bar);
    lv_obj_set_scrollable(s_bar, false);
    lv_obj_set_clickable(s_bar, false);
    lv_obj_set_pos(s_bar, 16, BAR_Y);
    lv_obj_set_size(s_bar, 1248, 76);
    lv_obj_set_style_radius(s_bar, 10, 0);
    lv_obj_set_style_bg_opa(s_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(0x37474F), 0);
    s_bar_text = label(s_bar, &lv_font_montserrat_36, lv_color_white(), 24, 18, 1200);

    /* Tube status pills. Recolour markup lets the dot and status take the line colour. */
    lv_obj_t *row = lv_obj_create(page);
    lv_obj_remove_style_all(row);
    lv_obj_set_scrollable(row, false);
    lv_obj_set_clickable(row, false);
    lv_obj_set_pos(row, 16, TUBE_Y);
    lv_obj_set_size(row, 1248, 48);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 12, 0);
    for (int i = 0; i < TUBE_MAX_LINES; i++) {
        s_pill[i] = lv_obj_create(row);
        lv_obj_remove_style_all(s_pill[i]);
        lv_obj_set_scrollable(s_pill[i], false);
        lv_obj_set_clickable(s_pill[i], true);
        lv_obj_add_event_cb(s_pill[i], on_pill_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_set_size(s_pill[i], LV_SIZE_CONTENT, 44);
        lv_obj_set_style_bg_color(s_pill[i], lv_color_hex(0x1E272E), 0);
        lv_obj_set_style_bg_opa(s_pill[i], LV_OPA_COVER, 0);
        lv_obj_set_style_radius(s_pill[i], 22, 0);
        lv_obj_set_style_pad_hor(s_pill[i], 16, 0);
        s_pill_text[i] = lv_label_create(s_pill[i]);
        lv_label_set_recolor(s_pill_text[i], true);
        lv_obj_set_style_text_font(s_pill_text[i], &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(s_pill_text[i], C_TEXT, 0);
        lv_label_set_text(s_pill_text[i], "");
        lv_obj_align(s_pill_text[i], LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_set_hidden(s_pill[i], true);
    }
    s_tube_demo = demo_badge(row, 0, 0);

    /* Overhead chip. */
    s_chip = lv_button_create(page);
    lv_obj_set_pos(s_chip, 16, CHIP_Y);
    lv_obj_set_size(s_chip, 1248, 60);
    lv_obj_set_style_bg_color(s_chip, lv_color_hex(0x1A237E), 0);
    lv_obj_set_style_shadow_width(s_chip, 0, 0);
    lv_obj_set_style_radius(s_chip, 10, 0);
    lv_obj_add_event_cb(s_chip, on_chip_clicked, LV_EVENT_CLICKED, NULL);
    s_chip_text = label(s_chip, &lv_font_montserrat_28, lv_color_white(), 8, 2, 1200);
    lv_obj_set_hidden(s_chip, true);

    refresh_cb(NULL);
    lv_timer_create(refresh_cb, REFRESH_MS, NULL);
}
