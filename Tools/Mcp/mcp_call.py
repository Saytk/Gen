"""Client JSON-RPC minimal du serveur MCP de l'éditeur (quand les outils unreal-mcp ne se connectent pas à Claude Code).

Usage (PYTHONIOENCODING=utf-8 conseillé sous Windows) :
    python Tools/Mcp/mcp_call.py --list                         noms des outils
    python Tools/Mcp/mcp_call.py --schema                       noms + schémas d'entrée
    python Tools/Mcp/mcp_call.py --py <fichier.py> [port]       execute_python_code, auto_save false
    python Tools/Mcp/mcp_call.py <outil> '<json>' [port]        ex. call_tool '{"toolset_name":"EditorToolset.EditorAppToolset","tool_name":"StopPIE","arguments":{}}'
Port par défaut 8010 (éditeur principal). Un seul appel à la fois par éditeur (CLAUDE.md).
"""
import json, sys, urllib.request

def post(url, payload, sid=None):
    data = json.dumps(payload).encode()
    req = urllib.request.Request(url, data=data, method="POST")
    req.add_header("Content-Type", "application/json")
    req.add_header("Accept", "application/json, text/event-stream")
    if sid:
        req.add_header("Mcp-Session-Id", sid)
    with urllib.request.urlopen(req, timeout=900) as r:
        body = r.read().decode("utf-8", "replace")
        sid = r.headers.get("Mcp-Session-Id") or sid
    if body.lstrip().startswith("event:") or "\ndata:" in body or body.startswith("data:"):
        lines = [l[5:].strip() for l in body.splitlines() if l.startswith("data:")]
        body = lines[-1] if lines else "{}"
    return (json.loads(body) if body.strip() else {}), sid

def main():
    args = sys.argv[1:]
    if args[0] == "--py":
        code = open(args[1], encoding="utf-8").read()
        tool, targs = "execute_python_code", {"code": code, "auto_save": False}
        port = args[2] if len(args) > 2 else "8010"
    else:
        tool = args[0]
        raw = args[1] if len(args) > 1 else "{}"
        targs = json.load(open(raw[1:], encoding="utf-8")) if raw.startswith("@") else json.loads(raw)
        port = args[2] if len(args) > 2 else "8010"
    url = f"http://127.0.0.1:{port}/mcp"
    init, sid = post(url, {"jsonrpc": "2.0", "id": 0, "method": "initialize", "params": {
        "protocolVersion": "2025-03-26", "capabilities": {}, "clientInfo": {"name": "cli", "version": "1"}}})
    try:
        post(url, {"jsonrpc": "2.0", "method": "notifications/initialized"}, sid)
    except Exception:
        pass
    if tool in ("--list", "--schema"):
        res, _ = post(url, {"jsonrpc": "2.0", "id": 1, "method": "tools/list"}, sid)
        for t in res.get("result", {}).get("tools", []):
            print(t["name"], json.dumps(t.get("inputSchema"))[:1500] if tool == "--schema" else "")
        return
    res, _ = post(url, {"jsonrpc": "2.0", "id": 1, "method": "tools/call", "params": {"name": tool, "arguments": targs}}, sid)
    if "result" in res:
        for c in res["result"].get("content", []):
            print(c.get("text", json.dumps(c)[:2000]))
        if res["result"].get("isError"):
            sys.exit(1)
    else:
        print(json.dumps(res, indent=1)[:4000])
        sys.exit(1)

main()
