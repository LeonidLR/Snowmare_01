# 05 — Producer plan: Tester Demo #1 «Sector-17 Perimeter» (2026-10-11)

**Feasibility verdict: CONCERNS.** The tutorial demo can start tomorrow. A *tester-ready* drop is realistic around **Mon 2026-11-02 (slip window to 11-06)**, not this week, for two reasons: nothing has ever been packaged, and the user alone builds the map, the WBPs and the art. To keep momentum, Sprint 1 ends with an internal **Drop 0**: today's build, packaged, with a controls cheat sheet, sent to 1–2 trusted people as a packaging rehearsal.

## 1. Synthesis: what Demo #1 is

**Demo #1 «Sector-17 Perimeter»** is one continuous level of **~35 min** for a first-time player, on a new map **`L_Demo01`** that follows the Stage 01 «Bunker Gate» layout (drop point → yard → guard lane → firing pad → gate arena → airlock). It teaches **one verb per beat** through non-blocking hint cards: the key goes on the card, the reason comes from an in-fiction HQ/squad radio line. A Welcome card offers *Guided* or *I know the game*. There is a per-beat checkpoint, a gentle `demo_01.json`, and a debrief with a feedback prompt. All text is English and goes through the narrative sheet pipeline (the CD's Russian-strings idea is overruled by the 2026-10-08 decision). Where the briefs differed, this plan settles it as follows:
- **Map:** new map, not L_MovementTest (UX proposed L_MovementTest).
- **Length:** ~35 min (UX said 20, LD 40).
- **Turn-based:** IN and forced once (QA wanted it deferred), provided the TB rule bugs are decided first.
- **Marksman:** OUT (CD + QA; LD had put him in the patrol).
- **Brute:** finale only.
- **Waves:** 2 waves, not 3. Wave 1 is prepared, wave 2 is a sudden start with the flyover.

| # | Beat (zone) | Mechanic taught (one main verb + support) | ~min |
|---|---|---|---|
| 0 | Briefing (frontend NEW GAME) | Dialogue window, objective tracker; intro trimmed to ≤6 lines; Welcome card | 1.5 |
| 1 | Landing / Move out (Z1 trench, cold ×0.3) | Camera WASD/Q-E/wheel, click move, double-click sprint, select 1–3 / box, stance Z/C/V, vault | 3 |
| 2 | Cold and fire (Z2a windy corridor) | Cold bar + vignette, **barrel + match**, heat pack U *(only if the user confirms the Sprint 15 design)* | 3 |
| 3 | Search and salvage (Z2b) | Crate loot, **mine reveal + Medic-Sapper defuse**, trapped crate, drawer / drag-drop hand-over | 3 |
| 4 | Silent patrol (Z3 guard lane) | **Vision cones**, crouch/prone, noise by gait, real-time cover (click wall), ambush on contact. Enemies: Frostbitten + Hound only | 5 |
| 5 | First contact / kennel (Z4) | **Priority target LMB, Ctrl+click barrel**, grenade G | 3 |
| 6 | Stop the clock (Z4 flat pad ≥ 21×21 m) | **Space tap = pause and plan; hold = Gorky grid** (8 AP, Tab, Enter, shot, grenade). Forced once. Pack: 4 Frostbitten + 2 Hound from 40 m | 5 |
| 7 | Power the gate (Z2c → Z5) | Quest chain canister → APC → generator (warm zone, powers the turret) → terminal | 4 |
| 8 | Prepare the line (Z5) | F deploy, R rotate, barricade + turret, F5 save; wave 1 is *prepared* (countdown / Start) | 3 |
| 9 | The Gate (Z5, full cold) | Synthesis. W1 Frostbitten + Hound; rest; W2 sudden + flyover + **Brute** (knockdown, Z get up) | 6 |
| 10 | Airlock debrief (Z6) | Victory + stats, EXP/profile glimpse, Sector-17 hook, «Thank you, tester», feedback / F8 | 2 |

