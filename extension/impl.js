import Clutter from 'gi://Clutter';
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import Meta from 'gi://Meta';
import St from 'gi://St';
import Atspi from 'gi://Atspi';

import * as Main from 'resource:///org/gnome/shell/ui/main.js';

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

// The real extension implementation. Loaded and hot-reloaded by the stable
// loader in extension.js — edit this file freely, no Shell restart needed.
//
// Renders the translator UI inside GNOME Shell: a small icon bar at the
// pointer when text is selected, and a streaming translation panel once
// clicked. Shell UI is the only UI that can be positioned at the pointer on
// GNOME Wayland. The Qt app stays the backend: it owns settings, the API
// key, and the streaming DeepSeek request, and forwards tokens over D-Bus.
export class TranslatorImpl {
    constructor(extension) {
        this._extension = extension;
    }

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

        // Track the focused widget via AT-SPI so a single-word selection can
        // be explained in the context of its sentence. Apps that expose no
        // text (e.g. Electron without accessibility) simply yield no context.
        this._focusedAccessible = null;
        try {
            if (!Atspi.is_initialized())
                Atspi.init();
            this._focusListener = new Atspi.EventListener(event => {
                this._focusedAccessible = event.source;
            });
            this._focusListener.register('focus');
        } catch (e) {
            this._focusListener = null;
            log(`translator: AT-SPI unavailable, word context disabled: ${e.message}`);
        }
    }

    disable() {
        if (this._focusListener) {
            try {
                this._focusListener.deregister('focus');
            } catch (_e) { /* already gone */ }
            this._focusListener = null;
        }
        this._focusedAccessible = null;
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
        const context = this._isSingleWord(text) ? this._getContextSentence(text) : '';
        const [x, y] = this._pointer;
        this._removeAutoHide();
        this._destroyBar();

        Gio.DBus.session.call(
            DBUS_NAME, DBUS_PATH, DBUS_IFACE, 'TranslateSelectionWithContext',
            new GLib.Variant('(ssii)', [text, context, x, y]),
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
        // Narrow column; height tracks content (capped via CSS).
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

    _isSingleWord(text) {
        return !/\s/.test(text) && text.length < 40;
    }

    // Reads the focused widget's text via AT-SPI and returns the sentence
    // containing the selection (or the caret-adjacent word occurrence).
    // Returns '' when the app exposes no text or nothing matches.
    _getContextSentence(word) {
        const acc = this._focusedAccessible;
        if (!acc)
            return '';
        try {
            const charCount = Math.min(acc.get_character_count(), 8000);
            if (charCount <= 0)
                return '';

            let selStart = -1;
            let selEnd = -1;
            if (acc.get_n_selections() > 0) {
                const [selText, start, end] = acc.get_selection(0);
                if (end > start && selText.trim() === word) {
                    selStart = start;
                    selEnd = end;
                }
            }

            // Fallback: find the word nearest to the caret.
            if (selStart < 0) {
                const full = acc.get_text(0, charCount);
                if (!full)
                    return '';
                const caret = acc.get_caret_offset();
                const index = full.indexOf(word, Math.max(0, caret - word.length - 1));
                if (index < 0)
                    return '';
                selStart = index;
                selEnd = index + word.length;
                return this._extractSentence(full, selStart, selEnd);
            }

            const full = acc.get_text(0, charCount);
            if (!full)
                return '';
            return this._extractSentence(full, selStart, selEnd);
        } catch (_e) {
            return ''; // the accessible died or exposes no text
        }
    }

    _extractSentence(text, start, end) {
        const isBoundary = ch => '.!?\n…。！？'.includes(ch);
        let s = start;
        while (s > 0 && !isBoundary(text[s - 1]))
            s--;
        let e = end;
        while (e < text.length && !isBoundary(text[e]))
            e++;
        if (e < text.length)
            e++; // include the closing punctuation
        const sentence = text.slice(s, e).trim();
        return sentence.length <= 1000 ? sentence : '';
    }

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
