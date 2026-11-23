# 074 - Session recording (`Codex.Demo.Record`): the designer's orders as imitation-learning data

Task #45 (`pl-session-record`). Base plan: `docs/port/gemini/outbox/016-map-45-session-recording.report.md` - THIS file overrides it where
they differ (JSONL, 60 s rule, state captured BEFORE the order runs, PMC order label, context gate). Goal: every human PIE session leaves
a local log of the designer's orders and each operative's situation, so a later converter can build (observation, action) pairs for the
Learning Agents policy (`git show ai/la-probe:docs/systems/pmc-policy-probe.md`). No LearningAgents code here: do not link the plugin,
do not touch branch `ai/la-probe`, do not write the converter.

## Branch / rules
- `Scripts/Tools/new_agent_worktree.ps1 -Name cx074 -Branch codex/074-session-recording -From main`; `$env:CODEX_AGENT = "codex"`.
- Small commits, explicit paths, no push, no merge (Claude reviews + merges). At 10 % of your limit left: stop, commit, report, wait.
- Never touch `Content/Maps/L_MovementTest.umap`, `Config/DefaultEditor.ini`, `RunTelemetrySubsystem.*` (read it as the pattern only).
- Recording is READ-ONLY for gameplay: no RNG use, no state written outside the new subsystem, no change to any order's behaviour.

## What to build
1. `SessionRecordingRules` (pure, no UObject / world access): `Public/Telemetry/SessionRecordingRules.h` + `Private/Telemetry/...cpp`.
   POD structs, named constants (`MinSessionSec = 60`, `SnapshotIntervalSec = 1`, `MaxThreats = 12`, `MoveDeltaCm = 300`, `FlankDeg = 45`,
   `MaxSessionSec = 14400`) and the functions `ShouldRecord`, `ShouldDiscard`, `MakeDemoFileName`, `MapToPmcOrder`,
   `ThreatDistanceBearing`, `SerializeSession` (all described below).
2. `USessionRecordingSubsystem : UWorldSubsystem` (`Public/Telemetry/SessionRecordingSubsystem.h` + .cpp): lifecycle, buffers, the hook
   API (`static USessionRecordingSubsystem* GetIfRecording(const UObject* WorldContext)` - null when off, so a hook costs one lookup),
   RAII `FDemoOrderScope`, `FDemoSuppressScope`, `SuppressSession(FName Why)`.
3. Hooks in `ACodexTacticsPlayerController` (list below). 4. Tests. 5. `docs/systems/session-recording.md` (schema + mapping table, <= 60 lines).

## Switch and gate
`Codex.Demo.Record` (int, default 1): 0 = never; 1 = auto; 2 = force (any game world; for manual checks). Auto records only a human
session - `ShouldRecord(FDemoGate)` is true when ALL hold: game world (PIE or Game), not `-unattended`, not `-nullrhi`, not
`-NoTelemetry`, not `IsRunningCommandlet()`, not `GIsAutomationTesting`. So: on in editor PIE, off in smokes / `-game` batch runs.
Checked again at flush - the session is dropped if the cvar is 0 then, if `URunTelemetrySubsystem::GetTesterProfile() != "HUMAN"`
(playtest bot), or if `SuppressSession` was called. Add ONE line `SuppressSession("arena")` at the start of the arena command
(grep `CodexTactics.PmcArena`). Log exactly one line per world: `[DemoRecord] off (<reason>)` or `[DemoRecord] recording`.

## File
`Saved/Demos/<YYYY-MM-DD>_<Map>_<HHMMSS>.jsonl` (local time; map name without the `UEDPIE_n_` prefix; characters outside
`[A-Za-z0-9_-]` become `_`), UTF-8 without BOM, one JSON object per line, written ONCE at session end (`Deinitialize` / world tear-down)
from plain buffers. `ShouldDiscard` = duration < 60 s OR zero accepted orders OR suppressed -> nothing is written, one log line says why.
Local only: no network, no user / machine name, no absolute paths inside the file. `Saved/` is git-ignored - verify with
`git check-ignore Saved/Demos/x.jsonl`; never commit a demo. Never delete existing demos. Positions rounded to 1 cm, angles to 0.1 deg.