**IN:** the beats above, F1 controls screen, F8 report, build stamp, checkpoints, Panic OFF, vision cones ON (set by the demo game mode, not by the global BP defaults).
**OUT (demo #2+):** Marksman, Cutter, Spitter, cryo drone, campfire / stove / flare / fire puddle, Susanin, the 4-min mid-fight horde (`horde_enabled:false`), stages 2–12, the base / level-up phase, tripwire lesson, gamepad gameplay, an upload server.

## 2. Milestones

| M | Goal | Key deliverables | Owners | Depends on | Exit criteria | Effort / target |
|---|---|---|---|---|---|---|
| **M0** Tester-build readiness | A reproducible, safe Win64 package | User commits WIP + tag; disk freed; `Scripts/package.ps1` (BuildCookRun Development); `MapsToCook`, `DirectoriesToAlwaysCook` (/Game/VFX, /Game/Data, /Game/UI, specific M4 clips only), UFS `Data/AI` + `Data/Weapons`; bot gated `!UE_BUILD_SHIPPING`; Shipping compile-only; console off; build stamp `0.1.0+n-hash` + `BuildInfo.json`; F8 report v0 + `SendReport.bat`; corner-aim log spam gated; `package_smoke.ps1` log scan; README_TESTERS draft | Claude; User (commit, disk, 2nd PC); Gemini (scalability, min spec) | D7, D8 | Package from a tagged commit boots on a clean PC → menu → map with no `Failed to load` / `EN missing` / ensure; Shipping links; Drop 0 sent | Claude 4–5 d · **Oct 12–16** |
| **M1** Tutorial framework | Data-driven hints that any beat can use | Command registry + `OnPlayerCommand` choke point; `UTutorialSubsystem` (JSON steps, triggers volume / event / after_step, completion by command id, timeout, skip, once, profile file, `tutorial.jsonl`); `ATutorialTriggerVolume`, `ATutorialMarkerActor`, `ABeatBarrier` (soft); `UTutorialOverlayWidget` (hint card + ≤3-line tracker + radio strip, fallback look until the WBP exists); F1 `Screen_Controls`; pause-menu TUTORIAL ON/OFF/RESET; per-beat checkpoint autosave + «Retry from checkpoint» on failure; debrief / feedback card; demo HUD preset; Panic toggle; vision-cone override; `GetTutorialText` | Claude; User (WBP look, later); Gemini (sheet tab) | M0 partial, G-17 | `CodexTactics.Tutorial.*` + `TutorialSmoke` + `DemoSaveResumeSmoke` green; steps fire on L_PatrolTest-style test setup; `[EN missing]` fallback works | Claude 8–10 d · **Oct 12–23** |
| **M2** L_Demo01 graybox + beats wired | Walkable start-to-end level | User: graybox Z1–Z6 to the beat sheet metrics, actors placed, navmesh, BP game mode. Claude: beat sheet + metrics, step JSON, triggers / markers data, `DemoMapLint` smoke (required tagged actors per beat), `DemoBeatSmoke` | **User** (layout); Claude (wiring) | D1, M1 actors | PIE run start → debrief, all beats fire once and in order; lint green; bot reaches the end | User 6–8 d, Claude 3–4 d · **Oct 13–28** |
| **M3** Content + tuning | Fair, clear, readable | `demo_01.json` (2 waves, `cold_drain_mult` ≤ 1.0, wave 1 prepared); bot `TUTORIAL` profile; 20 bot runs at **85–90 % wins, 0 stuck**; writer text in, intro trimmed; TB fixes per D3; ≥30 fps finale on a mid PC (packaged); user art pass on key props / WBPs | Claude; Writer; User; Gemini (#16, marker material) | M2, D3, G-17 text | Bot target met; no `[EN missing]`; manual checklist dry run passes | Claude 4–5 d, Writer 2–3 d · **Oct 26–30** |
| **M4** Tester drop #1 | Feedback from real players | Tagged package, QA manual checklist (45–60 min, packaged build, clean PC), README (spec, controls, known issues), questionnaire, private link | User (sign-off, distribution); Claude (package, checklist, triage digest) | M3 | 5–8 testers received it; first report zips triaged into a bug list within a week | 2 d · **Mon Nov 2** |

## 3. Sprint 1: Mon 12 Oct – Sun 18 Oct

**Goals:**
1. M0 done and Drop 0 packaged.
2. Every actor the user needs to place exists by Wednesday.
3. Beat sheet delivered on day 1.
4. Tutorial core running on a test map.

The ★ tasks are the critical path. 20 % of capacity is held back as buffer.

**User: start TODAY in the editor**
1. Answer D1–D8 (at minimum D1, D2, D3).
2. Approve Claude's backup-and-hash, then commit and tag your WIP assets (U1).
3. Make `L_Demo01` (D1): *Save Current Level As* from the committed L_MovementTest, or start a new level with PlayerStart, NavMeshBounds and floor.
4. Block out Z1–Z3 with cubes from the LD brief:
   - trench and low vault wall < 130 cm
   - windy corridor with 2 barrels
   - yard with 3 crates and 1 mine
   - guard lane ~40 m with high cover ≥ 180 cm
5. Reserve a **flat 21×21 m pad** for the grid (14 × 1.5 m cells).
6. Copy-paste the kit actors (barrels, crates, mines, quest objects, gate + `CombatStart`) **out of** L_MovementTest without editing the original.
7. Put the 4 `EnemySpawnPoint`s beyond the gate.

| ID | Task | Owner | Est | Deps | Acceptance |
|---|---|---|---|---|---|
| U1 ★ | Commit + tag WIP (BP_Operative, BP_Enemy_Marksman, L_MovementTest, L_PatrolTest, DefaultEditor.ini, Rifle2 deletion) | User | S | D8 | `git status` clean of these files; tag `demo-base-0` |
| U2 ★ | Free disk: approve the audit §6b deletions (~4.8 GB) | User | S | — | ≥ 30 GB free before the first cook |
| U3 ★ | Graybox Z1–Z3 + grid pad in `L_Demo01` | User | L | D1, C1 | Leader walks Z1 → Z4 on navmesh |
| C1 ★ | Beat sheet + graybox spec `docs/demo/DEMO01_BEAT_SHEET.md`: zones, sizes in m, cover heights, sightlines, required actors / tags / cold per beat, enemy placement | Claude | S | — | Delivered on day 1; the user can block out from it alone |
| C2 ★ | `ATutorialTriggerVolume` + `ATutorialMarkerActor` (placeable, StepId / BeatId, works before the subsystem lands) + `ABeatBarrier` stub | Claude | M | — | Placeable in the editor by Wed; unit test for the data properties |
| C3 | Per-zone cold: check `ACameraZoneVolume` (enum multiplier only) or add `AColdZoneVolume` (float multiplier) | Claude | S | — | Smoke: ×0.3 zone slows the drain |
| C4 ★ | Package spike: `package.ps1`, cook / stage settings, first Development package, `package_smoke.ps1` | Claude | L | U1, U2 | Package boots to the menu; log scan clean or a list of issues |
| C5 ★ | Bot gate (`ShouldCreateSubsystem`) + `!UE_BUILD_SHIPPING` guards + Shipping compile-only | Claude | M | — | BotStealth / Repair / Marksman smokes pass; Shipping links |
| C6 | Build stamp (ProjectVersion, git hash, BuildInfo.json, menu + HUD corner) + console off in tester builds | Claude | S | C4 | Visible in the package |
| C7 | Command registry + `OnPlayerCommand` choke point (behaviour unchanged) | Claude | M | — | Full `test.ps1` + input smokes unchanged |
| C8 ★ | `UTutorialSubsystem` v0 + `demo_01_steps.json` schema + `CodexTactics.Tutorial.*` rules tests | Claude | L | C2, C7 | 3 sample steps run end-to-end in a smoke |
| C9 | Minimal hint card + objective tracker (C++ base, fallback look, BindWidgetOptional for the future WBP) | Claude | M | C8 | Card shows / confirms / hides; does not take clicks |
| C10 | Demo game-mode flags: Panic OFF, vision cones ON, tutorial ON, demo HUD preset (hide debug squad panel / labels) | Claude | S | D5 | Smoke checks each flag |
| C11 | `demo_01.json` draft (Wave Editor) + first 10 bot runs on L_MovementTest | Claude | S | — | Jev digest posted; baseline win % |
| C12 | F8 report v0 + `SendReport.bat` + corner-aim log spam gated (B12) | Claude | M | C4 | Bundle has a non-black screenshot, save, log tail, report.json |
| C13 | File Gemini requests #17–#19 + reprioritise #15 / #16 in TANDEM; update HANDOFF / PORT_MATRIX | Claude | S | D-answers | Rows exist |
| G1 | Tutorial tab + `tutorial` manifest section (request #17) | Gemini | M | C13 | Sample ids sync to `narrative_manifest.json` |
| W1 | Draft text for beats 0–3 (cards, radio, trimmed intro) into the sheet | Writer | M | G1 | Text exists for ~12 ids |
| — | **Drop 0** (Sun): Drop-0 package + cheat sheet to 1–2 trusted people | User + Claude | S | C4–C6 | Boots on their PC; notes come back |

**Sprint 2 preview (19–25 Oct):**
- Controls screen
- Checkpoints + retry
- Debrief card
- Telemetry T1 / T3
- Wiring beats 4–10
- `DemoMapLint` / `DemoBeatSmoke`
- TB fixes per D3
- User: graybox Z4–Z6 and the WBP look.

## 4. Risk register (top 6)

| # | Risk | P | I | Mitigation |
|---|---|---|---|---|
| 1 | First cook / package fails, or dynamic `/Game/` loads come back null in the package and fail silently | High | High | Package spike on day 1–3 (C4), log-scan smoke on every package, Drop 0 rehearsal; no content freeze until the package is clean |
| 2 | Map, WBPs and art are all on the user: the graybox becomes the critical path | High | High | Graybox is acceptable for the drop; beat sheet with metrics on day 1; actors placeable by Wed; `DemoMapLint` tells the user what is missing; reuse the L_MovementTest kit by copy |
| 3 | Testers freeze or die and quit (bot: 16/19 losses to cold) | High | High | `demo_01.json` (2 waves, drain ≤ 1.0, no horde); cold zones; checkpoints + downed-not-dead in lessons; bot TUTORIAL 85–90 % gate |
| 4 | TB rule bugs (double armour / stance, wall cover ignored in TB) show up in the forced TB lesson | Med | High | D3 decided in Sprint 1; teach cover in real time only; no numbers in the tutorial text; TB smokes re-pinned |
| 5 | Performance / hardware floor (DX12 SM6, horde-era scans, package size) | Med | Med | Small waves, no horde; perf telemetry T3; Insights capture of the finale in the package (Gemini); README min spec; texture budget for a < 6 GB zip |
| 6 | Scope creep and review backlog (heat, knockdown, horde, sniper all wait on the user), plus one worktree and a nearly full disk | High | Med | Demo-only sprints with feature work frozen; review queue triaged into IN / OUT of the demo (D5); one agents worktree; free disk before the cook |

## 5. Decisions needed from the user (studio recommendation in bold)

1. **Map.**
   - (a) A new L_Demo01 from scratch.
   - (b) **L_Demo01 = Save-As of the committed L_MovementTest (gate arena + quest kit), extended with tutorial zones Z1–Z4.** This is the fastest route to a playable level, and the original stays the test bed.
   - (c) Reuse L_MovementTest directly. Not recommended: the smokes and your baseline depend on it.
2. **Length, scope and date.**
   - (a) **~35 min, the 11 beats above, drop Mon Nov 2.**
   - (b) A 20-min core loop with no TB, stealth or Brute (QA's cut). Earlier drop, but it misses the Gorky identity.
   - (c) 45+ min with everything. Slips into mid-November.
3. **Turn-based in the demo and the audit rule fixes.**
   - (a) **TB forced once (beat 6, skippable in «I know the game»).** Before the drop: fix the clear double-application bugs (armour twice, stance twice, dodge on blasts); unify the mode mismatches (crouch 0.75 / 0.70, barrel and mine damage) after a Godot parity check that Claude prepares; you pick the numbers. **Wall cover in TB: absorbs as in real time**, so that what the tutorial teaches holds in both modes.
   - (b) TB prompted only, never forced.
   - (c) TB out of demo #1.
4. **Failure model.**
   - (a) **Beats 1–6: a «lethal» hit downs the specialist, who is revived at the checkpoint. Permanent loss and the Commander-only defeat apply in the finale only. Per-beat checkpoint autosave plus «Retry from checkpoint».**
   - (b) Current rules (permanent loss, full restart).
5. **Systems on or off for the demo.**
   - **Panic / Rage OFF; vision cones ON (set by the demo game mode, not the BP defaults); Marksman OUT; Susanin OUT; mid-fight horde OUT.**
   - **Sprint 15: heat pack IN only if you confirm its design; campfire / stove / flare / fire puddle OUT.**
6. **Known rough behaviour before the drop.**
   - **Hound melee reach: let hounds close to bite range (fix).**
   - **Knockdown and horde flyover: confirm the current tuning** (both feature in the finale).
   - Otherwise: keep hounds out of beats 1–5 and list them in known issues.
7. **Testers and build.**
   - **5–8 trusted testers (2–3 strategy fans, 1–2 non-gamers).**
   - **Private zip via Google Drive or a restricted itch.io page.**
   - **Win10/11, DX12 SM6 GPU (RTX 2060 class), keyboard + mouse.**
   - **Development build with the console off, then Shipping + logging from drop 2. Telemetry local only, with a one-line notice.**
8. **M0 housekeeping.** Do you approve:
   - (a) committing and tagging your WIP assets now, with Claude's backup + hash check (or you commit them yourself)?
   - (b) retiring `ABP_Operative_Rifle2`, which makes ~285 MB plus its code dead?
   - (c) the audit §6b `Saved/` deletions?
   - **Recommended: yes to all three.**

## 6. Gemini requests to file in TANDEM (Claude files them in C13)

- **#17 Tutorial text pipeline:**
  - a Tutorial tab in the narrative Google Sheet (`StepID, Kind, Title_EN, Text_EN, Radio_EN`)
  - a `tutorial` section in `narrative_manifest.json` via `sync_narrative.py`
  - the intro (`DA_DialogueIntro`) trimmed to ≤6 lines
  - debrief and Sector-17 hook lines, coordinated with the scriptwriter

  Claude adds the reader `GetTutorialText`.
- **#16 Vision cone material:** raised to **high** because cones are ON in the demo and carry the stealth lesson.
- **#18 Tutorial marker look:** a world beacon / ground ring / arrow material for `ATutorialMarkerActor`, plus an amber «warm zone» ground read for the generator and lit barrels (the art bible's navigation-by-warm-light idea).
- **#19 Tester-build rendering:**
  - commit the scalability presets (`DefaultScalability.ini`)
  - confirm the minimum spec (SM6)
  - set a texture budget for a < 6 GB package
  - make an Insights capture of the beat-9 finale in the packaged build
- **#15 Heat VFX:** lowered to demo #2, except a quick check that the barrel fire and generator glow read well.
- Optional, after M4: the TB «grid draws in» reveal effect for beat 6; hint and confirm UI sounds (owner to be confirmed: Gemini or audio).

Выполнено моделью: Claude Opus 5.5 (claude-opus-5-5)
