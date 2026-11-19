# Smart Enemies - roadmap and the first two weeks

> **Status**: v1 proposal, 2026-11-19, Claude Fable (design architect session). Nothing here is dispatched: the lead session
> dispatches from this document after the designer approves the GDD direction. Decided by the designer on 2026-11-19
> ("Да, принимаю рекомендации по OQ-2 и OQ-10"): OQ-2 = authored squad plays on top of the existing per-fighter utility; OQ-10 =
> the trainer's weight evolution is paused and the worker runs scenario regressions at night.
> **GDD**: [smart-enemies.md](smart-enemies.md). **Audit**: [smart-enemies-audit.md](smart-enemies-audit.md).

## 1. The plan in one paragraph

Stop tuning. Build the missing behaviour bottom-up, test-first, on the designer's own three examples: first the measuring
stick (the two clips as red smokes, a scripted opponent, honest metrics), then **positions** (where it makes sense to stand,
with a checked line of fire), then **movement** (cover to cover, and round a building unseen), then the **squad layer** with
one play - "set an ambush on the predicted route" - until his clips turn green and the designer says yes to the re-recorded
clips. Only then widen: more plays on streets and roofs, buildings as the generator delivers them, monsters, encounters,
and at the very end tuning by night runs behind a gate.

Rules of the plan:
1. **One vertical slice first** (DS-00A + DS-00B + DS-00C option 2), not fifteen plays at 20 %.
2. **Smoke before code**: every behaviour gets its scenario smoke first, registered red (`knownFail`), then the code turns it
   green. No check is weakened to pass.
3. **The designer judges clips, not fitness**: each phase ends with 1-3 readable clips of the new situation.
4. **The night machine tests, it does not evolve**, until the training gate (GDD 9) passes.
5. **Numbers in JSON, pure rules + unit tests**, as everywhere in the project.

## 2. Decisions the designer must make, and what each blocks

| Decision (GDD open question) | Blocks | Needed by |
|---|---|---|
| OQ-2 - **RESOLVED 2026-11-19: B**, authored squad plays above the existing utility | nothing any more: Phase 3 / T-08 is unblocked | done |
| OQ-10 - **RESOLVED 2026-11-19: a**, weight evolution paused, scenario regressions at night | nothing any more: the nights follow section 5; T-10 delivers the runner and the gate | done |
| OQ-17 lab maps for judging (rec. small lab maps + generator arenas) | task T-02, all scenario smokes | day 1 |
| OQ-1 first milestone scope - **RESOLVED 2026-11-19: streets and roofs first** (Phase 4 before Phase 5) | the order of Phases 4 and 5 | decided |
| OQ-21 enemy charges scope | the charge half of T-11 | week 2 |
| OQ-22 defence in depth: who meets the player on the route | Phase 4 | week 3 |
| OQ-11 the evaluation opponent (rec. bot squad that sees humans) | Phase 8, part of Phase 0 | week 3 |
| OQ-3 resistance and survivors | Phase 7 profiles only | later |
| OQ-4 / OQ-5 first volley, difficulty | Phase 7 fairness pass | later |
| OQ-6 / OQ-8 / OQ-12 / OQ-14 window jumps, doors, grenades, monsters and doors | Phase 5 / 6 | before Phase 5 |

Tasks T-01, T-03, T-04 (metrics half), T-05, T-06, T-07, T-09, T-12 need no decision and can start at once. Since
2026-11-19 T-08 and T-10 need none either: T-08 waits only for its technical inputs (T-05 position queries, T-07 for the
mobile half), T-10 can start at once. T-02 has a default; only the charge half of T-11 still waits (OQ-21).

## 3. Phases in dependency order

Sizes for an agent: LIGHT < 0.5 day, MEDIUM 1-2 days, HEAVY 3-5 days. Owner types: **Lead** (Claude lead session: design
gate, review, merge, the core architecture), **Opus agent** (Claude sub-agent in `CodexTactics-claude`), **Antigravity**
(Gemini day worker: TS / Python, self-contained), **Codex** (light and test tasks only), **Grok** (one lane while its budget
lasts, about 2026-11-23), **Bgen** (the building-generator session), **Worker-ops** (the second machine).

