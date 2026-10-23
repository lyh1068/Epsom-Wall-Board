---
name: tab5-board
description: Build, flash and debug M5Stack Tab5 (ESP32-P4) firmware in this project. Use this whenever you touch firmware here - building, flashing, editing sdkconfig or components, bootstrapping from the BSP example, or chasing a dark screen, a board that will not boot, missing serial output, screen flicker or tearing, Wi-Fi/SDIO failures, or a clock that never syncs. Read it before running idf.py at all: the serial port and the config system both fail silently on this board in ways that have already cost days on a previous Tab5 project, and the failures all look like "your code is broken" when they are not.
---

# Tab5 board knowledge

Carried over from a working Tab5 firmware project (a USB-HID "typewriter"), where
every item below was hit, mis-diagnosed at least once, and then pinned down. None
of it is in the Espressif docs and none of it is visible from the code.

`PLAN.md` owns *what this project is building*. This skill is *how to work on
this board without losing a day*.

## Read this first: never open the serial port on a running Tab5

**Opening `/dev/cu.usbmodem*` resets the P4 into ROM download mode.** The app
stops, the screen goes dark, the logs stop. Verified with a passive open - DTR
and RTS explicitly held deasserted, no reset requested - which still produced:

```
rst:0x17 (CHIP_USB_UART_RESET),boot:0x204 (DOWNLOAD(USB/UART0/SPI))
waiting for download
```

So `idf.py monitor`, a serial terminal, or any script that opens that port stops
the firmware you are trying to observe. Flash, then "check the logs", and you
will find a dark screen and conclude your change broke the boot. It did not.
That mistake cost several cycles on the previous project, twice.

Consequences for how you work:

- **After flashing, stop touching the port.** `idf.py flash` ends with a hard
  reset that starts the app. Leave it there.
- **The screen is the readout.** Put diagnostics on the display - this project's
  planned status corner (Wi-Fi bars, last sync time, stale dots) is worth
  building early in Phase 1 for exactly this reason.
- **A `DEMO_MODE` build that needs no network is worth more here than on a normal
  board**, because it is the cheapest way to verify UI work without a log.
- **`ioreg` is safe** - it reads USB topology without opening the tty.
- **To recover a board stuck in download mode**: reflash, or
  `python -m esptool --chip esp32p4 -p <port> -b 115200 --after hard_reset run`.

`CHIP_USB_UART_RESET` means the reset came from the host, not from a BOOT button.
Read `references/serial-and-boot.md` before spending any time on straps.

## Build and flash

```bash
cd '/Users/yuanh/Epsom Wall Board'
. ~/esp/esp-idf/export.sh
idf.py set-target esp32p4        # first time only
idf.py build
idf.py -p /dev/cu.usbmodem2101 -b 115200 flash
```

