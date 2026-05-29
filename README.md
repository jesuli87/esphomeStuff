# ESPHome Stuff

Custom ESPHome components and Home Assistant scripts for the **LilyGo T5 4.7" e-paper display** (ESP32-S3).

## Repository structure

```
components/
  t547/                    # ESPHome display component for the T5 4.7" panel
  lilygo_t5_47_battery/    # Battery monitor component (see notes below)
pyscript/
  claudecalendar_data_conversion.py   # HA pyscript for CalendarClaude device
```

---

## `t547` — Display component

ESPHome external component for the LilyGo T5 4.7" e-paper panel, based on the [epdiy](https://github.com/vroland/epdiy) driver. Adapted for use with **ESP-IDF framework** (not Arduino) on the **ESP32-S3-WROOM-1** variant with 8 MB OPI PSRAM.

### Key changes from the original epdiy-based component

- **ESP-IDF 5.x compatibility**: replaced `ESP_ERROR_CHECK` hard aborts with graceful error handling throughout `i2s_data_bus.c`
- **Intel 8080 bus clock source**: tries `LCD_CLK_SRC_XTAL` then `LCD_CLK_SRC_PLL160M`; `LCD_CLK_SRC_PLL160M` (enum value 21) is rejected by `esp_clk_tree_src_get_freq_hz` in ESP-IDF 5.5.4 on ESP32-S3
- **IRAM section conflicts**: removed `IRAM_ATTR` from all `.h` declarations (kept only on `.c`/`.cpp` definitions) to fix ESP-IDF 5.x linker section number conflicts
- **SPIRAM allocation**: uses `heap_caps_malloc(size, MALLOC_CAP_SPIRAM)` instead of Arduino `ps_malloc()`
- **Legacy RMT driver**: `rmt_pulse.c` uses the legacy `driver/rmt.h` API; add `CONFIG_RMT_ENABLE_LEGACY_DRIVER: "y"` to your `sdkconfig_options` to suppress the deprecation warning
- **Arduino framework removed**: `display.py` no longer restricts to Arduino-only builds

### ESPHome YAML usage

```yaml
external_components:
  - source: github://jesuli87/esphomeStuff@claude
    components:
      - t547
    refresh: always

esp32:
  board: esp32-s3-devkitc-1
  variant: esp32s3
  framework:
    type: esp-idf
    sdkconfig_options:
      CONFIG_SPIRAM: "y"
      CONFIG_SPIRAM_MODE_OCT: "y"
      CONFIG_SPIRAM_SPEED_80M: "y"
      CONFIG_ESP32S3_DATA_CACHE_LINE_64B: "y"
      CONFIG_RMT_ENABLE_LEGACY_DRIVER: "y"

display:
  - platform: t547
    id: my_display
    rotation: 90
    update_interval: never
    lambda: |-
      // your drawing code
```

> **Note**: pin `CONFIG_RMT_ENABLE_LEGACY_DRIVER` to suppress the ESP-IDF 5.x deprecation warning for `driver/rmt.h`.

---

## `lilygo_t5_47_battery` — Battery monitor

Reads battery voltage on the LilyGo T5 4.7" S3 via GPIO14.

**Hardware constraints**:
- GPIO14 on ESP32-S3 is **ADC2 channel 3** (GPIO1–10 = ADC1, GPIO11–20 = ADC2).
- ADC2 conflicts with WiFi on ESP32-S3: `adc2_get_raw()` returns `ESP_ERR_INVALID_STATE` while WiFi is active. The component handles this gracefully — readings are skipped (last known value retained) rather than publishing NAN.
- In practice the device is almost always WiFi-connected, so voltage is read at boot before WiFi fully initialises and then not updated again until the next boot cycle. Battery level changes slowly enough that this is acceptable.

---

## `pyscript/claudecalendar_data_conversion.py`

