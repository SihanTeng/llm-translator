#!/usr/bin/env bash
# End-to-end test: the real app, a real D-Bus session bus, and a mock
# LLM server (OpenAI-compatible + Anthropic shapes). No display required
# (offscreen platform).
#
# Covers:
#   1. phrase translation (SSE streaming, correct request shape)
#   2. single word + context (JSON mode, response_format, Text:/Sentence:)
#   3. auth failure (401 -> TranslationError signal)
#   4. Anthropic provider (x-api-key auth, /v1/messages, event stream)
#   5. GetExcludedApps D-Bus round-trip and SpeakText crash-safety
#   6. history.json written for successful requests only
#
# Usage: clients/linux-qt/tests/e2e.sh [path-to-binary]
#   (default: build/clients/linux-qt/translator)

set -u

CLIENT_ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
REPO_ROOT="$(CDPATH= cd -- "$CLIENT_ROOT/../.." && pwd)"
BINARY="${1:-$REPO_ROOT/build/clients/linux-qt/translator}"
PORT="${MOCK_PORT:-8955}"
WORK="$(mktemp -d)"

cleanup() {
    [ -n "${MOCK_PID:-}" ] && kill "$MOCK_PID" 2>/dev/null
    rm -rf "$WORK"
}
trap cleanup EXIT

[ -x "$BINARY" ] || { echo "e2e: binary not found: $BINARY"; exit 2; }

CONF="$WORK/settings/translator/translator.conf"
mkdir -p "$WORK/settings/translator"
cat > "$CONF" <<EOF
[General]
provider=deepseek
targetLanguage=zh
autoUpdate=false
excludedApps=keepassxc, org.gnome.Terminal

[deepseek]
apiKey=test-key
baseUrl=http://127.0.0.1:$PORT
model=deepseek-v4-flash
EOF

MOCK_PORT="$PORT" MOCK_LOG_PATH="$WORK/requests.jsonl" \
    python3 "$CLIENT_ROOT/tests/mock_llm_server.py" &
MOCK_PID=$!
sleep 0.5

export TRANSLATOR_SETTINGS_DIR="$WORK/settings"
export QT_QPA_PLATFORM=offscreen
export WORK BINARY CONF PORT

timeout 90 dbus-run-session -- bash <<'INNER'
set -u
"$BINARY" & APP=$!
dbus-monitor --session "interface='org.translator.App'" > "$WORK/signals.log" 2>&1 &
MON=$!
sleep 2

gdbus call --session --dest org.translator.App --object-path /org/translator/App \
    --method org.translator.App.SetShellUiEnabled true > /dev/null

# App exclusion list round-trips over D-Bus (the Shell extension reads this).
gdbus call --session --dest org.translator.App --object-path /org/translator/App \
    --method org.translator.App.GetExcludedApps > "$WORK/excluded.txt"

# TTS entry point must not crash the app, even with no audio stack.
gdbus call --session --dest org.translator.App --object-path /org/translator/App \
    --method org.translator.App.SpeakText "hello" > /dev/null

# Case 1: long sentence (streams)
gdbus call --session --dest org.translator.App --object-path /org/translator/App \
    --method org.translator.App.TranslateSelectionWithContext \
    "The quick brown fox jumps over the lazy dog once again" "" 100 100 > /dev/null
sleep 3

# Case 2: short phrase (JSON mode, model decides -> phrase translation)
gdbus call --session --dest org.translator.App --object-path /org/translator/App \
    --method org.translator.App.TranslateSelectionWithContext "hello world" "" 100 100 > /dev/null
sleep 3

# Case 3: single word with context (JSON mode -> dictionary card)
gdbus call --session --dest org.translator.App --object-path /org/translator/App \
    --method org.translator.App.TranslateSelectionWithContext "bank" "We sat on the bank" 100 100 > /dev/null
sleep 3

# Case 4: bad credentials -> error signal. Rewrite the key and restart so
# the app reloads settings.
kill "$APP" 2>/dev/null; wait "$APP" 2>/dev/null
sed -i 's/^apiKey=.*/apiKey=wrong-key/' "$CONF"
"$BINARY" & APP=$!
sleep 2
gdbus call --session --dest org.translator.App --object-path /org/translator/App \
    --method org.translator.App.SetShellUiEnabled true > /dev/null
gdbus call --session --dest org.translator.App --object-path /org/translator/App \
    --method org.translator.App.TranslateSelectionWithContext "hello again" "" 100 100 > /dev/null
sleep 3

# Case 5: Anthropic provider (native Messages API shape against the mock).
kill "$APP" 2>/dev/null; wait "$APP" 2>/dev/null
cat > "$CONF" <<EOF
[General]
provider=anthropic
targetLanguage=zh
autoUpdate=false

[anthropic]
apiKey=test-key
baseUrl=http://127.0.0.1:$PORT
model=claude-haiku-4-5
EOF
"$BINARY" & APP=$!
sleep 2
gdbus call --session --dest org.translator.App --object-path /org/translator/App \
    --method org.translator.App.SetShellUiEnabled true > /dev/null
gdbus call --session --dest org.translator.App --object-path /org/translator/App \
    --method org.translator.App.TranslateSelectionWithContext \
    "This sentence is long enough to stream via the anthropic path" "" 100 100 > /dev/null
sleep 3

