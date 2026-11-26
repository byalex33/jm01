"""Fail-closed validation for a generated schedule.

The device treats an empty events list as valid and wipes its own events
(showing "OFF SEASON"), so the generator must refuse to publish anything
suspicious and keep the previous file instead.
"""
from __future__ import annotations

import datetime
import re

DATE_RE = re.compile(r"^\d{4}-\d{2}-\d{2}$")
# Latin-1 (<= 0xFF) plus the three extra glyphs the firmware fonts include.
EXTRA_OK = {"–", "’", "…"}  # en-dash, curly apostrophe, ellipsis


class ValidationError(Exception):
    pass


def _latin1_ok(s: str) -> bool:
    return all(ord(c) <= 0xFF or c in EXTRA_OK for c in s)


def _date(s: str) -> datetime.date:
    return datetime.date.fromisoformat(s)


def validate(new: dict, old: dict | None, today: datetime.date, min_upcoming: int = 1) -> bool:
    events = new.get("events")
    if not isinstance(events, list) or not events:
        raise ValidationError(
            "events is empty or missing — refusing to publish (would blank the device)"
        )

    errors: list[str] = []
    keys_new = set()
    upcoming = 0

    for i, e in enumerate(events):
        tag = f"events[{i}] ({e.get('name', '?')})"
        if e.get("tour") not in ("PPA", "MLP"):
            errors.append(f"{tag}: tour must be PPA or MLP, got {e.get('tour')!r}")
        for f in ("name", "city", "start", "end"):
            if not isinstance(e.get(f), str):
                errors.append(f"{tag}: missing/non-string field '{f}'")
        name, city = e.get("name", ""), e.get("city", "")
        start, end = e.get("start", ""), e.get("end", "")
        if name and not _latin1_ok(name):
            errors.append(f"{tag}: name has characters the display font can't render")
        if city and not _latin1_ok(city):
            errors.append(f"{tag}: city has characters the display font can't render")
        if not DATE_RE.match(start) or not DATE_RE.match(end):
            errors.append(f"{tag}: bad date format start={start!r} end={end!r}")
            continue
        try:
            sd, ed = _date(start), _date(end)
        except ValueError:
            errors.append(f"{tag}: invalid calendar date")
            continue
        if ed < sd:
            errors.append(f"{tag}: end {end} before start {start}")
        if ed >= today:
            upcoming += 1
        keys_new.add((e.get("tour"), start))

    if upcoming < min_upcoming:
        errors.append(f"only {upcoming} upcoming events (need >= {min_upcoming})")

    seeds = new.get("seeds")
    if seeds:
        names = {e.get("name") for e in events}
        if seeds.get("event") not in names:
            errors.append(f"seeds.event {seeds.get('event')!r} matches no event name")
        for k in ("MS", "WS", "MD", "WD"):
            v = seeds.get(k, "")
            if v and not _latin1_ok(v):
                errors.append(f"seeds.{k}: unrenderable characters")

    # Drop detection: an event that was still in the future last time must not
    # silently vanish (that signals a scrape/parse failure, not a real change).
    if old and isinstance(old.get("events"), list):
        for e in old["events"]:
            try:
                ed = _date(e.get("end", ""))
            except (ValueError, TypeError):
                continue
            if ed >= today and (e.get("tour"), e.get("start")) not in keys_new:
                errors.append(
                    f"future event '{e.get('name')}' ({e.get('start')}) disappeared "
                    "— possible scrape failure"
                )

    if errors:
        raise ValidationError("; ".join(errors))
    return True
