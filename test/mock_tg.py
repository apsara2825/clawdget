#!/usr/bin/env python3
import json, sys
from http.server import BaseHTTPRequestHandler, HTTPServer
calls = {"getUpdates": 0, "sendMessage": []}
class H(BaseHTTPRequestHandler):
    def do_POST(self):
        n = int(self.headers.get("Content-Length", 0))
        body = json.loads(self.rfile.read(n))
        if "getUpdates" in self.path:
            calls["getUpdates"] += 1
            if calls["getUpdates"] == 1:
                result = [{"update_id": 100, "message": {
                    "message_id": 1, "from": {"id": 555, "username": "tester", "first_name": "T"},
                    "chat": {"id": 555, "type": "private"}, "text": "hello mock bot"}}]
            else:
                result = []
            payload = json.dumps({"ok": True, "result": result}).encode()
        elif "sendMessage" in self.path:
            calls["sendMessage"].append(body)
            print("SENT:", body.get("text", ""), flush=True)
            payload = json.dumps({"ok": True}).encode()
        elif "/v1/chat/completions" in self.path:
            payload = b'data: {"choices":[{"index":0,"delta":{"content":"mock reply from LLM"},"finish_reason":null}]}\n\n' \
                      b'data: {"choices":[{"index":0,"delta":{},"finish_reason":"stop"}]}\n\n' \
                      b'data: [DONE]\n\n'
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.send_header("Content-Length", str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)
            print("LLM-SERVED", flush=True)
            return
        else:
            payload = b'{"ok": true}'
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)
    def log_message(self, *a): pass
HTTPServer(("127.0.0.1", 8793), H).serve_forever()