kill "$APP" "$MON" 2>/dev/null
INNER
INNER_STATUS=$?
[ "$INNER_STATUS" -eq 0 ] || { echo "e2e: session failed (status $INNER_STATUS)"; exit 1; }

python3 - "$WORK" <<'EOF'
import json
import sys

work = sys.argv[1]
failures = []

with open(f"{work}/requests.jsonl") as f:
    requests = [json.loads(line) for line in f if line.strip()]

# --- OpenAI-compatible request shapes (deepseek provider) --------------------
openai_reqs = [r for r in requests if r["path"].endswith("/chat/completions")]
streamed = next((r for r in openai_reqs if r["body"].get("stream") is True), None)
if not streamed:
    failures.append("no streaming request received (long sentence)")
else:
    content = streamed["body"]["messages"][-1]["content"]
    if "lazy dog" not in content:
        failures.append(f"streaming content wrong: {content!r}")
    if streamed["body"].get("thinking", {}).get("type") != "disabled":
        failures.append("thinking mode not disabled in streaming request (deepseek)")
    if "response_format" in streamed["body"]:
        failures.append("streaming request must not set response_format")
    if streamed.get("authorization") != "Bearer test-key":
        failures.append(f"unexpected auth header: {streamed.get('authorization')!r}")

json_requests = [r for r in openai_reqs if r["body"].get("stream") is False]
if len(json_requests) < 2:
    failures.append(f"expected >=2 JSON-mode requests (short phrase + word), got {len(json_requests)}")
for r in json_requests:
    if r["body"].get("response_format", {}).get("type") != "json_object":
        failures.append("JSON-mode request missing response_format json_object")

word_req = next((r for r in json_requests if "Text: bank" in r["body"]["messages"][-1]["content"]),
                None)
if not word_req:
    failures.append("no JSON-mode request for the word 'bank'")
else:
    content = word_req["body"]["messages"][-1]["content"]
    if "Text: bank" not in content or "Sentence: We sat on the bank" not in content:
        failures.append(f"word request lacks Text:/Sentence: context: {content!r}")
    system = word_req["body"]["messages"][0]["content"]
    if "Simplified Chinese" not in system:
        failures.append(f"system prompt does not name the target language: {system[:120]!r}")

# --- Anthropic request shape ---------------------------------------------------
anthropic = next((r for r in requests if r["path"].endswith("/v1/messages")), None)
if not anthropic:
    failures.append("no Anthropic /v1/messages request received")
else:
    body = anthropic["body"]
    if body.get("stream") is not True:
        failures.append("Anthropic request must stream")
    if "max_tokens" not in body:
        failures.append("Anthropic request missing required max_tokens")
    if "anthropic path" not in body["messages"][-1]["content"]:
        failures.append(f"Anthropic user content wrong: {body['messages'][-1]['content']!r}")
    if body["messages"][-1].get("role") != "user":
        failures.append("Anthropic messages must only carry the user role")
    if "Simplified Chinese" not in body.get("system", ""):
        failures.append("Anthropic system prompt missing target language")
    if anthropic.get("x-api-key") != "test-key":
        failures.append(f"Anthropic auth header wrong: {anthropic.get('x-api-key')!r}")

# --- D-Bus signals -------------------------------------------------------------
with open(f"{work}/signals.log", errors="replace") as f:
    signals = f.read()

for member in ("TranslationToken", "TranslationWordCard", "TranslationFinished"):
    if member not in signals:
        failures.append(f"missing signal {member}")
if "the land next to a river" not in signals:
    failures.append("word card JSON did not reach the bus")
if "这是一个模拟翻译" not in signals:
    failures.append("phrase translation (mock) did not reach the bus")
if '"Hola"' not in signals:
    failures.append("Anthropic stream tokens did not reach the bus")
if "TranslationError" not in signals:
    failures.append("expected TranslationError for the bad-key case")
if "unauthorized" not in signals and "401" not in signals:
    failures.append("error signal did not carry the 401 detail")

# --- app exclusion list ----------------------------------------------------------
with open(f"{work}/excluded.txt") as f:
    excluded = f.read()
if "keepassxc" not in excluded:
    failures.append(f"GetExcludedApps did not return the configured list: {excluded!r}")

# --- history ---------------------------------------------------------------------
try:
    with open(f"{work}/settings/translator/translator/history.json") as f:
        history = json.load(f)
except (OSError, json.JSONDecodeError) as e:
    failures.append(f"history file missing or invalid: {e}")
    history = []
if len(history) < 3:
    failures.append(
        f"expected >=3 history entries (stream, phrase JSON, word card), got {len(history)}")
sources = [e.get("source", "") for e in history]
if not any("lazy dog" in s for s in sources):
    failures.append(f"streamed translation not recorded in history: {sources!r}")
if not any(s == "bank" for s in sources):
    failures.append(f"word card not recorded in history: {sources!r}")
if not any("anthropic path" in s for s in sources):
    failures.append("Anthropic translation not recorded in history")
if any("hello again" in s for s in sources):
    failures.append("failed (401) request must not be recorded in history")

if failures:
    print("E2E FAILURES:")
    for failure in failures:
        print(" -", failure)
    sys.exit(1)
print("e2e: all cases pass (stream, phrase JSON, word card+context, 401, anthropic, exclusions, history)")
EOF
