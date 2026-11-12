# 022 - Night tooling (Claude, 2026-11-11 evening) - Python / PowerShell / HTML only, own branch per item from main, report to claude

1. **Agent chat on the phone via Tailscale (prep; the designer installs Tailscale tomorrow).** Scripts/Tools/agent_chat_server.py:
   optional `--bind <addr>` (default stays 127.0.0.1; allow only 127.0.0.1 or a 100.64.0.0/10 Tailscale address, refuse 0.0.0.0) and
   a required access token when bound to a non-loopback address: token generated once into Saved/AgentChat/access_token.txt (never
   printed to the chat / logs), sent as a cookie after `/login?token=...` (constant-time compare), all /api/* refuse without it; keep
   the same-origin checks. Tools/AgentChat/index.html: phone layout (<= 480 px: one column, big send box, "to" chips, the idle banner
   visible). unittest for bind validation, token required / wrong / right, same-origin. Docs: Scripts/Tools/agent_chat_phone.md (RU).
2. **Codex wake-up + local dispatcher (prep; Codex CLI gets installed tomorrow).** Scripts/Tools/codex_wake.ps1 + a Python helper:
   if `codex` is on PATH and Codex has not posted in the chat for N hours (param, default 3) and no codex process runs, compose a
   resume prompt with the LOCAL model (Scripts/Tools/local_task.py, qwen2.5-coder:14b) from: docs/port/codex/INTERIM_LEAD.md queue,
   `git -C <interim> status --porcelain` (uncommitted files!), the last 10 chat lines addressed to codex; then run
   `codex exec --cd "<interim worktree>" "<prompt>"` and log to Saved/Orchestrator/codex_wake/*.log. Dry-run mode (-WhatIf) that only
   writes the prompt. Never runs anything in the main checkout, never pushes. Tests for the decision logic on fixture chats.
3. Morning: the night trainer review (012 item 4 / 021 item 3).
