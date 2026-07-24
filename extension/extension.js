import Clutter from 'gi://Clutter';
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import Meta from 'gi://Meta';
import St from 'gi://St';

import * as Main from 'resource:///org/gnome/shell/ui/main.js';
import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';

const DBUS_NAME = 'org.translator.App';
const DBUS_PATH = '/org/translator/App';
const DBUS_IFACE = 'org.translator.App';

const MIN_LENGTH = 2;
const MAX_LENGTH = 4000;
const AUTO_HIDE_SECONDS = 8;

const BAR_STYLE = 'background-color: #2d7dff; color: white; border-radius: 10px; ' +
    'padding: 8px 10px; box-shadow: 0 2px 8px rgba(0,0,0,0.4);';
const PANEL_STYLE = 'background-color: #202124; color: #e8eaed; font-size: 15px; ' +
    'border-radius: 12px; padding: 14px 16px; border: 1px solid #3c4043; ' +
    'box-shadow: 0 4px 16px rgba(0,0,0,0.5); max-height: 1680px;';
const SOURCE_STYLE = 'color: #9aa0a6; font-size: 11px;';
const HEADER_BUTTON_STYLE = 'color: #9aa0a6; background-color: transparent; ' +
    'border-radius: 6px; padding: 3px 10px;';

// Renders the translator UI inside GNOME Shell: a small "Translate" bar at
// the pointer when text is selected, and a streaming translation panel once
// clicked. Shell UI is the only UI that can be positioned at the pointer on
// GNOME Wayland. The Qt app stays the backend: it owns settings, the API
// key, and the streaming DeepSeek request, and forwards tokens over D-Bus.
export default class TranslatorSelectionExtension extends Extension {
    enable() {
        this._bar = null;
        this._panel = null;
        this._translationLabel = null;
        this._translation = '';
        this._pendingText = '';
        this._pointer = [0, 0];
        this._autoHideId = 0;

        this._selection = global.display.get_selection();
        this._selection.connectObject('owner-changed',
            this._onOwnerChanged.bind(this), this);

        // The app renders no Qt UI for D-Bus translations while we exist.
        // Re-register whenever the app (re)appears on the bus.
        this._nameWatchId = Gio.bus_watch_name(
            Gio.BusType.SESSION, DBUS_NAME, Gio.BusNameWatcherFlags.NONE,
            () => this._setShellUi(true), null);
        this._setShellUi(true);

        this._signalSubId = Gio.DBus.session.signal_subscribe(
            null, DBUS_IFACE, null, DBUS_PATH, null,
            Gio.DBusSignalFlags.NONE, this._onAppSignal.bind(this));
    }

    disable() {
        if (this._signalSubId) {
            Gio.DBus.session.signal_unsubscribe(this._signalSubId);
            this._signalSubId = 0;
        }
        if (this._nameWatchId) {
            Gio.bus_unwatch_name(this._nameWatchId);
            this._nameWatchId = 0;
        }
        this._setShellUi(false);
        this._selection?.disconnectObject(this);
        this._selection = null;
        this._removeAutoHide();
        this._destroyBar();
        this._destroyPanel();
    }

    // ---- selection -> bar -------------------------------------------------

    _onOwnerChanged(_selection, selectionType, _owner) {
        if (selectionType !== Meta.SelectionType.SELECTION_PRIMARY)
            return;

        St.Clipboard.get_default().get_text(St.ClipboardType.PRIMARY, (_cb, text) => {
            if (!text)
                return;
            text = text.trim();
            if (text.length < MIN_LENGTH || text.length > MAX_LENGTH)
                return;

            this._pendingText = text;
            this._pointer = global.get_pointer();
            this._showBar();
        });
    }

    _showBar() {
        this._destroyBar();
        this._destroyPanel();

        this._bar = new St.Button({
            style: BAR_STYLE,
            reactive: true,
            can_focus: false,
        });
        this._bar.set_child(new St.Icon({
            icon_name: 'accessories-dictionary',
            icon_size: 18,
        }));
        Main.layoutManager.uiGroup.add_child(this._bar);
        this._placeNear(this._bar, ...this._pointer);
        this._bar.connect('clicked', () => this._onBarClicked());

        this._removeAutoHide();
        this._autoHideId = GLib.timeout_add(GLib.PRIORITY_DEFAULT,
            AUTO_HIDE_SECONDS * 1000, () => {
                this._destroyBar();
                this._autoHideId = 0;
                return GLib.SOURCE_REMOVE;
            });
    }

