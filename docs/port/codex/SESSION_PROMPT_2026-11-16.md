# Codex session prompt, 2026-11-16 (paste the block into a NEW Codex session)

```text
Resume work on CodexTactics: C:\Users\Zephyrus15Duo\Documents\Unreal Projects\CodexTactics.
Read docs/port/codex/README.md (rules - follow them exactly, EXCEPT the "Local model" section: the local model is removed, never use
local_task.py) and the top of docs/port/HANDOFF.md.

WORKTREE (changed): use ONLY C:\Users\Zephyrus15Duo\Documents\Unreal Projects\CodexTactics-agents (it exists and is built; it is on
branch fix/fire-gate, already merged). For each task: git switch -c codex/<NNN>-<slug> main. Do NOT use CodexTactics-cxnight (Gemini
works there now) and never CodexTactics-interim / CodexTactics-gemc5 / the main checkout.

Queue, in order, one task at a time:
  1. docs/port/codex/inbox/018-agent-chat-utf8.md        (warm-up, Python, small)
  2. docs/port/codex/inbox/014-retreat-facing-bisect.md  (bisect + fix; first re-check on current main - the fire gate a48e7540 changed
                                                          facing and may have changed this smoke; if it passes 3/3 now, report that and stop)
  3. docs/port/codex/inbox/016-knownfail-recheck.md      (smoke re-runs, report only - cheap tokens, long run)
  4. docs/port/codex/inbox/017-build-warnings-cleanup.md (mechanical warning fixes; also SKIP OperativeCharacterFireGate.cpp, FireGateRules.*,
                                                          OperativeCharacterSuppression/QuickFire/TargetedShots/Combat.cpp - fresh code)
Done by others - skip: 010, 011, 012, 013 (Gemini 071), 015, everything older.
All tasks are light-to-medium on purpose. If a task starts to need deep reading of many files, STOP that part, write
"needs Claude: <what>" in the report and the chat, and go to the next task.
After EACH task: commit, report, ONE chat line with the hash
  python Scripts/Tools/agent_chat.py post --as codex --to claude --topic codex-<NNN> --text "<NNN> done <hash> - <one sentence>"
then run: python Scripts/Tools/agent_chat.py wait --as codex --timeout 600
Claude answers with review notes or the NEXT task. No answer in 10 minutes -> take the next task. Queue empty -> post "queue empty" and
wait (--timeout 1800, repeat on NO_MESSAGES) - Claude sends more. Set $env:PYTHONIOENCODING = "utf-8" in every shell until 018 is merged.

Token economy: model_router.py --classify "<task>" first (level 1-2 -> cheaper model, 3-4 -> main model). Read only the files the task
names + what grep points to. Paste only the last ~15 lines of build / test output. Commit after each working step.
LIMIT RULE (designer): at 10 % of your token limit LEFT - stop. Commit, update the report, post ONE line
--topic codex-limit "limit 10%: <state>, last commit <hash>, next <step>, reset at <time>", then only
python Scripts/Tools/agent_chat.py wait --as codex --timeout 3600 (repeat on NO_MESSAGES). A fully spent limit switches you off and you
cannot come back by yourself - so stop at 10 %.

Do NOT touch: mantle / vault / ladder / nav links / formation / PlayerControllerOrders / OperativeAnimInstance*, CorpseSubsystem / ragdoll /
shotgun / panic (Claude's agent + Gemini), FireArcRules / OperativeCharacterAim, Content/Maps/**, .uasset, Config/**, the designer's
Depot files. Every shell: $env:CODEX_AGENT = "codex". Never push, never merge, never touch main.
```
