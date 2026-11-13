#!/usr/bin/env python3
"""Host-side check that the Open-Meteo parser + WMO mapping match the firmware.

Mirrors weather_map_wmo / weather_map_phase / current-object extraction in
src/wifi_weather.cpp. Fetches a live Tokyo response and also runs a code table.
"""
from __future__ import annotations

import json
import re
import sys
import urllib.request
from pathlib import Path

URL = (
    "https://api.open-meteo.com/v1/forecast"
    "?latitude=35.68&longitude=139.76"
    "&current=weather_code,is_day,cloud_cover,precipitation,temperature_2m"
    "&timezone=Asia%2FTokyo"
)


def map_wmo(code: int, precipitation: float, cloud_cover: int) -> str:
    if code in (0, 1):
        return "clear"
    if code in (2, 3, 45, 48):
        return "cloudy"
    if (
        51 <= code <= 57
        or 61 <= code <= 67
        or 71 <= code <= 77
        or 80 <= code <= 82
        or 85 <= code <= 86
        or 95 <= code <= 99
    ):
        return "rain"
    if precipitation > 0.05:
        return "rain"
    if cloud_cover >= 40:
        return "cloudy"
    return "clear"


def map_phase(is_day: int, local_hour: int) -> str:
    if not is_day:
        return "night"
    if 5 <= local_hour < 9:
        return "morning"
    if 9 <= local_hour < 16:
        return "day"
    if 16 <= local_hour < 19:
        return "evening"
    return "day"


def extract_current(text: str) -> dict:
    # Same rule as firmware: the JSON key "current", not "current_units".
    m = re.search(r'"current"\s*:\s*\{', text)
    if not m:
        raise SystemExit("no current object")
    start = text.find("{", m.start())
    depth = 0
    for i, ch in enumerate(text[start:], start):
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                return json.loads(text[start : i + 1])
    raise SystemExit("unbalanced current object")


def parse_hour(iso: str) -> int:
    t = iso.split("T", 1)
    if len(t) != 2:
        return 12
    return int(t[1][0:2])


# WMO samples: (code, name, expected map)
TABLE = [
    (0, "Clear", "clear"),
    (1, "Mainly clear", "clear"),
    (2, "Partly cloudy", "cloudy"),
    (3, "Overcast", "cloudy"),
    (45, "Fog", "cloudy"),
    (48, "Rime fog", "cloudy"),
    (51, "Light drizzle", "rain"),
    (53, "Moderate drizzle", "rain"),
    (55, "Dense drizzle", "rain"),
    (56, "Light freezing drizzle", "rain"),
    (57, "Dense freezing drizzle", "rain"),
    (61, "Slight rain", "rain"),
    (63, "Moderate rain", "rain"),
    (65, "Heavy rain", "rain"),
    (66, "Light freezing rain", "rain"),
    (67, "Heavy freezing rain", "rain"),
    (71, "Slight snow", "rain"),  # nearest existing scene
    (73, "Moderate snow", "rain"),
    (75, "Heavy snow", "rain"),
    (77, "Snow grains", "rain"),
    (80, "Slight rain showers", "rain"),
    (81, "Moderate rain showers", "rain"),
    (82, "Violent rain showers", "rain"),
    (85, "Slight snow showers", "rain"),
    (86, "Heavy snow showers", "rain"),
    (95, "Thunderstorm", "rain"),
    (96, "Thunderstorm + slight hail", "rain"),
    (99, "Thunderstorm + heavy hail", "rain"),
]

PHASE_TABLE = [
    (0, 12, "night"),
    (0, 3, "night"),
    (1, 6, "morning"),
    (1, 9, "day"),
    (1, 15, "day"),
    (1, 16, "evening"),
    (1, 18, "evening"),
    (1, 20, "day"),  # is_day wins over late hour
]


def main() -> int:
    failed = 0
    for code, name, expect in TABLE:
        got = map_wmo(code, 0.0, 0)
        ok = got == expect
        if not ok:
            failed += 1
        print(f"  {'OK' if ok else 'FAIL'}  WMO {code:3d}  {name:28s} -> {got} (want {expect})")

    for is_day, hour, expect in PHASE_TABLE:
        got = map_phase(is_day, hour)
        ok = got == expect
        if not ok:
            failed += 1
        print(f"  {'OK' if ok else 'FAIL'}  is_day={is_day} hour={hour:02d} -> {got} (want {expect})")

    raw_path = Path("/tmp/om-tokyo.json")
    print(f"\nLive fetch: {URL}")
    with urllib.request.urlopen(URL, timeout=20) as r:
        body = r.read()
        status = r.status
        ctype = r.headers.get("Content-Type", "")
    text = body.decode("utf-8")
    raw_path.write_bytes(body)
    print(f"  HTTP {status}  {ctype}  {len(body)} bytes")

    doc = json.loads(text)
    cur = extract_current(text)
    # Cross-check against json.loads of the full document
    assert cur["weather_code"] == doc["current"]["weather_code"]
    assert cur["is_day"] == doc["current"]["is_day"]

    code = int(cur["weather_code"])
    is_day = int(cur["is_day"])
    cloud = int(round(float(cur["cloud_cover"])))
    precip = float(cur["precipitation"])
    temp = float(cur["temperature_2m"])
    iso = cur["time"]
    hour = parse_hour(iso)
    mapped = map_wmo(code, precip, cloud)
    phase = map_phase(is_day, hour)
    print(
        f"  current time={iso} (hour={hour}) code={code} is_day={is_day} "
        f"cloud={cloud} precip={precip} temp={temp}"
    )
    print(f"  mapped weather={mapped} phase={phase}")
    print(f"  timezone={doc.get('timezone')} utc_offset={doc.get('utc_offset_seconds')}")

    if failed:
        print(f"\nFAILED {failed} mapping assertions")
        return 1
    print("\nAll mapping assertions passed; live payload parsed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
