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

Battery voltage monitor component. **Not recommended for use with the T5 4.7" S3**: GPIO15 (ADC input) conflicts with the display driver hardware on this board variant, causing the device to hang silently. Disable the battery sensor and return `NAN` from a template sensor instead.

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
