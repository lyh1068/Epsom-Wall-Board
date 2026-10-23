# Epsom Wall Board — M5Stack Tab5

Implementation plan for a wall-mounted home dashboard on an **M5Stack Tab5** (ESP32-P4).
Written as a handoff for Claude Code. Build it in the phase order below; each phase ends in something that runs on the device.

---

## 1. Goal

A two-page touchscreen board that hangs on the wall at home near Epsom station:

1. **Morning page**: live Epsom train departures, a "leave in X min" countdown, clock and weather, tube line status for the onward journey into London, and an "overhead now" plane chip.
2. **Radar page**: a live plane radar centred on home. Heathrow and Gatwick are both about 11 nm away, so traffic is constant.

### In scope
- Trains from **Epsom (CRS `EPS`) only**.
- Weather, clock, TfL tube line status, and plane radar.

### Out of scope
- Home Assistant
- Bin days
- Other stations and buses
- Camera, voice, cloud backend
- Any server or proxy. The device talks to the public APIs directly.

---

## 2. Hardware facts that matter

| Item | Detail | Why it matters |
|---|---|---|
| SoC | ESP32-P4 (RISC-V dual core, ~360–400 MHz), 16 MB flash, **32 MB PSRAM** | Plenty of RAM: put LVGL buffers, JSON parsing and the aircraft table in PSRAM. |
| Wi-Fi | Via an on-board **ESP32-C6** over SDIO (2.4 GHz Wi-Fi 6) | Wi-Fi goes through `esp_hosted` / `esp_wifi_remote`. **Use the official BSP**; don't hand-roll this. |
| Display | 5" IPS 1280×720 over MIPI-DSI, GT911 touch on early units; **newer revisions use ST7123/ST7121 panel and touch ICs** | Use a BSP version that probes for the panel variant. Don't hard-code the ILI9881C. |
| RTC | RX8130CE on I²C | Keep time across reboots, and show a clock before NTP syncs. |
| Mounting | Standard 1/4"-20 tripod nut on the back | Wall-mount with a cheap tripod-thread wall bracket. |
| Power | USB-C. The kit version has an NP-F550 battery. | For a wall board, run on permanent USB-C power. M5Stack advises shutting down before pulling power or a battery. |

---

## 3. Toolchain and project setup

- **ESP-IDF v5.4 or later** (P4 support), via the VS Code ESP-IDF extension or the CLI.
- **BSP**: the Espressif component `espressif/m5stack_tab5` (v1.3.x at the time of writing). Bootstrap from its LVGL example:
  ```
  idf.py create-project-from-example "espressif/m5stack_tab5:display"
  ```
  Then pin the BSP version in `main/idf_component.yml`.
- **LVGL 9.x** (whatever the BSP pulls in) via `esp_lvgl_port`.
- **JSON**: cJSON, which is bundled with IDF.
- **HTTPS**: `esp_http_client` with `esp_crt_bundle` for certificate verification.
- **Time**: SNTP plus the POSIX TZ `GMT0BST,M3.5.0/1,M10.5.0`, and RTC fallback.

### Repo layout

```
epsom-wallboard/
  main/
    app_main.c
    config.h               # non-secret defaults (lat/lon, ranges, poll intervals)
    secrets.h.example      # copy to secrets.h (gitignored)
    net/        wifi.c  http_json.c  sntp_time.c
    data/       trains.c  weather.c  tfl_status.c  aircraft.c  routes_cache.c
    model/      board_state.c/.h     # single source of truth, mutex-protected
    ui/         ui_root.c  page_morning.c  page_radar.c  widgets/*.c  theme.c
    geo/        geo.c                # projection, distance, bearing
  assets/       fonts/  icons/  radar_bg.png (optional)
  test/         host tests for parsers + geo maths
  tools/        make_radar_bg.py     # optional: render local map background
  README.md  PLAN.md
```

### Architecture

