#!/usr/bin/env bash
# Runs the dev build of the translator app as a single instance in the
# foreground, with graceful shutdown: when this script is stopped (Ctrl+C,
# `kill`, background-task stop), the app receives SIGTERM and is waited on
# (SIGKILL only as a last resort), so no orphaned translator process or
# dangling tray icon is left behind.
#
# Unlike dev.sh (file watcher that rebuilds and restarts on change), this
# script just runs the current build — it is meant to be launched as a
# single background task.
#
# Usage: ./scripts/dev-app.sh     (build first: cmake --build build)
set -u

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
APP="$ROOT/build/clients/linux-qt/translator"

if [ ! -x "$APP" ]; then
    echo "dev-app: $APP not found — build first (cmake --build build)" >&2
    exit 1
fi

# Single instance: stop any running translator (manual start, dev.sh, or
# D-Bus-activated) before taking over.
pkill -x translator 2>/dev/null
sleep 0.3

"$APP" &
APP_PID=$!
echo "[dev-app] started $APP (pid $APP_PID); stop this script to shut it down"

shutdown() {
    echo "[dev-app] stopping (pid $APP_PID)"
    kill -TERM "$APP_PID" 2>/dev/null
    # Give the app up to ~5s to exit on SIGTERM, then escalate.
    for _ in $(seq 1 25); do
        kill -0 "$APP_PID" 2>/dev/null || break
        sleep 0.2
    done
    if kill -0 "$APP_PID" 2>/dev/null; then
        echo "[dev-app] SIGTERM ignored, sending SIGKILL"
        kill -KILL "$APP_PID" 2>/dev/null
    fi
    wait "$APP_PID" 2>/dev/null
    echo "[dev-app] stopped"
    exit 0
}
trap shutdown INT TERM

# The app exiting on its own ends the script with the app's status.
wait "$APP_PID"
