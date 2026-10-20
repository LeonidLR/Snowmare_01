# Tutorial engine

> UE-only (Godot had no tutorial). Every player command passes `Core/PlayerCommands.h` (~90 ids) so beats can lock / unlock them; the pure
> `TutorialRules` + `TutorialStateMachine` drive hint cards, objectives, radio lines and feed lines from `Content/Data/Tutorial/*.json`. The content of the
> actual Demo 01 flow is in [demo01-lake](demo01-lake.md).
>
> **The top of this file (sections 1-17) is the authoritative reference of the steps JSON schema and the runtime semantics (current to 2026-10-20).**
> The revision-by-revision notes it was distilled from are kept verbatim under [History](#history) at the end.
>
> Key files:
> - `Core/PlayerCommands.h`, `Tutorial/TutorialTypes.h` (data), `Tutorial/TutorialRules` (parse, validate, locks, save format), `TutorialStateMachine` (pure
>   per-step machine), `TutorialSubsystem` (world glue), `TutorialTriggerVolume`, `TutorialMarkerActor`, `UI/TutorialOverlayWidget`
> - `Core/DemoModeSubsystem`; data `Content/Data/Tutorial/<file>.json`; texts `narrative_manifest.json` "tutorial" + `demo_01_texts_draft.json`
> - tests `CodexTactics.Tutorial.*` (schema: `Schema.Parse / UnknownKeys / References / DemoFile / DemoFileReferences / DemoFileA0A1Flow`); smokes
>   `TutorialSmoke`, `TutorialSaveSmoke`, `PeekMinesSmoke`, `TrapSoloSmoke`, `BridgeFenceSmoke`, `InteriorExitSmoke`
>
> [HANDOFF](../port/HANDOFF.md) | [system docs index](README.md) | [CHANGELOG](../port/CHANGELOG.md)

## Reference

### 1. Pipeline in one paragraph

The level JSON's `"demo": {"tutorial": true, "tutorial_steps": "<file>.json"}` makes `UTutorialSubsystem` load the steps file on its first tick
(`TutorialRules::LoadStepsFile` = `ParseSteps` + `ValidateSteps`; errors abort, warnings go to the log). `FTutorialStateMachine` (pure, unit-tested)
holds one runtime record per step and the lock list; the subsystem feeds it player commands (`UCodexEventBus::OnPlayerCommand`), gameplay events
(`OnGameplayEvent` + mine spotted / item used / soldier downed / dialogue finished / cold tier / combat start-end), tutorial volume entries and real time
(`FApp::GetDeltaTime`, max 0.25 s: hints keep their pace in the tactical pause), drains the machine's transitions and turns them into overlay views,
markers, feed lines, speech playback, world actions, checkpoints and telemetry. Nothing in the tutorial ever blocks or pauses the game.

### 2. Step life cycle (state machine)

`Idle -> (trigger) -> Waiting (trigger.delay_s) -> Queued (waits for its display slot) -> Active (shown) -> Completed | Skipped | TimedOut`.

| Rule | Detail |
|---|---|
| Display slots | one **hint card** at a time, then a 1.5 s breather (`CardBreatherSeconds`); up to 3 **objectives** (`MaxObjectives`); one **radio / walk_talk** speech; a **feed** step takes no slot. Queued steps are shown in the order they queued. |
| Counting before shown | Waiting and Queued steps count commands, events and volume entries too; reaching the completion there finishes the step **silently** (never shown). Hold time (`hold_s`) and timeouts run only while Active. |
| Same input, one step | An input counts only for steps that were already running when it arrived; a follower started by that very input does not take it. |
| Once | `"once": true` (default): never triggers again after it finished. `false`: may re-trigger after it finished (remedial hints). A running step is never re-triggered. |
| Finishing | Applies the step's locks if it never showed, releases the confinement it ends, cancels steps whose `cancel_on` names it, fires its `after_step` followers, refills the slots. A shown card that **completed / was skipped / timed out** arms the breather; a card **cancelled** or **dropped by a later beat** does not (the arrival's card shows at once, rev 5g). |
| A later beat's step fires | It starts that beat first (beats run in file order). Beat-less steps never start a beat. |
| Card breather bypass | A hint card triggered by a place (`volume`) or a `beat_start` clears the breather (not on forced starts: replay / dev command). |

**When a step becomes due** (after its delay, entering Queued) these checks run in this order, the first that applies wins:

1. an `after_step` follower whose beat is already over -> dropped (a card: precredited if the history satisfies it, else skipped; other kinds skipped);
2. `cancel_on.step` already finished -> cancelled (Skipped);
3. step `condition` does not hold -> Skipped silently;
4. `precredit` from the command history satisfied -> Completed (precredited, not shown);
5. `precredit` and `complete.condition` already holds -> Completed (precredited, not shown);
6. otherwise it queues for its slot.

Every way a step finishes without being played normally reaches the runtime as one flag on its transition (`FTutorialStateMachine::FinishFlagged`):
`bPassed` (the route passed it), `bPrecredited`, `bCancelled`; `bSilent` = it was never shown. Telemetry event names follow from them
(`passed`, `precredited`, `skipped`, `timed_out`, `completed_silently`, `completed`).

### 3. File root and beats

```json
{ "version": 1, "id": "demo_01_frozen_lake", "start_beat": "A0", "beats": [ ... ], "steps": [ ... ] }
```

| Key | Type | Default | Meaning |
|---|---|---|---|
| `version` | int | 1 | Informational. |
| `id` | string | "" | Shown in the log. |
| `start_beat` | beat id | none | Started when the steps load (fresh start / RESET). Unknown id: warning, ignored. None: no beat runs until a step / volume starts one. |
| `beats` | array | [] | Beat objects (below), in story order: the file order IS the beat order. |
| `steps` | array | **required** | Step objects; their file order also orders the route (section 9) and the "n / m" card counter. |
| `_anything` | any | - | Keys starting with `_` are comments at every level (never warned). |

Beat object:

| Key | Type | Default | Meaning |
|---|---|---|---|
| `id` | string | **required**, unique | |
| `lesson` | bool | false | While it is the current beat (tutorial active) a lethal hit on an operative downs him at 1 HP (`UHealthComponent::LethalDamageGuard` + `ForceKnockDown`) and shows the "Retry from checkpoint" prompt. |
| `checkpoint` | bool | **true** | When the beat starts (a `BeatStarted` transition) the subsystem writes the `checkpoint` save slot - the only save the game writes on its own; skipped while a load is pending, when `CanSaveNow` refuses (a fight), or when `USaveGameSubsystem::bCheckpointsEnabled` is off (headless checks). |
| `title_id` | text id | none | Checkpoint save title ("Checkpoint: A1 - <title>"). |
| `lock` / `unlock` | string or array of command patterns | [] | Applied when the beat starts (section 6). |

A step naming a beat that `beats` does not declare: warning, the beat is appended (not a lesson, checkpoint on).

**What starting a beat does** (`FTutorialStateMachine::StartBeat`; only forward):
(1) running objectives of other beats without any completion complete; (2) the beat becomes current, its `unlock` then `lock` apply; (3) confinements
owned by earlier-beat steps are released; (4) running hint cards of earlier beats are dropped (precredited or skipped, no breather); (5) earlier-beat
steps: never-started once-steps with a `volume` trigger are **passed** (Skipped + `bPassed`: their unlocks apply, followers fire), running objectives
with `complete.volume` complete, Waiting / Queued walk_talks are cancelled - a radio / walk_talk on screen plays out and interaction objectives
(event / condition) keep running in parallel; (6) a `BeatStarted` transition (checkpoint); (7) its `beat_start` steps fire in file order.
Beat-less steps are never "earlier": a later beat never drops them.

### 4. Step object

```json
{ "id": "A1_solo", "beat": "A1", "kind": "hint_card", "text_id": "TUT_A1_SOLO", "keys": ["Squad.Solo"],
  "trigger": { ... }, "complete": { ... }, "condition": ..., "lock": [], "unlock": [], "actions": [], "focus": "...",
  "once": true, "skippable": true, "precredit": true, "cancel_on": { ... }, "confine": { ... }, "lines": [ ... ] }
```

