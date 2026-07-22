#!/usr/bin/env bash
# Dev loop for the translator app: watches src/ and CMakeLists.txt, rebuilds
# on change, and restarts the app when the build succeeds. True hot reload
# is not possible for a compiled C++/Qt Widgets app; this is the practical
# equivalent (restart takes well under a second after a successful build).
#
# Usage: ./dev.sh        (Ctrl+C to stop)
# Logs:  /tmp/translator_dev.log (app), /tmp/translator_build.log (build)

set -u

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
CMAKE="$HOME/Qt/Tools/CMake/bin/cmake"
BUILD_DIR="$ROOT/build"

restart() {
    pkill -x translator 2>/dev/null
    sleep 0.3
    setsid "$BUILD_DIR/translator" > /tmp/translator_dev.log 2>&1 < /dev/null &
    echo "[dev] app restarted (log: /tmp/translator_dev.log)"
}

echo "[dev] initial build"
if "$CMAKE" --build "$BUILD_DIR" > /tmp/translator_build.log 2>&1; then
    restart
else
    echo "[dev] build FAILED (see /tmp/translator_build.log)"
fi

echo "[dev] watching $ROOT/src — edit and save to rebuild+restart"
while inotifywait -q -r -e modify,create,delete,move \
        "$ROOT/src" "$ROOT/CMakeLists.txt" > /dev/null 2>&1; do
    sleep 0.5  # debounce: let the editor finish writing
    if "$CMAKE" --build "$BUILD_DIR" > /tmp/translator_build.log 2>&1; then
        echo "[dev] build ok"
        restart
    else
        echo "[dev] build FAILED (see /tmp/translator_build.log)"
    fi
done
