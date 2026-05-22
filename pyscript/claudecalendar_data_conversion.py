# Dictionary to map calendar entity keys to display names.
# Single-word calendars are handled automatically:
#   calendar.work  ->  "Work"  (no entry needed)
# Multi-word or compound calendars need an explicit entry:
#   calendar.hello_world  ->  add "calendar.hello_world": "Hello World"
CALENDAR_NAMES = {
    "calendar.person1": "Person1",
}

DAY_NAMES = ["Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"]

# How many day-entries to send to the ESPHome device
MAX_ENTRIES = 10

import json
import logging
from homeassistant.util import dt as dt_util


def set_sensor_state(converted_data, logger):
    """Set sensor.claudecalendar_data state with entries and closest_end_time attributes."""
    try:
        entries = converted_data[0]
        closest_end_time = converted_data[1]

        hass.async_add_job(
            hass.states.set,
            "sensor.claudecalendar_data",
            "OK",
            {
                "closest_end_time": str(closest_end_time),
                "entries": json.dumps(entries),
            },
        )
        logger.info("Successfully set state for sensor.claudecalendar_data")
    except Exception as e:
        logger.error(f"Error setting state for sensor.claudecalendar_data: {e}")


@service
def claudecalendar_data_conversion(calendar=None, now=None, **kwargs):
    """Pyscript service called by the HA automation to convert raw calendar
    event data into the JSON format expected by the CalendarClaude ESPHome firmware."""

    output = {}
    logger = logging.getLogger("custom_components.pyscript.claudecalendar_data_conversion")

    calendar = kwargs.get("calendar", calendar)
    now = kwargs.get("now", now)

    logger.debug(f"Received calendar input: {calendar}")
    logger.debug(f"Current time input: {now}")

    if not calendar:
        logger.error("Error: The 'calendar' parameter was not provided or is None.")
        return

    events_by_date = {}
    entrie_count = 0
    closest_end_time = None

    today = now.split("T")[0]

    for calendar_key, events_list in calendar.items():
        if "events" not in events_list:
            logger.error(f"No 'events' key found in calendar '{calendar_key}'.")
            continue

        for event in events_list["events"]:
            if "description" in event:
                event.pop("description")

            parts = event["start"].split("T")
            event_date = parts[0]
            event_time = parts[1] if len(parts) > 1 else None

            # Clamp past events to today
            if event_date < now:
                event["start"] = now if event_time is None else f"{now}T{event_time}"
                event_date = now

            event["calendar_name"] = CALENDAR_NAMES.get(
                calendar_key, calendar_key.split(".")[1].capitalize()
            )

            if "location" in event:
                location_lines = event["location"].split("\n")
                event["location_name"] = location_lines[0]
                event.pop("location")

            if event_date in events_by_date:
                events_by_date[event_date].append(event)
            else:
                events_by_date[event_date] = [event]

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
            closest_end_time = sorted(
                other_events,
                key=lambda item: dt_util.parse_datetime(item["end"]),
            )[0]["end"]

        if all_day_events or other_events:
            other_events.sort(key=lambda item: dt_util.parse_datetime(item["start"]))

            result.append(
                {
                    "date": date,
                    "day": dt_util.parse_datetime(date).day,
                    "is_today": int(date == dt_util.now().isoformat().split("T")[0]),
                    "day_name": DAY_NAMES[dt_util.parse_datetime(date).weekday()],
                    "all_day": all_day_events,
                    "other": other_events,
                }
            )

    converted_data = (result, closest_end_time)
    logger.info(f"Converted data: {converted_data}")

    output["entries"] = converted_data[0]
    output["closest_end_time"] = converted_data[1]

    set_sensor_state(converted_data, logger)
