import GLib from 'gi://GLib';
import Gio from 'gi://Gio';
import Meta from 'gi://Meta';
import St from 'gi://St';

import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';

const DBUS_NAME = 'org.translator.App';
const DBUS_PATH = '/org/translator/App';
const DBUS_IFACE = 'org.translator.App';

const MIN_LENGTH = 2;
const MAX_LENGTH = 4000;

// GNOME Shell runs inside the compositor, so it is allowed to observe the
// PRIMARY selection of native Wayland apps — something a background client
// cannot do on Wayland. Each new selection is forwarded to the translator
// app over the session bus.
export default class TranslatorSelectionExtension extends Extension {
    enable() {
        this._selection = global.display.get_selection();
        this._selection.connectObject('owner-changed',
            this._onOwnerChanged.bind(this), this);
    }

    disable() {
        this._selection?.disconnectObject(this);
        this._selection = null;
    }

    _onOwnerChanged(_selection, selectionType, _owner) {
        if (selectionType !== Meta.SelectionType.SELECTION_PRIMARY)
            return;

        St.Clipboard.get_default().get_text(St.ClipboardType.PRIMARY, (_clipboard, text) => {
            if (!text)
                return;
            text = text.trim();
            if (text.length < MIN_LENGTH || text.length > MAX_LENGTH)
                return;

            const [x, y] = global.get_pointer();
            Gio.DBus.session.call(
                DBUS_NAME,
                DBUS_PATH,
                DBUS_IFACE,
                'TranslateSelection',
                new GLib.Variant('(sii)', [text, x, y]),
                null,
                Gio.DBusCallFlags.NONE,
                2000,
                null,
                null);
        });
    }
}
