#!/usr/bin/env python3
"""
Local web server for Agent Chat.
Serves on 127.0.0.1 only.
Standard library only.
"""

import argparse
import http.server
import json
import os
import sys
import urllib.parse

# Import functions from agent_chat
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import agent_chat

class AgentChatHandler(http.server.BaseHTTPRequestHandler):
    storage_dir = None
    html_path = None

    def log_message(self, format, *args):
        # Concise logging
        sys.stderr.write(f"[{self.log_date_time_string()}] {args[0]} {args[1]} -> {args[2]}\n")

    def send_json(self, status_code, data):
        payload = json.dumps(data, ensure_ascii=False).encode("utf-8")
        self.send_response(status_code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(payload)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.end_headers()
        self.wfile.write(payload)

    def do_OPTIONS(self):
        self.send_response(204)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.end_headers()

    def do_GET(self):
        parsed = urllib.parse.urlparse(self.path)
        path = parsed.path
        query = urllib.parse.parse_qs(parsed.query)

        if path in ("/", "/index.html"):
            if not os.path.exists(self.html_path):
                self.send_error(404, f"HTML file not found: {self.html_path}")
                return
            with open(self.html_path, "rb") as f:
                content = f.read()
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(content)))
            self.end_headers()
            self.wfile.write(content)
            return

        if path == "/api/messages":
            chat_file = os.path.join(self.storage_dir, "chat.jsonl")
            messages = agent_chat.read_all_messages(chat_file)
            since_val = query.get("since", [None])[0]
            if since_val is not None:
                try:
                    since_id = int(since_val)
                    messages = [m for m in messages if m.get("id", 0) > since_id]
                except ValueError:
                    pass
            self.send_json(200, {"messages": messages})
            return

        if path == "/api/presence":
            pres = agent_chat.get_presence_dict(self.storage_dir)
            self.send_json(200, pres)
            return

        self.send_error(404, "Endpoint not found")

    def do_POST(self):
        parsed = urllib.parse.urlparse(self.path)
        path = parsed.path

        if path == "/api/post":
            content_len = int(self.headers.get("Content-Length", 0))
            if content_len == 0:
                self.send_json(400, {"error": "Empty body"})
                return
            body = self.rfile.read(content_len).decode("utf-8", errors="replace")
            try:
                data = json.loads(body)
            except json.JSONDecodeError as e:
                self.send_json(400, {"error": f"Invalid JSON: {e}"})
                return

            to = data.get("to", "all").strip().lower()
            topic = data.get("topic", "").strip()
            text = data.get("text", "").strip()

            if not text:
                self.send_json(400, {"error": "Field 'text' cannot be empty"})
                return
            if to not in ("claude", "gemini", "all"):
                self.send_json(400, {"error": f"Invalid recipient 'to': {to}"})
                return

            try:
                msg_id = agent_chat.post_message(self.storage_dir, "designer", to, topic, text)
                if msg_id is None:
                    self.send_json(429, {"error": "TOPIC_LIMIT: ask the designer"})
                    return
                self.send_json(200, {"success": True, "id": msg_id})
            except Exception as e:
                self.send_json(500, {"error": str(e)})
            return

        self.send_error(404, "Endpoint not found")

def run_server(port=8765, root_override=None):
    storage_dir = agent_chat.get_storage_dir(root_override)
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.abspath(os.path.join(script_dir, "..", ".."))
    html_path = os.path.join(project_root, "Tools", "AgentChat", "index.html")

    AgentChatHandler.storage_dir = storage_dir
    AgentChatHandler.html_path = html_path

    server_address = ("127.0.0.1", port)
    httpd = http.server.HTTPServer(server_address, AgentChatHandler)
    print(f"Agent Chat Server listening on http://127.0.0.1:{port}")
    print(f"Storage dir: {storage_dir}")
    print(f"HTML UI:     {html_path}")
    print("Press Ctrl+C to stop.")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nShutting down server.")
        httpd.server_close()

def main():
    parser = argparse.ArgumentParser(description="CodexTactics Agent Chat Server")
    parser.add_argument("--port", type=int, default=8765, help="Port to serve on (default: 8765)")
    parser.add_argument("--root", help="Custom storage root directory")
    args = parser.parse_args()
    run_server(args.port, args.root)

if __name__ == "__main__":
    main()
