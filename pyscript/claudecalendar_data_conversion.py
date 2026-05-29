DAY_NAMES = ["Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"]

# How many day-entries to send to the ESPHome device
MAX_ENTRIES = 10

import json
import logging
from homeassistant.util import dt as dt_util


@service
def claudecalendar_data_conversion(device_name=None, calendar=None, calendar_names=None, **kwargs):
    """Processes raw HA calendar events and writes the result to HA state entities
    that the ESPHome device reads via platform: homeassistant.

    Data flow
    ---------
    1. ESPHome device fires esphome.calendar_refresh_request on boot, on the
       configured update interval, or when calendar config text entities change.
    2. HA automation (mode: queued) catches the event, calls calendar.get_events
       for the entity IDs passed in the event data, and calls this service with
       the response. mode: queued ensures multiple devices are handled in order
       without dropping events.
    3. This service processes the events and writes two HA state entities:
         sensor.{device_name}_calendar_data        — state: "ok", attribute "data": JSON
         sensor.{device_name}_closest_end_time     — state: Unix timestamp float string
    4. ESPHome receives the state changes via the native API subscription and
       re-renders the display automatically via on_value handlers.

    No direct ESPHome API service call is made from this service. Calling
    hass.services.call("esphome", ...) from pyscript blocks the HA event loop
    and causes system hangs; state.set() is non-blocking.

    Parameters
    ----------
    device_name    : str   ESPHome device name (matches esphome.name in device YAML).
    calendar       : dict  Raw response from calendar.get_events.
    calendar_names : str   Comma-separated display names, positionally matched to the
                           calendar entity IDs in the get_events response. Passed
                           directly from the event data (device reads its own NVS).
    """

    logger = logging.getLogger("custom_components.pyscript.claudecalendar_data_conversion")

    device_name    = kwargs.get("device_name",    device_name)
    calendar       = kwargs.get("calendar",       calendar)
    calendar_names = kwargs.get("calendar_names", calendar_names) or ""

    if not device_name:
        logger.error("'device_name' not provided.")
        return
    if not calendar:
        logger.error("'calendar' not provided.")
        return

    logger.debug(f"Processing calendar data for device '{device_name}'")

    # Build {entity_id: display_name} from the comma-separated names string.
    # Positional: calendar_names[i] maps to the i-th key in the calendar response.
    # Missing entries fall back to the capitalised last segment of the entity ID.
    names_list = [n.strip() for n in calendar_names.split(",") if n.strip()]
    entity_ids  = list(calendar.keys())
    calendar_names_map = {}
    for i, eid in enumerate(entity_ids):
        if i < len(names_list) and names_list[i]:
            calendar_names_map[eid] = names_list[i]
        else:
            calendar_names_map[eid] = eid.split(".")[1].capitalize()

    today = dt_util.now().date().isoformat()
    events_by_date = {}
    entrie_count = 0
    closest_end_time_ts = 0.0  # Unix timestamp; 0.0 = no upcoming event end

    for calendar_key, events_list in calendar.items():
        if "events" not in events_list:
            logger.error(f"No 'events' key for calendar '{calendar_key}'.")
            continue

        for event in events_list["events"]:
            if "description" in event:
                event.pop("description")

            parts = event["start"].split("T")
            event_date = parts[0]
            event_time = parts[1] if len(parts) > 1 else None

            # Clamp past events to today
            if event_date < today:
                event["start"] = today if event_time is None else f"{today}T{event_time}"
                event_date = today

            event["calendar_name"] = calendar_names_map.get(
                calendar_key, calendar_key.split(".")[1].capitalize()
            )

            if "location" in event:
                location_lines = event["location"].split("\n")
                event["location_name"] = location_lines[0]
                event.pop("location")

            events_by_date.setdefault(event_date, []).append(event)

    sorted_dates = sorted(events_by_date.keys())
    result = []

    for date in sorted_dates:
        all_day_events = []
        other_events = []

        for event in events_by_date[date]:
            if entrie_count == MAX_ENTRIES:
                break
            if "T" not in event["start"]:
                all_day_events.append(event)
            else:
                other_events.append(event)
            entrie_count += 1

        if other_events and date == today:
            earliest = sorted(other_events, key=lambda e: dt_util.parse_datetime(e["end"]))[0]
            parsed_end = dt_util.parse_datetime(earliest["end"])
            if parsed_end:
                closest_end_time_ts = float(dt_util.as_timestamp(parsed_end))

        if all_day_events or other_events:
            other_events.sort(key=lambda e: dt_util.parse_datetime(e["start"]))
            result.append({
                "date": date,
                "day": dt_util.parse_datetime(date).day,
                "is_today": int(date == today),
                "day_name": DAY_NAMES[dt_util.parse_datetime(date).weekday()],
                "all_day": all_day_events,
                "other": other_events,
            })

    entries_json = json.dumps(result)
    logger.info(f"Writing {len(result)} day entries for '{device_name}', closest_end_time_ts={closest_end_time_ts}")

    # Write to HA state entities — ESPHome reads these via platform: homeassistant.
    # Using state.set avoids any blocking call back into the ESPHome API.
    state.set(
        f"sensor.{device_name}_calendar_data",
        value="ok",  # state value must be ≤255 chars; JSON goes in the attribute
        new_attributes={
            "friendly_name": f"{device_name} Calendar Data",
            "data": entries_json,
        },
    )
    state.set(
        f"sensor.{device_name}_closest_end_time",
        value=str(closest_end_time_ts),
        new_attributes={
            "friendly_name": f"{device_name} Closest End Time",
            "unit_of_measurement": "s",
        },
    )
    logger.info(f"HA state entities updated for '{device_name}'.")