- **Data tasks**: one FreeRTOS task per source. Each polls on its own interval, parses into plain C structs, and writes into `board_state` under a mutex.
- **UI**: reads `board_state` only, on an LVGL timer (e.g. every 500 ms). The UI never does networking.
- Each data source records `last_ok_epoch` and `last_error`. The UI shows a small "stale" badge when data is older than 3× its poll interval.

### Secrets and config

- `secrets.h` holds Wi-Fi SSID and password, the Rail Data Marketplace key and the TfL `app_key`. It is gitignored, with a `secrets.h.example` committed.
- Later (Phase 8), move secrets into NVS with a tiny on-device web settings page.

---

## 4. Data sources

All are free. Verify each endpoint against the official docs before coding, because the details below are a guide.

| Data | Source | Auth | Poll |
|---|---|---|---|
| Trains | **Rail Data Marketplace**, "Live Departure Board" (Darwin LDBWS, JSON REST). Use `GetDepBoardWithDetails/EPS`, which includes calling points. The base URL and the API-key header are shown on the product's **Specification** tab after subscribing (seen in the wild as `https://api1.raildata.org.uk/1010-live-departure-board-dep/LDBWS/api/20220120/GetDepBoardWithDetails/{crs}`). | API key in header (register at raildata.org.uk; approval may take days) | 60 s (30 s during the commute window) |
| Weather | **Open-Meteo** `/v1/forecast` with current, hourly temperature, precipitation probability and weather code | None | 15 min |
| Tube status | **TfL Unified API** `/Line/{ids}/Status`, e.g. `northern,waterloo-city,jubilee,circle` | `app_key` from api-portal.tfl.gov.uk | 2 min |
| Aircraft | **adsb.lol** `/v2/point/{lat}/{lon}/{radius_nm}` | None | 10 s |
| Aircraft fallback | adsb.fi or airplanes.live (same readsb-style JSON); OpenSky `/states/all` with a bounding box needs OAuth2 client credentials, so it's last resort | None / OAuth2 | 10–15 s |
| Flight route | **adsbdb** `api.adsbdb.com/v0/callsign/{callsign}` → origin and destination plus airline | None | On demand, cached |

Implementation notes:
- **Trains**: parse `std` (scheduled), `etd` ("On time" / "HH:MM" / "Delayed" / "Cancelled"), `platform`, destination name, operator, and `nrccMessages` (disruption banners).
- **London-bound filter**: keep a service if its destination or any subsequent calling point is in a configurable set (default `WAT, VIC, LBG, CLJ, WIM`).
- **Aircraft**: fields of interest are `hex`, `flight` (callsign, trim the spaces), `t` (type), `r` (registration), `alt_baro` (may be the string `"ground"`), `gs`, `track`, `baro_rate`, `lat`, `lon`, `squawk`, `seen`.
- **Route lookups**: one HTTP call per new callsign. Cache in an LRU of about 500 entries in PSRAM and persist to SPIFFS/LittleFS. Routes for low-cost carriers that rotate callsign suffixes are often wrong, so label them "route (unverified)" when the airline is EZY, RYR or TOM, or when lookup data is missing.
- **Rate limits**: be a good citizen. Use exponential backoff on 429/5xx and never poll faster than the table above.

---

## 5. UI specification

1280×720 landscape. Dark theme by default with a high-contrast amber-on-black departure-board look for trains. Text must be readable from about 3 m, so the train rows need at least a 36 px font. Swipe horizontally between pages, with page dots at the bottom.

### 5.1 Morning page

```
┌──────────────────────────────────────────┬──────────────────────┐
│ EPSOM → London                    ⚠ msg │  07:42               │
│ 07:48  London Waterloo   P2  On time     │  Fri 24 Oct          │
│ 07:55  London Victoria   P4  Exp 07:58   │  ☁ 11°C  rain 10:00  │
│ 08:02  London Waterloo   P2  CANCELLED   │  ▁▂▃▅▃▂ next 12 h    │
│ 08:10  London Victoria   P4  On time     │                      │
│ [tap row → calling points]  [All | Ldn]  │                      │
├──────────────────────────────────────────┴──────────────────────┤
│  LEAVE IN 6 MIN  →  07:55 to Victoria (exp 07:58)                │
├──────────────────────────────────────────────────────────────────┤
│ Northern ●  Waterloo & City ●  Jubilee ● minor delays  Circle ●  │
│ ✈ Overhead: BAW87K A320 · 3,400 ft ↓ · LHR arrival   (tap→radar) │
└──────────────────────────────────────────────────────────────────┘
```