| Phase | Goal | Deliverables | Acceptance (GDD ids) | Owner | Size | Must NOT start before |
|---|---|---|---|---|---|---|
| **0 Measuring stick** | See the failure, be able to prove a fix | decision traces of the two clips (T-01); lab maps of both layouts (T-02); scripted opponent (T-03); new arena metrics + the two smokes red (T-04); enemy traversal smoke (T-12); training gate + scenario regression runner (T-10); night evolution paused | the two smokes FAIL on main for the reasons in the audit; every new metric non-zero in a hand test | Antigravity, Bgen, Opus agent, Codex | 5 MEDIUM | - |
| **1 Positions** | Layer 0: positions that can fire, everywhere a play needs them | firing position service with checked lines (T-05); fire-hold "distance is not contact" (T-06); tactical map export v2 with building ids, storeys, ground sites round goals, open crossings (T-09) | A-03; `overwatch_kills` > 0 and shots from posts > 0 on X4; A-04 `guard_share` not null | Lead + Opus agent, Grok, Antigravity | 1 HEAVY + 2 MEDIUM | T-01 result (it confirms or kills H1-H4) for the ground-site half only |
| **2 Movement** | Nobody runs across the open; a man can go round a building unseen | exposure cost F1, covered-route query, bounding move used by Advance / Objective / FallBack / Flank (T-07); fixes found by T-12 | A-01; flank success measured as shots from the flank, not attempts | Opus agent | 1 HEAVY | T-12 (know what is walkable); lab maps |
| **3 Squad layer + PL-03** | The designer's three examples work | squad plan (play, roles, positions), event-driven re-plan, role constraints on the fighter brain; PL-02 occupy; PL-03 with F8 kill zone, F2 crossfire, F3 static anvil, mobile anvil, F4 discipline; G008's roof defence moved into the layer as PL-05 (T-08, T-08b) | **A-00A, A-00B, A-00C (option 2: the cut-off on an obstacle top)**, A-02 green; the designer accepts the three re-recorded clips | Lead (core), Opus agent | 2 HEAVY | Phase 1 position queries (OQ-2 is decided); Phase 2 covered route (for the mobile anvil only - the static half may start earlier) |
| **4 Streets and roofs** | The squad fights as a group outdoors | attackers bound (PL-01 in the commander); PL-04 / PL-15 on the layer; PL-10 take the height; PL-11 flank by exposure; PL-12 layers; generator posts in the game: spawn at post, own charges known (T-11); defence in depth (OQ-22); attackers vs mines (PMC_DEFENCE D3) | A-09, A-10, A-11; clips of DS-04, DS-08, DS-09, DS-10, DS-12 | Lead + Opus agents, Grok / Codex for isolated pieces | 3 HEAVY + 3 MEDIUM | Phase 3 accepted by the designer |
| **5 Buildings** | Windows, storeys, stairs, exits | hearing (noise events); storeys in the belief; runtime building file (rooms, stair heads, exits, roles) exported by the generator; PL-06 window fire with duck / shift; PL-07 upstairs; PL-08 escape; then PL-09 enter and clear | A-05, A-06, A-07, A-12, then A-08; clips of DS-01, DS-03, DS-05, DS-07, DS-11, DS-13 and DS-00C option 1 (A-00C line 5: the building defender - window to window, window exit, run round) | Lead + Opus agents; Bgen for data | 4 HEAVY + 3 MEDIUM | building generator merged with P0-proof (`LevelGenBuildingsSmoke` >= 95 %) and S1 roles; S2 rooms for PL-07 / PL-09; door runtime R1 for PL-09 and for kick noise; OQ-6, OQ-8 |
| **6 Monsters and collisions** | The two languages meet | jumpers placed on tactical drop spots; noise attraction to firefights; doors rule (OQ-14); `MonsterDoorSmoke`; DS-14, DS-15, DS-17 | A-14 | Codex / Antigravity (isolated), Lead review | 3 MEDIUM | none for jumpers and noise (may run beside Phase 3-4 on a light lane); R1 + OQ-14 for doors |
| **7 Encounters, Director, factions, fairness** | The campaign uses it | garrisons by building role; hunt squads; reinforcement (OQ-9); Director budget hooks; faction profiles (resistance, survivors after OQ-3); tells (glint, tracks, voices), reaction times, first-volley rule | A-13; the 30-minute designer playtest (GDD whole-system acceptance) | Lead, Antigravity (Director data), audio / VFX owners | 3 HEAVY | Phases 4-5; OQ-3, OQ-4, OQ-5, OQ-9 |
| **8 Tuning** | Numbers of the designed plays | fitness = situation quality + behaviour floors; league with the stand-in; QD descriptors redefined on designed behaviours (height share, cut-off rate, bound discipline); night runs resume | gate G1-G5; a tuned candidate beats the baseline by F7 | Antigravity (Python), Worker-ops, Lead review | 2 MEDIUM | the gate; Phases 3-4 accepted |
| **9 Later** | - | Learning Agents imitation from recorded play; bait and lures; turn-based parity; difficulty tiers | - | - | - | Phase 8 |

Critical path: 0 -> 1 -> 3 (static) -> 2 -> 3 (mobile) -> 4. Phase 5 is paced by the building generator, not by this plan.

## 4. Building generator dependencies

| Need of the enemies | For | Generator / runtime item | State 2026-11-19 | Request |
|---|---|---|---|---|
| A usable access to every roof a play may use, exported with its top point and kind | DS-00B, PL-05, PL-10 | validator "an overwatch building must have a ladder"; tactical map `overwatch_roofs[].access` | exported; the AI's use is unproven (T-12) | keep; add the access id to every walkable roof, not only overwatch roofs |
| A roof edge a man can fire over | DS-00B, DS-02 | parapet 100 cm (`RoofParapet`) | positions sit behind the parapet: 0 shots (audit 2.1) | a rule for the firing step: stand point at the rim, gaps in the parapet, or parapet height by archetype - decide with T-05's finding |
| Building id and storey on every tactical item | PL-06, PL-07, garrisons | tactical map export | missing | T-09 |
| Stair cores every operative can walk | PL-07, PL-10 | P0 + P0b smoke (Codex 048) | X6 test map 17 of 18 flights; `LevelGenRampSmoke` red on 2026-11-18 | P0-proof >= 95 % before Phase 5 |
| Rooms and corridors | PL-07, PL-09 | S2 (approved 2026-11-19, starts after the 11-21 reset) | not started | none - follow S2 |
| Exits other than the stair: fire escape, balcony, back door, broken window | PL-08, R-B3 | S1 stories 4, 9, 10 | on the branch | export exits per building into the runtime file |
| Door and glass at runtime (open, kick, shatter + noise) | PL-08, PL-09, R-B4 hearing, DS-03 tells | R1, R2 | not started, owner question Q12 | schedule R1 before Phase 5's second half |
| Building roles (objective, garrison, overwatch, transit) | Phase 7 garrisons, F8 `early` | S1 | on the branch | export `role` to the level JSON |
| Lab layouts: the two clip layouts; a 2-storey block with a core, windows on both facades, a roof ladder, a back exit | Phase 0, Phase 5 | Training Ground + builder | `L_Gen_Train_1..3` regenerated on the branch | T-02 |
| Breach panels | R-B8 | S4 | waits | none now |
| Climbable obstacles beside chokepoints with a top one man can stand on (heights of the MANTLING table) | DS-00C option 2 | mantle edges / links, props layers | exist for the squad of the player; not known as firing positions | T-05 scores obstacle tops; T-09 exports them near chokepoints |
| Windows onto a named area as checked firing positions + window exits within the drop rule + a way round the building | DS-00C option 1 | S1 / S2, the runtime building file, glass R2 | on the branch / not started | Phase 5 |

## 5. What stops or is parked now

