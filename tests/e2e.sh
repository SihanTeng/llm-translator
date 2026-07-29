#!/usr/bin/env bash
# End-to-end test: the real app, a real D-Bus session bus, and a mock
# DeepSeek server. No display required (offscreen platform).
#
# Covers:
#   1. phrase translation (SSE streaming, correct request shape)
#   2. single word + context (JSON mode, response_format, Word:/Sentence:)
#   3. auth failure (401 -> TranslationError signal)
#
# Usage: tests/e2e.sh [path-to-binary]   (default: build/translator)

set -u

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
BINARY="${1:-$ROOT/build/translator}"
PORT="${MOCK_PORT:-8955}"
WORK="$(mktemp -d)"

cleanup() {
    [ -n "${MOCK_PID:-}" ] && kill "$MOCK_PID" 2>/dev/null
    rm -rf "$WORK"
}
trap cleanup EXIT

[ -x "$BINARY" ] || { echo "e2e: binary not found: $BINARY"; exit 2; }

mkdir -p "$WORK/settings/translator"
cat > "$WORK/settings/translator/translator.conf" <<EOF
[General]
apiKey=test-key
baseUrl=http://127.0.0.1:$PORT
model=deepseek-v4-flash
targetLanguage=zh
monitorEnabled=true
autoUpdate=false
EOF

MOCK_PORT="$PORT" MOCK_LOG_PATH="$WORK/requests.jsonl" \
    python3 "$ROOT/tests/mock_deepseek_server.py" &
MOCK_PID=$!
sleep 0.5

export TRANSLATOR_SETTINGS_DIR="$WORK/settings"
export QT_QPA_PLATFORM=offscreen
export WORK BINARY

timeout 90 dbus-run-session -- bash <<'INNER'
set -u
"$BINARY" & APP=$!
dbus-monitor --session "interface='org.translator.App'" > "$WORK/signals.log" 2>&1 &
MON=$!
sleep 2

gdbus call --session --dest org.translator.App --object-path /org/translator/App \
    --method org.translator.App.SetShellUiEnabled true > /dev/null

# Case 1: phrase translation (streaming)
gdbus call --session --dest org.translator.App --object-path /org/translator/App \
    --method org.translator.App.TranslateSelectionWithContext "hello world" "" 100 100 > /dev/null
sleep 3

# Case 2: single word with context (JSON mode)
gdbus call --session --dest org.translator.App --object-path /org/translator/App \
    --method org.translator.App.TranslateSelectionWithContext "bank" "We sat on the bank" 100 100 > /dev/null
sleep 3

# Case 3: bad credentials -> error signal. Rewrite the key and restart so
# the app reloads settings.
kill "$APP" 2>/dev/null; wait "$APP" 2>/dev/null
sed -i 's/^apiKey=.*/apiKey=wrong-key/' "$TRANSLATOR_SETTINGS_DIR/translator/translator.conf"
"$BINARY" & APP=$!
sleep 2
gdbus call --session --dest org.translator.App --object-path /org/translator/App \
    --method org.translator.App.SetShellUiEnabled true > /dev/null
gdbus call --session --dest org.translator.App --object-path /org/translator/App \
    --method org.translator.App.TranslateSelectionWithContext "hello again" "" 100 100 > /dev/null
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

# --- request shapes ---------------------------------------------------------
phrase = next((r for r in requests if r.get("stream") is True), None)
if not phrase:
    failures.append("no streaming phrase request received")
else:
    if phrase["messages"][-1]["content"] != "hello world":
        failures.append(f"phrase content wrong: {phrase['messages'][-1]['content']!r}")
    if phrase.get("thinking", {}).get("type") != "disabled":
        failures.append("thinking mode not disabled in phrase request")
    if "response_format" in phrase:
        failures.append("phrase request must not set response_format")

word = next((r for r in requests if r.get("stream") is False), None)
if not word:
    failures.append("no JSON-mode word request received")
else:
    if word.get("response_format", {}).get("type") != "json_object":
        failures.append("word request missing response_format json_object")
    content = word["messages"][-1]["content"]
    if "Word: bank" not in content or "Sentence: We sat on the bank" not in content:
        failures.append(f"word request lacks Word:/Sentence: context: {content!r}")

# --- D-Bus signals ----------------------------------------------------------
with open(f"{work}/signals.log", errors="replace") as f:
    signals = f.read()

for member in ("TranslationToken", "TranslationWordCard", "TranslationFinished"):
    if member not in signals:
        failures.append(f"missing signal {member}")
if "TranslationWordCard" in signals and "the land next to a river" not in signals:
    failures.append("word card JSON did not reach the bus")
if "TranslationError" not in signals:
    failures.append("expected TranslationError for the bad-key case")
if "unauthorized" not in signals and "401" not in signals:
    failures.append("error signal did not carry the 401 detail")

if failures:
    print("E2E FAILURES:")
    for failure in failures:
        print(" -", failure)
    sys.exit(1)
print("e2e: all cases pass (phrase stream, word JSON+context, 401 error path)")
EOF
