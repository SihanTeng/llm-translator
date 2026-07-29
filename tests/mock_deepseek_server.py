#!/usr/bin/env python3
"""Mock DeepSeek (OpenAI-compatible) endpoint for tests and e2e.

- POST /chat/completions: streams when the request has "stream": true,
  otherwise returns a single JSON completion (dictionary card payload).
- Returns 401 unless the Authorization header carries MOCK_EXPECT_KEY
  (default "test-key").
- Logs each request body as one JSON line to MOCK_LOG_PATH
  (default /tmp/translator_mock_requests.jsonl).
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
WORD_CARD = {
    "word": "bank",
    "phonetic": "/bæŋk/",
    "pos": "n.",
    "meaning": "the land next to a river",
    "explanation": "Bank here means the side of a river.",
    "example": "We sat on the bank.",
}


class Handler(BaseHTTPRequestHandler):
    def do_POST(self):
        length = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(length)
        with open(LOG_PATH, "a") as f:
            f.write(body.decode("utf-8", "replace") + "\n")

        if "chat/completions" not in self.path:
            self.send_error(404)
            return

        if self.headers.get("Authorization") != f"Bearer {EXPECT_KEY}":
            payload = json.dumps({"error": {"message": "unauthorized"}}).encode()
            self.send_response(401)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)
            return

        try:
            request = json.loads(body)
        except json.JSONDecodeError:
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
            payload = json.dumps({
                "choices": [{
                    "message": {"role": "assistant", "content": json.dumps(WORD_CARD)},
                    "finish_reason": "stop",
                }],
            }).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)

    def log_message(self, *args):
        pass


if __name__ == "__main__":
    HTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
