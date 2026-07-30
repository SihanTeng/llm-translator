#!/usr/bin/env python3
"""Mock LLM endpoint for tests and e2e. Speaks two API styles:

- POST /chat/completions (OpenAI-compatible): streams when the request has
  "stream": true, otherwise returns a single JSON completion (dictionary
  card payload). Requires "Authorization: Bearer $MOCK_EXPECT_KEY".
- POST /v1/messages (Anthropic Messages API): always streams the SSE event
  sequence with distinct tokens. Requires "x-api-key: $MOCK_EXPECT_KEY".

- Returns 401 unless the style's key header carries MOCK_EXPECT_KEY
  (default "test-key").
- Logs each request as one JSON line to MOCK_LOG_PATH: {"path", key
  headers, "body"} (default /tmp/translator_mock_requests.jsonl).
- Port: MOCK_PORT env var or first CLI arg (default 8931).
"""

import json
import os
import sys
import time
from http.server import BaseHTTPRequestHandler, HTTPServer

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else int(os.environ.get("MOCK_PORT", "8931"))
LOG_PATH = os.environ.get("MOCK_LOG_PATH", "/tmp/translator_mock_requests.jsonl")
EXPECT_KEY = os.environ.get("MOCK_EXPECT_KEY", "test-key")

TOKENS = ["Hello", ",", " this", " is", " a", " streamed", " translation", "."]
ANTHROPIC_TOKENS = ["Hola", " desde", " Anthropic", "."]
WORD_CARD = {
    "type": "word",
    "word": "bank",
    "phonetic": "/bæŋk/",
    "pos": "n.",
    "meaning": "the land next to a river",
    "explanation": "Bank here means the side of a river.",
    "example": "We sat on the bank.",
}
PHRASE_REPLY = {"type": "phrase", "translation": "这是一个模拟翻译。"}


def _selected_text(content: str) -> str:
    """Extracts the value after 'Text:' (or the whole content)."""
    first_line = content.split("\n", 1)[0]
    return first_line.removeprefix("Text:").strip()


class Handler(BaseHTTPRequestHandler):
    def _reject_unauthorized(self, payload: bytes):
        self.send_response(401)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def do_POST(self):
        length = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(length)
        try:
            parsed = json.loads(body)
        except json.JSONDecodeError:
            parsed = None
        with open(LOG_PATH, "a") as f:
            f.write(json.dumps({
                "path": self.path,
                "authorization": self.headers.get("Authorization"),
                "x-api-key": self.headers.get("x-api-key"),
                "body": parsed if parsed is not None else body.decode("utf-8", "replace"),
            }) + "\n")

        if "chat/completions" in self.path:
            self._handle_openai(parsed)
        elif self.path.endswith("/v1/messages"):
            self._handle_anthropic(parsed)
        else:
            self.send_error(404)

    # ---- OpenAI-compatible -------------------------------------------------

    def _handle_openai(self, request):
        if self.headers.get("Authorization") != f"Bearer {EXPECT_KEY}":
            payload = json.dumps({"error": {"message": "unauthorized"}}).encode()
            self._reject_unauthorized(payload)
            return
        if request is None:
            self.send_error(400)
            return

        if request.get("stream"):
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.end_headers()
            for token in TOKENS:
                chunk = {"choices": [{"delta": {"content": token}, "index": 0}]}
                self.wfile.write(f"data: {json.dumps(chunk)}\n\n".encode())
                self.wfile.flush()
                time.sleep(0.02)
            self.wfile.write(b"data: [DONE]\n\n")
            self.wfile.flush()
        else:
            # JSON mode: word card for single-word texts, phrase reply else.
            selected = _selected_text(request["messages"][-1]["content"])
            reply = WORD_CARD if " " not in selected else PHRASE_REPLY
            payload = json.dumps({
                "choices": [{
                    "message": {"role": "assistant", "content": json.dumps(reply)},
                    "finish_reason": "stop",
                }],
            }).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)

    # ---- Anthropic Messages API ---------------------------------------------

    def _handle_anthropic(self, request):
        if self.headers.get("x-api-key") != EXPECT_KEY:
            payload = json.dumps({
                "type": "error",
                "error": {"type": "authentication_error", "message": "invalid x-api-key"},
            }).encode()
            self._reject_unauthorized(payload)
            return
        if request is None:
            self.send_error(400)
            return

        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.end_headers()

        def send(event, data):
            self.wfile.write(f"event: {event}\ndata: {json.dumps(data)}\n\n".encode())
            self.wfile.flush()
            time.sleep(0.02)

        send("message_start", {"type": "message_start",
                               "message": {"id": "msg_mock", "role": "assistant", "content": []}})
        send("content_block_start", {"type": "content_block_start", "index": 0,
                                     "content_block": {"type": "text", "text": ""}})
        for token in ANTHROPIC_TOKENS:
            send("content_block_delta", {"type": "content_block_delta", "index": 0,
                                         "delta": {"type": "text_delta", "text": token}})
        send("content_block_stop", {"type": "content_block_stop", "index": 0})
        send("message_delta", {"type": "message_delta", "delta": {"stop_reason": "end_turn"}})
        send("message_stop", {"type": "message_stop"})

    def log_message(self, *args):
        pass


if __name__ == "__main__":
    HTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
