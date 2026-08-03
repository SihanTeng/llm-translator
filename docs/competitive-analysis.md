# Competitive analysis — selection-translation apps

Research date: 2026-07-29. Method: web research (official sites, repos, docs,
review threads) on mature open- and closed-source apps comparable to this
project, followed by a feature/UX comparison against our codebase.

> **Status (2026-08-03):** several "table stakes we lack" items below have
> since shipped — history (#4), TTS (#2 popup buttons), the app exclusion
> list (#5), multi-provider support, and the D-Bus/CLI control plane
> (`TranslateText`, `TranslateClipboard`, `CancelTranslation`,
> `translator --translate*` single-instance forward) plus generation-based
> session/op isolation in the Rust backend. Remaining open: multi-action
> bar, popup pin/retry/lang-toggle, OCR, replace-in-place. Treat the gap
> list as a snapshot, not a backlog.

## TL;DR

Every serious competitor has a hotkey trigger, history, TTS, popup
pin/retry/language-toggle, an app exclusion list, and OCR. We have none of
those. Our Wayland architecture (GNOME Shell extension bridge with exact
pointer positioning) is genuinely better than what Pot, Crow Translate, and
GoldenDict manage on Wayland — double down on it. Highest-leverage additions:
multi-action bar with LLM prompt presets, history/wordbook, a D-Bus hotkey
API, TTS, and an app exclusion list.

## Compared apps

| App | Platform | Model | Popularity | Signature strengths |
|---|---|---|---|---|
| Pot | Win/mac/Linux (Tauri) | OSS, BYOK | 19k stars, ~330k dl/release | 20+ engines in parallel cards, OCR, plugin system, HTTP API |
| Easydict | macOS | OSS, BYOK | 14k stars | Auto-icon on selection, 20+ services, smart word/sentence routing |
| Saladict | Browser ext | OSS (MIT) | 13k stars | Dictionary-card UX: profiles, context capture, Anki export |
| Bob | macOS | Closed, ~¥58 one-time | 9.7k stars | Polish: silent OCR-to-clipboard, 30+ services, JS plugin SDK |
| STranslate | Windows | OSS (MIT) | 7.6k stars | Multi-engine compare, bundled offline OCR, wordbook, back-translate |
| GoldenDict(-ng) | Linux/Win | OSS | ~9.5k combined | Offline dictionaries, click-to-confirm scan flag |
| TTime | Win/mac | Source-available freemium | 3.3k stars | Floating ball, replace-in-place, fold mode |
| Crow Translate | Linux/Win (KDE) | OSS | 2k stars, ~58k Flathub | CLI + D-Bus API, 20 MB RAM, OCR magnifier |
| PopClip | macOS | Closed $12–32 | 98% of 1.3k Setapp ratings | The selection action bar itself; 218 extensions; paste-result |
| DeepL desktop | Win/mac | Freemium | 100k+ business customers | Ctrl+C+C gesture; "Insert" replaces source with translation |
| Immersive Translate | Browser | Closed freemium | ~3M Chrome users | Inline-below-paragraph rendering, hover-Ctrl, 3×space input translation |

## Detailed findings

### Pot (pot-desktop) — https://github.com/pot-app/pot-desktop

- Tauri (Rust) + React; GPL-3.0; Win/macOS/Linux incl. Wayland; SQLite history.
- Engines: OpenAI, Gemini, Ollama (offline), DeepL, Google, Bing, Youdao,
  Cambridge, many Chinese engines; `.potext` plugins for translate/OCR/TTS/
  collection (Anki, Eudic). System OCR (Windows.Media, Apple Vision,
  Tesseract), screenshot translate, silent-to-clipboard mode, per-mode global
  hotkeys, second-target-language auto-switch, incremental append.
- UX: hotkey on selection (simulated copy) → floating window at cursor with
  parallel per-engine cards; per-card engine swap, TTS, copy, back-translate,
  retry, Anki buttons. No native hover icon — SnipDo/PopClip/Starry bridges
  call its localhost HTTP API (port 60828), which is also their Wayland
  workaround.
- Weaknesses: Wayland second-class (no in-app hotkeys, screenshot broken on
  Hyprland); unsigned macOS builds; WebKit2GTK+Nvidia crashes; 447 open
  issues; no release since May 2025.

### Easydict — https://github.com/tisfeng/Easydict

- macOS, GPL-3.0; 14k stars, ~579k downloads. Capture fallback chain:
  Accessibility → AppleScript → simulated ⌘C.
- 20+ services incl. OpenAI-compatible custom endpoints, Gemini, DeepSeek,
  Ollama, Claude; offline system OCR; silent screenshot OCR; MDict import;
  replace-original-text; `easydict://query?text=` URL scheme.
- UX: auto query icon appears at the caret after mouse selection (hover or
  click), plus ⌥D hotkey and ⌥S screenshot. Popup: copy, TTS, retry (⌘R),
  toggle target language (⌘T), pin (⌘P), per-service expand/collapse.
  Smart routing: word → dictionary services, sentence → translators.
- Weaknesses: no history/wordbook (top user requests), accessibility
  permission friction, web-API rate limits.

### Saladict 沙拉查词 — https://github.com/crimx/ext-saladict

- Browser extension, MIT; 13.2k stars. Aggregated hand-styled dictionaries +
  MT; human-voice pronunciation with AB repeat; notebook with **source-context
  capture** (saves the sentence around each word); WebDAV sync; one-click
  Anki via AnkiConnect; profiles switching dictionary combos by text
  language/length; triple-Ctrl standalone panel reading clipboard.
- UX: mixable triggers (icon, instant, double-click, hover+modifier);
  pinnable, draggable, editable popup panel.
- Weaknesses: scrapes web UIs (brittle), no LLM, no OCR, steep settings.

### Bob — https://bobtranslate.com

- macOS, closed shareware (free tier with daily caps; Pro ~¥58 one-time).
- 30+ services incl. OpenAI; screenshot/OCR with continuous-append, QR
  recognition, paragraph restoration, **silent OCR straight to clipboard**;
  history + favorites; replace-original-text; camelCase/snake_case splitting;
  JavaScript `.bobplugin` SDK (big community moat).
- UX: ⌥D on selection → floating window above the current app. PopClip
  extension provides the hover-bar trigger.

### STranslate — https://github.com/STranslate/STranslate

- Windows WPF, MIT; 7.6k stars. ~20 services via `.spkg` plugins with a
  market; concurrent multi-engine results; bundled offline WeChat OCR;
  **in-place image translation** (译文 rendered onto the screenshot);
  Edge TTS; vocabulary book; history; back-translation; custom-prompt AI
  actions (polish/summarize/explain); external-call API.
- UX: Alt+D hotkey; mouse-selection watch mode; **hold-key incremental
  selection** (swipe multiple fragments, release to translate all);
  Alt+F replace-translation; silent modes to clipboard.

### TTime — https://github.com/InkTimeRecord/TTime

- Win/mac (Linux promised, never shipped), Electron, Apache-2.0 + commercial
  restrictions; freemium. LLM sources (OpenAI, Gemini, Zhipu, Ollama), 9 OCR
  backends, history, cloud sync (paid). Floating-ball trigger,
  replace-translation, fold translation, clipboard-watch, silent OCR.
- Stalled: last commit Dec 2024; unsigned binaries.

### Crow Translate — https://github.com/crow-translate/crow-translate (now KDE)

- C++/Qt, GPL-3.0; Linux/Windows. Engines via Mozhi (Google, DeepL,
  LibreTranslate, …) — **no LLM support**. Tesseract OCR area translate with
  magnifier; TTS; **CLI with JSON output + D-Bus API** (users bind their own
  shortcuts — the Wayland answer); three result modes: popup / main window /
  tray notification. No history, no favorites, no plugins.

### GoldenDict / goldendict-ng

- Offline dictionary formats (StarDict, MDict, DSL, ZIM…) + online Wikipedia/
  Forvo + external-program "dictionaries"; scan popup shows a small flag icon
  you click to look up (deliberate two-step, avoids popup spam); Ctrl+C+C
  clipboard lookup; Anki integration in the ng fork.
- Scan popup is X11-dependent and broadly broken on Wayland.

### PopClip — https://www.popclip.app

- macOS, closed, $12 (2y updates) / $32 lifetime. Not a translator: a
  contextual selection **action bar** — built-in copy/search/spell/dictionary,
  218 extensions (translate, dictionaries, LLM snippets, text transforms).
  Actions can `paste-result` = replace the selection in the source app.
  Context-aware (URL/email/phone get different actions), keyboard-navigable,
  per-app exclusion list, global hotkey trigger.
- This is the originator of our action-bar UX — and its real insight is that
  the bar should offer *several* actions, not one.

### DeepL desktop — https://www.deepl.com/en/windows-app

- Closed freemium. Own MT only (no engine choice). **Ctrl+C+C**: select text,
  double-tap copy → floating translation window with copy, TTS, alternatives,
  language switch, and **"Insert"** (pastes translation back over the
  original). Privacy detail: single Ctrl+C sends nothing; only the second C
  ships text. Document translation, glossary, DeepL Write. No Linux app.

### Immersive Translate — https://immersivetranslate.com

- Closed freemium (was OSS until Nov 2023 acquisition); ~3M Chrome users.
  20+ engines incl. custom OpenAI-compatible; bilingual webpage/PDF/EPUB/
  subtitle translation; hover a paragraph + Ctrl → translation injected
  **inline below the paragraph** (zero context loss); input-box translation:
  type, hit space 3×, text replaced in place; selection popup with
  pronunciation. Aug 2025 trust crisis (third-party API ban attempt,
  page-snapshot privacy leak).

## Table-stakes features we lack

1. **Hotkey path** — everyone has one (⌥D, Alt+D, Ctrl+Alt+E). Pot (HTTP API)
   and Crow (D-Bus API) solve Wayland by exposing an API users bind to OS
   shortcuts. We already have the D-Bus interface; we just don't expose
   `TranslateText`/`TranslateClipboard` on it.
2. **History** — Pot (SQLite), STranslate, TTime, Bob, Saladict all persist.
   Easydict's top user requests are history/wordbook. Saladict saves the
   context sentence with each word — we already build `Text:`/`Sentence:`
   context for the LLM prompt.
3. **TTS / pronunciation** — universal; our dictionary card shows a phonetic
   string but can't play it. Qt has QtTextToSpeech (speech-dispatcher on
   Linux).
4. **Popup pin / retry / target-language toggle** — Easydict ⌘P/⌘R/⌘T,
   STranslate pin-topmost. Our popup is read-and-dismiss only.
5. **App exclusion list** — PopClip per-app blacklist; misfires near password
   managers/terminals are the #1 annoyance. Our GNOME extension knows the
   focused window's wm-class — cheap for us.
6. **OCR / screenshot translate** — universal except us and GoldenDict;
   silent-OCR-to-clipboard specifically beloved.
7. **Replace-in-place** — DeepL Insert, PopClip paste-result, STranslate
   Alt+F, Immersive 3×space. Most-loved pro feature. Hard on Wayland,
   feasible on X11 via XTest.

## Our competitive position

- **Wayland story is better than theirs.** Pot needs curl-to-localhost
  workarounds; GoldenDict's scan popup is broken on Wayland; Crow has no
  global shortcuts. Our GNOME Shell extension bridge (exact pointer
  positioning, in-Shell UI) is what the field hasn't figured out.
- **LLM-only is a fine niche.** Crow/GoldenDict/Saladict have no LLM support;
  the rest bolt it on as one of 20 engines whose web APIs break constantly
  (every competitor's issue tracker is full of "Google engine broke again").
  Streaming single-purpose LLM UX is our identity.

## Recommendations

### Tier 1 — cheap, high leverage, fits current architecture

1. **Multi-action action bar** (PopClip's real insight): Translate / Explain /
   Polish / Summarize prompt presets, user-editable in settings. For an
   LLM-backed app this is nearly free — it's a prompt string.
2. **Popup buttons**: Pin, Retry, zh⇄en toggle, TTS (QtTextToSpeech).
3. **D-Bus hotkey API + CLI verb**: `TranslateText(s)`, `TranslateClipboard()`
   on the existing interface; document binding GNOME custom shortcuts;
   `translator --translate-clipboard` (Crow's CLI pattern).
4. **History + wordbook**: persist every translation with its context
   sentence; heart-button on dictionary cards → wordbook; later AnkiConnect
   export (localhost HTTP JSON).
5. **App exclusion list** (extension sends wm-class; app filters).
6. **Ollama preset** in the base-URL docs (`http://localhost:11434/v1`).

### Tier 2 — bigger, differentiating

7. **Replace-in-place translation** (X11 first via XTest; Wayland
   extension-dependent).
8. **Screenshot OCR translate** via GNOME Screenshot portal + Tesseract, with
   a silent-OCR-to-clipboard variant.
9. **Input-translate window** (hotkey → type → stream) for non-selectable
   text (Bob ⌥A, Pot input mode).
10. **Hover-to-translate** toggle on the action bar (Easydict's edge).

### Tier 3 — strategic

11. **Stabilize KDE/wlroots path** (layer-shell unverified; add
    ext-data-control selection reading). KDE has no good option today.
12. **Parallel model comparison** (flash vs pro side-by-side) instead of full
    multi-engine aggregation.
13. **Double-C gesture** (DeepL): clipboard-watch; second rapid Ctrl+C
    triggers, single C sends nothing.

## What NOT to steal

- 20-engine web-API aggregation — constant breakage, rate-limit/proxy hell;
  why Pot has 447 open issues. BYOK-LLM simplicity is the feature.
- Plugin systems (`.potext`/`.bobplugin`) — premature; prompt presets capture
  80% of the value.
- Webpage/PDF/subtitle translation — different product category.
- Tray-notification result mode, QR recognition, manga OCR — long tail.
