"""PreToolUse gate for the orchestrator's own workflow rules (designer request 2026-11-09: "stop forgetting the router,
Jev and Gemini - just use them").

  python Scripts/Tools/agent_gate.py --agent    (hook on the Agent tool; reads the hook JSON on stdin)
  python Scripts/Tools/agent_gate.py --commit   (hook on Bash / PowerShell; reads the hook JSON on stdin)

--agent:
  * subagent_type Explore -> denied: code scouting goes to Gemini (`Scripts/Tools/gemini_agent.ps1 -Mode plan`).
  * a Claude coding agent (ue-l1..l4, general-purpose, claude, or no type) -> the prompt is classified with the model
    router (model_router.classify). When the router is confident (route true) and names another agent, the call is
    denied with the agent to use. The decision is cached by prompt hash, so a retry with the named agent is not
    re-classified (Jev answers can differ between calls).
  * anything else (fork, Plan, web-fetch, claude-code-guide, statusline-setup...) passes.
--commit:
  * a `git commit` in the main checkout -> `typesafe_triage.py --audit-diff --agent claude`; a boundary breach denies
    the commit. Commits in agent worktrees (CodexTactics-<name>) are left to the agents' own bar.
Never blocks on its own failure: any error -> exit 0 without output (the tool call goes ahead).
"""

import hashlib
import json
import os
import re
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
CACHE = os.path.join(ROOT, "Saved", "ModelRouter", "agent_gate_cache.json")
LOG = os.path.join(ROOT, "Saved", "ModelRouter", "agent_gate.log")
sys.path.insert(0, os.path.dirname(__file__))
for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8")
    except Exception:
        pass

CODING_AGENTS = {"", "general-purpose", "claude", "ue-l1-trivial", "ue-l2-routine", "ue-l3-major", "ue-l4-architect"}


def deny(reason):
    print(json.dumps({"hookSpecificOutput": {"hookEventName": "PreToolUse", "permissionDecision": "deny",
                                             "permissionDecisionReason": reason}}, ensure_ascii=False))
    log("DENY " + reason)
    sys.exit(0)


def log(line):
    try:
        os.makedirs(os.path.dirname(LOG), exist_ok=True)
        with open(LOG, "a", encoding="utf-8") as f:
            f.write(line.replace("\n", " ")[:400] + "\n")
    except Exception:
        pass


def load_cache():
    try:
        with open(CACHE, encoding="utf-8") as f:
            return json.load(f)
    except Exception:
        return {}


def save_cache(cache):
    try:
        os.makedirs(os.path.dirname(CACHE), exist_ok=True)
        if len(cache) > 200:
            cache = dict(list(cache.items())[-200:])
        with open(CACHE, "w", encoding="utf-8") as f:
            json.dump(cache, f, ensure_ascii=False)
    except Exception:
        pass


def gate_agent(data):
    tool_input = data.get("tool_input") or {}
    agent = (tool_input.get("subagent_type") or "").strip()
    if agent == "Explore":
        deny("Code scouting goes to Gemini, not a Claude Explore agent: write the scouting task to a file and run "
             "`powershell -ExecutionPolicy Bypass -File Scripts/Tools/gemini_agent.ps1 -Worktree \"<scout worktree>\" "
             "-PromptFile <file> -Mode plan` (read-only), then read Saved/Orchestrator/gemini_runs/<latest>.txt.")
    if agent not in CODING_AGENTS:
        return
    prompt = tool_input.get("prompt") or ""
    key = hashlib.sha1(prompt.encode("utf-8", "replace")).hexdigest()
    cache = load_cache()
    decision = cache.get(key)
    if decision is None:
        import contextlib
        import model_router
        with contextlib.redirect_stdout(sys.stderr):  # jev_client prints a status line; stdout must stay the hook JSON
            result = model_router.classify(prompt, model_router.load_config(), timeout=8.0)
        decision = {"agent": result.get("agent"), "route": bool(result.get("route")), "level": result.get("level"),
                    "confidence": result.get("confidence"), "reason": result.get("reason")}
        cache[key] = decision
        save_cache(cache)
    log("agent=%s router=%s" % (agent or "<default>", decision))
    if decision.get("route") and decision.get("agent") and decision["agent"] != agent:
        deny("Model router: this task is level %s (%.0f%%) -> launch it with subagent_type=\"%s\" (not \"%s\"). "
             "Re-send the same prompt with that agent; the decision is cached."
             % (decision.get("level"), 100 * float(decision.get("confidence") or 0), decision["agent"], agent or "general-purpose"))


COMMIT = re.compile(r"\bgit\b[^\n;|&]*\bcommit\b")


def gate_commit(data):
    command = (data.get("tool_input") or {}).get("command") or ""
    if not COMMIT.search(command):
        return
    if re.search(r"CodexTactics-[A-Za-z0-9_]+", command):
        return  # an agent worktree commit: the agent's own bar covers it
    env = dict(os.environ, PYTHONIOENCODING="utf-8")
    run = subprocess.run([sys.executable, os.path.join(ROOT, "Scripts", "Tools", "typesafe_triage.py"), "--audit-diff",
                          "--agent", "claude"], cwd=ROOT, capture_output=True, text=True, encoding="utf-8",
                         errors="replace", timeout=40, env=env)
    log("commit audit exit=%s" % run.returncode)
    if run.returncode == 1:
        breach = next((l for l in run.stdout.splitlines() if "Boundary Breach" in l), "boundary breach")
        deny("Jev audit (typesafe_triage --audit-diff --agent claude) refused this commit: %s. Unstage the protected "
             "paths and commit again." % breach.strip())


def main():
    try:
        data = json.loads(sys.stdin.read().lstrip("﻿"))  # PowerShell pipes prepend a BOM
    except Exception:
        return
    try:
        if "--agent" in sys.argv:
            gate_agent(data)
        elif "--commit" in sys.argv:
            gate_commit(data)
    except SystemExit:
        raise
    except Exception as error:
        log("error %s" % error)


if __name__ == "__main__":
    main()
