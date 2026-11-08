# Squad of six: fire teams, N-scalable formations and the "Tactics" skill

> Design document, 2026-11-07 (Claude Fable 5.1, lead designer / systems architect pass). Answers the game designer's question of
> 2026-11-07 ("two threes or a four and a two? or formations that work for 2..6? and recruits who cannot move in formation until
> Tactics 80 %"). Scope: design + systems impact + phased plan. **No code, data or map was changed for this document.**
> Every code claim below was either re-verified by hand (marked *verified*) or comes from a code scan done for this document
> (marked *scan*; line numbers are of main at 3c5b7844 and may drift). Guesses are marked **(guess)**.
> Related: `docs/port/TASK_BOARD.md` roadmap (#37 squad of 6, #57 wall corners, #35 blizzard, #47 director), `docs/systems/squad-and-orders.md`,
> `docs/design/FORMATION_SECTORS.md`, `FORMATION_CONTRACT.md`, `SUPPRESSION.md`, `WEAPONS_BALLISTICS.md` (Gemini draft, unreviewed),
> `docs/systems/pmc-squad-ai.md`.

## 0. The recommendation in one paragraph

Build **one N-scalable formation core (N = 1..6)** first, then add **fire teams as an optional layer** on top of it, implemented as
"a second formation group" rather than a new command system (Option C below). Do not build fire teams as the primary answer (Option A):
the game is solo-primary, the player already splits the squad ad hoc with box selection, and a mandatory team layer adds a third command
mode to a game that just spent a month making two modes (solo / formation) behave. Do not stop at "formations of six" either (Option B
alone): a six-man diamond does not fit the generated streets, and the content that motivates six men (a mortar crew, an RPG man, an APC
with infantry) is by its nature *two groups moving with different rules*, so a group object is needed anyway - for the PMC AI too, which
today has **no formation at all** (*scan*: no `Formation` symbol under `Source/CodexTactics/*/AI/`). For recruits, replace the single
**80 % hard gate** with **three tiers (Untrained < 40, Drilled 40-79, Tactical >= 80)** on a new `Tactics` stat that lives in the existing
XP / stat-point progression, plus a small use-based "drill" gain from walking behind a veteran. An untrained recruit is never *unable to
play* - solo orders always work, which is the primary mode - he is unable to *hold a formation place*: he tags along behind the rearmost
place, with no look sector, and breaks under fire. The gate is on *responsibility* (formation leader, wall-end corner man, mortar gunner),
not on *presence*.

## 1. Goals, pillars, constraints

**Player fantasy.** You are the commander of a handful of named people you care about (Gorky 17: every loss is a person, not a
unit) crossing a frozen town. Most of the time you move them one by one, from cover to cover, and the game rewards reading terrain.
When you must cross open ground, a street, a yard between buildings, you move them *together* - and the squad behaves like trained men:
sectors covered, rear watched, corners sliced. Growing to six is not "more firepower"; it is *more kinds of problem*: who carries the
tube, who stays with the ammo, who spots, who guards the APC's flank.

**Pillars (in priority order).**
1. **Solo is primary** (user, 2026-11-03). Every level starts in solo; formation is a tool for complex moves. Anything added for six men
   must make solo *not worse*: six individual orders per bound is already the cost of six men, and no new mode may be *required* to play.
2. **Pause-and-plan readability.** In the tactical pause the player must see at a glance: who is where, who looks where, who is in which
   group, who is a liability (an untrained recruit). Six portraits, six sectors, two group brackets - still readable on a PC screen.
3. **Nothing the squad does is magic.** A formation is a set of places and looks; a fire team is a set of men with a leader; a recruit's
   weakness is visible (he lags, he does not look, he dives). The player should be able to predict every behaviour from what he sees.
4. **One body, one rule set** (pmc-squad-ai.md 1): whatever the player's men can do in formation, the PMC must be able to do through
   the same rules and the same order catalog. A design that only the player can use is half a design.
5. **Determinism and parity.** The TypeScript simulator is the reference for pure formation rules (contract 2-3); every new rule is TS
   first, golden vectors, C++ parity. The 14 k existing golden cases must stay green (contract 3.1).

**Constraints.** The squad never splits *by accident* (v2.1); the player's manual choice is never cleared by the system; no box selection
inside formation (T6); solo -> formation clears the box; the 3 custom slots carry across levels; turn-based combat is per unit and knows
nothing of formations. Not a constraint: readability on a phone (PC game).

## 2. The three options

### 2.1 What exists today (the baseline every option starts from)

| Thing | State on main | Where |
|---|---|---|
| Roster | 3 rows (Commander, Engineer, Medic-Sapper) + Ivan Susanin as `EOperativeRole::Recruit`, joins as member 4 | `CodexTacticsGameMode.cpp:93-105` (*scan*), `DeployableRules.h:9` (*verified*: `Commander, Engineer, MedicSapper, Recruit`) |
| Selection | keys 1-4 only; **5 and 6 are the stealth lure** (`IA_StealthWhistle` on `EKeys::Five`, `IA_StealthThrow` on `EKeys::Six`) | `CodexTacticsPlayerController.cpp:144-148, 172-173` (*verified*) |
| HUD | the action bar builds exactly 4 slots; labels by *index* `{ "CMD", "ENG", "MED", "IVAN" }` | `ActionBarWidget.cpp:73, 210` (*verified*) |
| Formation roles | `EFormationRole { Commander, Right, Left, Rear }`; `FormationShapeRules::Roles(6)` returns 4 and a test **locks that in** (`"Roles of 6 clamp to 4"`) | `FormationTypes.h:18`, `FormationShapeRulesTest.cpp:46` (*verified*) |
| Runtime | a 5th member falls back to the old slot maths (`return false; // a 5th member and beyond: the old slot math`); the wall order refuses > 3 followers (`Others.Num() > 3`) | `SquadSubsystemFormation.cpp:976, 1460` (*verified*) |
| Custom formation | `MaxMembers = 4` (*verified*), members past it are cut from the JSON (*scan*) | `FormationCustomRules.h:83` |
| TS reference | `rolesFor` 1/2/3/else-4; bots and golden loop over `[2, 3, 4]`; `CUSTOM_MAX_MEMBERS = 4` | `Tools/TrainingGround/src/movement/rules.ts`, `custom.ts`, `golden.ts` (*scan*) |
| Carry / save | campaign carry matches members **by role first**, then squad index; the JSON save matches by name, then role | `CampaignCarry.cpp:123-131`, `SaveGameSubsystem.cpp:1718-1727` (*scan*) |
| Turn-based | units come from `GetMembers()`, no fixed count | `TurnBasedCombatSubsystem.cpp:187` (*scan*) |
| PMC | `pmc_squads.count` 1..6; fighters stand in a line; a per-fighter utility commander; **no leader, no formation slots** | `PmcEncounterRules.cpp:59`, `PmcSquadSubsystem.cpp:357, 1629` (*scan*) |
| Progression | XP -> level (max 10, `level x 250` XP), 3 stat points / level over `EProgressStat { Health, Luck, Accuracy, Fortitude }`, caps health 200 / luck 60 / accuracy 100 / fortitude 50; saved and carried | `ProgressionRules.h:11-35` (*verified*), `CampaignCarry.h` (*scan*) |
| Skills / perks / morale on operatives | **not found**; panic (`UPanicComponent`, stress 0-100, fortitude cuts stress 2.5 % a point up to 65 %) and rage exist; no suppression-resistance stat | `PanicRules.h`, `RageRules.h`, `SuppressionRules.h` (*scan*) |
| Godot archive | `AllegianceType { SQUAD_CORE, PMC, SURVIVOR ("high panic, doubled stress"), NEOPHYTE (a disguised cultist who betrays) }` - **not ported** (`PORT_MATRIX.md:47`: only Godot tests) | `Scripts/components/allegiance_component.gd` (*scan*) |

Two things to say out loud. First, "the PMC AI uses formations" is a **plan** (TASK_BOARD: "строй и стены у ЧВК через каталог"), not a fact.
Second, the squad already has an ad-hoc team mechanic in solo: a box-selected group walks together (`SquadFormation::ComputeGroupTargets`,
rows of two) and takes covers together (the group cover order). The question is only what a *persistent* team in formation mode adds.

### 2.2 Option A - fire teams (2x3, 4+2, teams of 1, attachments); formations stay at 2..4

**Idea.** The squad of six is two teams, Alpha and Bravo, each a formation of its own (pair / triad / diamond - the shapes that exist and
are proven), each with a leader. The player commands a team as he commands the squad today. Teams cooperate by the player alternating
them: one holds at a wall (corners held), the other moves; a flank is "Bravo, go round".

**Commanding.** Team selection = select any member of it (the team's leader gets the orders, as the squad leader does today) or a
team hotkey; the squad selection cycles with `Tab` **(guess, see Q4)**. Team creation: in solo, box-select the men, then a "detach"
action -> they are Bravo, with the selection's leader as team leader; "merge" dissolves it. Teams persist across solo / formation
(solo = everyone holds, as today, teams remembered). Team of 1 = the existing per-man **Guard** hold (such a man "is not in the
formation anyway", wall leadership rules) - no new concept needed. Attachments (a sniper on a roof while five move) = a team of 1.
Team orders = the orders the squad has today, given to the team's leader: move, wall / group cover, corner peek, formation shape (hold-B
menu applies to the team of the selected man), hold. Cooperation orders (bounding overwatch, "cover move": A suppresses while B moves) are
**not** new mechanics: they are a Suppress order on A plus a move on B, i.e. two clicks in a pause - exactly how the game designer dropped
"bounding overwatch" as a *formation* on 2026-10-25 (FORMATION_SECTORS "Dropped by the user: bounding overwatch, door stack"). A one-click
"cover move" macro can come later if play-tests ask for it.

**HUD.** The portrait bar groups portraits with a bracket per team and a small letter (A / B); the selected team's bracket is lit; the
formation HUD line ("Echelon left -> Column (wall)") shows per team. Turn-based: nothing changes (per unit). Custom formation: per team
(3 slots each, or 3 slots total - Q7).

**What A gets right.** Every formation stays 2..4 men wide - the shapes, sectors, wall layouts, corner stacks and 14 k golden vectors are
reused as they are; a triad + triad fits streets that a six-man diamond never will; it mirrors real doctrine; crews (mortar: gunner +
bearer) are teams by nature.

**What A gets wrong (why it must not be the only answer).**
- It makes a **third command mode**. The game just spent weeks making two modes behave (solo holds, Regroup, the followers-in-cover rule,
  wall leadership); a team layer multiplies the leader-switch cases: two leaders, two wall orders, two peeks, two guard trackers. Every
  bug class of the last month ("selecting B cancels A's order") returns squared.
- **The user's own content contradicts "always two teams."** The APC combined formation is "APC + infantry outside" - six men round one
  hull is one formation of six places, not two teams. A mortar crew of 2-3 + a rifle element of 3-4 is two teams - but on the march the
  user will want all six to walk as one column through a passage (never split, v2.1). A rule set that cannot express "six as one" fails
  the first street.
- **The runtime is a singleton.** `USquadSubsystem` has one leader, one `FFollowerState` set, one guard tracker, one slide, one peek, one
  wall group, one ramp file. Making it per team is the expensive part of A *and* of C (section 4), so A's apparent "reuse" buys less than
  it looks: the shapes are reused, the runtime is rewritten either way.

### 2.3 Option B - one scalable formation system for N = 2..6

**Idea.** Roles stop being four enum values and become *places in a template*; every formation is generated for N; sectors come from
the coverage allocator (already generic over members: K front, the rear guard, the others share the open arc); the wall layout places N
men along the wall; the trail column is `i x spacing` back along the leader's path (already generic); hedgehog = a ring of N.

**Slot templates per N (proposal; 2..4 must stay bit-identical to today's shapes so the golden stays green).**

| N | Open ground (diamond family) | Column (narrow) | Wall (group cover) | Blizzard |
|---|---|---|---|---|
| 2 | pair: K, Z behind the right shoulder (as today) | K, Z | K 7:30 / partner 4:30 (as today) | pair 1.2 m |
| 3 | triad K, P, Z (as today, `triad.rear_back` 4.2 m) | as today | Z-left 8 / K 6 / P 4 (as today) | tight triangle |
| 4 | diamond K / P L / Z (as today) | as today | 4th at the roomier end (as today) | tight diamond |
| 5 | **lozenge** K / P L / C / Z - a centre man C 2.8 m behind the P-L row, Z the rear guard 8.4 m back **(guess: numbers)** | K + 4 on the trail, 5th = rear guard | K + 2 each side, 1 m apart; end men on the corners | tight double file (see Q9) |
| 6 | **double wedge** K / P L / C / P2 L2 - the last row of TWO rear guards splitting the rear (5 and 7 o'clock) **(guess)** | K + 5 on the trail | K + 2 left + 3 right (the roomier side gets 3); overflow when the wall is short: a second rank prone 1.5 m behind (the group cover's "who does not fit prone behind it" rule, reused) | tight double file |

Rule of the template: rows alternate centre / pair / centre / pair ...; the *last filled row* holds the rear guard(s); places are dealt by
minimal travel (contract 10.6, generic); the coverage allocator shares the rest (contract 12.2, generic; the "member with no gap doubles
K's or the rear guard's share" clause already handles centre men with nothing to look at). Looks in a narrow column of N: alternate
"forward over the shoulder" (+15) and "back at a slant" (-165) by index parity, the last man 180 (Q8). Wall ends with N: the two end men
hold the corners at `wall_corner_look_deg` 112 (v2.7), the middle men sweep between the corners in turn - this is exactly #57's rule and
it is N-generic by construction. Blizzard with N >= 5: the 1.2 m tight diamond cannot hold six; a tight *double file* (two files 1.2 m
apart, 1.2 m spacing) is the shape in which nobody is more than 1.7 m from a mate - which is what #35 ("the blizzard separates the squad")
needs: *distance to the nearest mate*, not the shape name, is what the blizzard rule should test.

**What B gets right.** One code path; the mortar crew's "extra men" are just places; the PMC squad of 6 is the same object; the custom
formation only needs `MaxMembers` 6; testing is one smoke parametrised over N.

**What B gets wrong.** (1) A double wedge of six is 5.2 m wide and 8.4 m long: in the generated maps (3.6 m passages, 1.5 m stairs) it is
the trail column 80 % of the time; the user will see "they always walk in a line" and call it a bug. (2) Six places round one leader
means six men move at the leader's pace and the leader's path - a crew that must *stay* (the mortar, deployed 3.5 s, "cannot move while
deployed", WEAPONS_BALLISTICS 4.2) and a rifle element that must *move* cannot be one formation. (3) The APC needs a leader that is not a
man (the hull). All three say: a *group* object is needed anyway. B alone is a good first step and a bad final state.

### 2.4 Option C - the hybrid (recommended)

**One N-scalable core (B) as the only formation engine, and "formation groups" as its unit of execution.** A formation group =
`{ leader (an operative, or later a vehicle), members, shape request, guard / terrain tracker, follower states, wall order, slide, peek,
ramp file }` - everything `USquadSubsystem` holds for "the formation" today, moved into one struct that the subsystem owns **1..2** of.
Default: ONE group with every formation-capable member (so the game plays exactly as today with 3, and as B with 6). Fire teams = the
player (or a crew template, or the PMC commander) asking for a SECOND group. Hard limit 2 groups for the player (readability); the PMC
side may run one group per squad.

How each existing feature maps:

| Feature | With one group (default) | With two groups |
|---|---|---|
| Solo (B tap) | squad-wide, everyone holds (unchanged) | squad-wide; both groups remembered, re-form on Regroup |
| Box selection | solo only (T6), ad-hoc group move / group cover (unchanged) | solo only; a box across two teams is just a box (teams are a formation-mode concept) |
| Detach / merge | - | `Shift+B` with a box selection in solo = the selected become group B (their leader = the selection's leader), the rest group A; `Shift+B` with no box = merge; the hold-B menu gets no new item (its ring is full) **(guess, Q4)** |
| Leader switch (keys 1-6, portrait) | as today | selecting a man selects *his group's leader* for orders unless he IS a leader; `Tab` cycles the two leaders **(guess)** |
| Formation shape (hold-B) | the group | the *selected* group; HUD line per group |
| Custom formation | up to 6 places (`MaxMembers` 6), "a member the shape lacks walks behind the rearmost place" stays | per group, 3 slots total (Q7) |
| Wall work, corner peek, wall leadership (#57) | the group's wall; N-generic ends / middle sweep | each group its own wall; a corner peek hands the lead **within the group** |
| Guard (per-man hold) | unchanged = a "team of 1" | unchanged |
| Turn-based grid | per unit, unchanged | per unit, unchanged |
| PMC | a squad = one group, the commander's Move on its leader; the fighters take places and looks (the first real consumer of the group object - TASK_BOARD "строй и стены у ЧВК") | a PMC squad of 6 = two groups (the commander's Advance "no mate advancing" is already bounding-overwatch-shaped) |
| Mortar crew (P4) | - | a crew = a group with a role template (gunner leads; bearer within 3 m; the "spotter" is a *condition* - any living operative with LOS, WEAPONS_BALLISTICS 2.5 - not a place) |
| APC + infantry (P4) | - | the vehicle is the group's leader; places relative to the hull; the hull is a moving wall for ClearLook; the vehicle is speed-capped to the patrol pace while the group is attached |

### 2.5 Comparison

| Criterion | A: fire teams only | B: N-scalable only | C: hybrid |
|---|---|---|---|
| Player value | high for crews / overwatch; **negative** for the solo-primary player who must now manage teams to use formation at all | "six walk together" works; crews / APC impossible | B's value at once; teams when the content needs them; nothing forced |
| Readability | two brackets, two formation lines - fine; but 3 modes to learn | six portraits, six sectors - fine | as B by default; brackets only when a team exists |
| Implementation cost in THIS codebase | L: the singleton runtime -> per team (the hard part) + UI; shapes reused | M: roles -> places (`Roles`, `LocalSlot`, `EchelonIndex`, `Layout`, `ChainPairs`, `RoleForFollowerSlot`, the 4-clamps in `SquadSubsystemFormation*.cpp`), TS rules + golden for 5/6, HUD / keys / saves | M (P1 = B) + L (P2 = the group object) - but P2 is needed for the PMC and the APC regardless, so it is not *extra* cost, it is *ordered* cost |
| Risk | high: two leaders x every leader-switch rule of the last month | medium: wide shapes fold forever in streets; `AssignByTravel` is an N! search (720 at 6 - fine); crowd avoidance in 80 cm corridors with 6 (the ramp file exists) | B's risks in P1, A's in P2; each phase shippable alone |
| Testing | smokes x 2 teams x every wall scenario | one smoke parametrised over N = 2..6; TS red team `--members 5,6` | both, in order |
| AI (PMC) | the PMC would need the team layer to use formations at all | the PMC gets a formation but no crews | the PMC uses the same group object; `squad=6` arena rounds as two groups |
| Verdict | no | a good P1, a bad end state | **yes** |

## 3. The "Tactics" skill and non-tactical recruits

### 3.1 Who they are

The story (bible on `gemini/t4-narrative-bible`, not on main; brief in `GEMINI_WEEKEND_2026-11.md:97-110`): the three comrades (Sally the
commander, Roger the marksman / medic-sapper, Babai the engineer) went through the training camp together - the prologue "The Quad"
teaches "Wedge, Line, File". They are **Tactical from the first level** (Tactics 100 / 90 / 90 **(guess)**). Ronin's PMC are ex-military:
Tactical (90). Everybody else the squad "picks up" is not:

| Recruit type | Example | Start Tactics (guess) | Notes |
|---|---|---|---|
| Civilian scout | Ivan Susanin (exists: `EOperativeRole::Recruit`, joins as member 4) | 10-20 | the first and best pilot case: he already exists, is expendable, has his own balance prefix `susanin_` |
| Resistance fighter | the user's example; local partisans | 40 (Drilled) | can shoot, cannot hold a sector; a cheap "second rifle" |
| Rescued soldier / deserter | a GBR trooper, a PMC defector | 60-80 | one level away from Tactical |
| Neophyte | the user's word; in Godot `AllegianceType.NEOPHYTE` = a disguised cultist who betrays | 25 | **do not** couple betrayal to Tactics: allegiance is a narrative mechanic (reveal / turn hostile), Tactics is a competence. If the neophyte is a traitor, he is a traitor at any Tactics. Port allegiance separately if the story needs it (Q13) |

### 3.2 What "cannot move in formation" must mean (and why a hard gate is the wrong shape)

Critique of the 80 % hard gate as stated: (1) it is invisible until it bites - the player recruits a man, presses B, and one man does not
come; (2) it punishes *recruiting*, the thing the game wants the player to do; (3) 80 % of *what*? a bar the player cannot see progress on
is not a goal, it is a surprise; (4) in a solo-primary game a man who "cannot move in formation" still does 80 % of what any man does -
so the gate changes little and feels arbitrary. The user's underlying instinct is right and should be kept: **an untrained man is a
liability in a formation, and training him is a thing the player invests in.** Make the liability visible, gradual and manageable:

| Tier (Tactics) | In formation he... | Orders he takes | Under fire (Suppressed tier) |
|---|---|---|---|
| **Untrained** (< 40) | has NO place: he *tags along* behind the rearmost place of his group at 2 m, facing his walk, no look sector, no wall layout place (he kneels 1.5 m behind the line), never a corner man, never the leader; he is NOT counted by the coverage allocator (no sector is "held" by him) | every solo order (move, cover, fire, stance, use, heal) - solo is primary and he is fully playable there; group cover takes him as the prone man behind | breaks: drops prone or runs to the nearest shielding cover (the existing under-fire manoeuvre, with the formation's grace removed), and does not rejoin until the fight ends |
| **Drilled** (40-79) | takes a place, but only in the **last row** (rear / centre; never P / L of the first row), slot reaction slowed: follower gain x (0.5 + Tactics / 200) **(guess)**, his look scans at half amplitude; may be a wall *middle* man, never an end man; never the leader | all orders; may be in a team, never its leader | holds his place while the leader stands; walking under fire he stops and goes prone (no return fire from the move) |
| **Tactical** (>= 80) | full: any place, leader, corner man, point man, mortar gunner | all | as today |

Why 40 / 80 and not one 80: two thresholds give one *visible* promotion ("he can take a place now") halfway, and keep the user's 80 as the
mark of a real soldier. Why gates at all (and not only a continuous modifier): the *leader* and the *corner man* are responsibilities
where a half-trained man wrecks the squad's shape (the whole stack forms behind him) - those must be binary. Everything else (lag,
scan amplitude, break-under-fire chance) is continuous in Tactics so that progress is felt every level, not twice.

### 3.3 How Tactics grows

The game already has the machine: XP -> level -> 3 stat points -> `IncreaseStat` with caps, shown in the profile dialog (key P), saved
and carried. The honest option is to **add `Tactics` as the fifth `EProgressStat`** (cap 100, `tactics_per_stat_point` 5 **(guess)**):
the player chooses to invest in the recruit, the UI exists, the save exists, parity with the four other stats is free. From 40 to 80 is
8 points = 3 levels of a recruit - roughly one map **(guess; pace to be checked against `exp_reward_*` in DA_GameBalanceConfig)**.

Add ONE use-based source, small and capped, so that a recruit who *walks with* the veterans improves without the player spending points:
**drill** - +1 Tactics per minute spent tagging along / in a place behind a member with Tactics >= 80, while not in a fight, capped at
60 **(guess)**. Rationale: it is the fiction ("he learns by watching Sally") and it rewards exactly the behaviour the user wants (moving in
formation), without grinding past Drilled - the last 20 points come only from points, i.e. from fights. Not recommended: a pure use-based
skill (invites AFK walking; invisible progress), a trainer *building* (no base camp exists), or events-only (too rare to feel).

Mentor perk (optional, after P3): Sally's bible trait "stabilizes teammate aim when in tight formation" becomes a perk "Instructor": drill
rate x2 behind him. Cheap, narrative, and the first perk the game would have (none exist today).

### 3.4 Interactions

- **Fortitude / cold / panic.** Untouched. Panic already has `HoldOnLeader` and per-role stress multipliers; a recruit with low Fortitude
  AND low Tactics is the Godot SURVIVOR ("high panic, doubled stress") without a new system. Do not add "morale": it does not exist and
  the panic component is that system.
- **Suppression.** No per-operative resistance stat exists; do not add one now. Tactics decides only *what he does* when Suppressed
  (3.2), not how fast the level rises.
- **Blizzard (#35).** The blizzard rule should be "a man farther than X from his nearest mate loses the squad"; the Untrained man's 2 m
  tag-along and the Drilled man's slowed follower gain make recruits the first to be lost - a consequence the player can see coming.
- **Director (#47).** A recruit's break-under-fire is a tension lever the director can read (one more "open sector" input) - no design
  needed now, just expose the tier.
- **Roster, bench, permadeath.** Max 6 in the field = the roster; no bench and no base camp in P3 (nothing exists to hang one on). If the
  story ever gives more than six living recruits, an "assign to the field" screen at level start is a P5 item. Permadeath as today
  (`killed_in_action`, the campaign carry's fallen list): a dead recruit is gone; the three comrades' deaths are the story's business.
- **PMC.** The PMC fighters are Tactical (90) and use the same group rules; a `tactics` field in `pmc_operators.json` lets the designer
  spawn a green PMC squad (a Drilled line unit) for variety and for the trainer to learn against.

## 4. Systems impact

### 4.1 Data
- `EProgressStat` + `Tactics`; `AOperativeCharacter::Tactics` (default by role: the roster rows get a `Tactics` field in `FSquadMemberSpawn`
  and the generator's `<role>_tactics` tunable); `ProgressionRules::StatCap` for it; the profile dialog's fifth row.
- Save: `SaveOperative` + `tactics`; `FCarriedOperative` + `Tactics`; **carry / restore must key members by `SquadIndex` + name, not by
  role** - two Engineers (a recruit with the Engineer balance prefix) would both get the first Engineer's record today (`FindFor`,
  `RestoreSquad` - *scan*). The save does not store `SquadIndex` at all (*scan*: 0 hits) - add it.
- Roster: `SquadRoster` grows to 6 rows with their own spawn `Offset`s; `RecruitSusanin` must stop using the hard-coded `SquadIndex 3`
  (`RecruitSubsystem.cpp:115` and three smokes - *scan*); the balance prefix per role (`OperativeBalance.cpp:11-13`) needs a rule for new
  roles (Q2). `MissionSubsystem.cpp:328` `Roles[3]` / `Supply[3]` (level loadout by role) and `WaveVictorySubsystem.cpp:202-216` (one pointer
  per role) assume one member per role (*scan*).
- `formation_config.json`: new keys for N >= 5 (`row.back`, `row.side`, `rear_pair.split_deg`, blizzard double file); the per-count keys
  (`triad.*`, `diamond.rear_back`, `echelon.sector_4`, `line.fan_*`, `wall.neighbour_deg / end_deg`) stay for 2..4.

### 4.2 Formation code hard-coded for 3 / 4 (what P1 generalises)
Pure rules (TS first, then C++): `rules.ts rolesFor` and its `n === 3` branches (11 places), `custom.ts CUSTOM_MAX_MEMBERS`; C++
`FormationShapeRules::Roles / EchelonIndex / LocalSlot / Slots / TrailSlots / HedgehogSlots`, `FormationWallRules::Layout` (cases <= 1 /
2 / 3 / else-4), `FormationGuardRules::ChainPairs`, `FormationSectorRules` (echelon sectors 1..4), `FormationCustomRules::MaxMembers`,
`FormationRuntimeRules::RoleForFollowerSlot` (slots 0..2), `AssignByTravel` (`To.Num() > 4` -> empty), `KeepEndAfterSlide`
(`MemberCount < 4`). Runtime: `SquadSubsystemFormation.cpp` (`Min(..., 4)` at 692-693, 1597; `Clamp(1, 4)` at 489; the "FourthMan" lookups;
`UpdateSectorFollower`'s 5th-member fallback at 976; `OrderFormationWall`'s `Others.Num() > 3` at 1460), `SquadSubsystemFormationGuard.cpp:375`,
`SquadSubsystemFormationSlide.cpp:157, 419`, `SquadSubsystemWallGroup.cpp:272-274` (`Labels[] = { Right, Left, Rear }`). Model change: a
**place index** (0 = K, 1..N-1 in template order) replaces the four-value role in the runtime; the role enum stays for the 2..4 golden
and for logs ("P", "L", "Z") and gains `Centre`, `Right2`, `Left2` **(or an index - Q6)**.

### 4.3 HUD, selection, orders
Keys 1-6 (`SelectActions` One..Four -> One..Six; the Slate pre-processor's key list; `HandleSquadNumberKey`; `PlayerCommands.cpp` "1-4";
the HUD hint "press 1-4"); **move the stealth lure off 5 / 6** (Q3). `ActionBarWidget` 6 slots, `BarRoleTags` by role not by index,
`HandleSlot0..3` / `HandleBadge0..3` generalised. `SelectMember` indexes `Members[RosterIndex]` in real time but `SquadIndex` in
turn-based (*scan*) - unify on `SquadIndex` before adding men (a fallen member shifts the array). Custom placement: keys 1-6, "at most
four" text. Minimap friendlies: generic (*scan*).

### 4.4 Smokes and tests that assume 3 (from the scan; re-check before P1)
Strict equalities: `FrozenFactoryStartSmoke:164`, `GateExitLakeSmoke:317` (carry 3), `PmcTeamSmoke:163`, `SusaninSmoke:116, 151, 159, 161`
(member 4 = Susanin), `FormationBypassSmoke:594`, `FormationLabSmoke:1455`, `FormationCustomSmoke:469` (4), `RegroupAfterLossesSmoke:223`
and `SaveCoverageSmoke:769` (2 after a loss), `QuickFireSmoke:250` (`Members[1] / [2]`), `SniperSmoke:224`; tests `SquadSpawnRulesTest:44`,
`MovementAndFormationTest:204-210` (roster 3 + colours), `FormationShapeRulesTest:46` (the clamp - to be inverted), `CombatLabRunCollectorTest:181`,
`FormationPlacementTest:56`, `FormationCoverRulesTest:109` (`{3, 4}`). About 70 smokes compare `Num()` with 3 / 4; most are `< 3` wait-gates
that still pass. Rule for P1: the default roster stays 3 rows (so nothing moves until the designer adds rows) and every N-generic smoke
takes `Members=N` with Susanin-style extra spawns.

### 4.5 Training Ground sim and the PMC commander
TS: `rolesFor`, shapes for 5 / 6, `cli/bot.ts` / `redteam.ts` / `train.ts` member filters (`<= 4`), `golden.ts` loops `[2, 3, 4]` ->
`[2..6]`; new golden files for the row template; the red team with `--members 5,6`. PMC: P2 gives the commander a `FormUp` catalog entry
(the group object with the squad's `Mates[0]` as leader) and a `Detach` for 6-man squads; the arena's `squad=6`.

### 4.6 Phased plan

| Phase | Content | Done when | Size (guess) |
|---|---|---|---|
| **P0 design review** | this document; the designer answers section 6; the TS row template agreed; `formation-sectors.html` gets the 5 / 6 schemes | answers recorded in FORMATION_SECTORS.md "v3.0"; contract section 19 written | 1 day |
| **P1 N-scalable core** (= B) | TS rules for N = 5, 6 + golden; C++ parity; place index in the runtime; wall layout N + overflow rank; keys 1-6, action bar 6, stealth keys moved; `SelectMember` by `SquadIndex`; carry / save by index + name; roster 6 rows (default still 3); custom `MaxMembers` 6; `FormationLabSmoke Members=2..6`; `FormationShapeRulesTest` inverted | parity 0 diffs; FormationLab, WallSectors, GroupCover, SquadCornerPeek, WallLeadership smokes green at N = 3 AND N = 6; every strict-count smoke above green; a 6-man walk of `L_Gen_Depot_1`'s route in the bot report with checks 1-11 green at N = 6 | 5-7 days (Opus), TS part 2 days |
| **P2 formation groups** (the C layer) | `FFormationGroup` struct; `USquadSubsystem` owns 1..2; `Shift+B` detach / merge; `Tab`; HUD brackets + per-group line; PMC `FormUp`; arena `squad=6` | all P1 smokes green with one group; a two-group smoke on L_FormationLab (A holds the wall with corners, B moves through, merge, both re-form); PmcArena 6 v 6 with formations, no stuck events above baseline | 6-9 days |
| **P3 Tactics / recruits** | `EProgressStat::Tactics`, tiers, tag-along, last-row rule, break-under-fire, drill gain, profile row, save / carry, Susanin as the pilot, `pmc_operators.json tactics` | `SusaninSmoke` extended: he tags along, takes no sector, dives under fire; after 8 points he takes the rear place; a Tactical man unchanged (golden parity untouched - the rules take only formation-capable members) | 3-4 days |
| **P4 crews and the APC** | crew template (gunner / bearer), deploy layout; vehicle-led group; hull as a wall; speed cap | the mortar fires with the bearer within 3 m, the crew holds while the rifle group bounds; the APC walks a street with 4 men outside, nobody in the hull's path | after the weapons / APC systems; 5+ days |

## 5. Risks and de-risking

| Risk | De-risk |
|---|---|
| Six-man open shapes fold into the column in every generated street - "they always walk in a line" | prototype the row template in the TS sim on `L_Gen_Depot_1`'s layout (the level generator already exports walls); measure the share of time in the trail column at N = 3 vs 6; if > 60 % **(guess)**, make the 5 / 6 default a *staggered column* (`column` with 60 cm stagger - exists) instead of the double wedge, and keep the double wedge for "open" wider than 12 m |
| Crowd avoidance with six in 80 cm corridors and on ramps | the ramp file exists (`RampFileRules`); run `LevelGenRampSmoke` at N = 6 in P1; six is where the "two men meeting head-on" open item will bite - budget a fix |
| The runtime refactor (P2) regresses the leader-switch / Regroup / wall leadership rules | P2 starts by moving state into one group *without* a second instance (a pure refactor, every smoke green), then adds the second instance behind a CVar `Codex.Squad.Groups` (A/B) |
| Golden parity breaks for 2..4 while adding 5..6 | the template is used only for N >= 5 in v3.0; 2..4 keep their literal shapes; a parity run (`CodexTactics.Formation.Parity.*`) is the gate of every P1 commit |
| Vision-cone / sight cost x2 (six cones, six sight queries at 0.2 s) | #38's fixed ray budget is per squad ("one layer"); measure `ConePerfSmoke` at 6; the sight subsystem's per-man queries scale linearly - acceptable, verify |
| Turn-based fights get long with six units | a real gameplay cost; measure a Depot turn-based fight's wall-clock per round at 3 vs 6 in `TurnBasedSmoke`; if the player's turn doubles, consider "end turn for the rest" (one key) - a P5 item, not a blocker |
| The Tactics tiers make a recruit feel useless | the pilot is Susanin on the Lake with the designer; the tag-along must visibly *try* (he kneels behind the line, he looks at the squad, he runs when they run); if it reads as "broken follower", the Untrained tier is cut to "Drilled with a slower gain" |
| Carry / save by role collides once two men share a role | P1 moves the key to `SquadIndex` + name with a migration for v3 saves (read role when the index is missing) |
| Keys 5 / 6 are the stealth lure | decide in P0 (Q3); the number row belongs to selection, every RTS player expects it |

## 6. Open questions for the designer (each with my recommended answer; "yes" accepts it)

1. **Option.** Go with C (one N-scalable core first, fire teams as a second formation group later, max 2 groups)? - **Recommended: yes.**
2. **New roles.** Which roles do members 5 and 6 get? WEAPONS_BALLISTICS 4.3 (Gemini draft) proposes Squad Leader / Pointman-Scout /
   Marksman / Combat Engineer / Heavy Gunner-AT (RPG-7) / Support Gunner (mortar). - **Recommended: keep the three comrades' roles as
   they are (Commander, Engineer, Medic-Sapper with the sniper rifle) and add two `EOperativeRole` values `Grenadier` (RPG-7) and
   `Mortarman`; the sixth field slot is for a recruit (`Recruit` role, any Tactics) - so the story's "pick-ups" have a place.**
3. **Keys 5 / 6.** Move the stealth whistle / throw off the number row to free 5 and 6 for selection? - **Recommended: yes; whistle on `7`,
   throw on `8` as the minimal change (or a key you name).**
4. **Detach / merge and team switch.** `Shift+B` with a box selection in solo = detach the selected as team B; `Shift+B` with no box =
   merge; `Tab` cycles the two team leaders? - **Recommended: yes (the hold-B ring is full and B's tap / hold are taken).**
5. **Six as one.** When one group holds everyone, the default open-ground shape for 5 / 6 is the lozenge / double wedge of section 2.3
   (two rear guards at 5 and 7 o'clock for six)? - **Recommended: yes, pending the street-share measurement of section 5 (fallback: a
   staggered column).**
6. **Roles vs places.** Keep the K / P / L / Z names for the first four places and name the extra places `C`, `P2`, `L2` in logs and the
   HUD? - **Recommended: yes (readable logs; the golden's role order is untouched).**
7. **Custom formations.** Three custom slots in total, each storing up to six places, applied to the selected group (a group that has fewer
   men than the shape uses the first places)? - **Recommended: yes (per-group slots would be six to remember).**
8. **Narrow column looks for N >= 5.** Alternate "forward over the shoulder" / "back at a slant" by index, the last man the rear guard? -
   **Recommended: yes.**
9. **Blizzard with 5 / 6.** The tight double file (two files 1.2 m apart) replaces the tight diamond for five and six, and #35's "lost in
   the blizzard" rule tests the distance to the nearest mate, not the shape? - **Recommended: yes.**
10. **Tactics tiers.** Three tiers at 40 / 80 (Untrained tags along with no sector; Drilled takes last-row places only, never leads, never a
    corner man; Tactical full) instead of one 80 % gate? - **Recommended: yes.**
11. **How Tactics grows.** A fifth stat bought with the existing stat points (+5 a point, cap 100) plus "drill" (+1 / min walking behind a
    Tactical mate out of combat, capped at 60)? - **Recommended: yes; the numbers are tunables for the Training Ground.**
12. **Starting values.** Sally 100, Roger 90, Babai 90, PMC 90, Susanin 15, resistance fighter 40, rescued soldier 60-80? - **Recommended: yes.**
13. **Neophyte.** Keep betrayal (Godot `AllegianceType.NEOPHYTE`) a separate narrative mechanic, not a Tactics property? - **Recommended: yes;
    port allegiance only when the story bible names a traitor.**
14. **Max in the field.** Six is the whole roster - no bench / base camp until the story has more than six living recruits? - **Recommended: yes.**
15. **Order of work.** P1 (N-scalable core) in roadmap week 2 before the RPG-7; P2 (groups) only when the mortar crew / APC work starts
    (week 3), with the PMC `FormUp` as P2's first consumer? - **Recommended: yes.**

## 7. Designer decisions (2026-11-08)

The game designer answered section 6 on 2026-11-08. These answers override the recommendations above where they differ.

| Q | Decision | Consequence for the plan |
|---|---|---|
| 1 | **C, yes.** N-scalable core first, then formation groups. | as planned |
| 2 | **No fixed roles for members 4-6.** Any fighter can be added; there is no "grenadier" or "mortarman" post - any fighter can carry the RPG-7, and the mortar is a crew of at least three. The three free slots go to recruits with **any** Tactics. | no new `EOperativeRole` values; heavy weapons are equipment, not roles; the mortar crew template (P4) takes any three men; the balance prefix / carry / loadout code must stop assuming one member per role (4.1) |
| 3 | **Keys 4-5-6 select fighters; the whistle and the throw are removed from the number row** (whether the lure mechanic itself goes is to be confirmed, see open items). | `IA_StealthWhistle` (5) / `IA_StealthThrow` (6) leave keys 5 / 6 |
| 4 | **Groups like RTS control groups:** `Shift+1..4` puts the selected fighters into group 1..4; `F1..F4` switches control to group 1..4. Up to **four** groups (not two). | P2's group object must allow 1..4 instances; `F1` (controls screen), `F2` (map panel), `F3` (full map) are taken today and must move (open items) |
| 5 | **Needs a visual first.** Shapes drawn: `docs/design/squad-six-shapes.html` (5A lozenge, 5B wedge, 5C staggered column; 6A double wedge, 6B wedge + rear guard, 6C staggered column). | awaiting the designer's pick |
| 6 | Not understood - re-asked in plain words (place names in the formation vs the fighter's identity). | awaiting |
| 7 | Yes - three custom slots, up to six places each, applied to the selected group. | as planned |
| 8 | Yes - narrow column looks alternate by index, the last man the rear guard. | as planned |
| 9 | Yes - blizzard double file for 5 / 6; the "lost" rule tests the distance to the nearest mate. | as planned |
| 10 | Yes - Tactics tiers 40 / 80. | as planned |
| 11 | Yes - fifth stat (+5 a point, cap 100) plus drill (+1 / min, cap 60). | as planned |
| 12 | Yes, with **Susanin 20**; the others as proposed; the designer will set each recruit's value individually. | `susanin_tactics` 20; Tactics is a per-recruit data field, not derived from a type |
| 13 | **Separate mechanic that will affect Tactics - deferred.** | neophyte / allegiance not in P0-P4; leave a hook (a Tactics modifier source) when it is designed |
| 14 | **The squad starts with three, grows to six, and can lose fighters.** | roster 3 -> 6 over the campaign; losses as today (permadeath); no bench |
| 15 | **Order: (1) polish the three-man system; (2) extra weapons for the three - pistol, shotgun, sniper rifle, RPG-7; (3) Susanin on the map as the fourth; (4) two more fighters.** Faster proposals welcome. | phases re-ordered below |

### 7.1 Re-ordered plan (designer's order, with the proposed speed-ups)

1. **Three-man polish** (the current queue: #55 / #57 merge, AI quality, the roadmap W1 items).
2. **Weapons for the three**: pistol, shotgun, sniper rifle, RPG-7 (roadmap weapons framework). Proposed in parallel, touching no game file:
   the Training Ground TS prototype of the 5 / 6 shapes and the street-share measurement of section 5 (so Q5's fallback is decided with
   data before step 4).
3. **Susanin as the fourth** = P3 (Tactics, tiers, tag-along, Susanin as the pilot at Tactics 20) plus the cheap N-generic groundwork that
   a fourth recruit already needs: `SelectMember` by `SquadIndex`, save / carry by `SquadIndex` + name, key 4 / action bar slot 4, keys 5 / 6
   freed.
4. **Two more fighters** = P1 (shapes for 5 / 6, wall layout N, keys 5 / 6, roster 6 rows) and P2 (groups 1..4, `Shift+1..4`, `F1..F4`).
5. Crews and the APC (P4) after the weapons and vehicle systems.

### 7.2 Open items after the answers

- Q3: remove the stealth lure mechanic entirely (it drives the tutorial ambush lesson, the hound scent and enemy perception), or only take
  it off keys 5 / 6 (action-bar button / another key)?
- Q4: new keys for the controls screen (F1), the map panel (F2) and the full map (F3).
- Q5: the designer's pick from `squad-six-shapes.html`.
- Q6: re-asked.
