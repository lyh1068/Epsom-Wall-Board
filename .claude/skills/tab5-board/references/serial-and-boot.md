# Serial, download mode, and getting logs

## What happens

On this board and host, opening the USB-Serial/JTAG port resets the ESP32-P4 into
ROM download mode. The app stops running, so the screen goes dark, any HID or
network function disappears, and nothing is logged - because nothing is
executing.

```
ESP-ROM:esp32p4-eco2-20240710
Build:Jul 10 2024
rst:0x17 (CHIP_USB_UART_RESET),boot:0x204 (DOWNLOAD(USB/UART0/SPI))
waiting for download
```

Both halves matter:

- `rst:0x17 (CHIP_USB_UART_RESET)` - the reset was caused by the USB serial
  interface. **It came from the host.** Not a button, not a brownout, not a
  watchdog, not your code.
- `boot:0x204 (DOWNLOAD(...))` - GPIO0 was low at the reset edge, so the ROM
  entered the downloader instead of chain-loading the app.

## How it was established

Recorded because the wrong conclusion was reached twice first.

1. `idf.py monitor` printed the banner above after a flash. Read as "the BOOT
   button is stuck", and the user was asked to power-cycle. It changed nothing,
   because the cause was host-side.
2. A reset was issued from Python with the strap released: `dtr=False` and
   `rts=False` set before `open()`, then pulse RTS only. Same result.
3. A **passive** open - no reset requested, DTR and RTS never asserted - produced
   a *fresh* banner. Decisive: the act of opening the port is itself resetting
   the chip into download mode.

On USB-Serial/JTAG parts, DTR and RTS are not modem lines; the ROM maps them onto
the GPIO0 boot strap and the EN reset. macOS asserts them when a program opens
the tty, before pyserial's own settings are applied, which is why deasserting
from Python does not help.

## What to do instead

**Do not open the port on a board you want to keep running.** That is the whole
rule. After `idf.py flash`, esptool's closing hard reset starts the app; stop
there.

**Recover a stuck board:**

```bash
idf.py -p /dev/cu.usbmodem2101 -b 115200 flash
# or, without reflashing:
python -m esptool --chip esp32p4 -p /dev/cu.usbmodem2101 -b 115200 --after hard_reset run
```

**Check enumeration without opening the port.** `ioreg` reads USB topology and
never touches the tty:

```bash
ioreg -p IOUSB -l -w 0 | grep -i '"USB Product Name"'
```

`USB JTAG_serial debug unit` is the Tab5's USB-C side and is present whenever the
board is powered, running or not - it proves nothing about the app.

**Confirm nobody holds the port:** `lsof /dev/cu.usbmodem2101`.

## When you genuinely need boot logs

Accept that you get the *boot*, not the running app, and that reading them costs
you the run:

- `idf.py monitor` refuses to start without a TTY. Give it a pty:
  `script -q /dev/null idf.py -p /dev/cu.usbmodem2101 -b 115200 monitor`
- Expect the ROM banner rather than app output, for the reason above.
- A serial handle held across a reset is dead - the USB function re-enumerates
  and the device node identity changes. Close, wait, reopen.

## What this means for this project

Because the UART is effectively write-only in practice, **the display is the
debugging channel**. Two things from `PLAN.md` are therefore worth more than
their phase number suggests, and are worth pulling earlier:

- The **status corner** (Wi-Fi bars, last successful sync, a red dot per stale
  source) is listed under Phase 8 polish. It is really a debugging tool. Build a
  crude version in Phase 1.
- **`DEMO_MODE`** feeding fixtures into `board_state` lets you iterate on UI with
  no network and no logs at all.

Also give every data source a visible error state rather than only a log line.
`last_error` in `board_state` is in the plan; make sure something on screen can
show it, even if only as a tap-to-reveal on the stale badge.

## Scope of this finding

Verified repeatedly on this Mac with a Tab5 through the hub in use. It is
consistent with how USB-Serial/JTAG maps DTR/RTS, so it is likely general to
ESP32-P4 on macOS - but it has not been tested on another host or cable. If
someone reports `idf.py monitor` working normally on a Tab5, check their host
before assuming this note is wrong.
