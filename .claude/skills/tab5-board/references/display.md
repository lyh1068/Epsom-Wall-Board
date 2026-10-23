# Display: rotation, tearing, and the radar page

## The constraint

The Tab5 panel is **720×1280 portrait** in hardware (MIPI-DSI; ILI9881C on early
units, ST7121 on board version 3). This project's UI is **1280×720 landscape**,
so it must be rotated 90° in software:

```c
bsp_display_rotate(disp, LV_DISPLAY_ROTATION_90);
```

The DSI controller scans its framebuffer out continuously, and by default there
is only one (`CONFIG_BSP_LCD_DPI_BUFFER_NUMS=1`). Every flush therefore writes
into memory the display is part-way through reading. That is what reads as
flicker or tearing.

## Why you cannot simply turn tearing off

The BSP can fix it properly - `BSP_LCD_DPI_BUFFER_NUMS > 1` plus
`BSP_DISPLAY_LVGL_AVOID_TEAR` gives two framebuffers swapped on VSYNC. But
`bsp_display.c` hard-codes the rotation off whenever that is on:

```c
#if CONFIG_BSP_DISPLAY_LVGL_AVOID_TEAR
            .sw_rotate = false,   /* Avoid tearing is not supported for SW rotation */
```

So it is **tear-free portrait, or landscape with a tear window** - not both. A
landscape wall board means tearing can be made small, not removed. Do not
promise otherwise.

## Why the radar page is the hard case

The previous Tab5 project updated a few labels a second and the tearing was
noticeable but liveable. `PLAN.md` §5.2 asks for something much heavier:
dead-reckoned aircraft re-rendered at **about 5 fps**, plus trails, plus an
optional rotating sweep line.

If the scope is drawn as **one canvas that is redrawn each frame**, the dirty
region is the whole 720×720 scope, rotated and blitted into a live framebuffer,
five times a second. That will tear badly and visibly.

**The design that avoids most of it:** make each aircraft its own small LVGL
object - an image or label you reposition and rotate - over a static background
object holding the rings, home marker and airports. Then LVGL's dirty region per
frame is a dozen small rectangles around things that actually moved, not the
whole scope. Trails as a short ring buffer of small dots behave the same way.

This matters for how Phase 3 is written, not just how it is tuned afterwards, so
decide it before building the scope. Concretely:

- Static layer: rings, compass, home, runway lines. Drawn once; never
  invalidated.
- Per-aircraft objects: position via `lv_obj_set_pos`, rotation via an image
  transform. Reuse objects from a pool keyed by `hex` rather than
  creating/deleting each poll.
- The **sweep animation is the one feature that genuinely requires a large
  periodic redraw.** Treat it as the optional toy the plan already says it is,
  and judge it on the device.

## Rule out the backlight first

"The screen flickers" can mean PWM flicker, which has nothing to do with LVGL. It
is not the cause on this board - the BSP drives the backlight with LEDC at
`freq_hz = 5000`, far above anything visible - but checking costs one grep and
saves chasing the wrong subsystem.

## The three levers that help

**1. Rotate on the PPA, not the CPU.** `CONFIG_LVGL_PORT_ENABLE_PPA=y` hands the
90° rotation to the P4's Pixel Processing Accelerator, shortening every flush and
narrowing the window in which a tear can appear. Given this project rotates every
frame of an animated page, turn it on from the start.

Note that `esp_lvgl_port_disp.c` does `assert(disp_ctx->ppa_handle != NULL)`, so
a failure here is a hard abort, not a graceful fallback. Reading
`lvgl_port_ppa_create()` shows it only fails on an allocation (one small struct
plus `buffer_size * 2` bytes) or if `ppa_register_client()` fails on a peripheral
the P4 always has. On the previous project this symbol was wrongly blamed for a
dead boot; the real cause was the board sitting in ROM download mode.

**2. Never invalidate what has not changed.** LVGL redraws a label whenever you
set its text, even to an identical string. With a 500 ms UI timer reading
`board_state`, most ticks have nothing new - so guard every periodic setter:

```c
static void set_text(lv_obj_t *lbl, const char *txt)
{
    const char *cur = lv_label_get_text(lbl);
    if (cur && strcmp(cur, txt) == 0) return;
    lv_label_set_text(lbl, txt);
}
```

Worth having the same guard for style colours (the tube pills and train-row
colours will otherwise be re-set every tick).

**3. Do not let geometry move when only text changed.** An auto-sized label that
is *aligned* rather than sized shifts **and** resizes when its text grows a
character, so LVGL dirties both the old rectangle and the new one. Pin the box and
align the text inside it:

```c
lv_label_set_long_mode(lbl, LV_LABEL_LONG_CLIP);
lv_obj_set_width(lbl, FIXED_W);
lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_RIGHT, 0);
```

This bites hardest on right-aligned volatile fields - a clock, a countdown, an
aircraft count. The leave-by countdown and the "14 aircraft" readout are both in
this category.

Related: think about *how often* a field can change versus how often it needs to.
A per-second countdown does not need a 500 ms timer; divide it down.

## Buffer sizing

`buffer_size` is in pixels, and with `double_buffer = true` it is allocated twice
in PSRAM. A dirty region larger than the buffer is rendered, rotated and copied
in several passes, each landing in the live framebuffer - so for a page with one
big moving area, too small a buffer means several seams per frame. There is 32 MB
of PSRAM here, so there is room; but it is a boot-path allocation whose failure
mode is a dark screen, so change it alone and confirm the board still comes up.

## Order of attack

If flicker is reported: confirm the app is actually running (see
`serial-and-boot.md`), rule out the backlight, check every periodic update goes
through a guarded setter, check no volatile label has floating geometry, confirm
the radar is not redrawing a whole canvas, then look at PPA and buffer size.
Reaching for buffer sizes first is the expensive path.
