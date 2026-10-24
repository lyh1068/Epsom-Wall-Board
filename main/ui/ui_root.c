#include "ui_root.h"

#include "ui/bus_page.h"
#include "ui/morning.h"
#include "ui/radar.h"
#include "ui/weather_page.h"

/* Pages left to right; swipe between them. The order is only this table. */
enum { PAGE_MORNING, PAGE_RADAR, PAGE_BUSES, PAGE_WEATHER, PAGE_COUNT };

static void (*const PAGE_CREATE[PAGE_COUNT])(lv_obj_t *) = {
    [PAGE_MORNING] = morning_create,
    [PAGE_RADAR] = radar_create,
    [PAGE_BUSES] = bus_page_create,
    [PAGE_WEATHER] = weather_page_create,
};

#define DOT         12
#define DOT_GAP     12
#define C_DOT_ON    lv_color_hex(0xECEFF1)
#define C_DOT_OFF   lv_color_hex(0x455A64)

static lv_obj_t *s_tv, *s_tiles[PAGE_COUNT], *s_dots[PAGE_COUNT];

static void update_dots(void)
{
    lv_obj_t *active = lv_tileview_get_tile_active(s_tv);
    for (int i = 0; i < PAGE_COUNT; i++) {
        lv_obj_set_style_bg_color(s_dots[i], active == s_tiles[i] ? C_DOT_ON : C_DOT_OFF, 0);
    }
}

static void on_tile_changed(lv_event_t *e)
{
    update_dots();
}

void ui_root_create(lv_obj_t *screen)
{
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(screen, false);

    s_tv = lv_tileview_create(screen);
    lv_obj_set_size(s_tv, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(s_tv, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_tv, LV_OPA_COVER, 0);
    lv_obj_set_scrollbar_mode(s_tv, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(s_tv, on_tile_changed, LV_EVENT_VALUE_CHANGED, NULL);

    for (int i = 0; i < PAGE_COUNT; i++) {
        lv_dir_t dir = (i > 0 ? LV_DIR_LEFT : 0) | (i < PAGE_COUNT - 1 ? LV_DIR_RIGHT : 0);
        s_tiles[i] = lv_tileview_add_tile(s_tv, i, 0, dir);
        PAGE_CREATE[i](s_tiles[i]);
    }

    /* Page dots on the screen itself, so they stay put while the pages slide. */
    const int32_t span = PAGE_COUNT * DOT + (PAGE_COUNT - 1) * DOT_GAP;
    for (int i = 0; i < PAGE_COUNT; i++) {
        lv_obj_t *d = lv_obj_create(screen);
        lv_obj_remove_style_all(d);
        lv_obj_set_clickable(d, false);
        lv_obj_set_size(d, DOT, DOT);
        lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        lv_obj_align(d, LV_ALIGN_BOTTOM_MID, -span / 2 + DOT / 2 + i * (DOT + DOT_GAP), -6);
        s_dots[i] = d;
    }
    update_dots();
}

void ui_root_show_radar(void)
{
    lv_tileview_set_tile_by_index(s_tv, PAGE_RADAR, 0, LV_ANIM_ON);
    update_dots();
}
