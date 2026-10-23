# Epsom Wall Board

A wall-mounted home dashboard on an **M5Stack Tab5** (ESP32-P4), hung near Epsom
station. Two pages, swipe between them:

- **Morning** — live Epsom (`EPS`) departures, a "leave in X min" countdown,
  clock, weather, TfL tube status for the onward leg, and an "overhead now" plane
  chip.
- **Radar** — a live plane radar centred on home. Heathrow and Gatwick are both
  about 11 nm away, so there is always traffic.

No server, no proxy, no Home Assistant. The device talks to the public APIs
directly. Full specification in **[PLAN.md](PLAN.md)**.

---

## Status: nothing is built yet

This directory holds the plan, the board knowledge, and the credential tooling.
**Phase 0 (bootstrap from the BSP example) is the next step.** Nothing has been
compiled or flashed from here.

| Path | What it is |
|---|---|
| `PLAN.md` | The implementation plan and UI spec. Build in its phase order. |
| `.claude/skills/tab5-board/` | Hard-won Tab5 knowledge carried over from a working Tab5 project. **Read before running `idf.py`.** |
| `tools/gen_secrets.py` | Turns `.env` into a generated header at build time. Already smoke-tested here. |
| `.env.example` | Which credentials are needed and where to get them. |
| `.gitignore` | Keeps `.env`, `sdkconfig` and build output out of git — and keeps `dependencies.lock` *in*. |

---

## Before writing any code

**1. Start the Rail Data Marketplace registration now.** It is the only
credential with a lead time — approval can take days. Everything in phases 0–5
works without it, which is why `PLAN.md` deliberately puts the radar before the
trains. Get the TfL `app_key` at the same time; that one is instant.

**2. Set up credentials:**

```bash
cp .env.example .env    # then edit it
```

**3. Verify each API contract before writing its parser.** `PLAN.md` says the
endpoint details are a guide, and that is the right instinct — the Rail Data
Marketplace base URL and the API-key header name both come from the product's
Specification tab after you subscribe. Save a real response into
`test/fixtures/` and write the parser against that, not against the plan's prose.

---

## Build and flash

Needs **ESP-IDF ≥ 5.4** (developed against 5.5), plus `cmake` and `ninja`
(`brew install cmake ninja` — ESP-IDF's own installer does not supply them on
macOS).

```bash
. ~/esp/esp-idf/export.sh
idf.py set-target esp32p4
idf.py build
idf.py -p /dev/cu.usbmodem2101 -b 115200 flash
```

Two things that are not optional, both explained in the skill:

- **Flash at 115200.** 460800 has failed mid-write on this hardware, leaving a
  half-written flash that looks like a code bug.
- **After flashing, do not open the serial port.** On this board that resets the
  P4 into ROM download mode and stops the app. See below.

---

## Read this before you debug anything

Three traps on this board cost days on the previous Tab5 project, and all three
present as "your code is broken" when it is not. They are written up in
`.claude/skills/tab5-board/` — Claude Code loads it automatically in this
directory, and it is worth reading yourself too.

**Opening the serial port stops the firmware.** Opening `/dev/cu.usbmodem*`
resets the P4 into ROM download mode: screen dark, no logs, nothing running.
Verified with a passive open that never asserted DTR or RTS. So you cannot
monitor a running app — flash it, then leave the port alone and read the
*screen*. This is why the plan's status corner and `DEMO_MODE` are worth building
early rather than treating as polish: **the display is the debugging channel.**

**`sdkconfig.defaults` is ignored once `sdkconfig` exists.** Silently, with a
successful build. Regenerate and diff rather than hand-editing.

**Wi-Fi will likely fail on the first attempt, and not for a Wi-Fi reason.**
It runs on an on-board ESP32-C6 over SDIO, the slave reset polarity is inverted
from the board default on this hardware, and the symptom (`send_op_cond` →
`0x107`) looks exactly like dead silicon. Also: **2.4 GHz only.**

---

## Two places this differs from PLAN.md

Both are deliberate; say so if you would rather follow the plan as written.

**1. BSP 1.3.x is confirmed as the right choice.** The plan says to use 1.3.x and
rely on the BSP's panel detection — correct, and now verified rather than
assumed. Diffing the two cached versions, 1.3.0 adds
`priv_include/disp_init_data_st7121.h` and an ST7121 branch that 1.2.0 does not
have, and `bsp_display.c` selects the panel at runtime by board version
(`Discovered board version 3 (LCD ST7121, Touch ST712x, ...)`). 1.3.x is a
superset: it keeps the ILI9881C path and adds the newer panel.

Note that the previous Tab5 project pins `==1.2.0`. That pin was precautionary
during an unrelated bug hunt and was never shown to be necessary — **do not copy
it here.**

**2. Credentials come from `.env`, not `secrets.h.example`.** The plan's §3 says
to copy `secrets.h.example` to a gitignored `secrets.h`. This uses the mechanism
built and proven on the previous project instead: `tools/gen_secrets.py` reads
`.env` and generates a header into the *build* directory. The difference matters
because the thing you edit is never a source file, real environment variables
take precedence (so a key need not touch disk), and there is no committed file
one `git add -A` away from carrying a real value. `.gitignore` still excludes
`secrets.h` in case anyone takes the plan's route.

---

## A design decision worth making before Phase 3

The panel is **720×1280 portrait** in hardware and this UI is **1280×720
landscape**, so every frame is rotated in software — and the BSP refuses to
double-buffer the framebuffer while software rotation is on
(`.sw_rotate = false` is hard-coded under `BSP_DISPLAY_LVGL_AVOID_TEAR`). So it
is tear-free portrait *or* landscape with a tear window, not both. Landscape
obviously wins for a wall board, which means tearing can be made small but not
eliminated.

That was liveable on the previous project, which updated a few labels per second.
The radar page is a different proposition: `PLAN.md` §5.2 wants dead-reckoned
aircraft re-rendered at ~5 fps, plus trails and an optional sweep line. **If the
scope is one canvas redrawn each frame, that is a full-screen rotated blit into a
live framebuffer five times a second, and it will tear visibly.**

The way to avoid most of it is structural, so it should be decided before the
scope is built rather than tuned afterwards: draw the rings, home marker and
runways once as a static background, and give each aircraft its own small LVGL
object that you reposition and rotate, pooled by `hex`. Then the dirty region per
frame is a dozen small rectangles instead of the whole scope. Enable
`CONFIG_LVGL_PORT_ENABLE_PPA=y` from the start so the rotation runs on the P4's
2D accelerator rather than the CPU.

Reasoning and the measurement order are in
`.claude/skills/tab5-board/references/display.md`.

---

## Layout

As planned (`PLAN.md` §3) — none of it exists yet:

```
main/
  app_main.c
  config.h               # non-secret defaults (lat/lon, ranges, poll intervals)
  net/        wifi.c  http_json.c  sntp_time.c
  data/       trains.c  weather.c  tfl_status.c  aircraft.c  routes_cache.c
  model/      board_state.c/.h     # single source of truth, mutex-protected
  ui/         ui_root.c  page_morning.c  page_radar.c  widgets/*.c  theme.c
  geo/        geo.c                # projection, distance, bearing
assets/       fonts/  icons/
test/         host tests for parsers + geo maths
tools/        gen_secrets.py  make_radar_bg.py (optional)
```

Data tasks poll on their own intervals and write into `board_state` under a
mutex; the UI reads `board_state` only, on an LVGL timer, and never does
networking.
