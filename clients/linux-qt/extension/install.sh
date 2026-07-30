#!/bin/sh
# Installs the Translator Selection Bridge GNOME Shell extension.
# On Wayland, GNOME Shell must be restarted (log out / log in) before a
# newly installed extension can be enabled. After that ONE restart, the
# loader in extension.js hot-reloads impl.js on every change — no further
# restarts needed.
set -eu

EXT_DIR="$HOME/.local/share/gnome-shell/extensions/translator@translator"
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

mkdir -p "$EXT_DIR"
cp "$SCRIPT_DIR/metadata.json" "$SCRIPT_DIR/extension.js" "$SCRIPT_DIR/impl.js" "$EXT_DIR/"

gnome-extensions enable translator@translator 2>/dev/null || true

echo "Installed to $EXT_DIR"
echo "If 'gnome-extensions info translator@translator' shows it as disabled"
echo "or unknown, log out and back in, then run: gnome-extensions enable translator@translator"
