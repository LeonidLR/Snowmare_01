"""Economy-week guard for the Claude Code PreToolUse Agent hook (task 057)."""
from __future__ import annotations

import json
import sys
from pathlib import Path
from tempfile import NamedTemporaryFile


ROOT = Path(__file__).resolve().parents[2]
AGENT_FLAG = Path("Saved/Lead/ALLOW_SUBAGENTS")
FABLE_FLAG = Path("Saved/Lead/ALLOW_FABLE")
NO_SUBAGENTS = "no subagents while Saved/Lead/ALLOW_SUBAGENTS is absent"
HOOK = {
    "type": "command",
    "command": 'python "${CLAUDE_PROJECT_DIR}/Scripts/Tools/agent_guard.py"',
}


def is_fable_request(model: str, agent: str) -> bool:
    model = model.strip().casefold()
    return (
        model == "fable" or model.startswith(("claude-fable-", "claude fable "))
        or agent.strip().casefold() == "ue-l4-architect"
    )


def check_agent(payload: dict, root: Path) -> str | None:
    """Return the reason to deny the Agent call, or None to let it proceed."""
    if payload.get("tool_name") != "Agent":
        return None
    if not (root / AGENT_FLAG).is_file():
        return NO_SUBAGENTS
    details = payload.get("tool_input")
    if not isinstance(details, dict):
        return "Agent guard: tool_input must be a JSON object"
    model, agent = details.get("model"), details.get("subagent_type")
    if any(value is not None and not isinstance(value, str) for value in (model, agent)):
        return "Agent guard: model and subagent_type must be strings"
    if is_fable_request(model or "", agent or "") and not (root / FABLE_FLAG).is_file():
        return "Fable and ue-l4-architect require Saved/Lead/ALLOW_FABLE"
    return None


def install_local(root: Path) -> None:
    """Add the guard alongside existing local hooks without publishing local permissions."""
    path = root / ".claude/settings.local.json"
    settings = json.loads(path.read_text(encoding="utf-8-sig"))
    hooks = settings.setdefault("hooks", {}).setdefault("PreToolUse", [])
    agent_hooks = next((group for group in hooks if group.get("matcher") == "Agent"), None)
    if agent_hooks is None:
        agent_hooks = {"matcher": "Agent", "hooks": []}
        hooks.append(agent_hooks)
    if HOOK in agent_hooks["hooks"]:
        return
    agent_hooks["hooks"].append(HOOK)
    updated_path = None
    try:
        with NamedTemporaryFile(mode="w", encoding="utf-8", newline="\n", dir=path.parent, delete=False) as updated:
            updated_path = Path(updated.name)
            json.dump(settings, updated, ensure_ascii=False, indent=2)
            updated.write("\n")
        updated_path.replace(path)
    finally:
        if updated_path is not None:
            updated_path.unlink(missing_ok=True)


def main() -> int:
    if sys.argv[1:] == ["--install-local"]:
        try:
            install_local(ROOT)
        except (OSError, ValueError, TypeError, AttributeError, KeyError) as error:
            print(f"Agent guard setup: {error}", file=sys.stderr)
            return 1
        return 0
    try:
        payload = json.loads(sys.stdin.buffer.read().decode("utf-8-sig"))
        if not isinstance(payload, dict):
            raise ValueError("hook input must be a JSON object")
        reason = check_agent(payload, ROOT)
    except (OSError, ValueError) as error:
        reason = f"Agent guard: cannot read hook input ({error})"
    if reason:
        print(reason, file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
