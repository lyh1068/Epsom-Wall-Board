#include "ui_root.h"

#include "bsp/esp-bsp.h"
#include "ui/bus_page.h"
#include "ui/morning.h"
#include "ui/radar.h"
#include "ui/sky_page.h"
#include "ui/weather_page.h"

/*
 * Pages left to right, one visible at a time. A swipe cuts straight to the
 * next page: one full redraw. A finger-following slide re-renders, rotates
 * and copies the whole screen into the live framebuffer every frame, which on
 * this board is slow and tears (tab5-board skill, display.md).
 */
enum { PAGE_MORNING, PAGE_RADAR, PAGE_BUSES, PAGE_WEATHER, PAGE_SKY, PAGE_COUNT };

static void (*const PAGE_CREATE[PAGE_COUNT])(lv_obj_t *) = {
    [PAGE_MORNING] = morning_create,
    [PAGE_RADAR] = radar_create,
    [PAGE_BUSES] = bus_page_create,
    [PAGE_WEATHER] = weather_page_create,
    [PAGE_SKY] = sky_page_create,
};

#define DOT         12
#define DOT_GAP     12
#define C_DOT_ON    lv_color_hex(0xECEFF1)
#define C_DOT_OFF   lv_color_hex(0x455A64)

static lv_obj_t *s_pages[PAGE_COUNT], *s_dots[PAGE_COUNT];
static int s_current;

static void show_page(int i)
{
    if (i < 0 || i >= PAGE_COUNT || i == s_current) {
        return;
    }
    /* Show the new page before hiding the old, so the screen is never blank. */
    lv_obj_set_hidden(s_pages[i], false);
    lv_obj_set_hidden(s_pages[s_current], true);
    lv_obj_set_style_bg_color(s_dots[s_current], C_DOT_OFF, 0);
    lv_obj_set_style_bg_color(s_dots[i], C_DOT_ON, 0);
    s_current = i;
}

static void on_gesture(lv_event_t *e)
{
    lv_indev_t *indev = lv_event_get_target(e);
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_GESTURE_LEFT) {
        show_page(s_current + 1);
    } else if (code == LV_EVENT_GESTURE_RIGHT) {
        show_page(s_current - 1);
    } else {
        return;
    }
    /* The finger is still down on what was under it: no click when it lifts. */
    lv_indev_wait_release(indev);
}

void ui_root_create(lv_obj_t *screen)
{
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(screen, false);

    for (int i = 0; i < PAGE_COUNT; i++) {
        lv_obj_t *p = lv_obj_create(screen);
        lv_obj_remove_style_all(p);
        lv_obj_set_size(p, lv_pct(100), lv_pct(100));
        lv_obj_set_scrollable(p, false);
        lv_obj_set_hidden(p, i != 0);
        PAGE_CREATE[i](p);
        s_pages[i] = p;
    }

    /* Page dots on the screen itself, above every page. */
    const int32_t span = PAGE_COUNT * DOT + (PAGE_COUNT - 1) * DOT_GAP;
    for (int i = 0; i < PAGE_COUNT; i++) {
        lv_obj_t *d = lv_obj_create(screen);
        lv_obj_remove_style_all(d);
        lv_obj_set_clickable(d, false);
        lv_obj_set_size(d, DOT, DOT);
        lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(d, i == 0 ? C_DOT_ON : C_DOT_OFF, 0);
        lv_obj_align(d, LV_ALIGN_BOTTOM_MID, -span / 2 + DOT / 2 + i * (DOT + DOT_GAP), -6);
        s_dots[i] = d;
    }

    /* Swipes arrive at the touch device whatever is under the finger. */
    lv_indev_t *touch = bsp_display_get_input_dev();
    if (touch) {
        lv_indev_add_event_cb(touch, on_gesture, LV_EVENT_GESTURE_LEFT, NULL);
        lv_indev_add_event_cb(touch, on_gesture, LV_EVENT_GESTURE_RIGHT, NULL);
    }
}

void ui_root_show_radar(void)
{
    show_page(PAGE_RADAR);
}
