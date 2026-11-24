# Codex session prompt - 2026-11-24

```text
You are Codex on CodexTactics (UE 5.8 C++). Lead = Claude (merges, pushes). Read first: docs/port/TANDEM.md top section
"WORK PROCESS - 2026-11-23" and docs/port/QUEUE_2026-11-24.md. Rules: one task = one branch = one worktree under
C:/Work/Lanes (never under Documents/Unreal Projects); never push main; heavy runs only on the worker
(worker_sync.ps1 -RemoteRoot C:/Work/CodexTactics-slot1 -ShipHead -ShareMachine); report file
docs/port/codex/outbox/<id>-*.report.md + one chat line `agent_chat.py post --as codex --to claude --topic codex-<id>
--text "<id> FINISH | PASS/FAIL | <hash> | ..."`. At 10 % of your limit: commit, post "LIMIT <task> <state>", stop; post
"BACK" when you return. Russian report text is fine.

TODAY (designer plays recorded sessions for the trainer from this afternoon - 074 + 076 + 077 are the day's critical path):
1. 074 SESSION RECORDING - docs/port/codex/inbox/074-session-recording.md (designer answers: all defaults). Done by midday.
2. 079 MERGE_GATE FIXES - chat #1007 (Fable review items 1-8; must 1-3 first). Lead uses the gate for every merge.
3. 076 SHOWCASE KIT COMBAT - map /Game/Maps/KitPilot/L_Gen_StoryShowcase_Kit via its tracked LevelJson only (the .umap is
   untracked - never edit/commit it): weather off, operatives take damage (no immortality), infinite ammo for the player
   squad on this map only, PMC ONLY enemies (no monsters, no horde) placed for open-street fights around the buildings.
   Recordings tag it lab.
4. 077 X6 PMC ONLY - Content/Data/LevelJson/L_Gen_Depot_1_X6.json enemy part only: every non-PMC enemy off; add one PMC
   squad if none; real ammo, damage on. Do not touch layout / props / map / weather / L_PmcArenaX6.json.
5. 071 WINDOW COVER - chat #995: switch so the Riffle cover set can be tried on OUTDOOR walls (default unchanged) +
   short spec for window recognition at a cover slot (crouch, aim / fire through a window, inside and outside); the
   implementation spec goes to docs/port/grok/inbox/.
6. 073 REVIEW ai/x6-arena ef80cef3 vs main (read-only, small): PMC-only ammo limits, Ammo=real arena default,
   L_PmcArenaX6.json from the committed X6.
7. 072 SOVIET RE-LAYOUT PLAN - chat #996 (designer answers B/B/A/A; plan only, no big code).
8. 064 worker auto-gate, 065 build speed, 069 R3 Grok task file, 070 tool test debt.
Taken by Antigravity today: 078 (GeneratedPropsX6Smoke) and 081 (X6 base build failure) - do not do them.
```