    _onBarClicked() {
        const text = this._pendingText;
        const [x, y] = this._pointer;
        this._removeAutoHide();
        this._destroyBar();

        Gio.DBus.session.call(
            DBUS_NAME, DBUS_PATH, DBUS_IFACE, 'TranslateSelection',
            new GLib.Variant('(sii)', [text, x, y]),
            null, Gio.DBusCallFlags.NONE, 2000, null, null);

        this._showPanel(text, x, y);
    }

    // ---- translation panel ------------------------------------------------

    _showPanel(sourceText, x, y) {
        this._destroyPanel();
        this._translation = '';

        this._panel = new St.BoxLayout({
            vertical: true,
            style: PANEL_STYLE,
            reactive: true,
        });
        // Narrow column (half the previous width); height tracks content.
        const monitor = Main.layoutManager.currentMonitor;
        this._panel.set_width(Math.min(280, Math.round(monitor.width * 0.15)));

        const header = new St.BoxLayout({vertical: false});

        const copyButton = new St.Button({label: 'Copy', style: HEADER_BUTTON_STYLE, reactive: true});
        copyButton.connect('clicked', () => {
            St.Clipboard.get_default().set_text(St.ClipboardType.CLIPBOARD, this._translation);
            copyButton.set_label('Copied');
            GLib.timeout_add(GLib.PRIORITY_DEFAULT, 1200, () => {
                copyButton.set_label('Copy');
                return GLib.SOURCE_REMOVE;
            });
        });
        header.add_child(copyButton);

        const spacer = new St.Widget({x_expand: true});
        header.add_child(spacer);

        const closeButton = new St.Button({label: '×', style: HEADER_BUTTON_STYLE, reactive: true});
        closeButton.connect('clicked', () => this._destroyPanel());
        header.add_child(closeButton);

        this._panel.add_child(header);

        let source = sourceText.replace(/\s+/g, ' ');
        if (source.length > 140)
            source = `${source.slice(0, 140)}…`;
        const sourceLabel = new St.Label({text: source, style: SOURCE_STYLE});
        sourceLabel.clutter_text.set_line_wrap(true);
        this._panel.add_child(sourceLabel);

        this._translationLabel = new St.Label({text: 'Translating…'});
        this._translationLabel.clutter_text.set_line_wrap(true);
        this._panel.add_child(this._translationLabel);

        const footer = new St.BoxLayout({vertical: false});
        const settingsButton = new St.Button({label: 'Settings', style: HEADER_BUTTON_STYLE, reactive: true});
        settingsButton.connect('clicked', () => {
            Gio.DBus.session.call(
                DBUS_NAME, DBUS_PATH, DBUS_IFACE, 'ShowSettings',
                null, null, Gio.DBusCallFlags.NONE, 1000, null, null);
        });
        footer.add_child(settingsButton);
        this._panel.add_child(footer);

        Main.layoutManager.uiGroup.add_child(this._panel);
        this._placeNear(this._panel, x, y);
    }

    _onAppSignal(_conn, _sender, _path, _iface, signal, params) {
        if (!this._panel)
            return;
        const [payload] = params.deep_unpack();
        if (signal === 'TranslationToken') {
            if (!this._translation)
                this._translationLabel.set_text('');
            this._translation += payload;
            this._translationLabel.set_text(this._translation);
        } else if (signal === 'TranslationError') {
            this._translationLabel.set_text(payload);
        }
        // TranslationFinished: nothing to do, tokens are all shown.
    }

    // ---- helpers ----------------------------------------------------------

    _placeNear(actor, x, y) {
        const monitor = Main.layoutManager.currentMonitor;
        const px = Math.min(Math.max(x + 12, monitor.x), monitor.x + monitor.width - actor.width);
        const py = Math.min(Math.max(y + 16, monitor.y), monitor.y + monitor.height - actor.height);
        actor.set_position(px, py);
    }

    _setShellUi(enabled) {
        Gio.DBus.session.call(
            DBUS_NAME, DBUS_PATH, DBUS_IFACE, 'SetShellUiEnabled',
            new GLib.Variant('(b)', [enabled]),
            null, Gio.DBusCallFlags.NONE, 1000, null, null);
    }

    _removeAutoHide() {
        if (this._autoHideId) {
            GLib.Source.remove(this._autoHideId);
            this._autoHideId = 0;
        }
    }

    _destroyBar() {
        this._bar?.destroy();
        this._bar = null;
    }

    _destroyPanel() {
        this._panel?.destroy();
        this._panel = null;
        this._translationLabel = null;
    }
}