| Key | Type | Default | Meaning |
|---|---|---|---|
| `id` | string | **required**, unique | Also the name markers (`ATutorialMarkerActor::StepId`) and volumes (`StepId`) use. |
| `beat` | beat id | none | Owning beat. None = **beat-less** (e.g. `X_medkit`): may run in any beat, never dropped / passed by a later beat, never starts a beat. |
| `kind` | `hint_card` (`card`, `hint`) / `objective` / `radio` / `walk_talk` (`walk_and_talk`) / `feed` | `hint_card` | Presentation and slot (section 2 / table below). Unknown: error. |
| `text_id` | text id | none | Card title + body / objective text / the single speech or feed line when `lines` is empty. Neither `text_id` nor `lines`: warning. |
| `keys` | string or array | [] | Key caps on the card: a registered command id renders its key (`"Squad.Solo"` -> `[B]`), anything else is a literal label (`"H"`, `"Alt+V"`). |
| `lines` | array of `{ "speaker", "text_id", "duration_s" }` | [] | Speech / feed lines in order. `speaker` empty = the manifest's `speaker_en`, else `HQ` (radio) / `Squad` (walk_talk); `duration_s` <= 0 = `clamp(1.2 + 0.055 * chars, 2.5, 9)` s. A line without `text_id` is skipped (warning). No `lines` and `text_id` naming a multi-line narrative `sequences` entry: that whole sequence plays. |
| `trigger` | object | see section 5 | What starts the step. |
| `complete` | object | see section 6 | What finishes it. |
| `condition` | clause object or array (AND) | [] | Gate checked when the step becomes due (section 2, step 3); for a `condition` trigger it is also the trigger. |
| `lock` / `unlock` | string or array | [] | Applied once per trigger: when the step is shown, or when it finishes without ever showing (section 7). |
| `action` / `actions` | object or array | [] | World actions run when the step is **shown** (section 10). Both keys may be used; they are merged. |
| `focus` | `coldest_operative` / `wounded_operative` / `role:<Commander \| Engineer \| MedicSapper \| Recruit>` | none | The operative the step is about (section 10). |
| `once` | bool | true | See section 2. |
| `skippable` | bool | true | The card's Skip button / `Codex.Tutorial.Skip` may skip it while running. |
| `blocking` | bool | false | Forced false (warning when true): the tutorial never blocks the game. |
| `precredit` | bool | true | Section 8. |
| `cancel_on` | object | none | Section 11. |
| `confine` | object | none | Section 12. |
| `guide` | volume id / step id / `"none"` | none | Section 17: where this step sends the squad while it is shown (a volume = a guide beacon on its box; a step id = that step's markers). `"none"`: the step never names the current target. |
| `beyond` | clause object or array (AND) | [] | Section 17: the squad is already past this step's place (e.g. all across the crevasse) - the step is passed, the route moves past its place. |

| Kind | Slot | Shows | Ends on its own |
|---|---|---|---|
| `hint_card` | one at a time + breather | title, body, key caps, "n / m" among its beat's cards (file order), Skip | No completion at all: parse gives `timeout_s` 12 (warning). |
| `objective` | up to 3 | one tracker line (`text_en`, else `title_en`) | No completion: completes when another beat starts. |
| `radio` / `walk_talk` | one speech | subtitle strip (Enter = next line, hold Enter / SKIP = the whole speech **completes**) | No completion: completes when its lines have played; with a completion it stays until that / its timeout. |
| `feed` | none | its line(s) in the message feed | Completes the moment it is shown. |

### 5. Triggers (`"trigger"`)

No `trigger` object: `beat_start` when the step has a `beat`, else `manual`. A `trigger` object without `type`: `manual`.

| Key | Type | Default | Used by |
|---|---|---|---|
| `type` | see below | `manual` | |
| `volume` | volume id | none | `volume` |
| `event` | event pattern | "" | `event`, `failure_retry` (**required** there) |
| `step` | step id or array | none | `after_step` (**required**): EVERY listed step must have finished (completed / skipped / timed out); the last one to finish fires it. Unknown id: error. |
| `command` | command pattern | "" | `first_use` (**required**) |
| `count` | int >= 1 | 1 | `event`, `failure_retry`: occurrences needed (counted only while the step may trigger and is armed) |
| `delay_s` | float >= 0 | 0 | every type: Waiting this long before it becomes due (the condition / precredit checks run after the delay) |
| `requires` | step id or array | [] | every type except manual starts: the trigger counts only after each listed step has been triggered (left Idle) at least once; events / volumes / conditions before that are ignored, not stored. Unknown id: error. |

| `type` | Fires when |
|---|---|
| `manual` | Only `UTutorialSubsystem::TriggerStep`, a volume whose `StepId` names it, or `Codex.Tutorial.Step <id>` (forced: ignores `requires`). |
| `beat_start` | Its `beat` starts (needs `beat`: error otherwise). |
| `volume` | A squad member enters the tutorial volume `volume` (section 9 for the route effects). While a confinement holds, steps of later beats do not fire. |
| `event` | The gameplay event pattern happened `count` times. |
| `after_step` | All `step` ids have finished. |
| `first_use` | The first use (since the steps loaded) of a command id matching `command`. Hold ticks never count. |
| `failure_retry` | Like `event`, remedial: at most one `failure_retry` step fires per 60 s (`RemedialCooldownSeconds`; hits keep counting meanwhile). Pair it with `"once": false`. |
| `condition` | Polled every tick: its `condition` clauses hold (a `condition` trigger without `condition`: error). |

**Patterns.** Command and event patterns are `PlayerCommands::Matches`: an exact id, `"*"` (anything) or a trailing-`*` prefix (`"Stance.*"`,
`"Camera.Pan.*"` - a plain prefix match, so it spans segments). Case-insensitive.

### 6. Completion (`"complete"`)

| Key | Type | Default | Meaning |
|---|---|---|---|
| `commands` (also `command`) | string or array of patterns | [] | Each matching player command adds 1. |
| `event` | event pattern | "" | Each matching gameplay event adds 1 (after the filters below). |
| `count` | int >= 1 | 1 | Uses needed - commands and events add to the **same** counter. |
| `hold_s` | float >= 0 | 0 | Besides `count`, the matching continuous commands (pans, drags: `PlayerCommands::ReportHold`) must have been active this long **while the step is shown**. Without `commands`: warning (it would never complete). |
| `volume` | volume id | none | A squad member entering it completes the step at once (one entry, whatever `count`). |
| `condition` | clause object or array (AND) | [] | Polled while Queued / Active; completes when all hold (queued: silently). |
| `timeout_s` | float >= 0 | 0 | > 0: an **Active** step not done after this many seconds times out (TimedOut counts as finished for followers). |
| `distinct_operatives` | bool | false | Event filter: each operative counts once; events without a subject never count. |
| `in_volume` | volume id | none | Event filter: the event's subject must stand inside that volume's box at that moment. |
| `distinct_from` | step id or array | [] | Event / volume filter: an operative who counted for any of those steps never counts here (needs a subject). Self or unknown id: error. |
| `subject` | `"focus"` | none | Event filter: only the step's focus operative's events count (none chosen yet: anyone). Other values: error. |
| `role_spots` | bool | false | Event filter: the subject must stand at HIS role's spot of this step (section 10); also counted while already standing there in cover. |

A filter without the completion it filters (`in_volume`, `subject`, `role_spots`, `distinct_operatives` without `event`; `distinct_from` without
`event` / `volume`): warning. Completion paths combine as **OR**: whichever reaches the counter / holds first finishes the step; `hold_s` gates every
counting path (commands, events, volume entry), not `condition` or the timeout.

**Filter order for an event** (first failing filter drops it, nothing is stored): the step was running when the event came -> `event` pattern ->
`in_volume` -> `subject: focus` -> `role_spots` -> confinement order (section 12) -> `distinct_from` -> `distinct_operatives` -> count (the subject
becomes a contributor of the step). A **volume entry** passes: running -> `volume` id -> `distinct_from` -> confinement order -> complete.
**Presence** counts the same way without an event: a running step with `complete.volume` counts the members already standing in it when it queues /
shows; a running `role_spots` step counts, every tick, each member in cover (not mid-shimmy, alive) at his spot - with the event filters, never twice.

### 7. Command locks (`"lock"` / `"unlock"`)

- **Entries** are command patterns (`"Stance.*"`, `"Order.Sprint"`, `"*"`) plus exceptions `"!<pattern>"` the engine adds itself.
- **Order:** a beat applies them when it starts; a step once per trigger, when shown (or when it finishes without ever showing). `unlock` first, then `lock`.
- **unlock P**: `"*"` clears everything; otherwise every lock P covers goes (`"Stance.*"` frees `"Stance.Squad.*"` too); if a wider lock still covers P,
  an exception `"!P"` frees just P (beat A0 locks `"Trap.*"`, the mine card unlocks `"Trap.DisarmMine"` -> `["Trap.*", "!Trap.DisarmMine"]`).
- **lock P**: removes the exceptions P covers, adds P. Exceptions no lock covers any more are dropped.
- **Locked?** A command is locked when a lock pattern matches it and no exception does (an exception always wins).
- **Who enforces:** handlers ask the choke points `UTutorialSubsystem::CheckCommandLocked` / `FilterCommand` (`TutorialRules::ResolveLockedCommand`:
  a locked `Order.Sprint` degrades to `Order.Move` unless that is locked too; anything else is refused). A command whose handler never asks cannot be
  locked (the demo never locks the camera, `Order.Move`, `Select.*`, `Dialogue.*`, `System.*`). A refusal raises `Tutorial.Locked` + `Tutorial.Locked.<CommandId>`; when no step of the file has an
  `event` trigger matching that second event, ONE generic feed line per command family ("Not yet - follow the training.") is posted.
- **Safety:** a wave / horde fight (`WaveCombat`), the tutorial switched OFF and SKIP TUTORIAL drop every lock (and the confinement). Tutorial OFF /
  no steps = nothing is locked. Later beat / step lists still apply afterwards (the demo repeats earlier unlocks on later beats as a safety).
- `ValidateSteps` warns on a pattern that matches no registered command.

```json
"beats": [ { "id": "A0", "lock": ["Stance.*", "Trap.*", "Order.Sprint"] } ],
"steps": [ { "id": "A1_sprint", "unlock": ["Order.Sprint"], ... }, { "id": "A3_fence", "unlock": ["Squad.Solo"], "lock": ["Squad.Regroup"], ... } ]
```

### 8. Precredit (`"precredit"`, default true)

- The machine records every command (uses + hold seconds) since the steps loaded - also while a card is not up (the "history", saved).
- **Command precredit** applies to a `hint_card` with `once` true, `precredit` true and a completion by `commands` only (no `event`, no `volume`)
  (`TutorialRules::CanPrecredit`). When it becomes due and the history already sums to `count` uses and `hold_s` seconds over the commands it matches,
  it completes without showing (feed "✓ <title> - already done.", telemetry `precredited`). Fewer uses seed its progress when it fires
  (one Q press before the Q/E card = 1 of 2); hold time is never seeded.
- **Condition precredit:** any kind with `precredit` true whose `complete.condition` already holds when due completes precredited.
  With `"precredit": false` it still completes on the first poll after it is queued (it may flash up).
- Dropped earlier-beat cards (section 3) are precredited instead of skipped when the history satisfies them.
- Use `"precredit": false` for lessons that must happen now (Solo, select, regroup, sprint, loot open, postures, the lure).

### 9. Volumes, beat progression and the monotonic route

Tutorial volumes are `ATutorialTriggerVolume` actors: `VolumeId` (empty = `StepId`) is the id steps use; `StepId` (step to trigger), `BeatId`
(beat to start), `RequiredBeat` (fires only from that beat on), `ActivatedBy` (`Leader` default / `Any` operative / `Vehicle` with the squad aboard -
its driver counts), `FireOnce` (default true; not used up when the tutorial is off or the entry was refused). An entry is handled in this order
(`FTutorialStateMachine::HandleVolume`):

1. `complete.volume` of running steps (so the step that asked for this place completes before a beat change could drop it);
2. **route** (skipped while a confinement holds): the volume's place on the route is the earliest step that uses it (its `volume` trigger at step
   index i -> key 3i+1; its `complete.volume` / `in_volume` -> 3i+2, where a step started by `beat_start` names the END of its beat) or the start of the
   beat a volume actor starts (`BeatId`), whichever is later. Reaching a place of a later beat **starts that beat** (section 3); then everything behind
   the place is passed: never-started once `volume` steps whose trigger place lies behind are Skipped (`bPassed`: unlocks apply, followers fire),
   running "go to X" steps whose X lies behind complete (objectives) or are dropped (other kinds);
