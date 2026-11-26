"""Fetch the PPA Tour flagship schedule from ppatour.com.

The events page embeds a JSON-LD ItemList of the ~12 featured tour events
(Majors, Cups, Opens, Championships) with clean names, ISO dates and
"City, ST" addresses. That is far more stable than scraping visible text,
and it is already server-rendered, so a plain fetch (no JS) gets it.
"""
from __future__ import annotations

import json
import re
import urllib.request

PPA_EVENTS_URL = "https://www.ppatour.com/events/"
UA = "jm01-scraper/1.0 (+https://github.com/byalex33/jm01)"

# Title sponsors that prefix PPA event names in the JSON-LD. Stripped only as a
# whole leading token, so real name words (Greater, Cape, Newport) are safe.
SPONSOR_PREFIXES = [
    "Carvana", "Veolia", "Fasenra", "Vizzy", "Proton", "Guaranteed Rate",
    "Select Medical", "Fanatics Sportsbook", "Fanatics", "Hyundai",
    "Biofreeze", "Margaritaville", "Takeya", "Skechers", "Roar",
]

DATE_RE = re.compile(r"^\d{4}-\d{2}-\d{2}$")


def fetch_html(url: str = PPA_EVENTS_URL, timeout: int = 20) -> str:
    req = urllib.request.Request(url, headers={"User-Agent": UA, "Accept": "text/html"})
    with urllib.request.urlopen(req, timeout=timeout) as r:  # noqa: S310 (fixed https host)
        return r.read().decode("utf-8", "replace")


def _ld_blocks(html: str):
    for m in re.finditer(r'<script type="application/ld\+json">(.*?)</script>', html, re.S):
        try:
            yield json.loads(m.group(1))
        except json.JSONDecodeError:
            continue


def _event_items(html: str):
    """Yield SportsEvent dicts from any JSON-LD ItemList on the page."""
    for block in _ld_blocks(html):
        containers = []
        if isinstance(block, dict):
            containers.append(block)
            graph = block.get("@graph")
            if isinstance(graph, list):
                containers.extend(x for x in graph if isinstance(x, dict))
        for c in containers:
            els = c.get("itemListElement")
            if not isinstance(els, list):
                continue
            for el in els:
                item = el.get("item", el) if isinstance(el, dict) else el
                if isinstance(item, dict) and item.get("@type") == "SportsEvent":
                    yield item


def shorten_name(name: str, overrides: dict | None = None) -> str:
    """Trim sponsor prefixes and 'presented/powered by X' tails for the display."""
    if overrides and name in overrides:
        return overrides[name]
    s = name.strip()
    s = re.split(r"\s+(?:presented|powered)\s+by\s+", s, flags=re.I)[0].strip()
    for sp in sorted(SPONSOR_PREFIXES, key=len, reverse=True):
        if s.lower().startswith(sp.lower() + " "):
            s = s[len(sp):].strip()
            break
    return s


def is_junior(name: str) -> bool:
    return "junior" in name.lower()


def parse_events(html: str, overrides: dict | None = None, exclude_junior: bool = True):
    """Return flagship PPA events as firmware dicts, sorted by start date.

    Each item also carries a private '_slug' used later for seed lookups;
    callers strip underscore-prefixed keys before writing schedule.json.
    """
    out = []
    for it in _event_items(html):
        name = (it.get("name") or "").strip()
        start = (it.get("startDate") or "").strip()[:10]
        end = (it.get("endDate") or "").strip()[:10]
        loc = it.get("location") or {}
        city = (loc.get("address") or "").strip() if isinstance(loc, dict) else ""
        if not name or not DATE_RE.match(start) or not DATE_RE.match(end):
            continue
        if exclude_junior and is_junior(name):
            continue
        if city.upper() in ("TBD", "LOCATION TBD"):
            city = ""
        out.append({
            "tour": "PPA",
            "name": shorten_name(name, overrides),
            "city": city,
            "start": start,
            "end": end,
            "_slug": (it.get("url") or "").rstrip("/").split("/")[-1],
            "_raw_name": name,
        })
    out.sort(key=lambda e: e["start"])
    return out
