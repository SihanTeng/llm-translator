#!/usr/bin/env python3
"""Mock DeepSeek (OpenAI-compatible) SSE endpoint for smoke testing.

Responds to POST /chat/completions with a streamed chat completion and logs
each request body to /tmp/translator_mock_requests.jsonl.
"""

import json
import time
from http.server import BaseHTTPRequestHandler, HTTPServer

LOG_PATH = "/tmp/translator_mock_requests.jsonl"
TOKENS = ["Hello", ",", " this", " is", " a", " streamed", " translation", "."]


class Handler(BaseHTTPRequestHandler):
    def do_POST(self):
        length = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(length)
        with open(LOG_PATH, "a") as f:
            f.write(body.decode("utf-8", "replace") + "\n")

        if "chat/completions" not in self.path:
            self.send_error(404)
            return

        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.end_headers()
        for token in TOKENS:
            chunk = {
                "choices": [{"delta": {"content": token}, "index": 0}],
            }
            self.wfile.write(f"data: {json.dumps(chunk)}\n\n".encode())
            self.wfile.flush()
            time.sleep(0.05)
        self.wfile.write(b"data: [DONE]\n\n")
        self.wfile.flush()

    def log_message(self, *args):
        pass


if __name__ == "__main__":
    HTTPServer(("127.0.0.1", 8931), Handler).serve_forever()