3. `cancel_on.volume` of running steps;
4. `volume` triggers naming the volume (while confined: only steps of the current or earlier beats);
5. the actor's `StepId` (forced trigger).

Volumes unused by every step and starting no beat have no route place (only triggers / cancels use them).

### 10. Conditions, actions, focus, role spots

**Condition clause:** `{ "stat": <stat>, "at_least": N }` (stat >= N) or `{ "stat": <stat>, "below": N }` (stat < N); an array = AND. Unknown stat or
neither comparator: error. Stats (`UTutorialSubsystem::GetConditionStat`, "living" = no health component or alive; 0 without a squad):

| Stat | Value |
|---|---|
| `squad_cold_max` / `squad_cold_min` | highest / lowest cold % of the living members |
| `leader_cold` | the leader's cold % |
| `squad_heat_packs` / `squad_min_heat_packs` | heat packs carried by all living members / by the one with the fewest |
| `heat_packs_given` | heat packs handed between squad mates since the level started |
| `squad_solo` | 1 in Solo mode, else 0 |
| `squad_injury_max` | % of health the most wounded living member has lost (30 = below 70 %) |
| `squad_prone` | living members lying prone |
| `enemies_unseen` | living enemies no operative sees now (0 while the squad's sight is off) |
| `traps_spotted.<tag>` | traps with `SpotEventTag` = tag the squad knows of (spotted or dealt with) |
| `squad_not_low_in.<volume>` | living members NOT crouched / prone within 3 m of that volume's box (`below 1` = all are down there) |
| `squad_not_across.<volume>` | living members NOT across the bridge (`ABridgeCrossingVolume`) nearest to that volume: still on the volume's side of the span's middle, or on the span (`BridgeRules::IsAcrossFrom`); `below 1` = the whole squad is on the far side, however it got there. No bridge / no such volume: every living member counts. |

**Actions** (run when the step is SHOWN; a walk_talk waits for the speech slot):
`{"type": "cold_bump", "amount": 20, "min_level": 42}` - every squad member's cold + amount, raised to at least `min_level` (0-100; a cold shock, not
scaled by the level's cold rate); `{"type": "solo_focus"}` (2026-10-20) - Solo ON and the step's focus operative selected (he alone takes the orders);
`{"type": "set_posture", "posture": "passive" | "defensive" | "aggressive"}` - the whole squad's fire posture
(overrides cleared; missing / unknown posture: error). Unknown type: error.

**Focus:** `role:<EOperativeRole>` (2026-10-20, the A4 ambusher) = the living operative of that role, chosen when the step is TRIGGERED (so a
confinement's `free` operative is known at once), else the leader; a floating "<NAME>: YOUR TASK" over him when shown; `{focus}` also works in
objective texts. `coldest_operative` = the living member with the most cold, chosen when the step is shown; `wounded_operative` = the living member with the
lowest health share, chosen when the step starts running (so a medkit on him counts while the card queues) and re-chosen when shown if he is gone. The
card shows him (`FTutorialCardView::FocusOperative / FocusName`), a floating line over him (`COLDEST` / `WOUNDED`), and `{focus}` in the card title /
body and feed lines becomes his name (fallback "the coldest / wounded operative"). `"complete": {"subject": "focus"}` counts only his events.

**Role spots** (`"complete": {"role_spots": true, "event": ...}`): a `ATutorialMarkerActor` with this step's `StepId` and a `RoleId`
(`Commander` / `Engineer` / `MedicSapper`, the `EOperativeRole` name) is that role's spot; the subject counts only within its `RingRadiusCm` + 80 cm.
A role's ring hides once an operative of that role counted. A level with no role markers for the step: any place counts.

### 11. `"cancel_on"` - drop a step when its moment is over

`{ "step": id | [ids], "event": pattern, "volume": id | [ids] }` (any of them; none: warning; unknown / own step id: error). A running step is
cancelled (Skipped, `bCancelled`, no breather) when a listed step finishes, the event happens or a squad member enters a listed volume. Waiting /
Queued: never played later. Shown: a radio / walk_talk lets the line on screen run out and drops the rest; other kinds hide at once. If a listed step
had already finished when this step becomes due, it is cancelled immediately. Its `after_step` followers still fire. Twins cancel each other with
`cancel_on` both ways (`A3_posture` <-> `A4_posture`).

### 12. `"confine"` - hold the squad in an area during a lesson

```json
"confine": { "volume": "VOL_A1_HUT_APPROACH", "allow": ["VOL_A1_DOOR_L", "VOL_A1_DOOR_R", "VOL_A1_HUT"], "until": "A1_hut_enter",
             "stop": true, "span": true, "stance": "crouch", "exit": { "stance": "prone", "through": "CrawlGapPassed" } }
```

| Key | Type | Default | Meaning |
|---|---|---|---|
| `volume` | volume id | **required** | The holding area, always allowed. |
| `allow` | id or array | [] | More areas. An area some step **completes** in (`complete.volume` / `in_volume`) opens only while that step is shown or after it finished (the lesson's order); other areas are always open. |
| `until` | step id | the step itself | The confinement ends when that step finishes. Unknown id: error. |
| `stop` | bool | false | The whole squad stops when it starts (otherwise only walks heading outside are stopped). |
| `span` | bool | false | The allowed area is one box: the bounding box of the holding + open allowed volumes. |
| `stance` | `crouch` / `prone` | none | Every move order needs the ordered operative that low (crouch = crouched or prone). An operative in cover counts as low enough (shimmy / peek). |
| `exit.stance` | `crouch` / `prone` | none | Orders OUTSIDE the area are allowed for an operative in this stance. |
| `exit.through` | event pattern | "" | This event names an operative who got through (e.g. `CrawlGapPassed`): from then on he moves freely, any stance. (2026-10-20: the former `exit.one_at_a_time` queue was removed - several may cross at once, each on his own order; an old file gets the unknown-key warning and no queue.) |
| `free` | `"focus"` | none | 2026-10-20: the step's focus operative is never held (he does the lesson alone, anywhere); everyone else may move only inside the areas - an order outside is refused with "Hold here - <Name> handles it." (verdict `HeldBack`). A `free` without a step `focus`: warning. |

- **Starts** when the step is triggered (before its delay; a `volume` step: the moment the squad arrives) - unless its `until` step already finished.
  One at a time: a new confinement releases the old one.
- **Ends** when `until` finishes, when a later beat starts (it was owned by an earlier beat), or on tutorial OFF / SKIP TUTORIAL / a wave fight.
- **While it holds:** the route does not move (no beat start / passing from arrivals, later-beat volume steps do not fire), and a step that completes
  in one of its `allow` areas counts only while it is shown.
- **Move orders** (player clicks, cover orders, walk-ups, autonomy: `UTutorialSubsystem::CheckConfinedOrder`) are judged by
  `TutorialRules::JudgeConfinedMove`: `stance` -> destination inside (50 cm margin, 3 m height) = allowed -> no `exit` = refused -> already through =
  allowed -> `exit.stance` (each operative on his own, no queue) (with `free`: the free operative is always allowed first; the others
  inside = allowed, outside = `HeldBack`). A refusal posts one feed line per verdict per second and raises
  `Tutorial.Confined`; formation moves are pulled into the area instead (`ClampIntoConfinement`, 60 cm inside).

### 13. Runtime features outside the JSON

| Feature | Behaviour |
|---|---|
| Texts | `FNarrativeManifest::GetTutorialText`: manifest `"tutorial"` (`title_en`, `text_en`, `radio_en`, `speaker_en`) -> a one-line `sequences` entry -> `demo_01_texts_draft.json` -> `[EN missing: <id>]`. Tokens: `{key:<CommandId>}` (all texts: its key cap), `{focus}` (card title / body, feed lines), `{progress}` / `{count}` (card title / body: counted so far / `complete.count`). |
| Markers | `ATutorialMarkerActor`: with `StepId` shown while that step is Active, else with `BeatId` while that beat is current, and always while it is the current navigation target (section 17); role spots see section 10; `Label` drawn by the overlay; `Screen Edge Pointer` = it may carry the edge arrow when it is the current target. Hidden while the tutorial is inactive. The runtime guide beacon (`bRuntimeGuide`) is spawned by the subsystem, never placed. |
| Current target | Section 17: ONE edge arrow (small chevron + "42 m" from the leader, `UTutorialOverlayWidget`), ONE highlighted minimap icon (`UMinimapSubsystem::BuildIcons`, `bHighlight` on an Objective icon), telemetry event `guide` when it changes. |
| Tutorial ON / OFF | `Codex.Tutorial.Enabled` + `Saved/Tutorial/profile.json` (player setting). OFF hides everything, stops counting and unlocks everything; ON again does not re-lock. |
| SKIP TUTORIAL | `SkipTutorial()` (button offered while the start beat runs, pause menu; confirm first): off for this run, locks + confinement released, overlay cleared, no checkpoints, normal death rules; saved (`skipped`). RESET TUTORIAL starts it again. |
| Reset | `ResetTutorial()` / `Codex.Tutorial.Reset`: every step, lock, history and confinement forgotten; the start beat starts again. |
| Retry from checkpoint | Loads the `checkpoint` slot: the level's own steps reopen the level (as F9), runtime-injected steps load in place. |
| Dev console | `Codex.Tutorial.Skip` (visible card), `.SkipTutorial`, `.Reset`, `.Retry`, `.Step <id>`, `.Telemetry 0/1`. |
| Telemetry | `Saved/Tutorial/tutorial.jsonl`, one JSON per line (`t`, `session`, `map`, `event`, `step`, `beat`, `active_s`, `game_s`). |

### 14. Save behaviour (the save's `"tutorial"` section)

| Key | Written by | Restored as |
|---|---|---|
| `beat` | current beat | current beat, **without** re-running its start (no checkpoint, no beat_start steps) |
| `finished` | `{step: completed / skipped / timed_out}` | finished; their locks count as applied |
| `pending` | running steps (Waiting / Queued / Active) | queued again at once (delay not repeated, the due checks of section 2 run again; a radio replays from its first line) |
| `runtime` | `{step: {progress, held_s, subjects, contributors}}` | a pending step continues its count / hold / counted operatives; contributors are restored for every step (`distinct_from`) |
| `locks` | entries in force | as saved; an older save without `locks` rebuilds them from the beats up to the current one + the finished steps |
| `history` | `{command: {count, held_s}}` | precredit history |
| `confines` | `[{step, released, passed}]` | the confinement in force holds again; a released one stays released (an older save's `holder` is ignored) |
| `enabled` | the player setting | informational (the setting lives in `profile.json`) |
| `volumes_fired`, `lock_hints`, `skipped` | subsystem | used-up volumes (by actor name), families that already got the "Not yet" line, SKIP TUTORIAL |
| `route` | the furthest place reached (a volume id; 2026-10-20) | the route progress (its key is recomputed from the volume; an older save: the saved beat's start) - the current target follows from it and the finished steps |
| `guide` | the current target (`markers:<step>` / `volume:<id>`) | informational only (it is derived again after the load; `SaveCoverageLakeSmoke` dumps it as `tutorial.guide`) |

Not saved (restart after a load): trigger event counts, the first-use memory, timeouts / state timers, the card breather and the remedial cooldown,
the chosen focus operatives, the speech line position. A save applied before the steps are loaded is kept (`DeferredProgress`) and applied when they
load; while a load is pending the tutorial does not start (no checkpoint over the slot being loaded).

### 15. Validation

- `TutorialRules::ParseSteps`: **errors** abort the load (invalid JSON, no `steps`, missing / duplicate ids, unknown kind / trigger type / stat / action /
  focus / subject / confine stance, a type without its required field, every step reference: `trigger.step`, `requires`, `distinct_from`,
  `cancel_on.step`, `confine.until`); **warnings**: an unknown key at any level (root, beat, step, trigger, complete, condition clauses, action,
  cancel_on, confine, confine exit, line), undeclared beat, `blocking: true`, a filter without its completion, a card without completion, an empty
  `cancel_on`, no text, unknown `start_beat`.
- `TutorialRules::ValidateSteps` (run by `LoadStepsFile`): warnings for command patterns matching no registered command (`complete.commands`,
  `trigger.command`, beat / step `lock` / `unlock`), dotted key caps that are not registered, `subject: focus` without `focus`, `after_step` cycles,
  `beyond` on a step without a place (the route cannot move), a `guide` naming the step itself.
- Tests: `Schema.UnknownKeys`, `Schema.References`; the shipped file must load with **zero** warnings (`Schema.DemoFile`, `Schema.DemoFileReferences`,
  which also checks that every `text_id` / line / beat title exists in the manifest or the draft texts and every `{key:...}` token names a command).

### 16. Minimal examples

```json
{ "id": "card",  "beat": "A0", "text_id": "TUT_CARD", "keys": ["Order.Move"], "complete": { "commands": ["Order.Move"] } }
{ "id": "hold",  "beat": "A0", "trigger": { "type": "after_step", "step": "card" }, "complete": { "commands": ["Camera.Pan.*"], "hold_s": 1.0 } }
{ "id": "both",  "beat": "A0", "trigger": { "type": "after_step", "step": ["radio", "hold"] }, "complete": { "commands": ["Camera.Zoom"], "count": 2 } }
{ "id": "goto",  "beat": "A1", "kind": "objective", "text_id": "OBJ_CAMP", "complete": { "volume": "VOL_CAMP" } }
{ "id": "radio", "beat": "A0", "kind": "radio", "trigger": { "type": "beat_start", "delay_s": 1 }, "lines": [ { "speaker": "HQ", "text_id": "WT_1" } ] }
{ "id": "nosprint", "beat": "A0", "kind": "feed", "trigger": { "type": "event", "event": "Tutorial.Locked.Order.Sprint" }, "lines": [ { "text_id": "TUT_NO" } ] }
{ "id": "tip",   "beat": "A1", "trigger": { "type": "first_use", "command": "Grenade.Aim" }, "complete": { "timeout_s": 8 } }
{ "id": "retry", "beat": "A2", "once": false, "trigger": { "type": "failure_retry", "event": "RelocationRefused" }, "complete": { "commands": ["Select.Member"] } }
{ "id": "cold",  "beat": "A1", "trigger": { "type": "condition", "requires": ["bridge"] }, "condition": { "stat": "squad_cold_max", "at_least": 40 },
  "focus": "coldest_operative", "complete": { "event": "ItemUsed.HEAT_PACK", "timeout_s": 90 } }
{ "id": "gust",  "beat": "A1", "kind": "walk_talk", "trigger": { "type": "after_step", "step": "bridge", "delay_s": 20 },
  "condition": { "stat": "squad_cold_max", "below": 40 }, "action": { "type": "cold_bump", "amount": 20, "min_level": 42 }, "lines": [ { "text_id": "WT_GUST" } ] }
{ "id": "each",  "beat": "A1", "kind": "objective", "complete": { "event": "BridgeCrossed", "count": 3, "distinct_operatives": true } }
{ "id": "left",  "beat": "A1", "complete": { "event": "CoverEntered", "in_volume": "VOL_DOOR_L", "distinct_from": ["right"] } }
{ "id": "spots", "beat": "A3", "complete": { "event": "CoverEntered", "count": 3, "distinct_operatives": true, "role_spots": true } }
{ "id": "share", "beat": "A1", "kind": "objective", "complete": { "condition": [ { "stat": "squad_min_heat_packs", "at_least": 1 } ] } }
{ "id": "medkit", "focus": "wounded_operative", "trigger": { "type": "condition" }, "condition": { "stat": "squad_injury_max", "at_least": 30 },
  "complete": { "event": "ItemUsed.MEDKIT", "subject": "focus" } }
{ "id": "posture", "beat": "A3", "precredit": false, "unlock": ["Posture.*"], "complete": { "commands": ["Posture.Passive"] } }
{ "id": "bridge_talk", "beat": "A1", "kind": "walk_talk", "trigger": { "type": "volume", "volume": "VOL_BRIDGE" }, "cancel_on": { "step": "bridge" },
  "lines": [ { "text_id": "WT_B1" } ] }
{ "id": "fence", "beat": "A3", "trigger": { "type": "volume", "volume": "VOL_FENCE" }, "confine": { "volume": "VOL_FENCE", "stop": true },
  "complete": { "condition": { "stat": "squad_solo", "at_least": 1 } } }
{ "id": "crawl", "beat": "A3", "trigger": { "type": "after_step", "step": "fence" },
  "confine": { "volume": "VOL_FENCE", "exit": { "stance": "prone", "through": "CrawlGapPassed" } },
  "complete": { "event": "CrawlGapPassed", "count": 3, "distinct_operatives": true } }
{ "id": "postures", "beat": "A4", "action": { "type": "set_posture", "posture": "defensive" }, "kind": "radio", "lines": [ { "text_id": "WT_P" } ] }
```

### 17. The current navigation target (`"guide"`) and `"beyond"` (2026-10-20)

**Rule: at any moment of the tutorial there is exactly ONE current navigation target** (`FTutorialStateMachine::GetGuide`, resolved by
`UTutorialSubsystem::GetGuideView`, refreshed every tick). It is derived from the data, never stored as a step state, so the next target
appears the moment an action completes. Priority, first match wins:

1. **The shown hint card** (the lesson in hand): its `guide` (a volume or another step's markers), else its own markers, else the volume it
   completes in (`complete.volume`, then `complete.in_volume`). Never filtered by the route (it is the lesson of the moment).
2. **Shown objectives / talks** that name a spot: a `guide`, else their own markers - **only if their place is not behind the route
   progress**; several: the nearest place on the route first (no place: after the placed ones, file order). An objective without markers /
   guide (`A1_obj_camp`) names nothing here: its volume is a route place (rule 3).
3. **The route**: the next place ahead = the smallest route key beyond the progress (section 9: the volumes the steps use and the
   beat volumes) that the level has (`VolumeProvider`) and that something still uses (an unfinished step names it, or it starts a later
   beat) - the next beacon, the next beat's start volume. A place whose steps are all done is skipped.
4. **The place reached last** when the data names nothing further (A4: the overlook - hold there).

`"guide": "none"` on a step: it names nothing (the next rule decides). `"guide": "@patrol_route"` (a **spot** the runtime computes,
`ETutorialGuideKind::Spot`): the point of the level's patrol routes (`APatrolRouteActor` waypoints, a loop closed) nearest to the step's focus
operative (else the leader) - where the A4 ambusher rigs the wire; no patrol route in the level: the automatic choice. Unknown `@` spots: warning. An explicit `guide` the level cannot show (no such volume /
markers) falls back to the automatic choice (the log says `NOT in the level`).

**Route progress** (`GetRouteProgressKey`): the furthest place reached - an arrival (`HandleVolume`, not while a confinement holds), a
step passed by `beyond` (its place), at least the current beat's start. Never goes back. Saved as `"route"` (the volume id).

**Places behind never call the squad back:**
- When the route moves past a point (section 9), a RUNNING objective - or any step with a `guide` - whose place lies behind is finished
  (objective: Completed, others: Skipped; `bPassed`, telemetry `passed`), not only "go to X" steps. A step's **place** = the first of its
  `guide` volume, `complete.volume`, `complete.in_volume`, `trigger.volume` that has a route point. Example: `A1_bridge` (triggered at
  `VOL_A1_BEACON2`, completed by `BridgeCrossed` x3) ends when the squad reaches the hut approach.
- **`"beyond"`**: clauses (the condition syntax) polled every tick (not while confined) for every unfinished step of the current or an
  earlier beat (beat-less: always); when they hold the step is passed - never started: Skipped (`bPassed`, its unlocks apply, its
  followers fire), running: an objective Completed, others Skipped - and the route moves PAST its place (`NoteRouteProgress` +
  `PassRouteBefore(place + 1)`: what waits at that place is passed too). `A1_bridge`: `"beyond": {"stat": "squad_not_across.VOL_A1_BEACON2",
  "below": 1}` - all three across the crevasse, however they got there.
- A finished once-step never shows again, so its markers never come back; the target is never a place behind the progress (rules 2/3).

**What the player sees** (`UTutorialSubsystem::RefreshGuide`): a markers target -> the nearest of that step's markers to the leader (a
taken role spot does not count) is shown (also when its step is not active, e.g. a `guide` naming another step); a volume target -> ONE
runtime guide beacon (`ATutorialMarkerActor` with `bRuntimeGuide`, spawned once, moved to the volume's ground point: a trace down from
the box top, else its bottom). Exactly one edge arrow (only the target's, when off-screen; `ATutorialMarkerActor::bScreenEdgePointer`)
with the distance in metres, and the target's minimap icon drawn larger with a white frame. Other shown markers stay as rings without
an arrow.

**Pure helpers** (unit-tested): `TutorialRules::ProjectToView` (the camera's perspective view - the same rule headless, a 1920 x 1080 view,
so smokes check the arrow), `ComputeEdgePointer`, `FormatGuideDistance`; `BridgeRules::IsAcrossFrom`. Tests `CodexTactics.Tutorial.Guide.*`
(`Parse`, `NextTargetFromRoute`, `MarkersBehindDropped`, `LakeBridgeSkipped`, `SaveRoundTrip`, `EdgePointer`, `BridgeAcross`); smokes
`TutorialSmoke` stages 140-153 (the guide course), `TutorialSaveSmoke` (the target survives F5 / F9), `SaveCoverageLakeSmoke` (`tutorial.guide`).

```json
{ "id": "A1_bridge", "beat": "A1", "kind": "objective", "trigger": { "type": "volume", "volume": "VOL_A1_BEACON2" },
  "beyond": [ { "stat": "squad_not_across.VOL_A1_BEACON2", "below": 1 } ], "complete": { "event": "BridgeCrossed", "count": 3, "distinct_operatives": true } }
{ "id": "A3_low", "beat": "A3", "guide": "VOL_A3_RIDGE", "trigger": { "type": "volume", "volume": "VOL_A3_APPROACH" }, ... }
```

## History

The notes below are the revision-by-revision record the reference above was distilled from (moved verbatim from the old `docs/port/HANDOFF.md` in the
split of 2026-10-19; headings shifted one level down). Where they disagree with sections 1-16, the reference is current.

Original header of this file (2026-10-19 split):

> UE-only (Godot had no tutorial). Every player command passes `Core/PlayerCommands.h` (~60 ids) so beats can lock / unlock them; the pure
> `TutorialRules` + `TutorialStateMachine` drive hint cards, objectives and radio lines from `Content/Data/Tutorial/*.json`. The content of the actual
> Demo 01 flow is in [demo01-lake](demo01-lake.md).
>
> Key files:
> - `Core/PlayerCommands.h`, `Tutorial/TutorialRules`, `TutorialStateMachine`, `TutorialSubsystem`, `TutorialTriggerVolume`, `TutorialMarkerActor`, `UI/TutorialOverlayWidget`
> - `Core/DemoModeSubsystem`; data `Content/Data/Tutorial/<file>.json`; tests `CodexTactics.Tutorial.*`; smoke `TutorialSmoke`
>
> Moved verbatim from the old `docs/port/HANDOFF.md` (split of 2026-10-19; nothing was dropped). [HANDOFF](../port/HANDOFF.md) | [system docs index](README.md) | [CHANGELOG](../port/CHANGELOG.md)

### Role-specific tutorial spots (old §5)

- **Role-specific tutorial spots (rev 5j, every level; the A3 cover lesson):** completion key `"role_spots": true` (`FTutorialCompletion::bRoleSpots`): an event counts only when its subject stands at HIS role's spot of the step = a `ATutorialMarkerActor` with that `StepId` and `RoleId` (the `EOperativeRole` name: `Commander` / `Engineer` / `MedicSapper`), within its `RingRadiusCm` + `RoleSpotToleranceCm` 80 (`UTutorialSubsystem::IsAtRoleSpot`, `FTutorialStateMachine::RoleSpotProvider`); `UTutorialSubsystem::CountRoleSpotPresence` (tick) also counts a member already in cover there (`HandleRoleSpotPresence`: only that step, its filters, never other steps on the same event); a role's ring hides once he counted (`IsRoleSpotDone`); a level with no role rings for the step: any place counts. Marker `Label` (FText) is drawn over the beacon by the tutorial overlay (`UTutorialOverlayWidget::MarkerLabels`, the marker's colour). `A3_cover`: `"role_spots": true` (text: the rings / roles); `A3_peek` completes at `traps_spotted.A3PeekMines` >= 4 (all four, found by the medic's cone). Map: additive section `5j` (`docs/demo/DEMO01_AUTOLAYOUT.md` §11). Tests `CodexTactics.Tutorial.RoleSpots.*` (Parse, EachAtHisOwnSpot, NoSpotsAnyCover); TutorialSmoke stage 115 (runtime rings: another role's ring does not count, own ring counts once, ring hides, any order).

### Tutorial framework (old §5)

**Tutorial framework (Demo 01 Sprint 1, C2 / C7 / C8 / C9 / C10, 2026-10-11)** — UE-only (Godot had no tutorial).
- **Command choke point** `Core/PlayerCommands.h`: ~60 logical command ids (`Order.Move`, `Squad.Solo`, `Squad.Regroup`,
  `Stance.Crouch`, `Stance.Squad.Prone`, `Cover.Enter`, `Cover.Shimmy`, `Cover.Corner`, `Object.Relocate`, `Object.LightBarrel`,
  `Item.HeatPack`, `Loot.Open`, `Trap.DisarmMine` / `DisarmObject` / `DisarmTripwire`, `Trap.SetOnObject`, `Deploy.Mine` /
  `Barricade` / `Turret` / `Tripwire`, `Attack.TargetedShot`, `Grenade.Aim`, `Time.Pause` / `Resume` / `TurnBased` / `RealTime`,
  `TB.EndTurn` / `TB.Attack`, `Posture.*`, `System.QuickSave`, camera ...) + a registry (default key label, section, description:
  the F1 Controls screen is generated from it: `PlayerCommands::GetSections()` order, `GetKeyLabel()` honours `SetKeyLabelOverride()` for rebinds, `CodexControlsRules::BuildSections()`; texts live ONLY in `PlayerCommands.cpp`). `PlayerCommands::Report(Context, Id, FPlayerCommandContext)` -> `UCodexEventBus::OnPlayerCommand`.
  Reported (one line each, behaviour unchanged) from `ACodexTacticsPlayerController` handlers (`ControllerCommands::Report`),
  `UInteractionSubsystem` (menu confirm -> light / defuse / use, loot, set trap), `URelocationSubsystem` (relocate / deploy /
  tripwire orders), `ATacticalCameraPawn` (WASD / edge pan start). Gameplay events `GameplayEvents::Raise` ->
  `UCodexEventBus::OnGameplayEvent`: `RelocationDone`, `RelocationRefused`, `BarrelLit`, `LessonDowned`, `TripwireSpotted` /
  `TripwireDisarmed` / `TripwireTriggered` (subject = the operative), cover (`AOperativeCharacter`, all modes: exploration, real
  time, pause execution, turn-based grid): `CoverEntered` (+ `CoverEntered.High` / `.Low`) once he has SETTLED at the slot (run-in
  glide / enter clip blend over, corner snap done, still; not on the order, not for the quiet return after an open shot), `CoverLeft`,
  `CoverShimmy` (a player's side-step; the automatic corner snap is none), `CornerReached` (kind Left / Right). Details for
  Blueprints / tools: `GameplayEvents::RaiseWithContext` -> `UCodexEventBus::OnGameplayEventDetailed(Name, FGameplayEventContext)`
  (Subject / SubjectId = the operative, Target / TargetId = the wall actor, Kind = High / Low / Left / Right, Location = slot);
  the tutorial also maps
  `OnMineSpotted` -> `MineSpotted`, `OnItemUsed` -> `ItemUsed` (+ `.ItemId`), `OnSoldierDowned`, `OnDialogueFinished`, cold tier ->
  `ColdTier` (+ `.Chills` / `.Freezing` / ...), flow -> `CombatStarted` / `CombatEnded`.
- `Tutorial/TutorialRules` (pure: steps JSON parse + validation, matching, line timing, lesson rule, save / telemetry formats),
  `Tutorial/TutorialStateMachine` (pure per-step machine: Idle -> Waiting (delay) -> Queued -> Active -> Completed / Skipped /
  TimedOut; one hint card at a time + 1.5 s breather, <= 3 objectives, one radio / walk-and-talk step; queued steps complete
  silently; once / replay; failure_retry rate-limited 60 s; export / import progress), `Tutorial/TutorialSubsystem`
  (UTickableWorldSubsystem glue: loads the steps file named by the level's demo flags on the first tick, real-time ticking,
  overlay views, markers, speech playback, checkpoint autosave per beat (slot `checkpoint`, only when `CanSaveNow`), lesson rule via
  `UHealthComponent::LethalDamageGuard` (HP floor 1 + `ForceKnockDown` + retry prompt), "Retry from checkpoint" = in-place
  `LoadGame("checkpoint")`, save section `"tutorial"` (progress applied later if the steps load after the save), telemetry
  `Saved/Tutorial/tutorial.jsonl`, ON / OFF `Codex.Tutorial.Enabled` + `Saved/Tutorial/profile.json`; dev console
  `Codex.Tutorial.Skip / Reset / Retry / Step <id>`), `Tutorial/TutorialTriggerVolume`, `Tutorial/TutorialMarkerActor`,
  `UI/TutorialOverlayWidget` (HUD z 14). Texts: `FNarrativeManifest::GetTutorialText` (manifest `"tutorial"` section, else a
  `sequences` entry, else `[EN missing: <id>]`), `UTutorialSubsystem::GetTutorialText` for Blueprints.
- **Demo game-mode flags** `Core/DemoModeSubsystem` + `FDemoModeSettings` (level JSON `"demo"` object, `LevelJsonRules::ParseDemoSettings`):
  Panic off on every operative, vision cones forced on / off on operatives and enemies (also later spawns), tutorial on + steps file,
  demo HUD preset (`ACodexTacticsHUD::bDemoHudPreset`: debug squad panel and operative labels hidden). No `"demo"` object = no change.
- **Command locks / conditions / actions (2026-10-12, Demo 01 A0-A1 redesign):** beats and steps carry `lock` / `unlock`
  (command id patterns; beat: on start, step: when shown or, never shown, when finished) -> `FTutorialStateMachine::IsCommandLocked`
  / `GetLocks` (saved as `"locks"` in the save's tutorial section; older saves rebuild them). The lock choke point for order handlers is
  `UTutorialSubsystem::CheckCommandLocked` / `FilterCommand` (`TutorialRules::ResolveLockedCommand`: a locked `Order.Sprint` degrades
  to `Order.Move`, other locked ids are refused) - used by `ACodexTacticsPlayerController::HandleWorldHit` (double click on the ground /
  on an object); a refusal raises `Tutorial.Locked` + `Tutorial.Locked.<CommandId>` (no error line). Tutorial OFF / no steps = no locks.
  Trigger `condition` (polled each tick) + step `condition` clauses (`squad_cold_max` / `squad_cold_min` / `leader_cold` /
  `squad_heat_packs`, `UTutorialSubsystem::GetConditionStat` via `FTutorialStateMachine::StatProvider`; a due step whose condition
  fails is skipped silently, followers go on), `trigger.requires` (arming steps), step `actions` (`cold_bump`:
  `UColdSurvivalComponent::AddColdShock` = `ColdRules::ApplyColdShock`, run when shown), `focus: coldest_operative` (card names / marks
  him, `{focus}` token, `FTutorialCardView::FocusOperative`), kind `feed` (message-feed line, no slot, completes at once). New event
  `ItemLooted` + `ItemLooted.<Item>` (`GameplayEvents::RaiseItemLooted`; `ALootCrateActor::TakeItem` = click / Take all, and the
  crate / pile drag in `USquadTransferSubsystem`). Level JSON `squad_loadout.heat_packs_count` (`FSquadLoadout::HeatPacksCount`,
  `LoadoutRules::StartingHeatPacks`, applied at spawn by the game mode; -1 = the tactical_heat.json role kit).

### Tutorial tests (old §5)

- Tests `CodexTactics.Tutorial.*` (9 + 8 + 2: `Lock.Rules`, `Lock.Schema`, `Lock.LockUnlock`, `Lock.Saved`, `Action.GustTimeout`,
  `Condition.Trigger`, `Schema.DemoFileA0A1Flow`, `Demo.HeatPackLoadout`, `Precredit.Rules`, `Precredit.StateMachine`), smoke
  `TutorialSmoke` (exclusive; runs the real demo file A0 -> A1), `SaveRoundTripSmoke` covers the "tutorial" save section.

#### Tutorial: how the user places volumes / markers and edits steps (Demo 01, 2026-10-11; rev 5 2026-10-13; rev 5b 2026-10-13; rev 5c 2026-10-13; rev 5d 2026-10-14; rev 5e 2026-10-14; rev 5f 2026-10-14; rev 5g 2026-10-14; rev 5h 2026-10-15; rev 5i 2026-10-15)

### How to turn the tutorial on, place volumes / markers, edit steps and texts (old §6)

1. **Turn it on for a level**: in its level JSON (`Content/Data/LevelJson/<level>.json`) add
   `"demo": { "panic": false, "vision_cones": true, "tutorial": true, "tutorial_steps": "demo_01_steps.json", "hud": "demo" }`
   (every key optional; no `"demo"` object = normal game; `intro_dialogue` defaults to false when a `demo` object exists). **Which level JSON a map gets (2026-10-12):** `-LevelJson=` on the command line wins; else `Content/Data/LevelJson/<MapName>.json` if it exists (map name without the PIE prefix, e.g. `L_Demo01_A_FrozenLake.json`; `LevelJsonRules::ResolveLevelJsonFile`, used by `ACodexTacticsGameMode::LoadLevelConfig` and therefore by `PeekLevelConfig` / the mission intro flag); else the game mode BP's `LevelJsonFile` (default `level_01_outpost.json`, so L_MovementTest / L_PatrolTest are unchanged). A new map gets its own level by saving a JSON with the map's name (Wave Editor: level_id = map name). `L_Demo01_A_FrozenLake.json` is a DRAFT (3 sudden waves on lanes NORTH_ICE / EAST_GULLY / WEST_SHORE, `combat_start: button`, horde off, loadout mines 2 / barricades 1 / turret 1 / matches 3; the `matches` key is only in the Wave Editor type, C++ does not read it yet; `breach_wave` / `susanin_wave` keys do not exist: the breach is the map's dynamic spawn point, Susanin the SusaninSpawn point).
2. **Volumes**: Place Actors -> `TutorialTriggerVolume` (class `ATutorialTriggerVolume`, amber box; resize the `Box` component).
   Details -> **CodexTactics|Tutorial**: `Step Id` (step to start), `Beat Id` (beat to start: checkpoint + lesson rule),
   `Volume Id` (the name steps use in `"trigger": {"type": "volume", "volume": ...}` / `"complete": {"volume": ...}`; empty =
   Step Id), `Required Beat` (fires only from that beat on), `Activated By` (Only The Leader / Any Operative), `Fire Once`.
   Without steps / with the tutorial off it does nothing and is not used up.
3. **Markers**: Place Actors -> `TutorialMarkerActor` (`ATutorialMarkerActor`: ground ring + bobbing beacon, visible in the editor,
   in game only while active). `Step Id` (shown while that step is on screen) or `Beat Id` (shown during that beat); `Ring Radius Cm`,
   `Show Beacon`, `Beacon Height Cm`, `Screen Edge Pointer` (overlay arrow when off-screen), `Color` (amber), `Marker Material`
   (Gemini's material slot, needs a vector parameter `Color`; request #18).
4. **Steps**: `Content/Data/Tutorial/<file>.json` (staged as UFS). Root `{ "id", "version", "start_beat", "beats": [...], "steps": [...] }`.
   Beat: `{ "id", "lesson": bool (lethal hits only down), "checkpoint": bool (the "checkpoint" save at its start = the previous stage done; rev 5f: the only automatic save), "title_id" }`.
   Step: `{ "id", "beat", "kind": hint_card | objective | radio | walk_talk, "text_id", "keys": [command ids or literal keys],
   "trigger": { "type": manual | beat_start | volume | event | after_step | first_use | failure_retry, "volume", "event", "step",
   "command", "count", "delay_s" }, "complete": { "commands": [...], "count", "hold_s", "event", "distinct_operatives", "volume", "timeout_s" }, "once", "skippable",
   "blocking" (always false), "lines": [ { "speaker", "text_id", "duration_s" } ] }`. No trigger = `beat_start`.
   **2026-10-12 additions (Demo 01 A0-A1 redesign):** kind `feed` (its line(s) go to the message feed at once, no display slot,
   completes immediately: short reactions while a card / speech is up). Beat and step `"lock": [...]` / `"unlock": [...]` (command id
   patterns, `"*"` = all; beat: at its start; step: when shown, or when it finishes without showing; unlock first, then lock): a
   locked `Order.Sprint` walks, the try raises `Tutorial.Locked` / `Tutorial.Locked.Order.Sprint` (trigger a hint with
   `{"type": "event", "event": "Tutorial.Locked.Order.Sprint"}`); saved with the progress; tutorial OFF = no locks. Trigger type
   `condition` + `"trigger.requires": [step ids]` (the trigger counts only after those steps were triggered; works for every trigger
   type) + step `"condition": {"stat": ..., "at_least" | "below": N}` (or an array = AND; stats `squad_cold_max`, `squad_cold_min`,
   `leader_cold`, `squad_heat_packs`): for a `condition` trigger it fires the step; on any step it is re-checked when the step is due
   (after `delay_s`) and a failed check skips it silently (its `after_step` followers still fire). Step `"action": {"type":
   "cold_bump", "amount": 20, "min_level": 42}` (or `"actions": [...]`): run when the step is SHOWN (a walk-and-talk waits for the
   speech slot). Step `"focus": "coldest_operative"`: the card / feed text may use `{focus}` (his name), he gets a floating
   "COLDEST" line, `FTutorialCardView::FocusOperative / FocusName` for the WBP. Unknown stat / action / focus, a `condition`
   trigger without a condition and `requires` naming an unknown step are parse ERRORS. Events for loot: `Loot.Open` (command, crate
   window opened), `ItemLooted` / `ItemLooted.<Item>` (`HeatPack`, `Medkit`, ... - taken into an operative's kit); heat pack used:
   `ItemUsed.HEAT_PACK`. Patterns:
   exact, `"Prefix.*"`, `"*"`. **`"precredit"` (2026-10-13, default true):** a once-only hint card completed by COMMANDS whose completion
   the player's command history already satisfies when the card becomes due (uses / hold time since the steps loaded, recorded even while
   a radio plays or a card is not up) is completed at once without showing ("✓ <title> - already done." in the feed, telemetry
   `precredited`); fewer uses than needed seed the card's progress (one Q press before the Q/E card = 1 of 2). `"precredit": false` for
   lessons that must happen at that moment (regroup, the sprint card, loot open, the tripwire). Event / volume completions and `"once":
   false` cards never precredit. The history is saved in the tutorial section (`"history"`) and forgotten by RESET TUTORIAL. Lock targets
   today: every player command family (rev 5b progressive unlock, see the block at the top); `Order.Sprint` degrades to a walk,
   everything else is refused by its handler; each refusal raises `Tutorial.Locked.<Id>` once per try.
   `"distinct_operatives": true` (2026-10-12): each operative's event counts once, so `count` = how many
   different operatives must do it ("put each operative into cover": `{"event": "CoverEntered", "count": 3, "distinct_operatives": true}`;
   "enter cover 4 times": `{"event": "CoverEntered", "count": 4}`). **`hold_s` (2026-10-12):** accumulated seconds a CONTINUOUS command was
   active while the card is SHOWN (never during the card breather, never while merely queued), in addition to `count` uses:
   `{"commands": ["Camera.Pan.Keys", "Camera.Pan.Edge"], "hold_s": 1.0}`. Continuous inputs report ONE use when the action starts and
   then `PlayerCommands::ReportHold(id, dt)` ticks (`FPlayerCommandContext::HeldSeconds` > 0, same bus; ticks are never uses and never
   fire `first_use`); `FTutorialStateMachine::HandleCommandHold`, rule `TutorialRules::IsProgressMet`. A wildcard is a plain prefix match
   so it spans segments (`Camera.*`, `Camera.Pan.*`). **Camera ids (2026-10-12, were Camera.Pan / .Rotate):** `Camera.Pan.Keys` (WASD, hold ticks),
   `Camera.Pan.Edge` (cursor at the screen edge, hold ticks), `Camera.Pan.Drag` (MMB drag, hold ticks), `Camera.Rotate.Keys` (Q / E, one per
   press), `Camera.Rotate.Mouse` (RMB drag, hold ticks), `Camera.Zoom` (one per wheel notch); key hints in the registry = the F1 Controls
   screen rows (6 camera rows now). **Beat A0 is STAGED:** `A0_cam_pan` (WASD / edge, hold 1 s) -> `A0_cam_rotate_keys` (Q/E x2) ->
   `A0_cam_rotate_mouse` (RMB drag, hold 0.5 s) -> `A0_cam_zoom` (wheel x2) -> `A0_move`; no timeout (skippable); texts
   `TUT_A0_CAM_PAN / _ROTATE_KEYS / _ROTATE_MOUSE / _ZOOM` (draft file). The card counter is per beat ("n / 5" in A0, move included). Sample: `demo_01_steps.json` beat A6 (`A6_*`, volume `VOL_A6_FIELD`). A hint card without completion gets `timeout_s` 12; radio / walk-and-talk steps without completion
   end when their lines have played. Body text may contain `{key:Squad.Solo}` (-> `[B]`). Unknown keys / commands are warned in the
   log; the unit test `CodexTactics.Tutorial.Schema.DemoFile` fails on any warning of `demo_01_steps.json`.
5. **Texts** live in the narrative sheet (Tutorial tab, request #17) -> `narrative_manifest.json` `"tutorial": { "<id>":
   { "title_en", "text_en", "radio_en", "speaker_en" } }`; a walk-and-talk `text_id` may also name a `sequences` entry. Until then
   the overlay shows `[EN missing: <id>]`.
6. **Overlay look**: a WBP child of `UTutorialOverlayWidget` set as the HUD's `TutorialOverlayWidgetClass`, with widgets named
   `TutorialHintCard` (Border), `TutorialCardTitle`, `TutorialCardBody`, `TutorialCardKeys`, `TutorialCardProgress`,
   `TutorialCardDone` (TextBlocks), `TutorialSkipButton`, `TutorialTracker` (VerticalBox) + `TutorialObjective1..3`,
   `TutorialRadioStrip` (Border), `TutorialRadioSpeaker`, `TutorialRadioLine`, `TutorialRadioHint` (TextBlock, rev 5b),
   `TutorialRadioSkipButton` (Button, rev 5b; make it non-focusable), `TutorialRetryPanel`, `TutorialRetryText`,
   `TutorialRetryButton`, `TutorialContinueButton`. Keep panels Self-Hit-Test-Invisible (only the buttons take clicks).
7. **Testers' log**: `Saved/Tutorial/tutorial.jsonl` (one JSON per line: session, map, event shown / completed /
   completed_silently / skipped / timed_out / beat_started / lesson_downed / retry / enabled / disabled, step, beat, active_s, game_s).

