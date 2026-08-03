# translator

Select-text popup translator for modern Linux (Wayland and X11). Highlight
text anywhere and a popup appears with a streaming translation, powered by
an LLM (DeepSeek by default; OpenAI, Gemini, Anthropic, Grok, OpenRouter,
MiMo and custom OpenAI-compatible endpoints also supported).

Bring your own key — no proxy, no bundled key: your key goes straight from
your machine to the configured provider's API endpoint.

## Requirements

- Qt 6.11 (Widgets, Network, DBus)
- CMake ≥ 3.21, Ninja, a C++20 compiler
- Rust toolchain (cargo) for the backend crate
- Wayland (GNOME ≥ 45 for the selection bridge) or X11

Fedora:

```sh
sudo dnf install -y cmake ninja-build qt6-qtbase-devel gcc-c++ cargo
```

Or point CMake at an existing Qt SDK, e.g.:

```sh
cmake -G Ninja -B build \
    -DCMAKE_MAKE_PROGRAM="$HOME/Qt/Tools/Ninja/ninja" \
    -DCMAKE_PREFIX_PATH="$HOME/Qt/6.11.1/gcc_64"
cmake --build build   # builds the Rust backend (cargo) + the Qt app
./build/clients/linux-qt/translator
```

## Wayland: install the selection bridge

Wayland deliberately does not let background apps read other apps'
selections, so on GNOME the selected text is delivered by a small GNOME Shell
extension that forwards selections to the app over D-Bus
(`org.translator.App.TranslateSelection`):

```sh
sh clients/linux-qt/extension/install.sh
```

On Wayland, GNOME Shell must be restarted (log out / log in) before a newly
installed extension can be enabled; afterwards run
`gnome-extensions enable translator@translator` if it isn't already.

On X11 sessions no extension is needed — the app monitors the PRIMARY
selection directly via `QClipboard` and positions its own popup.

### Wayland notes

- The extension renders the action bar and the translation panel **inside
  GNOME Shell**, positioned exactly at the pointer — the only way to do that
  on GNOME Wayland, which gives clients no control over window placement.
  The app stays the backend (settings, API key, streaming request — the
  Rust crate does the actual provider traffic) and forwards tokens over
  D-Bus signals (`TranslationToken`, `TranslationFinished`,
  `TranslationError`); the extension registers itself via `SetShellUiEnabled`.

### KDE Plasma 6 / wlroots (Sway, Hyprland, …)

KWin and wlroots compositors support the `wlr-layer-shell` protocol, which
allows exact popup placement with plain Qt (LayerShellQt). Build with:

```sh
sudo dnf install layer-shell-qt   # Fedora
cmake -G Ninja -B build -DTRANSLATOR_WITH_LAYERSHELL=ON [...]
```

This compiles `clients/linux-qt/src/LayerShellPopup.cpp` (overlay-layer
placement with no keyboard interactivity) and `CursorPosition.cpp` (pointer
position via `hyprctl cursorpos` / `swaymsg -t get_seats`; X11 uses
`QCursor::pos()`).

**Status: unverified** — LayerShellQt is unavailable on GNOME, so this module
was written but not compiled or tested here. Verify on a KDE/wlroots session
before relying on it. Selection reading on those compositors additionally
needs an `ext/wlr-data-control` reader (not yet implemented — as a
workaround, XWayland selections are still visible via the X11 path).

## Usage

1. On first run the Settings dialog opens — pick a provider and paste your
   API key. Each provider also honors its conventional env var
   (`DEEPSEEK_API_KEY`, `OPENAI_API_KEY`, `GEMINI_API_KEY`,
   `ANTHROPIC_API_KEY`, `XAI_API_KEY`, `OPENROUTER_API_KEY`,
   `MIMO_API_KEY`), which overrides the stored key and is never written to
   disk.
2. Select text with the mouse — a small **Translate** icon bar appears near
   the pointer. No API call is made yet.