Home Assistant [pyscript](https://hacs-pyscript.readthedocs.io/) service that converts raw HA calendar events into the JSON format expected by the CalendarClaude ESPHome firmware, then writes the result to HA state entities that the device reads via the native API.

### Data flow

```
ESPHome device
  │  fires esphome.calendar_refresh_request (on boot, on interval, on config change)
  │  event data: device_name, calendar_entities, calendar_names
  ▼
HA automation (mode: single)
  │  calls calendar.get_events for the entity IDs from the event data
  │  passes response to pyscript
  ▼
pyscript.claudecalendar_data_conversion
  │  groups events by date, strips descriptions, normalises locations
  │  clamps past events to today
  │  computes closest_end_time Unix timestamp for deep-sleep wake optimisation
  │  writes sensor.{device_name}_calendar_data      (state: "ok", attribute "data": JSON)
  │  writes sensor.{device_name}_closest_end_time   (state: Unix timestamp as string)
  ▼
ESPHome native API (state subscription)
  │  device receives state change automatically
  ▼
on_value handler → re-renders display
```

**Why `state.set` instead of calling the ESPHome API service directly**: calling `hass.services.call("esphome", ...)` from pyscript blocks the HA event loop and causes system-wide hangs. `state.set()` is non-blocking.

### What it does

1. Called by the HA automation when the device fires `esphome.calendar_refresh_request`
2. Receives raw events from `calendar.get_events` (called by the automation) and calendar names from the event payload — no HA entity state lookups needed
3. Groups events by date, strips descriptions, normalises locations, clamps past events to today
4. Computes the `closest_end_time` Unix timestamp for the wake-at-event-end deep sleep optimisation
5. Writes `sensor.{device_name}_calendar_data` and `sensor.{device_name}_closest_end_time` — ESPHome picks these up automatically via `platform: homeassistant`

### Configuration

Calendar selection, display names, and sleep/update intervals are configured directly on the device page in HA. All values are stored in ESP32 NVS flash and survive deep sleep cycles — no reflash needed to change calendars.

| Entity | Type | Purpose | Default |
|---|---|---|---|
| `text.{name}_calendar_entities` | text (config) | Comma-separated HA calendar entity IDs | — |
| `text.{name}_calendar_names` | text (config) | Comma-separated display names (positional) | — |
| `number.{name}_update_interval` | number (config) | Daytime fetch + sleep interval | 15 min |
| `number.{name}_night_sleep_duration` | number (config) | Night-time sleep duration | 240 min |

Changing either text entity while the device is awake triggers an immediate re-fetch. Single-word calendars without a matching name entry fall back to the capitalised last segment of the entity ID.

Night-time window start/end hours are still set as YAML substitutions (`night_time_start`, `night_time_end`) since they rarely change.

### HA automation

One automation handles **all** CalendarClaude devices. The device fires an event on boot, on its configured update interval, and whenever calendar config changes. `mode: single` prevents concurrent runs from stacking if the device fires faster than the automation completes.

```yaml
alias: ESPHome Calendar Refresh
description: >-
  ESPHome Calendar automation — fetches calendar data from HA and passes it to
  pyscript for processing. Triggered by each device on boot, on its configured
  update interval, and when calendar config changes. mode: queued ensures
  multiple devices are handled sequentially without dropping events.
triggers:
  - event_type: esphome.calendar_refresh_request
    trigger: event
actions:
  - variables:
      device_name: "{{ trigger.event.data.device_name }}"
      calendar_entities_str: "{{ trigger.event.data.calendar_entities }}"
      calendar_names_str: "{{ trigger.event.data.calendar_names }}"
  - target:
      entity_id: >
        {{ calendar_entities_str.split(',') | map('trim') | select | join(', ') }}
    data:
      duration:
        days: 180
    response_variable: calendar_response
    action: calendar.get_events
  - data:
      device_name: "{{ device_name }}"
      calendar: "{{ calendar_response }}"
      calendar_names: "{{ calendar_names_str }}"
    action: pyscript.claudecalendar_data_conversion
mode: queued
max_exceeded: silent
```

**Why calendar config comes from the event payload (not `states(...)`)**: on boot the device connects to HA and fires the event before HA has had time to sync the text entity states back. Reading from the event payload (which the device populates from its own NVS) avoids this race condition.

### Setup

1. Flash the device
2. Drop `claudecalendar_data_conversion.py` into your pyscript directory and reload pyscript
3. Add the automation above to HA (one copy works for all devices)
4. Set **Calendar Entities** and **Calendar Names** on the device page in HA
5. Press **Refresh Screen** to fetch data immediately without rebooting

---

## Hardware notes

| Item | Detail |
|---|---|
| Board | LilyGo T5 4.7" S3 |
| SoC | ESP32-S3-WROOM-1 |
| PSRAM | 8 MB OPI (confirmed via `heap_caps_get_free_size(MALLOC_CAP_SPIRAM)` = 8,386,228 bytes) |
| Flash | 16 MB |
| Display | 960 × 540 grayscale e-paper |
| Deep sleep wakeup | Timer only — do not use `esp32_ext1_wakeup` on GPIO21; it is pulled high by hardware and will cause immediate re-wakeup on every sleep cycle |

### Deep sleep: EXT1 wakeup persists and re-enables across cycles

**Root cause 1 — RTC register persistence**: Removing `esp32_ext1_wakeup` from the ESPHome YAML is not sufficient if the device was previously flashed with EXT1 configured. The ESP32 stores wakeup configuration in the RTC domain, which survives deep sleep cycles and software resets (OTA). A power-on reset clears it, but that is not guaranteed during normal OTA updates.

**Root cause 2 — ESPHome's `begin_sleep()` path**: Even after calling `esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_EXT1)` in the `enter_deep_sleep` script, wakeup cause 3 (EXT1) returns on every cycle after the first. Something inside ESPHome's `begin_sleep()` shutdown sequence (WiFi teardown, shutdown callbacks, etc.) was re-enabling EXT1 between the disable call and the actual `esp_deep_sleep_start()`.

**Symptom**: device wakes with cause 3 within seconds of every sleep entry — or works for exactly one cycle then reverts to cause 3.

**Fix**: bypass `deep_sleep.enter` entirely. Call `esp_deep_sleep_start()` directly from a lambda so nothing runs between the disable and the actual sleep:

```cpp
// In enter_deep_sleep script — final lambda, nothing after this runs:
esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_EXT1);
esp_sleep_enable_timer_wakeup((uint64_t)sleep_duration_us);
esp_deep_sleep_start();
```

Diagnostic sensors that `on_shutdown` would normally update must be explicitly called with `component.update` earlier in the same script, followed by a ~1 s delay to allow the native API to flush state to HA before the connection drops. `esp_deep_sleep_start()` handles WiFi power-down internally so no explicit WiFi stop is needed.

### Battery level across sleep cycles

Battery level is stored in a `restore_value: yes` global (`battery_level_stored`) and published to HA immediately at boot start, before the ADC measurement runs (~60 s into the boot cycle). This ensures HA sees the last known battery level as soon as the device reconnects rather than showing "unavailable" for the first minute.

---

## Planned improvements

| # | Area | Description |
|---|---|---|
| 3 | Calendar mini-grid | Day dots should show a rolling window of X days forward instead of only the current calendar month |
| 4 | Battery component | Migrate from legacy `driver/adc.h` (`adc2_get_raw`) to the ESP-IDF 5.x `esp_adc/adc_oneshot.h` oneshot API once the include-path issue with ESPHome's external component CMake setup is resolved. Deprecated-declarations warnings are currently suppressed via `#pragma GCC diagnostic` in `Lilygot547Battery.cpp`. |
| 5 | RMT driver (t547) | Migrate `rmt_pulse.c` from legacy `driver/rmt.h` to the new `driver/rmt_tx.h` API to clear the deprecation warning |
