import Gio from 'gi://Gio';
import GLib from 'gi://GLib';

import { Extension } from 'resource:///org/gnome/shell/extensions/extension.js';

const IMPL_BASENAME = 'impl.js';

// Stable hot-reload loader. GNOME Shell caches extension modules per URI and
// never re-imports them (disable/enable does not help on Wayland), so this
// file never changes: the real implementation lives in impl.js and is
// re-imported under a fresh filename whenever it changes on disk (verified:
// the Shell's module cache keys on the full URI, a new filename = new code).
// dev.sh syncs impl.js here on save -> the Shell UI reloads live.
export default class TranslatorSelectionExtension extends Extension {
    enable() {
        this._impl = null;
        this._generation = 0;
        this._reloadId = 0;
        this._loadImpl();

        this._monitor = this.dir
            .get_child(IMPL_BASENAME)
            .monitor_file(Gio.FileMonitorFlags.NONE, null);
        this._monitor.connect('changed', (_m, _f, _o, event) => {
            if (event !== Gio.FileMonitorEvent.CHANGES_DONE_HINT) return;
            // Debounce: editors and cp produce several events per save.
            if (this._reloadId) GLib.Source.remove(this._reloadId);
            this._reloadId = GLib.timeout_add(GLib.PRIORITY_DEFAULT, 300, () => {
                this._reloadId = 0;
                this._loadImpl();
                return GLib.SOURCE_REMOVE;
            });
        });
    }

    disable() {
        this._generation++; // drop any in-flight import
        this._monitor?.cancel();
        this._monitor = null;
        if (this._reloadId) {
            GLib.Source.remove(this._reloadId);
            this._reloadId = 0;
        }
        this._disableImpl();
    }

    _disableImpl() {
        if (!this._impl) return;
        try {
            this._impl.disable();
        } catch (e) {
            logError(e, 'translator: impl disable failed');
        }
        this._impl = null;
    }

    _loadImpl() {
        const generation = ++this._generation;
        let uri;
        try {
            const source = this.dir.get_child(IMPL_BASENAME);
            const [, contents] = source.load_contents(null);
            const mtime = source
                .query_info('time::modified', Gio.FileQueryInfoFlags.NONE, null)
                .get_modification_date_time()
                .to_unix();
            const cacheDir = this.dir.get_child('.hot');
            try {
                cacheDir.make_directory_with_parents(null);
            } catch (_e) {
                /* already exists */
            }
            const target = cacheDir.get_child(`impl.${mtime}.js`);
            target.replace_contents(
                contents,
                null,
                false,
                Gio.FileCreateFlags.REPLACE_DESTINATION,
                null
            );
            uri = target.get_uri();
        } catch (e) {
            logError(e, 'translator: impl prepare failed');
            return;
        }

        import(uri)
            .then((module) => {
                if (generation !== this._generation) return; // superseded or disabled while importing
                try {
                    this._disableImpl();
                    this._impl = new module.TranslatorImpl(this);
                    this._impl.enable();
                } catch (e) {
                    logError(e, 'translator: impl enable failed');
                }
            })
            .catch((e) => logError(e, 'translator: impl load failed'));
    }
}