**Train list**
- Show the next 6 services.
- **Hide trains you can't catch**: anything departing sooner than `WALK_MIN` (configurable, default 8) is hidden or dimmed.
- Colours: amber for normal, red for cancelled or more than 5 min late, plus a yellow banner for `nrccMessages`.
- Tap a row to open a modal with calling points and ETAs.
- The `All | London` toggle switches the filter.

**Leave-by bar**
- Target the first catchable London-bound train. Show `minutes_until(expected_departure) - WALK_MIN`.
- The bar turns green at 0–2 min ("go now"), and grey when there's nothing within 60 min.
- Optional chime from the speaker at "go now", during weekday commute hours only.

**Weather**: current temperature, icon, the time of the next rain from hourly precipitation probability ≥ 50%, and a 12-hour sparkline.

**Tube status**: one pill per configured line. Green is good service, amber is minor delays, red is severe delays, suspended or part closure. Tap a pill for the reason text.

**Overhead chip**: the nearest aircraft within 5 nm, or hidden if there isn't one. Tap it to jump to the radar page with that aircraft selected.

### 5.2 Radar page

```
┌───────────────────────────────────────────────┬──────────────────┐
│               N                               │ ✈ BAW87K         │
│        ·   ✈↘    ·                            │ A320  G-EUYA     │
│    ·      ╭───────╮     ·   ← edge arrows for │ 3,400 ft ↓ 900fpm│
│   ·   ✈→  │   ⌂   │  ✈↓  ·    planes just     │ 180 kt  hdg 095  │
│    ·  LHR ╰───────╯ LGW ·     out of range    │ GLA → LHR        │
│        ·          ·                           │ 6.2 nm  bearing NW│
│               S                               │──────────────────│
│  rings: 5 / 10 / 20 nm                        │ 14 aircraft      │
│                                               │ Range [10][20][40]│
└───────────────────────────────────────────────┴──────────────────┘
```

**Radar scope** (about a 720×720 square on the left)
- Range rings, north up, and a home marker.
- Airports drawn as small runway lines: Heathrow EGLL (51.4700, −0.4543) and Gatwick EGKK (51.1481, −0.1903). Runway endpoints come from the OurAirports CSV, pre-baked into a header at build time.
- **Aircraft icon** rotated to `track` and coloured by altitude: under 3,000 ft red/orange, 3–10k yellow, 10–25k green, above 25k cyan. Ground traffic is hidden.
- **Trails**: the last 8 positions as fading dots.
- **Smooth motion**: dead-reckon each aircraft between 10 s polls using `gs` and `track`, re-rendering at about 5 fps. Snap to the real position on the next poll.
- **Edge indicators**: aircraft within 1.5× range but outside it appear as small arrows on the scope edge.
- **Optional sweep animation**: a rotating sweep line, with blips that brighten as it passes. It's toggleable, mostly for fun.

**Detail panel** (right)
- Tap an aircraft to select it. The panel shows callsign, type, registration, altitude with climb/descent rate, speed, heading, distance and bearing from home, and the route if known.
- With nothing selected, it shows the aircraft count and the range buttons.

**Alerts**: squawk 7700, 7600 or 7500 gets a red highlight plus a banner on both pages.

**Projection**: equirectangular around home.
```
x = (lon - lon0) * cos(lat0) * 60   // nm east
y = (lat - lat0) * 60               // nm north
px = cx + x * scale ; py = cy - y * scale
```
This is accurate enough within 40 nm. Put it in `geo.c` with host unit tests.

