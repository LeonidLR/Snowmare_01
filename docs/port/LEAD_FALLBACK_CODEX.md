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

## STATE (Claude Lead, 2026-11-28 ~13:40) - Claude is Lead again (weekly reset 0 %); Codex is an executor
origin/main b4848608: 154b replay rounds + notes filter (Gemini, merged e4e2c00b), merge_gate -LocalSmokes / -LocalBaselineJson (worker off).
Worker codex-worker OFF today; the designer moves the main PC back to the university network later today (agy_task.ps1 picks proxy 8899
vs direct itself). Local X6 smokes on the main PC are noisy: FormationScanPitchSmoke + WindowDetourSmoke fail on base main,
FormationFenceCrossSmoke + TorsoDriftSmoke pass only on re-run (baseline Saved/Logs/baseline-1128-x6.json).
DESIGNER DECISION 2026-11-28: FREEZE the cover / window / corner / orders node for new features until the code audit; agent MERGE-READY
without real smoke results is rejected. Audit tonight: MSVC /analyze (build.ps1 -StaticAnalyzer VisualCpp) + Fable review of 8 hot systems.
In flight (both lanes now have junctions to the content packs + DDC + Content/Maps/KitPilot, so smokes run in the lane):
1. Gemini 144 fix round (agy, lane 144, prompt Saved/Lead/agy_144_fix_1128.md): gate in main failed - WallHugWindowSmoke setup finds no
   window pair, FormationFenceCrossSmoke 0/3, RoofFightSmoke fire gate 1/2. Lead merged main into 144 (2ecf491e) + registered its smoke.
2. Codex 155 fix round (lane 155, prompt Saved/Lead/codex_155_fix_1128.md): gate built + 18 smokes PASS but LevelGenRampSmoke designer155
   planned 0 legs and the RoofAccessAudit table reads no navmesh almost everywhere (audit timing?) - reverted, needs real evidence.
Next after these: Codex flaky-smokes task on the worker (after the PC move). QUEUE then: 139 (waits for LadderActor_22 map fix 49->125) ->
147 -> 143 -> 146 -> 136 -> 137 -> 138 -> 140 (respect the freeze). 157 per-shot accuracy only if the designer says yes.

## Re-plan 2026-11-26 evening (designer: Gemini unreliable, rely on Codex)
Codex: 139 (finishing) -> 121 full recording (Gemini WIP fbad5357; needed before the designer's fights 11-27) -> 147 PMC fled = win ->
148 fight replay + building numbers + drawn routes -> 143 -> 146 -> 136 -> 137 -> 138 -> 140 -> G022 -> smoke grenade.
Gemini (smaller, always reviewed): 145 (+B) -> 135b -> 141 -> 144.
Designer 17:45: priority = full recording (121) + drawing in TG (148) + productive bot training this weekend (149).
Codex: 139 -> 121 -> 148 -> 149 -> 147 -> 143 -> 146 -> 136 -> 137 -> 138 -> 140 -> G022. Both machines may be used tonight.
