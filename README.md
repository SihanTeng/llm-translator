# translator

Select-text popup translator for modern Linux (Wayland and X11). Highlight
text anywhere and a popup appears with a streaming translation, powered by an
LLM (DeepSeek by default). The app UI is in English; the default translation
direction is English → Simplified Chinese.

Bring your own key — no proxy, no bundled key: your key goes straight from
your machine to the configured API endpoint.

## Requirements

- Qt 6.11 (Widgets, Network, DBus)
- CMake ≥ 3.21, Ninja, a C++20 compiler
- Wayland (GNOME ≥ 45 for the selection bridge) or X11

Fedora:

```sh
sudo dnf install -y cmake ninja-build qt6-qtbase-devel gcc-c++
```

Or point CMake at an existing Qt SDK, e.g.:

```sh
cmake -G Ninja -B build \
    -DCMAKE_MAKE_PROGRAM="$HOME/Qt/Tools/Ninja/ninja" \
    -DCMAKE_PREFIX_PATH="$HOME/Qt/6.11.1/gcc_64"
cmake --build build
./build/translator
```

## Wayland: install the selection bridge

Wayland deliberately does not let background apps read other apps'
selections, so on GNOME the selected text is delivered by a small GNOME Shell
extension that forwards selections to the app over D-Bus
(`org.translator.App.TranslateSelection`):

```sh
sh extension/install.sh
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
  The Qt app stays the backend (settings, API key, streaming request) and
  forwards tokens over D-Bus signals (`TranslationToken`, `TranslationFinished`,
  `TranslationError`); the extension registers itself via `SetShellUiEnabled`.

### KDE Plasma 6 / wlroots (Sway, Hyprland, …)

KWin and wlroots compositors support the `wlr-layer-shell` protocol, which
allows exact popup placement with plain Qt (LayerShellQt). Build with:

```sh
sudo dnf install layer-shell-qt   # Fedora
cmake -G Ninja -B build -DTRANSLATOR_WITH_LAYERSHELL=ON [...]
```

This compiles `src/LayerShellPopup.cpp` (overlay-layer placement with no
keyboard interactivity) and `src/CursorPosition.cpp` (pointer position via
`hyprctl cursorpos` / `swaymsg -t get_seats`; X11 uses `QCursor::pos()`).

**Status: unverified** — LayerShellQt is unavailable on GNOME, so this module
was written but not compiled or tested here. Verify on a KDE/wlroots session
before relying on it. Selection reading on those compositors additionally
needs an `ext/wlr-data-control` reader (not yet implemented — as a
workaround, XWayland selections are still visible via the X11 path).

## Usage

1. On first run the Settings dialog opens — paste your DeepSeek API key.
   Alternatively set `DEEPSEEK_API_KEY` in the environment (it overrides the
   stored key and is never written to disk).
2. Select text with the mouse — a small **Translate** action bar appears near
   the pointer. No API call is made yet.
3. Click **Translate**; the translation streams into the popup. Dismiss the
   bar instead by clicking anywhere else, pressing `Esc`, or waiting a few
   seconds.
4. `Esc` or clicking anywhere else dismisses the popup; `Copy` copies the
   translation.
5. The tray icon (where a tray is available) opens Settings, pauses
   monitoring, or quits.

Settings are stored in `~/.config/translator/translator.conf`.

- **Base URL** defaults to `https://api.deepseek.com` and can point at any
  OpenAI-compatible chat-completions endpoint.
- **Model** defaults to `deepseek-v4-flash`; `deepseek-v4-pro` is available.
- **Translate to** defaults to Simplified Chinese; English and auto
  (Chinese ↔ English) are also available.

## Development

`tests/` contains smoke-test helpers:

- `mock_deepseek_server.py` — local OpenAI-compatible SSE endpoint on
  `127.0.0.1:8931`.
- `sse_client_test.cpp` — exercises `DeepSeekClient` streaming end to end.
- `selection_setter.cpp` — owns the X11 PRIMARY selection with given text.

With the mock server running and the app pointed at it (base URL
`http://127.0.0.1:8931`), the D-Bus path can be triggered manually — it
shows the action bar without making a request:

```sh
gdbus call --session --dest org.translator.App \
    --object-path /org/translator/App \
    --method org.translator.App.TranslateSelection "Hello world" 640 400
```

`action_bar_test.cpp` verifies the click-to-translate flow (click emits
`translateRequested`, bar hides, `dismissed` fires).

## Notes on the DeepSeek integration

- Endpoint: `POST {baseUrl}/chat/completions`, `Authorization: Bearer <key>`,
  OpenAI-compatible, streamed over SSE.
- Thinking mode is explicitly disabled (`"thinking": {"type": "disabled"}`)
  to keep popup latency low. Per the official docs, the legacy model names
  `deepseek-chat` / `deepseek-reasoner` are deprecated in favor of
  `deepseek-v4-flash` / `deepseek-v4-pro`.
