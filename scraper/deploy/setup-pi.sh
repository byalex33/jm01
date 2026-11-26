#!/usr/bin/env bash
# One-shot setup for the JM-01 schedule auto-updater on a Raspberry Pi.
# Safe to re-run. Run it ON THE PI:
#     bash ~/jm01/scraper/deploy/setup-pi.sh
set -euo pipefail

REPO_HTTPS="https://github.com/byalex33/jm01.git"
REPO_SSH="git@github.com:byalex33/jm01.git"
REPO_DIR="${JM01_DIR:-$HOME/jm01}"
KEY="${JM01_KEY:-$HOME/.ssh/jm01_deploy}"
BRANCH="main"

say() { printf '\n== %s ==\n' "$1"; }

say "Checking tools"
command -v git     >/dev/null || { echo "Install git first:  sudo apt install -y git"; exit 1; }
command -v python3 >/dev/null || { echo "Install python3 first:  sudo apt install -y python3"; exit 1; }
echo "git + python3 present"

say "Repository"
if [ ! -d "$REPO_DIR/.git" ]; then
  git clone "$REPO_HTTPS" "$REPO_DIR"
else
  echo "Reusing $REPO_DIR"
fi
cd "$REPO_DIR"

say "Deploy key"
mkdir -p "$(dirname "$KEY")"; chmod 700 "$(dirname "$KEY")"
if [ ! -f "$KEY" ]; then
  ssh-keygen -t ed25519 -f "$KEY" -N "" -C "jm01-pi"
fi
echo "Public deploy key:"
echo "-----------8<------------"
cat "$KEY.pub"
echo "----------->8------------"

say "Git remote + commit identity"
git remote set-url origin "$REPO_SSH"
git config user.name  "JM-01 Pi"
git config user.email "jm01@users.noreply.github.com"
echo "origin -> $REPO_SSH"

say "Checking push access"
if ! GIT_SSH_COMMAND="ssh -i $KEY -o IdentitiesOnly=yes -o StrictHostKeyChecking=accept-new" \
     git ls-remote origin -h >/dev/null 2>&1; then
  echo "The deploy key above is NOT authorized on the repo yet."
  echo "Add it at GitHub -> repo Settings -> Deploy keys -> Add -> tick 'Allow write access'"
  echo "(or paste the key line to Claude), then run this script again."
  exit 1
fi
echo "push access OK"

say "Test: generate schedule (no write)"
python3 scraper/generate.py --no-seeds --dry-run >/dev/null && echo "generator OK"

say "Installing systemd timer"
USER_NOW="$(id -un)"
sed -e "s#/home/pi/jm01#$REPO_DIR#g" \
    -e "s#/home/pi/.ssh/jm01_deploy#$KEY#g" \
    -e "s/^User=pi$/User=$USER_NOW/" \
    scraper/deploy/jm01-schedule.service | sudo tee /etc/systemd/system/jm01-schedule.service >/dev/null
sudo cp scraper/deploy/jm01-schedule.timer /etc/systemd/system/jm01-schedule.timer
sudo systemctl daemon-reload
sudo systemctl enable --now jm01-schedule.timer

say "Done"
systemctl list-timers jm01-schedule --no-pager || true
echo
echo "Run it now and watch the log:"
echo "  sudo systemctl start jm01-schedule.service && journalctl -u jm01-schedule -n 40 --no-pager"
