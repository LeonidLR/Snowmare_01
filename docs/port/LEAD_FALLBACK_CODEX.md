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

## STATE (updated by the Codex Lead - 2026-11-27, after gate-v10 completion) - HANDOVER: Claude weekly limit 98 %, resets 2026-11-28 11:00
Designer 2026-11-27 15:10: "when close to 1 % of tokens - Codex is the new Lead"; 15:20 ACTIVATED ("Да, Codex лид"): Codex leads
until Claude's reset (Sat 2026-11-28 11:00 local), then hands back (post a short STATE here + chat line).
origin/main 8af22865 (pushed). Gate-v10-1/2/3 all EXIT 0: 135b (50334424), 153 (f17b3f35), 154 (8af22865); builds, Smart tests and mapped worker smokes passed; designer files intact. Earlier today merged: 149b trainer metrics, 151 per-round demo marks + TG->Play X6 sync, 152 run order kept after traversals.
Designer priority: recording + analysis of his fights; trainer runs NEXT week (memory trainer-next-week-2026-11-27; 149b worker baseline task disabled).

LIVE STATUS (Codex Lead takeover; designer explicitly requested 156 -> 155):
1. Gate chain 135b -> 153 -> 154 is GREEN and pushed; Saved/Logs/gate-v10-{1,2,3}.exit each says EXIT 0.
   Play copy contains 8af22865 (play/current 44bd2a72); its worker build was still running at takeover. Do not start another play build.
2. Gemini 144 -> 154b is running via agy_task.ps1; 144 START at 15:16, heartbeat at 15:46 local. The earlier authentication failure
   was resolved by the designer removing dead Antigravity Tools NRPT DNS rules. agy_task.ps1 probes proxy 8899 then direct Google access;
   this reviewed route fix is committed with this handover. Watch MERGE-READY / BLOCKER / LIMIT; do not duplicate the run.
3. Designer request: after committing this STATE, launch 156 -> 155 sequentially through Scripts/Tools/codex_task.ps1 in the EXISTING lanes.
   156 resumes WIP 45dc6940 using Saved/Lead/codex_156_resume.md; incorporate origin/main (135b + 153) before verification.
   155 uses docs/port/codex/inbox/155-ramp-roof-first-try.md. Queue runner Saved/Lead/codex_156_155.ps1 stops on failed/limited 156.
   The app Lead returns after launch to avoid concurrent active Codex work on the same usage. No main merge/push by executors.
4. This handover commit remains local: automatic approval review rejected origin/main push as insufficiently explicit authorization.
   Existing gameplay merges above are already on origin/main. Ask the designer before retrying the handover push.
QUEUE after these: 139 (MERGE-READY 69927b35; merge only after the designer's LadderActor_22 TopStandoff/TopExitStandoff 49->125 map fix) ->
147 PMC fled -> 143 -> 146 -> 136 -> 137 -> 138 -> 140. 157 per-shot accuracy recording: ONLY if the designer says yes (asked, no answer yet).
Designer analysed 2 fights in "Хроника боя" (CodexTactics-play/Saved/Demos/2026-11-27_*_085013 / _104238 .notes.json): keep the notes, the
trainer work next week turns them into rules / plays (5 annotated fights first, then A/B).
Lead review lessons today: Codex once changed shared infra unasked (worker_sync LFS, reverted); agents' self-reported BLOCKERs were often fine
(121, 148) - always look at the diff + gate yourself; agy needs prompts as a FILE pointer (PS 5.1 breaks long args).

## Re-plan 2026-11-26 evening (designer: Gemini unreliable, rely on Codex)
Codex: 139 (finishing) -> 121 full recording (Gemini WIP fbad5357; needed before the designer's fights 11-27) -> 147 PMC fled = win ->
148 fight replay + building numbers + drawn routes -> 143 -> 146 -> 136 -> 137 -> 138 -> 140 -> G022 -> smoke grenade.
Gemini (smaller, always reviewed): 145 (+B) -> 135b -> 141 -> 144.
Designer 17:45: priority = full recording (121) + drawing in TG (148) + productive bot training this weekend (149).
Codex: 139 -> 121 -> 148 -> 149 -> 147 -> 143 -> 146 -> 136 -> 137 -> 138 -> 140 -> G022. Both machines may be used tonight.
