#!/usr/bin/env bash
# Dev loop for the translator app:
#  - clients/linux-qt/src or CMake changes  -> rebuild (incl. cargo backend), restart the app
#  - backend/ (Rust) changes                -> same rebuild + restart
#  - clients/linux-qt/extension/ changes    -> sync to the installed GNOME Shell
#    extension dir; the loader there re-imports impl.js live (no relogin)
#
# Usage: ./dev.sh        (Ctrl+C to stop)
# Logs:  /tmp/translator_dev.log (app), /tmp/translator_build.log (build)

set -u

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
CLIENT="$ROOT/clients/linux-qt"
CMAKE="$HOME/Qt/Tools/CMake/bin/cmake"
BUILD_DIR="$ROOT/build"
EXT_DIR="$HOME/.local/share/gnome-shell/extensions/translator@translator"

restart() {
    pkill -x translator 2>/dev/null
    sleep 0.3
    setsid "$BUILD_DIR/clients/linux-qt/translator" > /tmp/translator_dev.log 2>&1 < /dev/null &
    echo "[dev] app restarted (log: /tmp/translator_dev.log)"
}

sync_extension() {
    if [ -d "$EXT_DIR" ]; then
        cp "$CLIENT/extension/extension.js" "$CLIENT/extension/impl.js" \
           "$CLIENT/extension/metadata.json" "$EXT_DIR/" && \
            echo "[dev] extension synced (Shell hot-reloads impl.js)"
    fi
}

echo "[dev] initial build"
if "$CMAKE" --build "$BUILD_DIR" > /tmp/translator_build.log 2>&1; then
    restart
else
    echo "[dev] build FAILED (see /tmp/translator_build.log)"
fi
sync_extension

echo "[dev] watching clients/linux-qt/src, backend/, extension/ — edit and save to reload"
while read -r dir _events _file; do
    sleep 0.5  # debounce: let the editor finish writing
    case "$dir" in
        *extension*)
            sync_extension
            ;;
        *)
            if "$CMAKE" --build "$BUILD_DIR" > /tmp/translator_build.log 2>&1; then
                echo "[dev] build ok"
                restart
            else
                echo "[dev] build FAILED (see /tmp/translator_build.log)"
            fi
            ;;
    esac
done < <(inotifywait -m -r -e modify,create,delete,move \
        --format '%w %e %f' "$CLIENT/src" "$CLIENT/extension" "$ROOT/backend/src" \
        "$ROOT/backend/Cargo.toml" "$ROOT/spec" "$ROOT/CMakeLists.txt" \
        "$CLIENT/CMakeLists.txt" 2>/dev/null)