**Optional map background**: `tools/make_radar_bg.py` renders a faint greyscale OpenStreetMap image for each range preset at build time, stored in flash, with the "© OpenStreetMap contributors" credit drawn in a corner. No live tile fetching.

### 5.3 Global behaviours
- **Night mode**: between 22:30 and 06:00, backlight at 10% with a large clock only. Touch wakes it to the full UI for 60 s.
- **Status corner**: Wi-Fi bars, time of last successful sync, and a red dot if any source is stale.
- **Return to Morning page** after 2 min idle on the radar page, except between 06:30 and 09:00 on weekdays, when the board always stays on the Morning page.

---

## 6. Phases and acceptance criteria

| # | Phase | Done when… |
|---|---|---|
| 0 | **Bootstrap** | BSP LVGL example builds and flashes; touch works; the panel variant is logged at boot. |
| 1 | **Static UI** | Both pages render with **fake data** from `board_state` (no network). Swipe works. Fonts are readable from 3 m. |
| 2 | **Wi-Fi + time** | Connects from `secrets.h`; NTP sync; London TZ handles BST/GMT; RTC shows the time before sync. |
| 3 | **Radar (live)** | adsb.lol polling; planes plotted correctly (sanity-check against flightradar24 or tar1090 by eye); tap shows details; edge arrows; range buttons. *Needs no API key, so do it while waiting for rail approval.* |
| 4 | **Route lookup** | adsbdb lookups with a persistent cache; "unverified" labelling; overhead chip on the Morning page. |
| 5 | **Weather** | Open-Meteo current, next-rain time and sparkline. |
| 6 | **Trains** | Live EPS board, London filter, hide uncatchable trains, cancellations and delays, calling-points modal, NRCC banner. |
| 7 | **Leave-by + tube status** | Countdown bar logic, TfL line pills with reason text, go-now chime in the commute window. |
| 8 | **Polish** | Night mode, stale badges, backoff, NVS settings and a small web settings page (Wi-Fi, keys, WALK_MIN, lines), OTA updates, idle return. |

### Testing
- **Host unit tests** (ESP-IDF `linux` target, or a plain CMake + Unity build) for the train, tube, weather, adsb.lol and adsbdb JSON parsers, using saved fixture responses in `test/fixtures/`.
- Also host-test `geo.c` (projection, distance, bearing) and the leave-by calculation, including edge cases: cancelled next train, no trains, the midnight wrap, and the BST changeover.
- **On device**: a `DEMO_MODE` build flag that feeds fixture data into `board_state`, so the UI can be checked without network.

---

## 7. Gotchas to watch for

- **Wi-Fi is on the C6 co-processor**. Use the BSP and IDF `esp_wifi_remote`/`esp_hosted` components; check the C6 firmware version compatibility noted in the BSP README.
- **Display panel variants** differ across Tab5 production batches, so rely on the BSP's detection.
- **TLS**: use `esp_crt_bundle_attach` and don't disable certificate checks. HTTPS handshakes are slow-ish, so reuse clients where possible.
- **LVGL thread safety**: only touch LVGL objects under `lvgl_port_lock()`.
- **Train time edge cases**: `etd` can be "On time", "Delayed", "Cancelled" or "HH:MM"; services cross midnight.
- **`alt_baro` can be the string "ground"**, and `flight` may be missing or padded with spaces.
- Keep API keys out of git.

---

## 8. Backlog (nice ideas from similar projects; only after Phase 8)

- **"Train approaching" alert**: flash the row when your chosen train is two minutes out.
- **Plane stats**: aircraft seen today, the rarest type of the week, the lowest overflight.
- **ISS pass overhead**: predict the next visible pass from CelesTrak TLEs and show a countdown.
- **Sunrise and sunset** times plus a moon phase in the clock panel.
- **Wake on approach** using the camera or IMU tap, instead of touch.
- **Own ADS-B receiver**: an RTL-SDR on a Pi running readsb. Point the aircraft source at the local `aircraft.json` for zero latency and no rate limits.
