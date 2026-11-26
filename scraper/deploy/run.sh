#!/usr/bin/env bash
# Wrapper the systemd timer calls. Generates schedule.json and, if it changed,
# commits and pushes it to the jm01 repo via the deploy key (see config.json).
set -euo pipefail
cd "$(dirname "$0")/.."            # -> scraper/
exec python3 generate.py --publish "$@"
