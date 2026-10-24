#include "ui_root.h"

#include "ui/morning.h"
#include "ui/radar.h"

#define DOT         12
#define DOT_GAP     12
#define C_DOT_ON    lv_color_hex(0xECEFF1)
#define C_DOT_OFF   lv_color_hex(0x455A64)

static lv_obj_t *s_tv, *s_tiles[2], *s_dots[2];

static void update_dots(void)
{
    lv_obj_t *active = lv_tileview_get_tile_active(s_tv);
    for (int i = 0; i < 2; i++) {
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

    s_tiles[0] = lv_tileview_add_tile(s_tv, 0, 0, LV_DIR_RIGHT);
    s_tiles[1] = lv_tileview_add_tile(s_tv, 1, 0, LV_DIR_LEFT);
    morning_create(s_tiles[0]);
    radar_create(s_tiles[1]);

    /* Page dots on the screen itself, so they stay put while the pages slide. */
    for (int i = 0; i < 2; i++) {
        lv_obj_t *d = lv_obj_create(screen);
        lv_obj_remove_style_all(d);
        lv_obj_set_clickable(d, false);
        lv_obj_set_size(d, DOT, DOT);
        lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        lv_obj_align(d, LV_ALIGN_BOTTOM_MID, (i == 0 ? -1 : 1) * (DOT + DOT_GAP) / 2, -6);
        s_dots[i] = d;
    }
    update_dots();
}

void ui_root_show_radar(void)
{
    lv_tileview_set_tile_by_index(s_tv, 1, 0, LV_ANIM_ON);
    update_dots();
}