**Flash at 115200.** 460800 failed mid-write on the previous project ("chip
stopped responding"), leaving a half-written flash and a board that looked
code-broken. Confirm `Hash of data verified` for every region.

**`python3` must be the Homebrew 3.14.** ESP-IDF derives its virtualenv path from
the running Python's version, so it wants `idf5.5_py3.14_env`. There is a
miniconda at `~/miniconda3`; if its `python3` leads on `PATH`, `export.sh` looks
for a venv that does not exist and fails as if IDF were broken. Check
`python3 --version` first; if wrong:

```bash
mkdir -p /tmp/shim && ln -sf /opt/homebrew/bin/python3.14 /tmp/shim/python3
export PATH="/tmp/shim:$PATH"
```

## Changing sdkconfig

**`sdkconfig.defaults` only seeds a *new* `sdkconfig`.** Once `sdkconfig` exists,
edits to the defaults file are ignored - silently, with a successful build. This
produced a "fix" that was never compiled in. Keep `sdkconfig` gitignored and
`sdkconfig.defaults` as the source of truth, then:

```bash
cp sdkconfig /tmp/sdkconfig.working
rm sdkconfig && idf.py reconfigure
diff <(grep ^CONFIG_ /tmp/sdkconfig.working | sort) <(grep ^CONFIG_ sdkconfig | sort)
```

**Always run that diff.** Regenerating touches everything, and this board has
symbols where a silent change is fatal rather than cosmetic - the SDIO slave
reset polarity above all. The diff should show exactly what you meant to change.

## Components and versions

**Commit `dependencies.lock`.** Do not gitignore it. On the previous project it
was ignored, a fresh resolve moved the BSP a minor version with nothing in any
diff to show for it, and that became a phantom suspect during an unrelated bug
hunt.

**Use `espressif/m5stack_tab5` 1.3.x for this project**, as `PLAN.md` says. That
is the right call and the reason is concrete: 1.3.0 adds
`priv_include/disp_init_data_st7121.h` and an ST7121 branch that 1.2.0 does not
have, and `bsp_display.c` picks the panel at runtime by board version:

```
Discovered board version 3 (LCD ST7121, Touch ST712x, FW %u)
```

So 1.3.x is a superset - it keeps the ILI9881C path and adds the newer panel.
(The previous project pins `==1.2.0`. That pin was precautionary during a bug
hunt and was never shown to be necessary; do not copy it here.)

**Floor, do not pin, `esp_hosted`:** `>=2.12.10`. Pinning it lower makes
`CONFIG_ESP32P4_TAB5_C6_BOARD` vanish from Kconfig with no error, the SDIO pins
fall back to EV-board defaults, and Wi-Fi fails looking nothing like a version
problem. The rule for this board: **pin the display BSP, floor the transport
stack, commit the lock file.**

## Wi-Fi will probably not work on the first try

Wi-Fi is an on-board ESP32-C6 over SDIO, **2.4 GHz only** - a 5 GHz SSID is never
found, however right the password. The failure that wasted the most time was the
**SDIO slave reset polarity being inverted from the board default**. Full detail
and both log transcripts are in `references/wifi-and-time.md`; read it at Phase 2
rather than after a day of debugging.

## The radar page and screen tearing are in tension

Worth knowing before Phase 3, because it is a design constraint and not a bug:
the panel is **720×1280 portrait** and this project's UI is **1280×720
landscape**, so it must be rotated in software - and the BSP refuses to
double-buffer the framebuffer while software rotation is on. The radar's
5 fps dead-reckoned motion is the worst case for that.

There is a design that avoids most of it (per-aircraft LVGL objects rather than
one redrawn canvas). `references/display.md` has the reasoning, the config, and
what to measure.

## When the board misbehaves

Work down this list before suspecting your own change:

| Symptom | Check first |
|---|---|
| Dark screen, no logs | Download mode. Did anything open the serial port? Reflash, leave it alone. |
| Serial reads 0 bytes | The USB-Serial/JTAG function re-enumerates across a reset; a handle held over one is dead. Reopen. |
| Change had no effect | Did you edit `sdkconfig.defaults` without regenerating `sdkconfig`? |
| Wi-Fi never associates | `send_op_cond ... 0x107` is SDIO, not Wi-Fi. See `references/wifi-and-time.md`. Also: 2.4 GHz only. |
| Clock never syncs | SNTP fails *hard* on a config mismatch rather than warning. Same reference. |
| Flicker / tearing | `references/display.md`. It is single-framebuffer scanout, not the backlight (PWM is 5 kHz). |
| HTTPS fails | `esp_crt_bundle_attach`, and check free heap - TLS handshakes are hungry; put buffers in PSRAM. |

## Working discipline this board earned

**Change one thing per flash.** You cannot read logs from a running board, so
each flash costs a human looking at the screen and reporting back. Bundle three
changes and a dark screen tells you nothing. The exception is when every change
is a revert *toward* a known-good state.

**Read the failure mode before enabling a config symbol.** On the previous
project `CONFIG_LVGL_PORT_ENABLE_PPA` was switched on without checking what
happens when it fails (`assert(...)` - a hard abort). It turned out to be
harmless, but for anything on the boot path, go read the code that consumes the
symbol first.

**Verify that a change took effect.** Several dead ends came from assuming one
had: the ignored `sdkconfig.defaults`, a component version that moved on its own,
a `.gitignore` pattern that silently did not match. Note that `#` only starts a
comment at the *start* of a line in `.gitignore` - a trailing comment breaks the
pattern. Check with `git check-ignore -v <path>`, never by eye. That one nearly
committed a secrets header.

**Distinguish "no evidence for" from "evidence against".** A dark screen means
nothing if the app never ran. Say which one you have.

**Verify the API contracts before writing parsers.** `PLAN.md` says the endpoint
details are a guide, which is the right instinct - the Rail Data Marketplace base
URL and header name in particular come from the product's Specification tab after
subscribing. Save a real response into `test/fixtures/` and write the parser
against that, not against the plan's prose.

## Deeper references

- `references/serial-and-boot.md` - download mode in full: evidence, what
  DTR/RTS do on USB-Serial/JTAG, how to reset into the app, how to get logs when
  you genuinely need them.
- `references/display.md` - rotation versus tearing, why the radar page is the
  hard case, PPA, and the LVGL redraw habits that keep the dirty area small.
- `references/wifi-and-time.md` - the SDIO reset polarity, the esp_hosted
  version trap, and the SNTP failure that presents as a slow sync.
