#!/usr/bin/env python3
"""Mock OpenAI-compatible SSE server for e2e testing clawdget.

Scripted flow driven by the last tool result:
  no tool msgs                      -> list_dir tool_call
  last tool == dir listing          -> write_file tool_call
  last tool startswith "wrote"      -> exec tool_call (echo)
  last tool contains "hello-pico"   -> final answer
Prints each request's messages to stderr for inspection.
"""
import json
import sys
from http.server import BaseHTTPRequestHandler, HTTPServer

def sse(evs):
    out = b""
    for e in evs:
        out += b"data: " + json.dumps(e).encode() + b"\n\n"
    out += b"data: [DONE]\n\n"
    return out

def call(name, args, cid="call_1"):
    return [{"choices": [{"index": 0, "delta": {"tool_calls": [
        {"index": 0, "id": cid, "type": "function",
         "function": {"name": name, "arguments": ""}}]}, "finish_reason": None}]},
        {"choices": [{"index": 0, "delta": {"tool_calls": [
            {"index": 0, "function": {"arguments": args}}]}, "finish_reason": None}]},
        {"choices": [{"index": 0, "delta": {}, "finish_reason": "tool_calls"}]}]

def text(t):
    return [{"choices": [{"index": 0, "delta": {"content": t}, "finish_reason": None}]},
            {"choices": [{"index": 0, "delta": {}, "finish_reason": "stop"}]}]

class Handler(BaseHTTPRequestHandler):
    def do_POST(self):
        n = int(self.headers.get("Content-Length", 0))
        body = json.loads(self.rfile.read(n))
        msgs = body.get("messages", [])
        tool_msgs = [m for m in msgs if m.get("role") == "tool"]
        print(f"--- request ({len(msgs)} msgs) ---", file=sys.stderr)
        for m in msgs:
            print(f"  {m.get('role')}: {str(m.get('content',''))[:100]!r}", file=sys.stderr)

        if not tool_msgs:
            user_text = " ".join(m.get("content", "") for m in msgs
                                 if m.get("role") == "user")
            if "sysinfo" in user_text:
                payload = sse(call("sysinfo", "{}"))
            elif "fetch" in user_text:
                host = "172.17.144.61:8792"
                payload = sse(call("http_fetch", "{\"url\": \"http://%s/status\"}" % host))
            else:
                payload = sse(call("list_dir", "{\"path\": \".\"}"))
        else:
            last = tool_msgs[-1].get("content", "")
            if last.startswith("wrote"):
                payload = sse(call("exec", "{\"command\": \"echo hello-pico\"}"))
            elif "hello-pico" in last:
                payload = sse(text("done: file written and exec echoed."))
            else:
                payload = sse(call("write_file",
                                   "{\"path\": \"note.txt\", \"content\": \"hello from e2e\"}"))
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def log_message(self, *a):
        pass

if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8791
    HTTPServer(("0.0.0.0", port), Handler).serve_forever()
