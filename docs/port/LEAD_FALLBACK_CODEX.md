# Fallback Lead = Codex (when Claude is out of tokens) - kept current by the Claude Lead

Designer decision 2026-11-26: if the Claude Lead runs out of tokens, **Codex becomes the Lead** (routes, reviews, merges, pushes) and
**Gemini (Antigravity, Hydrogen) executes**. Activation ONLY by the designer's own words ("Codex, ты теперь Lead"). Until then Codex is an
executor as usual. The Claude Lead updates the STATE block below after every merge / new task / designer decision.

## Role (what the fallback Lead does)
1. Read: CLAUDE.md, docs/port/TANDEM.md (top), this file, the inbox files of the queue below.
2. Talk to the designer in Russian, short and plain (he reads on the phone). Code / commits / docs in English.
3. Every designer request -> an inbox file `docs/port/<agent>/inbox/<NNN>-<slug>.md` (what, why, rules, tests / smoke, branch, FINISH line,
   queue place), commit + push it, then ONE chat line to the agent:
   `python Scripts/Tools/agent_chat.py post --as codex --to gemini --topic gemini-<NNN> --text "Lead: new task <NNN> ..."`.
4. Watch the chat only for FINISH / MERGE-READY / BLOCKER / LIMIT lines (`agent_chat.py read` or tail `Saved/AgentChat/chat.jsonl`).
5. Review every diff before merging: `git diff origin/main...<branch> -- Source` - does it reach the GAME (not only a test config)? smoke asserts the
   real pose / position, not a variable? no weakened checks?
6. Merge: `powershell -ExecutionPolicy Bypass -File Scripts/Tools/merge_gate.ps1 -Branches '<b1>,<b2>'` (run in the background, ~20-40 min).
   `-AcceptPreExisting` only for smokes that also fail on base main; never fake the gate state; red -> fix and `-Resume`.
   **Never commit in main while a gate runs.** The gate refuses while an editor of the MAIN checkout is open - wait, do not kill it.
   A green gate removes merged idle agent lanes itself (cleanup_merged_lanes.ps1) - keep C: free (check `C:/Work/Lanes` count + free GB).
7. Push: `$env:CODEX_AGENT='claude'; git push origin main` (the pre-push guard allows main only with this value; as fallback Lead you may use it -
   designer decision 2026-11-26, only after activation).
8. After each push: update the play copy when its editor is closed: `git -C ../CodexTactics-play merge --no-edit origin/main`, then
   `powershell -ExecutionPolicy Bypass -File Scripts/build.ps1 -Remote worker` inside CodexTactics-play (only if Source changed), then
   `python Scripts/Tools/editor_status.py set play`. Never reset the play copy; it holds the designer's own copied files.
9. Update docs/port/HANDOFF.md / TASK_BOARD.md (done list) when merging; keep this file's STATE current.
10. Do not decide gameplay forks yourself: ask the designer, with 2-3 options + a recommendation.

## Hard rules
- Never touch: the designer's uncommitted `Content/Data/LevelJson/L_Gen_Depot_1_X6.json` in main, `.uasset` / maps he edits (ABP_Operative etc.),
  DefaultScalability.ini, Content/VFX. No Spitter / CryoDrone anything (they do not exist).
- No new agent lanes beyond one per task; delete merged lanes (the gate does it); the main PC disk is small, the worker (ssh codex-worker) is big.
- Builds of agent work: `Scripts/Tools/machine_dispatch.py` or `Scripts/build.ps1 -Remote worker`; never build in main while his main editor is open.
- No TypeSafe audit for Codex (designer 11-26: local boundary audit only). No Neon model switching for Gemini: Gemini self-reviews with a
  `pro` subagent, the Lead reviews every diff.
- Spend little: react only to FINISH / blockers / the designer.

## STATE (updated by the Claude Lead - 2026-11-26 ~17:40)
origin/main 6f2a80a7 (+ docs). 17:35 claude/firstaid-cover (24771d72); 20:30 Gemini 145 look direction lock (b70b0338 + delta 6f2a80a7, gates v8-2/v8-3 green).
Live queues (re-plan, supersedes the lines below): Codex 139 -> 121 -> 148 -> 149 -> 147; Gemini 150 -> 135b -> 141 -> 144.
Designer away tonight (Saved/Lead/MAIN_PC_FREE set - delete when he is back). Merged today: G023 + Codex 134 (4851b3aa), Gemini 135 (10963d3a), Codex 142 TG Main/Play switch (549b0868),
lanes auto-cleanup (8605d41d), claude/ux-1126 = selected-only vision cones (Shift+O), inventory closes on item pick, Take ALL closes loot (82c06f7b).
Play copy = a12dea10 built (has 135; NOT yet ux-1126 - update when its editor is closed).
Codex queue: 139 roof ladder no path (WIP 29de97cd) -> 147 PMC fled = tactical defeat (designer plays sectors) -> 143 threat arrows ->
146 critical self recovery -> 136 breach click + door peek reveal (layout only, inner-room enemies hidden; works in big buildings, not small) ->
137 corpse search distance -> 138 fence queue vs new orders (buffered traversal) -> 140 control groups (Ctrl+1..4 / F1..F4) -> G022 medkit -> smoke grenade.
Gemini queue: 145 look direction lock (FINISH 04bdf1b9; part B arrival turn after a RAMP roof climb + roof pitch-down check pending, then
MERGE-READY) -> 135b left corner body ~1 m off + body ignores Shift look right (measure bones, not capsule) -> 141 straight-line moves
(cover order runs around a fence first; Alt+click strict straight, Ctrl+click ground in pause = waypoint) -> 144 wall-hug windows -> 121
(paused at fbad5357) -> G027 -> G021 -> 105b -> 118.
Known flaky: FormationStairSmoke S04 leg (83 s budget) failed once then passed (gate 82c06f7b) - give it to Codex if it repeats.
Designer decisions today: smoke grenade cloud 60 s + 15 s fade, squad throws smoke on retreat + on order; control groups max 4 (F1-F4);
straight moves 1+2, waypoints only in tactical pause; Shift+click = world direction lock (variant B, keep it); FocalRig pilot after the
weekend demo check (memory focalrig-pilot); HTN plugin rejected; stay on Unreal (Godot back-port ~4-6 months).
API credit: ~7.5 USD left under the 100 USD hard cap (Saved/Orchestrator/api_spend.jsonl); designer 11-26: agents may use it for one
Sonnet review of their own diff per L3 task (api_task.py, changed files only). Monthly code audit (Fable, separate $ budget) - talk with
the designer on 11-28; plan: Codex runs Scripts/Tools/weekly_audit.ps1 first, Fable only on 8-10 hot systems.

## Re-plan 2026-11-26 evening (designer: Gemini unreliable, rely on Codex)
Codex: 139 (finishing) -> 121 full recording (Gemini WIP fbad5357; needed before the designer's fights 11-27) -> 147 PMC fled = win ->
148 fight replay + building numbers + drawn routes -> 143 -> 146 -> 136 -> 137 -> 138 -> 140 -> G022 -> smoke grenade.
Gemini (smaller, always reviewed): 145 (+B) -> 135b -> 141 -> 144.
Designer 17:45: priority = full recording (121) + drawing in TG (148) + productive bot training this weekend (149).
Codex: 139 -> 121 -> 148 -> 149 -> 147 -> 143 -> 146 -> 136 -> 137 -> 138 -> 140 -> G022. Both machines may be used tonight.
