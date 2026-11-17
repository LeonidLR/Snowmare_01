# Codex session prompt, 2026-11-17 (paste the block into a NEW Codex session)

```text
Resume work on CodexTactics: C:\Users\Zephyrus15Duo\Documents\Unreal Projects\CodexTactics.
Read docs/port/codex/README.md (rules - follow them exactly, EXCEPT the "Local model" section: the local model is removed, never use
local_task.py) and the top of docs/port/HANDOFF.md. Then the latest chat: python Scripts/Tools/agent_chat.py read --last 10

WORKTREE: ONLY C:\Users\Zephyrus15Duo\Documents\Unreal Projects\CodexTactics-agents (built). Never CodexTactics-gemday (Gemini /
Antigravity), CodexTactics-gemcli (Claude's agents by day, Gemini CLI at night) or the main checkout (the designer plays there).

YOU ARE RESUMING task 037 - docs/port/codex/inbox/037-red-smokes-triage.md, branch codex/037-red-smokes (already checked out there),
report docs/port/codex/outbox/037-red-smokes-triage.report.md (your last commit 3cfe9463).
Done in 037 (do not redo): step 0 (RetreatFacingSmoke + CoverSmoke green on 01efefe9), MilitaryZombieSmoke 11b04070,
CorpsePersistSmoke b3fafd28 (CorpseCost 0.92/1.70 ms > 0.5 ms is handed to Gemini task 078 - not yours), OptimizedCharacterSmoke 4616ea40.
Next, in this order: StealthModeSmoke -> CoverHugSmoke -> MantleDescentSmoke -> FormationJitterSmoke -> LevelGenRampSmoke.
CHANGED: RpgPmcSmoke + PmcEncounterHuntSmoke are now Gemini task 077 (PMC order lifecycle) - skip them, just note it in the report.
NOTE: main moved since your base 01efefe9 - today Claude merged: return to the ordered facing after a fight (ThreatFacingRules,
OperativeCharacterAim.cpp), F-2 leader takes the corner (FormationLeaderCornerRules, SquadSubsystemFormation*), F-3 obstacle clicks
(PlayerControllerOrders, MantleClickSmoke). Before CoverHugSmoke / FormationJitterSmoke / MantleDescentSmoke: `git merge main` into your
branch (resolve nothing by guessing - a conflict -> stop and ask in the chat), rebuild, then re-check that smoke once on the merged base.
Per smoke: one commit, ONE chat line "037 <Smoke> | fixed <sha> / needs Claude | cause in 10 words", the gate from the task file.
When 037 is finished: post "037 done <hash> - <summary>" and then "queue empty", and wait:
  python Scripts/Tools/agent_chat.py wait --as codex --timeout 1800   (repeat on NO_MESSAGES) - Claude sends more.

Token economy: model_router.py --classify "<task>" first (level 1-2 -> cheaper model, 3-4 -> main model). Read only the files the task
names + what grep points to. Paste only the last ~15 lines of build / test output. Commit after each working step.
LIMIT RULE (designer): at 10 % of your token limit LEFT - stop. Commit, update the report, post ONE line
--topic codex-limit "limit 10%: <state>, last commit <hash>, next <step>, reset at <time>", then only
python Scripts/Tools/agent_chat.py wait --as codex --timeout 3600 (repeat on NO_MESSAGES). A fully spent limit switches you off and you
cannot come back by yourself - so stop at 10 %.

Smokes: powershell -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -NoRetry -ExtraArgs -Multiprocess -Commands <FullName>
(full registered names). The designer has priority: the scripts wait while his editor starts or a Training Ground map build runs - let them.
Never weaken / delete an assertion just to go green (a changed expectation needs the reason in the commit), never knownFail / knownFlaky.
Do NOT touch: .uasset, ABPs, Content/Maps/**, Content/VFX, Config/**, DefaultScalability.ini, the designer's Depot files
(LevelJson/L_Gen_Depot_1*, props/L_Gen_Depot_1.props.json), Pmc* code (Gemini 077), Corpse* code (Gemini 078).
Every shell: $env:CODEX_AGENT = "codex"; $env:PYTHONIOENCODING = "utf-8". Never push, never merge into main, never touch main.
Spitter and CryoDrone do not exist - never mention them.
```
