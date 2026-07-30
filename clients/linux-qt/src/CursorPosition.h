#pragma once

#include <QPoint>

// Best-effort global pointer position per platform. Wayland does not expose
// the pointer position to unfocused clients, so compositor-specific helpers
// are used where available.
//
//  - X11: QCursor::pos()
//  - Hyprland: `hyprctl cursorpos`
//  - Sway: `swaymsg -t get_seats` (cursor position of the first seat)
//  - otherwise: (0, 0), callers should fall back to anchored placement
//
// Only built with -DTRANSLATOR_WITH_LAYERSHELL=ON.
// UNVERIFIED on this machine (GNOME; hyprctl/swaymsg unavailable).
QPoint globalCursorPosition();
