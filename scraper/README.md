# JM-01 schedule scraper

Keeps `schedule.json` in this repo up to date so the device shows the live
pickleball calendar. The device fetches `schedule.json` from the repo every
hour; this scraper regenerates it and pushes a commit when it changes.

- **Schedule** — the flagship Carvana PPA Tour events (Majors, Cups, Opens,
  Championships). Read from the JSON-LD the PPA events page already serves, so
  it needs no browser, no API key, and only the Python standard library.
- **Seeds** (optional, phase 2) — the #1 seeds for the next event's four pro
  divisions, scraped from that event's "draw reveal" article via Firecrawl.
  Draws are only published a few days before each event and the articles are
  written in prose, so this half is best-effort and can be empty.

## What it produces

```json
{
  "updated": "2026-11-26",
  "events": [
    { "tour": "PPA", "name": "Malibu Showcase", "city": "Malibu, CA",
      "start": "2026-12-14", "end": "2026-12-20" }
  ],
  "seeds": { "event": "Malibu Showcase", "MS": "Staksrud", "WS": "Anna Leigh Waters",
             "MD": "Johns / Tardio", "WD": "Waters / Parenteau" }
}
```

`end` is the last day (inclusive). Names are shortened for the 240px round
screen; see `name_overrides` below for ones that still clip.

## Run it

```bash
python generate.py --fixture fixtures/ppa_events.html --dry-run   # offline, prints only
python generate.py --dry-run                                      # live fetch, prints only
python generate.py                                                # live, writes ../schedule.json
python generate.py --publish                                      # live, commits + pushes if changed
```

`--no-seeds` skips the seeds scrape. The generator **fails closed**: if the
scrape returns no events, dates are invalid, a still-future event has vanished,
or `seeds.event` doesn't match an event, it refuses to write and keeps the old
file. It also only commits when the content actually changed.

## config.json

| Key | Meaning |
|---|---|
| `repo_root` | Repo to write `schedule.json` into. `null` = the repo this folder is in. |
| `branch` | Branch to push to (`main`). |
| `deploy_key` | Path to the SSH private key used for `git push` (see below). `null` = your normal git auth. |
| `min_upcoming` | Refuse to publish if fewer than this many events are still upcoming. |
| `name_overrides` | Map a scraped name to a shorter display name, e.g. `"Pickleball Players Championships": "Players Champs"`. Needed when a name clips on the screen. |
| `seeds.enabled` | Turn the Firecrawl seeds scrape on. |
| `seeds.firecrawl_base` | Firecrawl URL — your Pi's instance (`http://localhost:3002`) or the cloud API. |
| `seeds.firecrawl_api_key` | Firecrawl / LLM key if your instance needs one. |

## Deploy on the Raspberry Pi

1. **Clone the repo** on the Pi: `git clone git@github.com:byalex33/jm01.git`.
2. **Make a write-only deploy key** (can push commits, cannot create releases or
   touch firmware):
   ```bash
   ssh-keygen -t ed25519 -f ~/.ssh/jm01_deploy -N "" -C "jm01-pi"
   cat ~/.ssh/jm01_deploy.pub
   ```
   Add the printed public key at **GitHub → repo → Settings → Deploy keys →
   Add deploy key**, and tick **Allow write access**. Then set
   `"deploy_key": "/home/pi/.ssh/jm01_deploy"` in `config.json`.
3. **Test once**: `python3 generate.py --publish`. Confirm a `schedule.json`
   commit lands on GitHub and nothing else.
4. **Install the daily timer** (edit the paths/user in the unit files first):
   ```bash
   sudo cp deploy/jm01-schedule.service deploy/jm01-schedule.timer /etc/systemd/system/
   sudo systemctl daemon-reload
   sudo systemctl enable --now jm01-schedule.timer
   systemctl list-timers jm01-schedule        # check next run
   journalctl -u jm01-schedule -n 50          # see the last run's log
   ```
   It runs **daily** (not weekly) because draws appear only days before an event.

## Check it without the board

The firmware ships a PC renderer that runs the real parsers and drawing code:

```bash
# one-time: pio run (downloads libs) and the MinGW compiler
pio pkg install -g -t platformio/toolchain-gccmingw32
bash ../test_native/build.sh ../.pio/libdeps/jm01/lvgl ../.pio/libdeps/jm01/ArduinoJson/src ../build_native
export PATH=~/.platformio/packages/toolchain-gccmingw32/bin:$PATH
python generate.py --out /tmp/schedule.json
../build_native/jm01_native.exe ../test_native/data /tmp/schedule.json ../renders HAM
python ../test_native/sheet.py ../renders faces.png     # faces.png is a sheet of every screen
```

## Notes / limits

- **MLP** events aren't included yet — the 2027 MLP schedule isn't published. When
  it is, add a second source the same way; `tour` is already `"PPA"` or `"MLP"`.
- **Seeds gap**: between events (and any time the next draw isn't out) the Top
  Seeds screen shows `SEEDS / LOADING`. Carrying the last seeds forward covers
  the days an event is live; the gap before the next draw can't be filled. A
  small firmware change could show "DRAW NOT OUT YET" instead — ask if you want it.
- The PPA page is server-rendered today. If it ever moves to a different format,
  the validator fails closed and the last good `schedule.json` stays up.
