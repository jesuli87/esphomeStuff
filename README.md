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

Home Assistant [pyscript](https://hacs-pyscript.readthedocs.io/) service that converts raw HA calendar events into the JSON format expected by the CalendarClaude ESPHome firmware.

### What it does

1. Called by an HA automation on a `time_pattern` trigger (every minute)
2. Receives raw events from `calendar.get_events` for the configured calendars
3. Groups events by date, strips descriptions, normalises locations, clamps past events to today
4. Writes the result to `sensor.claudecalendar_data` with `entries` and `closest_end_time` attributes

### Configuration

Edit `CALENDAR_NAMES` at the top of the file:

```python
CALENDAR_NAMES = {
    "calendar.person1": "Person1",
    "calendar.family":  "Family",
}
```

Single-word calendar entity IDs (e.g. `calendar.work`) are mapped automatically to their capitalised name without needing an entry here.

### HA automation

```yaml
- trigger:
    - platform: time_pattern
      minutes: "/1"
  action:
    - service: calendar.get_events
      data:
        duration:
          days: 180
      target:
        entity_id:
          - calendar.person1
      response_variable: calendar_response
    - service: pyscript.claudecalendar_data_conversion
      data:
        calendar: "{{ calendar_response }}"
        now: "{{ now().date() }}"
  sensor:
    - name: Claude Calendar Data
      unique_id: claudecalendar_data
      state: "{{ now().isoformat() }}"
      attributes:
        todays_day_name: >
          {{ ['Monday','Tuesday','Wednesday','Thursday','Friday','Saturday','Sunday'][now().weekday()] }}
        todays_date_month_year: >
          {% set months = ["January","February","March","April","May","June","July","August","September","October","November","December"] %}
          {{ months[now().month-1] }} {{ now().strftime('%Y') }}
        closest_end_time: "{{ state_attr('sensor.claudecalendar_data', 'closest_end_time') }}"
        entries: "{{ state_attr('sensor.claudecalendar_data', 'entries') }}"
```

### ESPHome YAML substitutions

```yaml
substitutions:
  calendar_data_entity_id: sensor.claude_calendar_data
  calendar_data_update_during_deep_sleep_entity_id: binary_sensor.claude_calendar_data_update_during_deep_sleep
```

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

Diagnostic sensors that `on_shutdown` would normally update must be explicitly called with `component.update` earlier in the same script before the sleep lambda. `esp_deep_sleep_start()` handles WiFi power-down internally so no explicit WiFi stop is needed.

---

## Planned improvements

| # | Area | Description |
|---|---|---|
| 1 | Deep sleep | Sleep durations (`deep_sleep_duration`, `night_time_deep_sleep_duration`) configurable from HA instead of hardcoded YAML substitutions |
| 2 | Calendar data | Calendar display names configurable from HA (currently hardcoded in `CALENDAR_NAMES` dict in the pyscript) |
| 3 | Calendar mini-grid | Day dots should show a rolling window of X days forward instead of only the current calendar month |
| 4 | Battery component | Migrate from legacy `driver/adc.h` (`adc2_get_raw`) to the ESP-IDF 5.x `esp_adc/adc_oneshot.h` oneshot API once the include-path issue with ESPHome's external component CMake setup is resolved. Deprecated-declarations warnings are currently suppressed via `#pragma GCC diagnostic` in `Lilygot547Battery.cpp`. |
| 5 | RMT driver (t547) | Migrate `rmt_pulse.c` from legacy `driver/rmt.h` to the new `driver/rmt_tx.h` API to clear the deprecation warning |
