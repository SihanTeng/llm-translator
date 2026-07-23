#pragma once

#include <QPoint>

class QWidget;

// Popup placement at an arbitrary global position via the wlr-layer-shell
// protocol (KDE Plasma 6/KWin, Sway, Hyprland and other wlroots
// compositors). GNOME does not support layer-shell; there the GNOME Shell
// extension renders the UI instead.
//
// Only built with -DTRANSLATOR_WITH_LAYERSHELL=ON (requires LayerShellQt,
// e.g. 'sudo dnf install layer-shell-qt' on Fedora).
//
// UNVERIFIED: this module cannot be compiled or tested on GNOME (LayerShellQt
// is unavailable). Verify on a KDE Plasma 6 or wlroots session before
// relying on it.
namespace LayerShellPopup {

// True when the current Wayland session advertises zwlr_layer_shell_v1.
bool isSupported();

// Shows the frameless window on the overlay layer at the given global
// position (top-left anchored, margins relative to the output containing
// the point), with no keyboard interactivity so focus is never stolen.
void placeAt(QWidget *window, const QPoint &globalPos);

} // namespace LayerShellPopup
