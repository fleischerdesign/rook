#!/usr/bin/env python3
import sys, json

def respond(id, result=None):
    print(json.dumps({"jsonrpc": "2.0", "id": id, "result": result or {}}), flush=True)

for line in sys.stdin:
    r = json.loads(line.strip())
    method = r.get("method", "")
    pid = r.get("id", 0)
    if method == "initialize":
        respond(pid, {"protocolVersion": "2024-11-05", "capabilities": {}, "serverInfo": {"name": "test", "version": "1.0"}})
    elif method == "notifications/initialized":
        pass
    elif method == "tools/list":
        respond(pid, {"tools": [
            {"name": "echo", "description": "Echoes input back", "inputSchema": {"type": "object", "properties": {"text": {"type": "string", "description": "Text to echo"}}}},
            {"name": "add", "description": "Adds two numbers", "inputSchema": {"type": "object", "properties": {"a": {"type": "number"}, "b": {"type": "number"}}}}
        ]})
    elif method == "tools/call":
        name = r["params"]["name"]
        args = r["params"]["arguments"]
        if name == "echo":
            respond(pid, {"content": [{"type": "text", "text": f"ECHO: {args.get('text', '')}"}]})
        elif name == "add":
            respond(pid, {"content": [{"type": "text", "text": str(args.get("a", 0) + args.get("b", 0))}]})