- **Night evolution (GA and QD)**: paused by the designer's decision of 2026-11-19 (OQ-10) until the training gate of GDD
  section 9 passes; nobody starts `run_night_trainer.ps1` meanwhile. At night the worker runs scenario regressions: until
  T-10's runner exists, the existing sweep (`smoke_parallel`) over the PMC / tactical smokes; with T-10, N seeds per
  scenario smoke and a pass-rate report every morning.
- **Branch `grok/G008b-overwatch-hold`** (10 commits, unfinished): not merged as a branch. Its two working commits
  (`07c27808` hold until the post's range, `089c480e` keep the hold when a mate dies) are re-applied by T-06; its parapet
  finding becomes input of T-05; then the branch is closed.
- **QD axis recalibration, new commander genes, trainer champion work**: parked until Phase 8 (the descriptors will be
  redefined).
- **PMC_DEFENCE P3 (attackers vs mines), P4 (trainer genes)**: moved to Phase 4 / Phase 8.
- **Flat-utility fixes that only move scores** (new weights, new order factors): no new ones; bugs are fixed, but behaviour
  goes into the squad layer.

## 5a. Work rhythm: day loop + night regression (designer decision 2026-11-19)

The designer asked whether night-only runs are the right rhythm. Decision: a hybrid. While behaviour is being BUILT
(Phases 0-7) the work is debugging, not training, and needs a short loop; the nights give the statistics.

- **Day loop (per change).** After each behaviour fix the worker runs only the affected scenario smoke (A-00A crossroads,
  A-00B alley, A-00C bridge, or the task's own smoke) on 20-50 seeds - target under ~15 minutes - and returns a short digest
  (pass rate, the worst 3 seeds, the decision trace of one failed seed). The implementer reads the digest, never raw logs,
  fixes, and reruns. Repeat until the pass rate meets the task's gate. Screenshots / a short clip of one seed go to the
  designer when a clip scenario first turns green.
- **Night regression.** The former trainer window (16:30-08:00 on the worker) runs every scenario smoke on hundreds of
  seeds. Morning report: what got worse since yesterday, what is stable, flaky seeds. A behaviour that works in 1 of 3
  rounds is not done.
- **Weight tuning (Phase 8 only).** Only after the behaviour passes its acceptance on the designer's clips and the
  training gate (T-10) is green, the trainer tunes NUMBERS of designed plays (fire-open range, ambush hold time, spacing).
  That tuning may run at night again.
- **Why not night-only:** one mistake costs a day, and evolution cannot create a behaviour the order catalogue lacks (the
  2026-11-19 night). **Why not day-only:** the worker idles at night and day runs alone give too few seeds to trust; each
  day iteration also costs tokens for reading the digest, so digests stay short (Jev / `jev_digest.py`).
- **Needs:** T-10's runner with a quick mode (one scenario, N seeds, digest) - see T-10; Worker-ops moves the 16:30-08:00
  window from the trainer to the regression sweep.

## 6. The first two weeks

Calendar assumption: week 1 = 2026-11-21 .. 11-27 (after the weekly limit reset), week 2 = 11-28 .. 12-04. The main
PC format is POSTPONED (lead, 2026-11-19: the designer keeps working on the main PC for now), so builds run as usual;
if the format happens later, tasks that need an Unreal build (T-02's map build, T-05) wait for a confirmed build machine.
Dispatched 2026-11-19 (lead, fd9715a6): Grok G009 = T-06; Codex 049 = T-04, 050 = T-12; Antigravity 089 = T-01, 090 = T-09,
091 = T-10. Held: T-02 / T-03 (OQ-11 / OQ-17 open), T-05 / T-07 / T-08 / T-11 (lead work after the 11-21 reset).

| Day | Lead / Opus agents | Antigravity | Codex | Grok | Bgen |
|---|---|---|---|---|---|
| W1 d1-2 | T-03 scripted opponent | T-01 clip traces | T-04a metrics | T-06 fire hold | T-02 lab layouts |
| W1 d3-5 | T-05 firing positions | T-09 tactical export v2 | T-04b the two smokes (red), T-12 traversal smoke | (budget ends) | S2 as planned |
| W1 d6-7 | T-05 review + merge; T-07 start | T-10 gate + regression runner | T-12 report | - | - |
| W2 d1-4 | T-07 movement; T-08 squad layer + PL-02 + PL-03 static (A-00A) | T-10 finish; T-09 follow-ups | regression fixes from T-12 (light) | - | roof firing-step rule |
| W2 d5-7 | T-08b mobile anvil + height (A-00B); T-11 posts in game; T-08c obstacle-top cut-off (A-00C) starts, may end in week 3 | scenario regression nightly report | - | - | - |
| End of W2 | two re-recorded clips to the designer; Phase 4 (streets / roofs) continues - OQ-1 decided 2026-11-19 | | | | |

### Tasks (docs/port/TASK_TEMPLATE.md form)

Common to every task below, exactly as in the template and therefore written once:
- *Limit rule*: at 10 % of your daily / weekly limit left: stop, commit (WIP is fine), handover at the top of the report,
  FINISH line. *Base*: origin/main newest. Before switching branches in the worktree: no UnrealEditor-Cmd / build may run on it.
- *Never* (the three template lines): change / weaken a smoke or test check, its timing or its fixture to make it pass
  (unless the task IS the smoke); raise a budget to make a check pass; push or merge main, touch existing maps, Content/VFX,
  Config/DefaultScalability.ini, the designer's uncommitted files.
- *Gate* (default): build `Scripts/build.ps1 -Remote worker` (exit 2 -> local) | named smokes via
  `Scripts/smoke_parallel.ps1` | `Scripts/test.ps1 -Smart` | `python Scripts/Tools/typesafe_triage.py --audit-diff --agent
  <agent>` | `git diff --check`.
- *Report*: `docs/port/<agent>/outbox/NNN-<slug>.report.md` + the FINISH line, as in the template.
- NNN and the exact branch name are given by the lead at dispatch.

---

#### T-01 - Say exactly what each fighter decided in the designer's two clips (TOOL) - MEDIUM, Python
Owner: Antigravity. Branch `gemini/NNN-pmc-round-trace`. Worktree `CodexTactics-gemday`.
**Design gate.** Spec: audit 4.1 (hypotheses H1-H4). Acceptance: for each of the two rounds a one-page timeline the designer
could follow: scenario, objective place and level, each defender's posts, his order sequence with scores from spawn to first
contact, seconds standing in the open, first-shot times; and a verdict per hypothesis H1-H4 (holds / does not / cannot tell).
Proved by: `Scripts/Trainer/tests/test_round_trace.py` on a committed fixture round. Designer decision needed: none.
**Evidence so far.** The designer's two screenshots and notes (GDD DS-00A / DS-00B); audit 4.1; the night's rounds are on the
worker / `Saved/Reports/worker-2026-11-19/` (worker-ops names the two job ids from the clip names).
**Task.** 1. Get the two round folders (arena.json, `round_*.track.json`, the job log) from worker-ops; do not re-run them.
2. Write `Scripts/Tools/pmc_round_trace.py <arena.json> --round N`: reads the track + `[PmcAI]` log lines, prints the timeline
(markdown), reusing `nighttrainer/track_stats.py`. 3. Run it on both rounds; write the verdicts. 4. Stop rule: if the logs do
not carry order scores, report which log line is missing (that becomes a C++ logging task) - a good result too.
**Never** (besides the common lines): change the trainer's behaviour or config; read whole `runs.jsonl` into the report.
**Gate.** `python -m unittest discover -s Scripts/Trainer/tests`; triage audit; `git diff --check`.
**Allowed paths.** `Scripts/Tools/pmc_round_trace.py`, `Scripts/Trainer/tests/test_round_trace.py`,
`Scripts/Trainer/fixtures/`, `docs/port/gemini/outbox/`.

#### T-02 - Three small lab maps that reproduce the designer's screenshots (TEST fixture) - MEDIUM, TS + builder
Owner: Bgen session (or Antigravity for the layouts, the lead for the Unreal build). Branch `claude/lab-ambush-maps`.
Worktree `CodexTactics-bgen`.
**Design gate.** Spec: GDD DS-00A, DS-00B, A-00A, A-00B; OQ-17. Acceptance: `L_Lab_Crossroads` - an open crossroads, >= 4
cover objects (crates / blocks) on its far side, a walled enclosure with one gap on the near side, attacker start 60-80 m
away, a goal beyond the crossroads; `L_Lab_Alley` - a lane 3-5 m wide along a one-storey building, a 2-storey building with a
walkable roof, a parapet and a ladder of >= 7 rungs at the lane's mouth, a way round the back of the low building, the goal
at the mouth; `L_Lab_Bridge` - a crossing over a cut (a drop on both sides within the drop rule), two cover objects at its
near end, a climbable obstacle of a MANTLING-table height beside its far end, a building with windows onto the deck. Each map <= 80 x 80 m, with its tactical, objectives and level JSON files. Proved by: `TacticalMapSmoke`-style
load check on both maps (counts = JSON, every spot on the navmesh), the validator green. Designer decision needed: OQ-17
(default if unanswered: build them; they are new files and cost nothing elsewhere).
**Evidence so far.** The two screenshots (lead has them); no existing map isolates either layout; X4 rounds are 160 m
routes where the situation appears by chance.
**Task.** 1. Author the two layouts in the Training Ground (hand-placed, locked), accept them, export tactical + objectives.
2. Build the maps with `Scripts/Editor/build_levelgen_map.py` (designer's editor closed). 3. Register the load check in
`Scripts/test_map.json`. 4. Stop rule: if the builder cannot make the enclosure wall with a gap or the parapet, ship the
layout and report the missing module.
**Never** (besides the common lines): touch any existing map or accepted layout; regenerate X2 / X4 / X6.
**Gate.** Training Ground `npm test` + typecheck; the load smoke on both maps; triage audit.
**Allowed paths.** `Content/Data/LevelGen/accepted/gen_lab_*.json` (+ meta), `Content/Data/LevelGen/{tactical,objectives,props}/L_Lab_*`,
`Content/Data/LevelJson/L_Lab_*.json`, `Content/Maps/Generated/L_Lab_*.umap` (new files only), `Scripts/test_map.json`
(two new rows), the report.

#### T-03 - A scripted opponent for scenario smokes: attackers that just run, later that bound (GAME test support) - MEDIUM, C++
Owner: Opus agent. Branch `claude/pmc-scripted-opponent`. Worktree `CodexTactics-claude`.
**Design gate.** Spec: GDD "Acceptance Criteria" (scenario smokes use a scripted opponent, not self-play), A-00A lines 5-6.
Acceptance: `CodexTactics.PmcArena ... Opponent=Script Script=run` puts side B on the route and walks it to the goal in one
sprint, firing back by the body's own rules, with no commander; the same seed gives the same walk. Proved by: unit test
`CodexTactics.AI.PmcArena.ScriptOpponent` (the waypoint list from a route) + `PmcArenaSmoke` unchanged + one manual round
logged in the report. Designer decision needed: none.
**Evidence so far.** Self-play is the only working opponent; the bot squad does not perceive hostile operatives
(pmc-squad-ai.md:424; bot rounds on X4: closest approach 38-68 m). Scenario smokes need an opponent that does the same thing
every time.
**Task.** 1. Diagnose how the arena spawns side B for `Opponent=Pmc` / `Bot`. 2. Add `Opponent=Script`: the commander off
for that team (`SetTeamCommanded(false)`), a waypoint follower along the scenario route (pure rule: route -> waypoints every
N m, N in `pmc_arena` JSON), `Script=run`. Leave a clear seam for `Script=bound` (task T-07 fills it). 3. Stop rule: ~1.5
days; if team control without a commander fights the operative's own autonomy, report the conflict and stop.
**Never** (besides the common lines): change commander scoring; change existing scenarios' results (golden seeds stay).
**Gate.** Default; smokes `PmcArenaSmoke`, `PmcArenaObjectiveSmoke`, `PmcArenaTacticalSmoke`.
**Allowed paths.** `Source/CodexTacticsDev/Private/Debug/PmcArenaCommand.cpp`, `Source/CodexTactics/{Public,Private}/AI/PmcArena*`,
`Source/CodexTacticsTests/`, the arena JSON under `Content/Data/AI/` or `Content/Data/LevelJson/L_PmcArena*.json` (new keys only).

#### T-04 - Measure what the designer looks at, and pin both clips as red smokes (TEST) - MEDIUM, C++
Owner: Codex (two small parts). Branch `codex/NNN-pmc-position-metrics`. Worktree `CodexTactics-agents`.
**Design gate.** Spec: GDD A-00A, A-00B, hard rules 1-3. Acceptance, part a: the arena's defence block gains
`open_idle_s` (per man: seconds not moving and not in cover / at a post, before first contact and in total),
`on_position_before_contact` (share of defenders at a cover slot or post when the first attacker comes within weapon range),
`crossfire_deg` (largest angle between two defenders' lines to the attackers' centroid at the first shot),
`max_exposed_run_m` (attackers: longest walk segment with no cover slot within 2 m), `first_shot_gap_s`; pure rules with unit
tests. Part b: `PmcCrossroadsSmoke`, `PmcAlleySmoke` and `PmcBridgeSmoke` (A-00C lines 1-4) on the lab maps with the A-00A / A-00B / A-00C checks, registered
`knownFail` with the audit's reason. Proved by: `CodexTactics.AI.PmcArenaBehaviour.*` new tests; the two smokes run and FAIL
on the listed checks, not on a crash or a missing actor. Designer decision needed: none.
**Evidence so far.** The 11 behaviour metrics do not contain any of the designer's criteria; `spread_m` exists (G008).
**Task.** 1. Part a first (no dependency). 2. Part b after T-02 and T-03 are merged. 3. Stop rule: a metric that needs data
the arena does not record (e.g. cover state per frame) - list it, add the smallest record, do not guess.
**Never** (besides the common lines): tune a threshold so today's code passes; reuse `time_in_contact_s` style proxies.
**Gate.** Default; smokes: the two new ones (expected red), `PmcArenaTacticalSmoke`, `PmcRoofDefenceSmoke`.
**Allowed paths.** `Source/CodexTactics/{Public,Private}/AI/PmcArenaBehaviourRules.*`, `PmcDefenceRules.*` (metrics only),
`Source/CodexTacticsDev/Private/Debug/PmcArenaCommand.cpp`, `Source/CodexTacticsTests/`, `Scripts/test_map.json`,
`docs/port/grok/outbox/G002-arena-behaviour-schema.md` (schema note).

#### T-05 - Firing positions that can actually fire: corners, crates, roof edges, windows (GAME) - HEAVY, C++
Owner: Lead (design + review) with an Opus agent. Branch `claude/firing-positions`. Worktree `CodexTactics-claude`.
**Design gate.** Spec: GDD section 5 "Firing positions", F2, hard rule 5; audit 2.1. Acceptance: a query
`PositionsCovering(area, weapon band, n)` returns stand point + stance + facing + covered share for corner slots, low-cover
slots, roof-edge spots, window stands and obstacle tops a mantle reaches (DS-00C option 2); every returned position has a traced line of fire from the muzzle at its firing
stance to >= 1 sample of the area; a marksman placed on any returned roof-edge position of X4 fires at a target standing in
its area. Proved by: unit tests `CodexTactics.AI.FiringPosition.*` (pure scoring F2) + `FiringPositionSmoke` on X4 and
`L_Lab_Alley` (GDD A-03: 100 % of returned positions fire; `overwatch_kills` > 0 on the two X4 seeds 11 / 33 used by G008b).
Designer decision needed: none (the parapet rule for the generator comes out of this task as a finding).
**Evidence so far.** 0 shots from 203 overwatch posts (`worker-overwatch-kills.md`); G008b: "round 2 fix shot through
obstacles (rejected), round 3 safe but 0 shots", round 4 unfinished ("Lind snapped ~5.6 m"); TACTICAL_MAP.md 6: posts are
geometric.
**Task.** 1. Diagnose on one X4 roof why the kneeling / standing shot is blocked (trace from the muzzle; numbers).
2. `FiringPositionRules` (pure): kinds, score F2; `UFiringPositionSubsystem`: candidates from `FindCoverNear`'s slots, the
tactical map's roof spots and windows; validation traces cached per position, re-validated on a geometry event only.
3. Roof edge: the stand point at the rim with a step-to-fire, taken from G008b's `8a2cc2d2` / `78fade83` ideas only where
the trace proves the line. 4. Stop rule: if a roof kind cannot fire from any point, report it as a generator fault (section 4
row 2) and exclude that kind; do not shoot through geometry.
**Never** (besides the common lines): let any shot pass an obstacle to make a metric move; widen `floor_tolerance_cm` /
`SpotRadiusCm`; per-tick traces for all positions (budget: no new frame over the hitch budget, `HitchProbeSmoke`).
**Gate.** Default; smokes `FiringPositionSmoke`, `TacticalPmcSmoke`, `PmcArenaTacticalSmoke`, `PmcFireLineSmoke`,
`HitchProbeSmoke`.
**Allowed paths.** `Source/CodexTactics/{Public,Private}/AI/FiringPosition*` (new), `PmcSquadSubsystem.*` (call sites only),
`Source/CodexTactics/{Public,Private}/LevelGen/TacticalMap*` (read helpers), `Content/Data/LevelGen/tactical_runtime.json`,
`Source/CodexTacticsDev/Private/Debug/`, `Source/CodexTacticsTests/`, `Scripts/test_map.json`.

#### T-06 - A man on a post waits until the enemy is in HIS range, not until the first rumour (GAME fix) - LIGHT / MEDIUM, C++
Owner: Grok (wrote the original; one lane while its budget lasts) or Codex. Branch `grok/GNNN-hold-until-range`.
Worktree `CodexTactics-grok`.
**Design gate.** Spec: GDD F4 "distance is NOT contact"; PMC_DEFENCE P1b. Acceptance: on X4 tactical the posted defenders'
median time on the post rises from ~17 s to "until an enemy is within the post's range or `ambush_max_hold_s` after it";
unposted men unchanged. Proved by: unit test `CodexTactics.AI.PmcCommander.AmbushHoldRange`; `PmcArenaTacticalSmoke` with a
new check line "time on post >= X before the first in-range enemy". Designer decision needed: none.
**Evidence so far.** `PmcCommanderRules.cpp:301` gates Ambush on `!bEverContact`; `PmcSquadSubsystem.cpp:386 / :799` set it
at any distance; 203 posted defenders left after a median 17.2 s with the threat 120-155 m out. Branch
`grok/G008b-overwatch-hold` commits `07c27808` and `089c480e` did this and measured "on post 0.6 -> 12-20 s"; the rest of
that branch (roof firing) is replaced by T-05 and must not be brought over.
**Task.** 1. Re-apply only those two commits on a fresh branch from main (cherry-pick, resolve). 2. Generalise the test to
every post kind with a range (overwatch: the roof's `range_m`; window / ambush: the weapon band's max). 3. Stop rule: half a
day; if the cherry-pick drags the roof-rim code in, stop and report.
**Never** (besides the common lines): touch the firing / muzzle code; change `ambush_max_hold_s`.
**Gate.** Default; smokes `PmcArenaTacticalSmoke`, `PmcRoofDefenceSmoke`, `PmcStalemateSmoke`, `TacticalPmcSmoke`.
**Allowed paths.** `Source/CodexTactics/{Public,Private}/AI/PmcCommanderRules.*`, `PmcSquadSubsystem.*`,
`Content/Data/AI/pmc_commander.json` (one key), `Source/CodexTacticsTests/`.

#### T-07 - Cross open ground cover to cover, and go round a building unseen (GAME) - HEAVY, C++
Owner: Opus agent, Lead review. Branch `claude/pmc-bound-move`. Worktree `CodexTactics-claude`.
**Design gate.** Spec: GDD PL-01, F1, A-01. Acceptance: on a 40 m street with five covers a walking squad member stops only
in cover, never runs more than `bound_max_m` exposed, one mover at a time while a mate has a fire line on the threat sector;
`CoveredRoute(from, to, threats)` returns a path whose exposure is the lowest found or says none exists; the scripted
opponent gains `Script=bound`. Proved by: unit tests `CodexTactics.AI.PmcBound.*` (exposure of a polyline, bound splitting);
`PmcBoundSmoke` (A-01 lines); `max_exposed_run_m` from T-04. Designer decision needed: none.
**Evidence so far.** Advance is one straight walk (`PmcSquadSubsystem.cpp:1958-1972`); flank success 1.54 %; retreats
survived 10.8 %; "the fighters run for a long time" (designer).
**Task.** 1. Diagnose with the T-04 metric on three X4 rounds how long attackers run exposed today. 2. `PmcBoundRules` (pure:
F1, bound splitting over cover slots along a path corridor); the walk executor takes a bound list instead of one goal for
Advance / Objective / FallBack / Flank; numbers in a new `squad_plays.json`. 3. `CoveredRoute`: navmesh path candidates
(direct, both sides round the blocking building) priced by F1. 4. Stop rule: if path candidates round buildings need a
navmesh feature that costs more than two days, ship bounding on the direct corridor and report the route half.
**Never** (besides the common lines): raise sprint speed or give enemies a move the body does not have; change the player's
movement.
**Gate.** Default; smokes `PmcBoundSmoke`, `PmcCommanderSmoke`, `PmcStalemateSmoke` (re-decide and quiet limits unchanged),
`PmcArenaObjectiveSmoke`, `PmcStandoffSmoke`.
**Allowed paths.** `Source/CodexTactics/{Public,Private}/AI/PmcBound*` (new), `PmcSquadSubsystem.*`, `PmcArena*` (script
seam), `Content/Data/AI/squad_plays.json` (new), `Source/CodexTacticsDev/Private/Debug/`, `Source/CodexTacticsTests/`,
`Scripts/test_map.json`.

#### T-08 - The squad sets an ambush on the predicted route: the designer's three clips turn green (GAME) - HEAVY x 2, C++
Owner: Lead (core architecture), Opus agent for the rules + tests. Branch `claude/squad-plays`. Worktree `CodexTactics-claude`.
**Design gate.** Spec: GDD sections 3-4 (layers, hard rules, PL-02, PL-03, PL-05), F2, F3, F4, F8; A-00A, A-00B, A-02.
Acceptance: `PmcCrossroadsSmoke` green (T-08, static anvil), then `PmcAlleySmoke` green (T-08b, height + mobile anvil), then `PmcBridgeSmoke` green (T-08c: the cut-off climbs an
obstacle top - this adds a `MantleTop` entry to `SquadOrderCatalog` so the AI can order the climb, and
`Source/CodexTactics/{Public,Private}/AI/SquadOrderCatalog.*` joins the allowed paths);
`PmcRoofDefenceSmoke` still green with the roof defence running as PL-05 inside the layer; with no kill zone found the squad
still occupies positions (PL-02) - `open_idle_s` <= 2 s per man before contact on X4 tactical. Proved by: unit tests
`CodexTactics.AI.SquadPlan.*` (kill-zone score, role assignment with spacing, retreat prediction, opening-fire rule) and the
three smokes; three clips re-recorded for the designer. Designer decision needed: none - OQ-2 was
decided on 2026-11-19 (option B, which is this task).
**Evidence so far.** Audit 2.4 (no squad layer), 4.1 (both clips), 0 matches for crossfire / cut-off in `Source/`.
**Task.** 1. `PmcSquadPlanRules` (pure): F8 over the tactical map's chokepoints and open crossings + the positions of T-05;
F2 role assignment; F3; F4. 2. A plan object per team in `UPmcSquadSubsystem`: play, kill zone, role -> fighter -> position;
re-planned on events only (spawn / objective set, casualty, position lost, the enemy's route changed); the fighter brain
keeps its utility but orders outside the role score 0 until the play releases him. 3. PL-02 and PL-03 static first; G008's
posts become PL-05 data, behaviour unchanged. 4. T-08b: the height role through T-05's roof positions and the access; the
mobile anvil through T-07's `CoveredRoute`, sent only if he arrives before the enemy. 5. Stop rule: if the role constraint
breaks the commitment rules (stalemate smoke), stop and report before loosening anything.
**Never** (besides the common lines): add knowledge the squad did not see, hear or get from its post data; teleport or
pre-place fighters on positions (they walk there); win the smoke by lowering the attackers' health / accuracy.
**Gate.** Default; smokes `PmcCrossroadsSmoke`, `PmcAlleySmoke`, `PmcRoofDefenceSmoke` (3 seeds), `PmcStalemateSmoke`,
`PmcArenaTacticalSmoke`, `PmcEncounterSmoke`, `TacticalPmcSmoke`; remove the two `knownFail` marks only when green on 5 seeds.
**Allowed paths.** `Source/CodexTactics/{Public,Private}/AI/PmcSquadPlan*` (new), `PmcSquadSubsystem.*`,
`PmcCommanderRules.*` (the role gate only), `PmcDefenceRules.*`, `Content/Data/AI/squad_plays.json`,
`Source/CodexTacticsDev/Private/Debug/`, `Source/CodexTacticsTests/`, `Scripts/test_map.json`, `docs/systems/pmc-squad-ai.md`.

#### T-09 - The tactical map knows buildings, storeys, ground positions round goals and open crossings (TOOL) - MEDIUM, TS
Owner: Antigravity. Branch `gemini/NNN-tactical-export-v2`. Worktree `CodexTactics-gemday`.
**Design gate.** Spec: GDD section 5 "Sources", F8 candidates; audit 2.1. Acceptance: `codex.tactical_map/2` adds, without
removing a `/1` field: `building` + `floor` on windows, roof spots, hiding spots and chokepoints where one applies; the access
(id, kind, top point) on every walkable roof; `open_crossings[]` (route segments >= 10 m with no cover within 2 m of the
line, with the cover objects that see them); `goal_sites[]` (per mission goal: cover objects within 25 m with the sector
each covers). The C++ loader reads `/2` and still reads `/1`. Proved by: `tacticalMap.test.ts` new cases; `TacticalMapSmoke`
on X4 with the new counts; regenerated files for X2 / X4 / X6 / the lab maps committed (not the designer's modified X6
files - see Never). Designer decision needed: none.
**Evidence so far.** TACTICAL_MAP.md 6 "not done" list; building-stories GDD 1 ("places carry no building id");
`guard_share` None on ground goals.
**Task.** 1. Diagnose which analysis data already carries the building / storey. 2. Export + loader + tests. 3. Stop rule:
`goal_sites` sectors that need line traces in TS - export the geometry only and leave validation to T-05.
**Never** (besides the common lines): overwrite `Content/Data/LevelGen/**/L_Gen_Depot_1_X6*` or the accepted
`gen_ref_01_02_depot_1_x6*` files while the designer's uncommitted edits exist (ask the lead); change analysis thresholds.
**Gate.** Training Ground typecheck + `npm test`; build; `Scripts/test.ps1 -Filter CodexTactics.LevelGen`; smokes
`TacticalMapSmoke`, `TacticalPmcSmoke`, `MissionGoalsSmoke`.
**Allowed paths.** `Tools/TrainingGround/src/levelgen/tacticalMap*`, `Tools/TrainingGround/scripts/export-tactical-map.ts`,
`Source/CodexTactics/{Public,Private}/LevelGen/TacticalMap*`, `Content/Data/LevelGen/tactical/` (not X6),
`Source/CodexTacticsTests/`, `docs/design/TACTICAL_MAP.md` (section 8 "v2").

#### T-10 - No night run starts on dead metrics; the night machine runs scenario regression instead (TOOL) - MEDIUM, Python
Owner: Antigravity, with Worker-ops for the schedule. Branch `gemini/NNN-training-gate`. Worktree `CodexTactics-gemday`.
**Design gate.** Spec: GDD section 9 (gate G1-G5), F7. Acceptance: `night_trainer.py --check-gate` exits non-zero and names
the reason when (a) a metric used by fitness / descriptors is constant over a 6-round probe, (b) no baseline with >= 30
rounds per scenario exists for the freeze commit, (c) a listed smoke is not green in the last sweep; `run_night_trainer.ps1`
refuses to start without a fresh gate file; `Scripts/Tools/scenario_regression.py` runs a list of scenario smokes on N seeds
on the worker and writes pass rates + the worst seeds (markdown + JSON); a quick day mode (`--scenario X --seeds 20..50`,
target < ~15 min) prints a short digest for the day loop of section 5a. Proved by: `Scripts/Trainer/tests/test_gate.py`
(the 2026-11-19 config with `overwatch_kills` 0 and `guard_share` None is refused - fixture from that night). Designer
decision needed: none - OQ-10 was decided on 2026-11-19: evolution is paused and the worker runs scenario regressions at
night; this task delivers that runner and the gate that must pass before evolution may resume.
**Evidence so far.** A 14.5 h run on two dead metrics (night 2026-11-19); 6 rounds per candidate, promotion margin 0.
**Task.** 1. Gate checks + tests. 2. The regression runner over `smoke_parallel.ps1` with smoke arguments (`seed=N` - today
smoke_parallel cannot pass them: G008 report). 3. Stop rule: if passing a seed needs a registry change, add an optional
`args` field, nothing else.
**Never** (besides the common lines): start a trainer run; change fitness weights or QD axes (Phase 8).
**Gate.** `python -m unittest discover -s Scripts/Trainer/tests`; a dry run of the gate on the committed config; triage audit.
**Allowed paths.** `Scripts/Trainer/` (gate, tests, fixtures, `run_night_trainer.ps1`), `Scripts/Tools/scenario_regression.py`,
`Scripts/smoke_parallel.ps1` (the `args` pass-through only), `docs/tools/night-trainer.md`.

#### T-11 - Generator-placed squads stand where the generator put them and know their own charges (GAME) - MEDIUM, C++
Owner: Opus agent. Branch `claude/pmc-posts-in-game`. Worktree `CodexTactics-claude`.
**Design gate.** Spec: PMC_DEFENCE D2 (designer 2026-11-18 evening); GDD hard rule 4. Acceptance: a level JSON squad with
`post` spawns on it (posture defend), facing its yaw, with no random ring and no delay; charges of the `pmc_charges` props
layer belong to the PMC: its squads never path over them and are not hurt by walking near them unless triggered by the
player's side; the player's squad spots them by the normal 3 m rule. Proved by: unit test for `ParseSquad` (`post`), new
`PmcPostsSmoke` on X4 (after the lead accepts the «ЧВК» layer for X4 on the Training Ground page and rebuilds), `PeekMinesSmoke`
unchanged. Designer decision needed: **OQ-21** for the charge half (the spawn-at-post half has none).
**Evidence so far.** 087 report "Left": `ParseSquad` does not read `post`; `owner: "pmc"` is not read
(`PropsGameplayRules.cpp:57` treats the layer as ordinary mines); no accepted layout has a `pmc` block.
**Task.** 1. Spawn at post. 2. Ownership: the props spawn tags the charge with the owning side; path cost / avoidance for
that side. 3. Stop rule: if ownership needs a change in the deployable base class larger than a day, ship spawn-at-post and
report.
**Never** (besides the common lines): change the player's mine rules; hand-edit accepted layouts or level JSON of existing maps.
**Gate.** Default; smokes `PmcPostsSmoke`, `PmcEncounterSmoke`, `PmcEncounterHuntSmoke`, `PeekMinesSmoke`,
`GeneratedPropsX6Smoke`.
**Allowed paths.** `Source/CodexTactics/{Public,Private}/AI/PmcEncounter*`, `Source/CodexTactics/{Public,Private}/LevelGen/Props*`,
`Source/CodexTactics/{Public,Private}/Interactables/Deployable*` (owner tag only), `Source/CodexTacticsDev/Private/Debug/`,
`Source/CodexTacticsTests/`, `Scripts/test_map.json`.

#### T-12 - Can an enemy fighter actually walk every stair, ladder, fence and drop of a generated map? (TEST) - MEDIUM, C++
Owner: Codex. Branch `codex/NNN-pmc-traversal-smoke`. Worktree `CodexTactics-agents`.
**Design gate.** Spec: GDD section 2 body verbs (P3 "the same body"), hard rule 5; audit 2.2 UNVERIFIED rows, 2.6.
Acceptance: `PmcTraversalSmoke` on X4 and on `L_Lab_Alley`: one hostile operative (team 1, commander off) is ordered across
every stair flight, ladder (>= 7 rungs), mantle link and drop link in turn; the smoke prints per link kind "N of M crossed"
and the ids that failed with the reason (no path / stuck / refused), and passes at >= 95 % per kind. If today's rate is
lower it is registered `knownFail` with the numbers - the fixes are separate tasks. Proved by: the smoke itself.
Designer decision needed: none.
**Evidence so far.** `LevelGenRampSmoke` red on the 2026-11-18 sweep (stuck at the top of `B_warehouse_stair_0`); test X6:
17 of 18 flights for the player's squad; no test moves a hostile operative over mantle links or window sills; a roof goal
once got a 2-point straight path (pmc-squad-ai.md:630).
**Task.** 1. Reuse the probes of `LevelGenRampSmoke` / `LevelGenLaddersSmoke` / `MantleFenceRouteSmoke` for a team-1 operative.
2. Report the table. 3. Stop rule: no game fixes in this task; a crash is reported with the stack and the link id.
**Never** (besides the common lines): lower the 95 % line; skip a link kind that fails.
**Gate.** Default; smokes `PmcTraversalSmoke`, `LevelGenRampSmoke`, `LevelGenLaddersSmoke`.
**Allowed paths.** `Source/CodexTacticsDev/Private/Debug/` (new smoke file), `Scripts/test_map.json`, the report.

## 7. Risks

| Risk | Effect | Mitigation |
|---|---|---|
| The squad layer fights the existing utility (order churn, stalemate smokes) | Phase 3 slips | the role gate zeroes scores instead of adding orders; stalemate smokes stay in every gate; static anvil first |
| Roof edges cannot fire on the current parapet geometry | DS-00B, DS-02 blocked | T-05 reports it in week 1; the generator gets a firing-step rule (section 4); until then the height role is not assigned (hard rule 5) |
| Buildings arrive later than Phase 5 needs | the designer's window / upstairs wishes wait | Phases 3-4 deliver the two clips and street / roof fights meanwhile; say this to the designer with OQ-1 |
| Budget: Grok ends about 11-23, no API tokens, Codex light only, the lead at ~50 % | week-1 lanes thin out | T-06 is small and Codex can take it; the critical path (T-05, T-07, T-08) sits on Claude agents only |
| The main PC format (announced, then postponed 2026-11-19) | builds / maps stop until it is back or the worker takes over | Antigravity + Codex lanes continue; T-02's layouts are authored first, the Unreal build follows on whichever machine builds |
| Pressure to restart night training early | another wasted night, wrong conclusions | the gate (T-10) refuses; the worker has scenario regression to do |
| Scenario smokes pass but clips still look wrong | false confidence | every phase ends with clips to the designer; his "no" adds an A-nn line, not a tuning run |
| New enemy behaviour is too lethal for the player | frustration | fairness pass (Phase 7) is designed now (GDD 6); first volley rule OQ-4; the bound and tells are tested on both sides (A-00A line 6) |
