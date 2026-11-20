# T-11 (second half) - the PMC's own charges belong to the PMC (report)

Agent: Claude (sub-agent, worktree `CodexTactics-claude`), branch `claude/pmc-charges-owned` from origin/main da1c6e3a. Not merged, main not touched.
Spec: roadmap T-11 + `docs/design/PMC_DEFENCE.md` D2; designer OQ-21 (2026-11-19): charges as accepted on 11-18, round PMC posts only.

## Handover

**Done.** A mine / tripwire of the generator's `pmc_charges` layer (`owner: "pmc"`) is owned by the PMC:
- the props spawn marks it (actor tag `trap_owner=pmc`) and gives it a nav area;
- it is set off by the player's side only: PMC fighters (operatives of another team, tagged enemies) walk over it unharmed;
- the PMC fighters' path filter prices that area at x1000, so their paths go round it;
- the player's side is unchanged: it spots the charge by the normal rule (reveal 3 m / mine spotting), and sets it off.

**Finding that drove the design.** Before this change PMC fighters set off ANY level charge: `AProximityMineActor::Tick` blows for every tagged enemy
(PMC fighters carry the Enemy tag, `OperativeCharacter.cpp:210`) and `ATripwireActor::Tick` trips for every operative of any team. The first
smoke run (control cases) proves it is today's behaviour for ordinary level charges, which stay unchanged.

**Left / open (check them):**
- Only owned charges changed. Ordinary level mines / wires (random `mines` layer) still hurt PMC fighters (control cases of the smoke). Designer call if
  they should not.
- Not run in a real generated PMC layout: no accepted layout has a `pmc` block yet, so `UCodexLevelPropsSubsystem` picking the owner from the props
  data (`PropsGameplayRules::IsPmcOwned` -> `AdoptCharge`) is covered by the unit test and by code reading, not by a map run. The smoke calls the same
  `AdoptCharge` the spawner calls. After the lead rebuilds X4 with «ЧВК» a `GeneratedPropsX6Smoke`-style X4 check should be added.
- Behaviour change for PMC pathing outside this task (please look): `AOperativeCharacter::ChooseMoveFilter` now returns a PMC filter for every non-player
  operative. Before, a PMC fighter's filter followed the PLAYER squad's autonomy switch (NoCrawl = known traps costly when autonomy was on, TrapLiteral
  when off). Now: always "known squad traps cost nothing, crawl rules by stance, mantle by `bMantlePathAllowed`" - what a PMC fighter got with autonomy off.
- Radius of the nav area: mine = trigger radius + 60 cm; wire = its span + 60 cm. When the squad spots the charge, the ordinary known-trap area is
  added over it; the smoke shows the PMC still goes round (333 cm vs trigger 160 cm), so the two modifiers do not cancel here.
- Files outside the strict allowed list that had to change (the behaviour lives there): `CodexLevelPropsSubsystem.cpp` (the spawn hook, 10 lines),
  `ProximityMineActor.cpp` / `TripwireActor.cpp` (one skip condition each), `OperativeCharacterMovement.cpp` (`ChooseMoveFilter`, 5 lines),
  `DeployableRules.*` (the tag + predicate; no new member in a deployable base class, so the stop rule did not trigger).

## What changed

| File | What |
|---|---|
| `Source/CodexTactics/Public/AI/PmcEncounterNav.h`, `Private/AI/PmcEncounterNav.cpp` (new) | `UNavArea_PmcCharge` (cost 1), 4 PMC move filters (`UNavFilter_PmcFighter[Prone][NoMantle]`, area cost `OwnChargeTravelCost` 1000, not excluded so a start inside still paths), `FilterFor`, `AdoptCharge` |
| `Source/CodexTactics/Public/Interactables/DeployableRules.h`, `Private/.../DeployableRules.cpp` | `PmcOwnerTag()`, `IsPmcOwned(actor)`, `IgnoresBody(owned, bodyIsPmcFighter)` |
| `Source/CodexTactics/Private/Interactables/ProximityMineActor.cpp` | Tick: PMC fighters skipped for an owned mine |
| `Source/CodexTactics/Private/Interactables/TripwireActor.cpp` | Tick: PMC fighters skipped for an owned wire |
| `Source/CodexTactics/Public/LevelGen/PropsGameplayRules.h`, `Private/.../PropsGameplayRules.cpp` | `IsPmcOwned(layerId, object)` (`owner` key "pmc", else layer `pmc_charges`; another owner wins), `PmcChargesLayerId` |
| `Source/CodexTactics/Private/LevelGen/CodexLevelPropsSubsystem.cpp` | after `FinishSpawning`: an owned mine / wire (when traps are hostile) gets `AdoptCharge` |
| `Source/CodexTactics/Private/Characters/OperativeCharacterMovement.cpp` | `ChooseMoveFilter`: non-player operative -> `PmcEncounterNav::FilterFor` |
| `Source/CodexTacticsTests/Private/AI/PmcOwnedChargesTest.cpp` (new) | `CodexTactics.AI.PmcEncounter.OwnedChargesOwnership`, `.OwnedChargesNav` |
| `Source/CodexTacticsDev/Private/Debug/PmcOwnedChargesSmokeCommand.cpp` (new), `Scripts/test_map.json` | `PmcOwnedChargesSmoke` (registry, map entry, PMC rule's smoke list) |

## Smoke (PmcOwnedChargesSmoke, L_Gen_Depot_1, no map / level JSON edit)

A PMC fighter on an ordinary mine / wire: blows / trips (control, unchanged). On an owned mine / wire (3 s): armed, nothing. The player's man on the owned
wire: trips; on the owned mine: sets it off. Navmesh paths over an open run with an owned mine in the middle: default (player) filter passes 115 cm
from it (not steered), PMC filter 333 cm (round it, trigger 160 cm), and still 333 cm after `Reveal()` (spotted).

## Gate results (worktree, `CODEX_AGENT=claude`)

| Gate | Result | Log (`Saved/Logs/T11b/`) |
|---|---|---|
| `Scripts/build.ps1` | PASS | `build.log` (first build 27 min: a shared-UBT queue with another build and a DeployableRules.h change) |
| unit filters `CodexTactics.AI.PmcEncounter`, `.LevelGen.Props`, `.Interactables` | PASS, 106 tests, 0 failed (2 new) | `test.log` |
| `PmcOwnedChargesSmoke` (new) | PASS (78 s) | `Smoke-PmcOwnedChargesSmoke.log` |
| `PeekMinesSmoke` | PASS | `Smoke-PeekMinesSmoke.log` |
| `GeneratedPropsX6Smoke` | PASS | `Smoke-GeneratedPropsX6Smoke.log` |
| `PmcPostsSmoke` | PASS | `Smoke-PmcPostsSmoke.log` |
| `PmcEncounterSmoke`, `PmcEncounterHuntSmoke` | PASS, PASS | `Smoke-PmcEncounter*.log` |
| `git diff --check` | PASS | - |

Not run: `-Smart`, the triage audit, a real generated X4 with a `pmc` block (does not exist yet).

## Next steps

1. Lead: accept «ЧВК» on X4, rebuild, then add an X4 variant (the PMC men walk the posts, the charges stay unharmed, the player's route meets them).
2. Designer: should ordinary level mines / wires also spare the PMC?
3. Check the PMC pathing note above on a long arena run (autonomy-on PMC pathing changed).
