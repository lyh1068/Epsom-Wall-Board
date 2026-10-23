# Wi-Fi, SDIO, and the clock

Wi-Fi is the part of this board most likely to cost you a day. Read this at the
start of Phase 2, not after.

## Wi-Fi is not on the P4

It comes from an on-board **ESP32-C6** reached over SDIO via `esp_hosted` /
`esp_wifi_remote`. Two consequences:

- **2.4 GHz only.** A 5 GHz SSID will never be found, however correct the
  password. Check this before debugging anything else.
- The C6 needs powering before anything enumerates:
  `bsp_feature_enable(BSP_FEATURE_WIFI, true)`. Call it **early** - esp-hosted's
  SDIO probe does not wait for the C6, so if you power it immediately before
  connecting there is nothing on the bus to answer. On the previous project it
  was called before the display came up, which is the right place.

## The SDIO slave reset polarity is inverted from the board default

This is the one that wastes afternoons. Selecting the Tab5 board sets
`CONFIG_ESP_HOSTED_SDIO_RESET_ACTIVE_LOW`, and upstream's Kconfig even carries a
comment saying active-*high* is what leaves the C6 stuck in reset. On this
hardware it is precisely the other way round.

With the board default:

```
E (4762) sdmmc_common: sdmmc_init_ocr: send_op_cond (1) returned 0x107   (x15)
E (7396) H_SDIO_DRV: card init failed
E (7415) H_API: ESP-Hosted link not yet up
```

With `CONFIG_ESP_HOSTED_SDIO_RESET_ACTIVE_HIGH=y` it enumerates first try:

```
I (5529) transport: Received INIT event from ESP32 peripheral
I (5535) transport: Identified slave [esp32c6]
I (5542) transport: capabilities: 0xd
I (5581) H_API: Transport active
```

So in `sdkconfig.defaults`:

```
CONFIG_ESP_HOSTED_SDIO_RESET_ACTIVE_LOW=n
CONFIG_ESP_HOSTED_SDIO_RESET_ACTIVE_HIGH=y
```

`send_op_cond` timing out means nothing on the bus answered the first "are you
there?", which looks exactly like dead hardware - that is what makes it
misleading. If you see it, flip this symbol before suspecting anything else.

Already ruled out on the previous unit, so you need not repeat them: pin
assignments, `BSP_WIFI_EN` polarity (both levels behave identically), C6 power-up
timing, and host component versions 2.12.3 and 2.12.12.

A triage step that saves real time: flash M5Stack's stock demo with M5Burner. If
Wi-Fi works there, the C6 is healthy and the fault is host-side configuration -
which is how this was found.

## Do not pin esp_hosted low

Tab5 support only landed in later 2.12.x releases. Pin below that and
`CONFIG_ESP32P4_TAB5_C6_BOARD` **silently vanishes from Kconfig** - no error, no
warning - the SDIO pins fall back to ESP32-P4-EV-Board defaults, and Wi-Fi fails
in a way that looks nothing like a version problem. Use `>=2.12.10,<3.0`.

`CONFIG_ESP32P4_TAB5_C6_BOARD=y` is what supplies the right pins; do not hand-set
them. The boot log prints what it actually used - check it first:

```
I (5305) sdio_wrapper: GPIOs: CLK[12] CMD[13] D0[11] D1[10] D2[9] D3[8] Slave_Reset[15]
```

A harmless version warning also appears; the transport works regardless:

```
W (5561) transport: Version mismatch: Host [2.12.0] > Co-proc [0.0.0]
```

## SNTP fails hard, not softly

This one presented as a slow sync and was anything but. On the previous project
Wi-Fi associated, the IP appeared on screen, HTTPS to a public API worked - and
the clock sat on "waiting for time sync..." forever.

The cause: asking for the DHCP-provided NTP server without the option compiled
in. ESP-IDF does not warn and carry on; it fails the whole call:

```c
/* esp_netif_sntp.c */
if (config->server_from_dhcp) {
#if LWIP_DHCP_GET_NTP_SRV
    sntp_servermode_dhcp(1);
#else
    ESP_GOTO_ON_FALSE(false, ESP_ERR_INVALID_ARG, err, TAG,
        "Tried to configure SNTP server from DHCP, while disabled. ...");
#endif
}
```

Wrapped in `ESP_RETURN_ON_ERROR`, that returned early and **SNTP never started at
all**. Wi-Fi survived only because `esp_wifi_start()` ran a few lines earlier.

The working configuration, if you want DHCP-provided NTP:

```
CONFIG_LWIP_DHCP_GET_NTP_SRV=y
CONFIG_LWIP_SNTP_MAX_SERVERS=2
```

```c
sntp_cfg.server_from_dhcp           = true;
sntp_cfg.renew_servers_after_new_IP = true;
sntp_cfg.index_of_first_server      = 1;   /* slot 0 belongs to DHCP */
sntp_cfg.ip_event_to_renew          = IP_EVENT_STA_GOT_IP;
```

When DHCP offers a server, lwip writes it into **slot 0**, overwriting whatever
was configured there, so `index_of_first_server = 1` keeps your own server. That
requires `MAX_SERVERS >= 2`; at the default of 1 the renew writes past the end of
the list.

If you do not need DHCP NTP, the simplest correct thing is to leave
`server_from_dhcp` false and configure `pool.ntp.org` directly.

**The generalisable lesson:** a subsystem that failed to initialise and one that
is merely slow look identical from outside. When something never completes, check
that it *started* before reasoning about timeouts or the network.

## Make init failures loud, not fatal

Log an initialisation failure at error level and carry on, rather than returning
out of a setup function. The silence is what made the SNTP bug expensive, and on
the Pico ancestor of that project a Wi-Fi failure that returned early left the
second core blocked forever on a FIFO pop.

For this project that applies directly: `PLAN.md` gives every data source a
`last_error` and a stale badge. Make sure a *failure to start* lands in the same
place a *failure to poll* does, or an unstarted task will look like a quiet one.

## Timezone

`GMT0BST,M3.5.0/1,M10.5.0/2` via `setenv("TZ", ...)` then `tzset()` gives GMT in
winter and BST from the last Sunday in March to the last Sunday in October. Note
`PLAN.md` §3 writes the rule without the final `/2`; include the end-transition
hour so the October changeover lands at the right time. Host-test the changeover,
as the plan already calls for.

The RX8130CE RTC over I²C is what gives you a clock before NTP - worth wiring up
in Phase 2 so a boot without network is not blank.
