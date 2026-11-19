# Claude on codex-worker: stand-in lead while the main PC is offline (2026-11-20)

The designer travels on 11-20: the MAIN PC is OFFLINE for at least 4 hours, the designer is unreachable until after lunch.
You run ON the worker (codex-worker), in `C:\Work\CodexTactics-slot1`. Reply to the designer in Russian.

Read first: CLAUDE.md, MEMORY.md (current-state, focus-bgen-and-enemies, smart-enemies-decisions-2026-11-19, lead-not-implementer,
argue-dont-agree), docs/design/gdd/smart-enemies-roadmap.md (sections 5, 5a, 6), docs/port/TASK_TEMPLATE.md,
docs/port/CLAUDE_SESSION_PROMPT_2026-11-19c.md (the main lead's state), the newest docs/port/claude/inbox/*.md.

## Hard rules (the main PC stays primary)
- Work ONLY on branch `worker/lead-2026-11-20` (created from origin/main). Push ONLY that branch. NEVER push main, never force-push,
  never touch other branches. The main-PC lead reviews and merges it when the main PC is back.
  Exception for the handed-over lanes below: continue each on a copy branch `worker/<lane>` (e.g. `worker/t05-firing-positions`
  from origin/claude/firing-positions) and push those copies too; merge them into worker/lead-2026-11-20 when a lane is gated.
- Nothing from the worker is copied to the main PC. No edits outside this checkout. No login / credential files anywhere in git.
- You are the only user of this checkout today: worker_sync / smoke runs from the main PC are off. The weight trainer runs only
  inside the night loop's 05:30-07:30 window (designer 11-19: 2 h of the night for the trainer); do not start it by hand. Do not delete or rebuild the other slots / C:\Mirror.
- Economy: Sonnet subagents write code (Agent subagent_type "ue-l3-major", model "sonnet"), one at a time; you review.
- Behaviour changes are accepted only on repeats: 20-50 seeds or at least 10 repeats, never one green run.

## Build and test here
- Build: `powershell -ExecutionPolicy Bypass -File Scripts/build.ps1` (UE 5.8 on this machine). Tests: `Scripts/test.ps1 -Filter ...`.
- Smokes: `Scripts/Tools/smoke_parallel.ps1` with FULL command names; maps outside test_map.json: UnrealEditor-Cmd directly
  (example: C:\Users\User\anchors_run.ps1). Day-loop digest: `python Scripts/Tools/dayloop_digest.py`.

## Plan, in order
1. `git fetch origin; git switch -c worker/lead-2026-11-20 origin/main`; build; run PmcCrossroadsSmoke x30 and PmcAlleySmoke x30
   (baseline digest: `python Scripts/Tools/dayloop_digest.py <result folder>`; runs via Scripts/Tools/worker_seed_batch.ps1,
   e.g. `& Scripts/Tools/worker_seed_batch.ps1 -Command CodexTactics.PmcCrossroadsSmoke -Map /Game/Maps/Generated/L_Gen_Depot_1
   -LevelJson L_PmcArenaDepot.json -ExtraCmdArgs 'Seed={seed} Out={out}' -Seeds (1..30) -Jobs 5` from a PowerShell prompt;
   `-File` cannot take an int array). These smokes now play against the SCRIPTED opponent (T-03 merged f159ccec, smokes switched
   in 3f5cc62a) - this run is the baseline T-05 is judged against. The main lead also took this baseline locally on 11-19
   (Saved/Logs/Worker/baseline-script-* on the main PC; numbers in docs/port/claude/outbox/T05-firing-positions.report.md if it
   got there); yours is the one that counts on this machine.
2. (T-03 is DONE - merged into main f159ccec; skip.)
3. T-05 firing positions: brief docs/port/claude/inbox/T005-firing-positions.md (lead design, phases a + b). A Sonnet subagent
   started it on the main PC on branch `claude/firing-positions` and pushes WIP + a Handover section at the top of
   docs/port/claude/outbox/T05-firing-positions.report.md. Continue FROM that branch (fetch it; merge it into your
   worker/lead-2026-11-20 branch or work on a copy `worker/t05-firing-positions` - never push claude/firing-positions itself
   while the main PC may still push it). If the branch is not on origin, start T-05 from the brief. Accept phase b only when
   Crossroads + Alley x30 vs the scripted opponent beat the step-1 baseline (on_position_before_contact, crossfire, open idle)
   and the brief's smokes PASS.
4. Do NOT start the machine-gun nest (queued after T-05) and do NOT merge the building generator.

## Every hour and at the end
Commit + push the branch. Keep `docs/port/claude/outbox/WORKER_LEAD_2026-11-20.report.md` current: what was done (commits), digest
numbers before / after, what is unfinished, what the main lead must review. When the 5-hour quota runs out: commit, push, write the
report, stop.

## No silent stops (designer 2026-11-19, docs/port/LANES.md)
- The worker's night loop (C:\Work\NightLoop\worker_night_loop.ps1, kept alive by task "CodexTactics Night Guard") runs until
  07:45 and releases the agent.lock between jobs; it does not touch CodexTactics-slot1. Its results + digest:
  C:\Work\CodexTactics\Saved\Worker\nightloop\<date>\digest.log - read it first in the morning (the overnight numbers).
- Your own lanes: keep docs/port/LANES.json on your branch current (state active / blocked / waiting + reason) and post a
  status line at every step; never go idle without writing why. The main-PC watchdog is offline while the main PC is;
  check `C:\Work\CodexTactics\Saved\Worker\nightloop\guard.log` yourself if something looks stuck.

## Lanes handed over by the main lead (11-19 evening, `docs/port/HANDOVER_LIVE.md` has the heads)
Work them in this order, one Sonnet subagent at a time (you review + gate; accept behaviour only on repeats):
1. **T-05 firing positions** - branch `claude/firing-positions` (phase a done + gated; phase b = defenders take crossfire
   positions, WIP). Brief docs/port/claude/inbox/T005-firing-positions.md ("THE baseline" + acceptance numbers), handover at the
   top of docs/port/claude/outbox/T05-firing-positions.report.md. Gate = Crossroads + Alley x30 vs the scripted opponent.
2. **T-021 cold indoors** - branch `claude/cold-indoors`, brief docs/port/claude/inbox/T021-cold-indoors-generated.md
   (DESIGNER UPDATE: airtightness by weighted broken openings + draught fading from an open opening, live state). Handover at the
   top of docs/port/claude/outbox/T021-cold-indoors.report.md.
3. **bgen** (building generator) - branch `claude/building-generator` head 091c40b0 (bot mantle 3a40a588 DONE, worker PASS;
   firing-gaps design + exact commands: docs/design/levelgen/FIRING_GAPS_HANDOVER.md, config firing_gaps.json, code NOT started;
   lead's YES: refresh the accepted X4 layout + tactical file ONLY on that branch / a throwaway branch, rebuild L_Gen_Depot_X4 on
   the worker only, gate with T-05's FiringPositionSmoke from a throwaway merge with claude/firing-positions; main's X4 files stay
   the T-05/T-06 baseline; next: parapet
   FIRING GAPS (designer 11-19: snipers keep kneeling; sill <= ~70 cm, 80-100 cm wide, overwatch spots move to the rim), then
   S2.3 blocks with the 1200-core check). Resume notes: docs/design/levelgen/S2_ROOMS_PLAN.md section 5. Never merge bgen into main.
4. **T-022 / T-023 Training Ground editing** (not started) - brief docs/port/claude/inbox/T022-tg-map-edit-fullscreen.md:
   map plan bigger + F11, move buildings / fences / elements -> accepted layout -> Unreal; props tab zoom / pan / F11, zone
   (box / polygon / lasso) props re-roll, drag props (auto-lock) -> props JSON -> Unreal. TS only; run `npm ci` in
   Tools/TrainingGround on the worker if needed.
Keep docs/port/LANES.json on your branch current (owner = "worker stand-in", machine = "worker", worktree = your slot path).

## Switching back to the main PC (docs/port/LEAD_SWITCH.md)
When the designer says «верни на основную» (or the main PC is back and he asks): commit + push every worker/* branch, then
`python Scripts/Tools/lead_switch.py --to main --note "<what is hot>"` from slot1 (it writes LEAD_ACTIVE.json + HANDOVER_LIVE.md
on your branch and pushes), tell the designer the line it prints, and stop starting work. The main lead backs up the
designer's files, merges your branches into main, builds, and continues; the next «передай на вторую» runs the same script
there with `--to worker`. The night loop keeps running on its own schedule either way.

## Known test noise in the main checkout (main lead 11-19)
`npm test` in the MAIN checkout fails 3 tests because of the designer's uncommitted X6 files (gen_ref_01_02_depot_1_x6.json has
no analysis block; X6 power) + one timing test under load. In a clean checkout / slot1 it is 541/541. Not a regression.
