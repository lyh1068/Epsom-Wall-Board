#include "icons.h"

#include <math.h>
#include <string.h>
#include "esp_heap_caps.h"

typedef struct {
    float x, y;
} pt_t;

/* Outlines in a 32x32 box, nose up. */
static const pt_t PLANE[] = {
    { 16, 1 },  { 17.6f, 3 },  { 18, 11 },  { 30, 18 },  { 30, 20.5f }, { 18, 17 },
    { 17.6f, 25 }, { 21.5f, 28.5f }, { 21.5f, 30.5f }, { 16, 29 },
    { 10.5f, 30.5f }, { 10.5f, 28.5f }, { 14.4f, 25 }, { 14, 17 }, { 2, 20.5f }, { 2, 18 },
    { 14, 11 }, { 14.4f, 3 },
};
static const pt_t ARROW[] = {
    { 16, 4 }, { 27, 27 }, { 16, 21 }, { 5, 27 },
};

static bool inside_poly(const pt_t *p, int n, float x, float y)
{
    bool in = false;
    for (int i = 0, j = n - 1; i < n; j = i++) {
        if ((p[i].y > y) != (p[j].y > y)
            && x < (p[j].x - p[i].x) * (y - p[i].y) / (p[j].y - p[i].y) + p[i].x) {
            in = !in;
        }
    }
    return in;
}

static bool inside_blip(float x, float y)
{
    float dx = x - 16, dy = y - 16;
    return dx * dx + dy * dy <= 7.0f * 7.0f;
}

/* 4x4 supersampling per pixel for a smooth edge. */
static const lv_image_dsc_t *rasterise(const pt_t *poly, int n)
{
    const size_t size = ICON_SIZE * ICON_SIZE * 4;
    uint8_t *px = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    lv_image_dsc_t *dsc = heap_caps_calloc(1, sizeof(*dsc), MALLOC_CAP_SPIRAM);
    if (px == NULL || dsc == NULL) {
        return NULL;
    }
    for (int y = 0; y < ICON_SIZE; y++) {
        for (int x = 0; x < ICON_SIZE; x++) {
            int hits = 0;
            for (int sy = 0; sy < 4; sy++) {
                for (int sx = 0; sx < 4; sx++) {
                    float fx = x + (sx + 0.5f) / 4, fy = y + (sy + 0.5f) / 4;
                    hits += poly ? inside_poly(poly, n, fx, fy) : inside_blip(fx, fy);
                }
            }
            uint8_t *p = &px[(y * ICON_SIZE + x) * 4];
            p[0] = p[1] = p[2] = 0xFF;          /* B G R: white */
            p[3] = (uint8_t)(hits * 255 / 16);  /* A */
        }
    }
    dsc->header.magic = LV_IMAGE_HEADER_MAGIC;
    dsc->header.cf = LV_COLOR_FORMAT_ARGB8888;
    dsc->header.w = ICON_SIZE;
    dsc->header.h = ICON_SIZE;
    dsc->header.stride = ICON_SIZE * 4;
    dsc->data_size = size;
    dsc->data = px;
    return dsc;
}

radar_icons_t icons_create(void)
{
    return (radar_icons_t) {
        .plane = rasterise(PLANE, sizeof(PLANE) / sizeof(PLANE[0])),
        .blip = rasterise(NULL, 0),
        .arrow = rasterise(ARROW, sizeof(ARROW) / sizeof(ARROW[0])),
    };
}
