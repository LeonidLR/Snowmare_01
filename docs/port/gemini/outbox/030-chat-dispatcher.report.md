# Queue 030 Report — Chat Dispatcher (`chat_dispatcher.py`)

**To:** `claude`  
**Branch:** `gemini/030-dispatcher`  
**Status:** Ready for review (`Claude reviews and switches it on`).

---

## Deliverables
1. **Daemon & Config:**
   - [Scripts/Tools/chat_dispatcher.py](../../Scripts/Tools/chat_dispatcher.py)
   - [Scripts/Tools/chat_dispatcher.json](../../Scripts/Tools/chat_dispatcher.json)
2. **Start / Stop & Designer Guide:**
   - [Scripts/Tools/dispatcher_start.ps1](../../Scripts/Tools/dispatcher_start.ps1) (background start with PID file and `-ClearStop` / `-DryRun`)
   - [Scripts/Tools/dispatcher_stop.ps1](../../Scripts/Tools/dispatcher_stop.ps1) (creates kill switch `Saved/Orchestrator/dispatcher/STOP` and stops daemon PID)
   - [docs/tools/chat_dispatcher.md](../../docs/tools/chat_dispatcher.md) (Russian guide for the designer)
3. **Unit Tests:**
   - [Scripts/Tools/tests/test_chat_dispatcher.py](../../Scripts/Tools/tests/test_chat_dispatcher.py)

---

## Behavior Summary (Matching Queue 030)
- **Classification & Verbatim Forwarding:** Classifies new messages via `local_task.py` (`codex-local`) with deterministic RU/EN keyword fallback into `status | task | question | blocker | merge-request`. Messages are ALWAYS forwarded verbatim with fixed header `[переслал диспетчер, оригинал #<id> от <from>]`; `status` never wakes anyone.
- **Per-Recipient Wake:**
  - `claude`: appends to `Saved/Orchestrator/dispatcher/wake_claude.jsonl` for `question | blocker | merge-request` only.
  - `codex`: verifies no running `codex`/`Codex` process, reuses fail-closed exact-branch `interim/2026-11-12` + non-main guard, runs `codex exec resume --last` (fallback `codex exec`) in `--cd <interim>` with the verbatim message + `HARD_SAFETY_RULES_BLOCK`.
  - `gemini`: waits `gemini_idle_min` (15 min) after an incoming message, cancels automatically if Gemini already replied afterwards, checks `$20/week` cap in `gemini_spend.jsonl`, and launches `Scripts/Tools/gemini_agent.ps1` in `-Mode plan` (for `question`) or `-Mode yolo` (for `task`/`blocker`/`merge-request`).
  - `designer`: appends verbatim to `Saved/Orchestrator/dispatcher/designer_inbox.md`.
- **Guards & Limits:** Max 1 wake per agent per 10 min; quiet hours `23:00–07:00` (`blocker` only); kill switch `Saved/Orchestrator/dispatcher/STOP`; ignores meta-instructions trying to alter dispatcher rules/keys/schedules; `--dry-run` produces zero disk/subprocess side effects; first live boot starts cursor at `max(id)` unless `--from-start` is passed so old chat history never triggers wakes.

---

## Verification
- `python -m unittest Scripts/Tools/tests/test_chat_dispatcher.py -v`: **5/5 PASS** (classification fallback, safety meta-guard, verbatim forwarding, per-recipient wake routing, codex branch guard + `resume` fallback, gemini 15m idle wait + plan/yolo + weekly budget cap + reply cancel, rate limit, quiet hours, kill switch `STOP`, dry-run zero side effects).
- `python Scripts/Tools/chat_dispatcher.py --once --dry-run --tail 3`: **PASS** (classified `#263`–`#265`, produced verbatim dry-run plan, wrote zero files).
- `python Scripts/Tools/typesafe_triage.py --audit-diff --agent gemini`: verified on staged commit.
