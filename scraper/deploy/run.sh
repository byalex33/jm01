#!/usr/bin/env bash
# Manual helper to run an update by hand: generate schedule.json and, if it
# changed, commit + push it via the deploy key. The systemd timer calls
# generate.py directly, so this script is only for manual runs.
set -euo pipefail
cd "$(dirname "$0")/.."            # -> scraper/
exec python3 generate.py --publish "$@"