3. Click the bar; the translation streams into the popup. Dismiss the
   bar instead by clicking anywhere else, pressing `Esc`, or waiting a few
   seconds.
   - Selecting a **single word** triggers dictionary mode instead: the app
     requests a structured JSON response (`response_format: json_object`)
     and shows a parsed card — word, phonetic, part of speech, meaning,
     explanation, and an example sentence. Explanations are monolingual:
     the word is explained in its own language using simpler terms
     (learner's-dictionary style), while sentences are translated into
     your configured target language.
4. `Esc` or clicking anywhere else dismisses the popup; `Copy` copies the
   translation; the speaker button reads the selected text aloud (via
   speech-dispatcher's `spd-say`).
5. The tray icon (where a tray is available) opens History or Settings, or
   quits.

### Hotkeys and CLI (control plane)

Selection already uses the action bar; for a global shortcut (the Wayland-safe
path) bind an OS custom key to one of:

```sh
# Clipboard (Ctrl+C buffer) — most useful hotkey
translator --translate-clipboard
# or without a second process if the app is already running:
gdbus call --session --dest org.translator.App \
  --object-path /org/translator/App \
  --method org.translator.App.TranslateClipboard

# Arbitrary text
translator --translate 'Hello world'
gdbus call --session --dest org.translator.App \
  --object-path /org/translator/App \
  --method org.translator.App.TranslateText 'Hello world'

translator --show-settings
translator --cancel
```

`translator` is single-instance on the session bus: a second invocation with
`--translate*` / `--show-settings` / `--cancel` forwards to the running app
and exits. If nothing is running, the binary starts the app and runs the
verb. On GNOME: Settings → Keyboard → Custom Shortcuts.

The app does **not** start at login. On GNOME Wayland the Shell extension
stays loaded, but it only D-Bus-activates the backend when you click the
Translate bar (or open Settings from the panel). Quit from the tray anytime;
the next Translate click starts it again. On X11, start the app yourself
(desktop entry, `translator`, or the tray after a manual launch).

Every completed translation is appended to a local history
(`~/.local/share/translator/translator/history.json`, capped at 500 entries,
newest first). The tray menu's **History…** opens a filterable list with
copy and clear actions; failed requests are never recorded.

Settings are stored in `~/.config/translator/translator.conf`.

- **Provider** picks the LLM backend (see the table below). Each provider
  keeps its own key and model; the **Get an API key →** link opens the
  provider's key page.
- **Model** defaults to a cheap/fast model per provider and is editable.
- **Base URL** is only shown for the **Custom** provider (any
  OpenAI-compatible endpoint, e.g. Ollama at `http://localhost:11434/v1`).
- **Translate to** defaults to Simplified Chinese; two dozen mainstream
  languages are available, shown with flag icons — the LLM translates into
  any of them.
- **Exclude apps** is a list of window classes (WM_CLASS); selections in
  those apps never show the Translate bar — useful for password managers.
  Click **Choose…** to pick from your installed apps instead of typing class
  names. GNOME Wayland only (X11 selections carry no source-app
  information).

## Development

`dev.sh` runs the dev loop: rebuild + restart the app on C++/Rust changes,
and hot-reload the GNOME Shell extension (`clients/linux-qt/extension/impl.js`)
on save.

### Tests

Three layers: Rust backend tests (`cargo test`), Qt unit tests wired into
CTest, and an e2e script driving the real app over D-Bus against a mock LLM
server (no display needed):

```sh
cargo test --manifest-path backend/Cargo.toml   # backend: SSE parsing, spec, history
cmake --build build
cd build && ctest --output-on-failure           # 7 Qt suites (UI side)
cd .. && ./clients/linux-qt/tests/e2e.sh        # stream, word card, 401, anthropic
```

- `backend/src/*.rs` tests — both SSE parsers, spec/providers.json and
  prompts validation, lenient JSON extraction, history persistence/cap/clear.
- `clients/linux-qt/tests/tst_*.cpp` — semver compare, dictionary card HTML
  (incl. XSS escaping), selection filter, settings roundtrip (isolated via
  `XDG_CONFIG_HOME`), .desktop app picker parsing, language list + flag
  resources, action bar signals.
- `clients/linux-qt/tests/e2e.sh` — real binary + `dbus-run-session` +
  `mock_llm_server.py` (both API styles: OpenAI-compatible and
  Anthropic `/v1/messages`); asserts request shapes (stream vs JSON mode,
  `Text:`/`Sentence:` context, provider auth headers), D-Bus signal flows,
  the `GetExcludedApps` round-trip, `SpeakText` crash-safety, and that only
  successful translations land in `history.json`.
- `clients/linux-qt/tests/selection_setter.cpp` — manual helper: owns the
  X11 PRIMARY selection with given text for interactive testing.
- `TRANSLATOR_SETTINGS_DIR` env var isolates app settings and data from the
  real `~/.config` / `~/.local/share` (used by the e2e script).

Pre-commit runs `scripts/check.sh` (clang-format, prettier, node --check,
cargo fmt/clippy/test, full build); CI mirrors it and adds the test jobs on
every push.

## Providers

| Provider | Default model | Env var override |
|---|---|---|
| DeepSeek | `deepseek-v4-flash` | `DEEPSEEK_API_KEY` |
| OpenAI | `gpt-5.6-luna` | `OPENAI_API_KEY` |
| Google Gemini | `gemini-3.5-flash-lite` | `GEMINI_API_KEY` |
| Anthropic | `claude-haiku-4-5` | `ANTHROPIC_API_KEY` |
| Grok (xAI) | `grok-4-1-fast-non-reasoning` | `XAI_API_KEY` |
| OpenRouter | `google/gemini-2.5-flash-lite` | `OPENROUTER_API_KEY` |
| Xiaomi MiMo | `mimo-v2.5` | `MIMO_API_KEY` |
| Custom (OpenAI-compatible) | any model / base URL | `DEEPSEEK_API_KEY` (backward compat) |

Architecture: the shared backend is a **Rust crate**; platform shells link
it. No server anywhere — keys go straight from the app to the provider.

- `backend/` — the `translator-backend` crate (sync `ureq` + one worker
  thread per request; no async runtime): provider registry, both API
  styles, streaming/SSE, prompts, word-card JSON extraction, history store.
  Exposed to clients via a small hand-rolled C ABI
  (`backend/include/translator_backend.h`); unit-tested with `cargo test`.
- `clients/linux-qt/` — the Linux desktop client: Qt Widgets shell
  (selection capture, popup, settings, tray) whose `Backend` QObject wraps
  the C ABI with Qt signals, plus the GNOME Shell bridge (`extension/`)
  and Qt/e2e tests. Future clients (macOS, Windows, mobile) get their own
  directory here and link the same crate.
- `spec/` — the cross-platform contract as data: `providers.json` and
  `prompts.json` are the single source of truth — the Rust backend embeds
  them at compile time, the Qt settings UI parses the same files, and a
  non-Rust client (e.g. a Chrome extension) can implement against them
  directly. See `spec/integration.md`.

The backend implements two API styles: `openai-compatible`
(`chat/completions`, Bearer auth, used by every provider except Anthropic)
and `anthropic` (`/v1/messages`, `x-api-key` + `anthropic-version`,
top-level `system`, `content_block_delta` stream parsing). Adding a
provider is one `spec/providers.json` entry; a new API shape is a new
style module in the crate.

Dictionary (JSON) mode uses `response_format: json_object` where it's
documented (DeepSeek, OpenAI, Grok, OpenRouter, MiMo) and prompt-only with
lenient JSON extraction elsewhere (Gemini, Anthropic). On DeepSeek,
thinking mode stays disabled (`"thinking": {"type": "disabled"}`) to keep
popup latency low.