Lines, sorted by `t` (game seconds since session start; `rt` = real seconds, keeps running in the tactical pause):
- `{"type":"header","v":1,"session":"<file stem>","map":..,"started_utc":..,"world":"PIE|Game","units":"cm,deg,s",`
  `"squad":[{"id","role","hp_max","weapon","clip_max"}],"flags":{"immortal":bool,"infinite_ammo":bool},"goal":[x,y,z]|null}`
  (`flags`: grep `Immortal` / `InfiniteAmmo` for the combat lab's switches; if not readable write `{}` and say so in the report).
- `{"type":"order","t","rt","kind","pmc_order":"TakeCover"|null,"unmapped":"<reason>"|"","ok":bool,"plan":bool,"mode":"solo|formation",`
  `"actor":id,"sel":[ids],"sprint":bool,"dest":[x,y,z]|null,"cover":{"p","yaw","h"}|null,"target":{"id","arch","p"}|null,`
  `"facts":{"thr_d","thr_d_new","flank_deg","goal_d","goal_d_new"},"ops":[...],"threats":[...]}`
- `{"type":"snap","t","rt","mode","formation":"<EFormationKind>","paused":bool,"autonomy":bool,"ops":[...],"threats":[...]}` every 1 s.
- `{"type":"mode","t","rt","what":"solo|autonomy|cease_fire|pause","on":bool}`
- `{"type":"footer","duration_s","orders","orders_ok","snaps","result":"VICTORY|DEFEAT|ABORTED","alive","truncated":bool,"mark":""}`
  (result from the same game-flow delegate RunTelemetrySubsystem binds; `mark` = last value of console `Codex.Demo.Mark <text>`).

`ops` entry (every squad member): `id, alive (0/1), p[x,y,z], yaw, st (0 stand / 1 crouch / 2 prone), mv (moving), cov (in cover), hp,
clip, res (reserve rounds, current weapon), sel (selected), lead (formation leader), thr_n (count of squad-visible enemies), thr_d (2D cm
to the nearest of them, -1 = none), thr_b (its bearing off his facing yaw, -180..180, + = right, 0 = none), goal_d (2D cm to the active
mission goal, -1 = none), lo (his last order: the `pmc_order` name, else the `kind`, "Follow" for a formation follower, "none"), lo_t`.
`threats` entry (the <= 12 squad-visible enemies nearest to the squad centre): `id, arch, p[x,y,z]`.
Sources - use existing getters only, add no gameplay state: `AOperativeCharacter::GetStance / IsMoving / GetCoverSlot / GetReserve`,
`USquadSubsystem::IsSoloMode / GetFormationLeader / GetFormationKind`, `UTacticalSightSubsystem::IsVisibleToSquad(Enemy)` over the
hostile set that subsystem itself evaluates (it must include hostile PMC operatives; write NO line-of-sight test of your own),
`UMissionGoalsSubsystem` (`Public/LevelGen/MissionGoalsSubsystem.h`) for the goal point. A getter that does not exist: omit the key
and list it in the report - do not invent state.

## Hooks (player controller; state is captured at function ENTRY, before the order changes anything)
`FDemoOrderScope Scope(this, EDemoOrderKind::X, ...)` as the first statement; bool functions call `Scope.SetResult(bResult)` (void ones
count as ok). The subsystem keeps a depth counter: only the OUTERMOST scope records, nested ones are ignored. Kinds and entry points:
- Move: `OrderGroupMove(Destination, bSprint, bPlan)`. CornerPeek: `PlanRunCornerPeekClick`, `PlanGroupCornerPeek`.
- Cover: the function that calls `OrderGroupCover(...)` and falls back to the single cover order (ONE scope around both).
- Shot / QuickFire: `IssueTargetedShot(HitActor)` / `IssueQuickFire(Hit)` - NOT the dispatchers `IssueCtrlClick` / `HandlePlayerClick`.
- Suppress: `IssueSuppress(Hit)`, recorded only when it returns true; a cease toggle gets `unmapped:"suppress_cease"`.
- Stance: `CycleLeaderStance`, `SetEntireSquadStance(Stance)`. Medkit: `UseMedkit`. Breach: `OrderBreach(Plan, bSprint)`.
- Grenade: the click that confirms the throw (not `GrenadeKey`, which only starts the aim).
`mode` lines: `ToggleSoloMode`, `ToggleAutonomy`, `SetSquadCeaseFire`, `SpacePressed` / `SpaceReleased` (tactical pause on / off).
Planned orders (`bPlan` / tactical pause): recorded when PLANNED, `"plan":true`. Check how planned orders are released (grep
`PlannedOrders` in `SquadSubsystem.cpp`); if the release re-enters a hooked function, wrap that release in `FDemoSuppressScope` so nothing
is recorded twice. `sel` = the box-selected group, else the leader; `actor` = the directly ordered operative. `ok:false` orders are
written but do not count for the "zero orders" rule.

## Mapping to the PMC order catalog (`EPmcOrder`, `Public/AI/PmcCommanderRules.h`) - `MapToPmcOrder(FDemoOrderFacts)`, pure
Computed for `actor` only. Threat = his nearest squad-visible enemy. `thr_d` / `thr_d_new` = 2D distance from him / from the destination
to that threat; `delta = thr_d_new - thr_d`; `flank_deg` = angle at the threat between threat->actor and threat->destination. Within
a line the first matching condition wins; null = `pmc_order:null` with the `unmapped` reason in brackets:
- Cover / CornerPeek: no threat -> Ambush; delta >= +300 -> FallBack; actor already in cover -> Relocate*; otherwise TakeCover.
- Move with a threat: delta >= +300 -> FallBack; flank_deg >= 45 -> Flank*; delta <= -300 -> Advance*; otherwise null (`reposition`).
- Move without a threat: goal known and goal_d_new <= goal_d - 300 -> Objective; otherwise null (`explore_move`).
- Suppress (accepted, not a cease) -> Suppress. Stance: crouch requested + threat + not moving + not in cover -> Crouch, else null (`stance`).
- Medkit -> Heal*. Shot / QuickFire -> null (`fire_target`). Grenade -> null (`grenade`). Breach -> null (`breach`).
`*` = in the catalog but NOT one of the nine actions of the current policy (Hold, TakeCover, Suppress, Reload, FallBack, Objective,
Ambush, Crouch, RpgStrike). Write the order's NAME only (no action index: that list lives on `ai/la-probe`).
What does not map, stated plainly: (a) Shot / QuickFire, Grenade, Breach, prone / stand, cease fire have no catalog order - recorded
raw; window / mantle, watch-sector and formation-shape orders are not hooked in v1. (b) Reload, Sidearm, RpgStrike are never ordered
by the player; Hold is implicit: the converter derives it from the 1 Hz `ops` (`lo`, `lo_t`, `mv`). (c) The policy picks WHICH order,
the designer's skill is mostly WHERE (which cover slot, which spot): `dest` / `cover` / `threats` keep that for a later spatial model -
the nine-action policy cannot use it. (d) The policy's agent is an arena DEFENDER: post / hold-clock / point observations have no
counterpart in a normal level. (e) In formation mode one order moves the squad: only `actor` gets the label, followers get
`lo:"Follow"`. Solo sessions map best.

## Cost budget
No Tick function on the subsystem: a looping 1 s world timer takes snapshots (none while the world is paused; orders still record).
Steady state allocates nothing: flat `TArray`s of POD structs reserved at start for 30 min (frames hold index ranges into one ops array
and one threats array - no nested arrays, `FName` ids, no `FString` / JSON work before the flush); growth past the reserve is allowed
(rare, amortised); at `MaxSessionSec` snapshots stop and `truncated` is set. Wrap the snapshot and the order capture in
`TRACE_CPUPROFILER_EVENT_SCOPE`. Target: snapshot <= 0.1 ms with 4 operatives + 12 threats; a hook with recording off = one lookup.

## Tests (`Source/CodexTacticsTests/Private/Telemetry/SessionRecordingTest.cpp`, `CodexTactics.Telemetry.SessionRecording.*`)
- `Gate`: cvar 0 / 1 / 2 x PIE / Game / `-unattended` / `-NoTelemetry` / `-nullrhi` / automation.
- `Discard`: (59.9 s, 5) drop; (60, 0) drop; (60, 1) keep; suppressed drop; `ok:false`-only orders drop.
- `FileName`: `2026-11-23_L_Gen_Depot_1_143022.jsonl`; `UEDPIE_0_` stripped; bad characters replaced.
- `Mapping`: one case per mapping condition plus the edges 299 / 300 cm and 44.9 / 45 deg, and every `unmapped` reason.
- `Bearing` (`ThreatDistanceBearing`): ahead 0, right +90, left -90, behind 180, yaw wrap across +-180, no threat -> (-1, 0).
- `Serialize`: a hand-built session (header, 2 orders, 2 snaps, 1 mode, footer) -> every line parses with `FJsonSerializer`, first is
  header, last is footer, `t` never decreases, no NaN / inf, one order line compared to an exact golden string (stable key order).
- `Lifecycle` (PIE-free, the one world test): a scratch game world (reuse the helper the tests module already has - grep `CreateWorld(`
  in `Source/CodexTacticsTests`); assert the subsystem is NOT recording under automation; then `StartForTest(TempDir)` with
  explicit-time entry points (`RecordOrderAt(t, Facts)`, `SnapshotAt(t)`, `FinishAt(tEnd)` - production code calls the same functions
  with world time): 2 orders + 61 s -> exactly one `.jsonl` in a directory under `FPaths::AutomationTransientDir()`, line counts right;
  30 s -> no file; 61 s + 0 orders -> no file. Delete the temp directory. Tests never write to `Saved/Demos`.

## Acceptance
1. `Scripts/build.ps1` green; `Scripts/test.ps1 -Filter CodexTactics.Telemetry.SessionRecording` passes; full `test.ps1`: no new failure.
2. One existing smoke run (`-game -unattended -NoTelemetry`) leaves `Saved/Demos` unchanged and logs `[DemoRecord] off (...)`.
3. No Tick on the subsystem; no allocation in the snapshot / order path after start (state how you checked); gameplay files changed
   only by scope / mode-hook lines (list every touched line range in the report).
4. `git status` shows nothing under `Saved/`. Report lists: getters not found, hooks not placed and why, whether planned-order release
   needed `FDemoSuppressScope`. A real PIE session (>= 60 s, >= 1 order -> one file) is checked by the Lead, not by you.

## Open questions for the designer (do NOT wait for answers - build the defaults)
1. Lab sessions with immortality / infinite ammo: do they count as "your tactics"? Default: recorded with `flags`, left out of training.
2. Learn from every session or only from ones you mark? Default: keep all; `Codex.Demo.Mark good|bad` tags the session's footer.
3. Disk (a few MB per hour of play; C: is nearly full): auto-delete old demos past a cap? Default: never delete, log a warning at 300 MB.
