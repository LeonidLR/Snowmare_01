# Cover, peek and corner aim

> Tactical cover lives in the `Cover*` / `CornerAim` clusters of `AOperativeCharacter`. The corner peek leans the operative's head out past an edge:
> it reveals hidden traps inside a sight cone (growing reach), is the eye point of the peek sight (see [sight-and-vision-cones](sight-and-vision-cones.md))
> and after a knockdown the operative returns to the same slot unless enemies surround him.
>
> Key files:
> - `Private/Characters/OperativeCharacterCover.cpp`, `…CoverEntry.cpp`, `…CornerAim.cpp`; `Interactables/TrapSpotRules`; `Characters/SquadOrderRules`;
> - `Combat/KnockdownRules::DecideCoverReturn`; smokes `CoverSmoke`, `CornerHoldSmoke`, `PeekMinesSmoke`, `KnockdownCoverSmoke`
>
> Moved verbatim from the old `docs/port/HANDOFF.md` (split of 2026-10-19; nothing was dropped). [HANDOFF](../port/HANDOFF.md) | [system docs index](README.md) | [CHANGELOG](../port/CHANGELOG.md)

## Corner peek reveals traps (old §5)

- **The corner peek reveals hidden traps (rev 5h, every level):** `AOperativeCharacter::IsPeekingRoundCorner` (in cover, corner aim active, at a manual peek spot or the corner-hold spot) -> `UpdatePeekTrapSpotting` (called from `UpdateCover`, cluster file `OperativeCharacterCornerAim.cpp`; every `PeekSpotConfig.IntervalSeconds` 0.2 s; not in the wave fight): every `AInteractableActor::IsHiddenTrap()` (an armed hidden level mine; an armed, laid, concealed PMC wire) inside `TrapSpotRules::IsInPeekSightCone(GetCoverFireOrigin(), CoverFacingRules::CornerAimDirection(CoverSlot, CoverFacing, CornerAimOutwardDeg), Target, RangeCm 1200, HalfAngleDeg 30)` with a clear visibility trace (pawns do not block) is revealed by `RevealByPeek` (the normal spot response of part 1; the spotter is not turned, so the pose stays; extra event `TrapSpotted.Peek`). `FTrapPeekSpotConfig PeekSpotConfig` = BP_Operative Class Defaults -> CodexTactics|Cover|Peek (`bEnabled`, `RangeCm`, `HalfAngleDeg`, `bRequireLineOfSight`, `TargetHeightCm` 20, `IntervalSeconds`). **Trap groups:** `AInteractableActor::SpotEventTag` (Details -> CodexTactics|Trap) raises `TrapSpotted.<tag>` and counts in the tutorial condition stat **`traps_spotted.<tag>`** (`TrapSpotRules::ParseSpottedStatTag`, `TutorialRules::IsKnownStat`, `UTutorialSubsystem::GetConditionStat`: traps of that tag no longer hidden). Tests `Interactables.TrapSpot.PeekCone` / `.GroupEventAndStat`; smoke `PeekMinesSmoke`.
- **The peek finds traps PHYSICALLY, one by one (rev 5j, game designer 2026-10-19: "not all lesson mines light up when the peek starts").** `UpdatePeekTrapSpotting` looks from the peek sight's leaned-out eye and aim (`GetPeekSight`, the cone drawn for him) with `FTrapPeekSpotConfig` now `HalfAngleDeg` 45 (= the peek view's 90 deg cone; was 30), `RangeCm` 1200 (shorter than the 18 m enemy view: a mine lies flush with the snow) and new `SweepSeconds` 1.5: the reach grows from 0 to 12 m after he leans out (`TrapSpotRules::PeekSweepRange`, `PeekSpotElapsed`), so the mines are revealed one by one, the nearest first. The sight line ignores hidden traps AND spotted armed mines / wires (a revealed near mine's trigger shape hid the next one). Tests `Interactables.TrapSpot.PeekCone` (cone = the peek view's) / `.PeekSweep`; smoke `PeekMinesSmoke`.

## Followers in cover (old §5)

- **Followers in cover (rev 5g, same play-test):** `SquadOrderRules::ShouldLeaveCoverToFollow` (`FCoverFollowFacts`: in cover, not on his own order, not holding (Solo / camera-zone perimeter), not guarding, formation active = exploration, leader walking and NOT in / going to cover, slot > `CoverLeaveSlotDistanceCm` 400) -> `USquadSubsystem::UpdateFollower` calls `LeaveCoverToMove("follows the leader")` (+ stand up from a crouch for a standing leader; the stance clip roots him, the next repath walks him). Before, `FollowTo` refused anyone in cover and nothing took him out, so he stayed at the wall forever in group mode. `ExitSoloMode` (Regroup) takes in-cover followers out of cover too. Fights: the formation is off (`IsFormationActive`), the autonomy / ROE rules are unchanged. Tests `Squad.Orders.CoverFollowerLeavesForWalkingLeader`; `SoloRejoinSmoke` stage 108-116 (an 8 m wall: B in cover through a selection switch, stays with the leader standing / in Solo, Regroup takes him out, the leader walks 15 m away in group mode -> B leaves the cover and reaches the formation).

## Return to cover after a knockdown (old §5)

**Return to cover after a knockdown (user rule 2026-10-18, every level)** — «if enemies surround him, don't return him to cover; if a
grenade / blast just happened to explode next to him and he fell, return him to cover». `AOperativeCharacter::HandleKnockdownPhase`
remembers the cover slot (+ the cause) when a knockdown takes him out of cover; at the get-up `ReturnToCoverAfterKnockdown` asks
`KnockdownRules::DecideCoverReturn` (Off > NotInCover > OrderGiven (an order buffered while he lay wins) > TurnBased > NotABlast (pounce,
Brute blow, heavy hit) > EnemiesClose (a living enemy within `CoverReturnThreatRadiusCm` 400) > Return) and on Return orders the SAME
slot (`OrderTakeCover`; a corner slot takes up the corner hold again by the usual rules). Data: `FKnockdownConfig` / `Codex.Knockdown.CoverReturn`
(1), `Codex.Knockdown.CoverReturnThreatRadius` (400 cm) - settable in `ai_tuning.json`. Log `[Knockdown] <name> up again: <decision>`.
Tests `CodexTactics.Combat.Knockdown.CoverReturn`; smoke `KnockdownCoverSmoke` (blast -> back in 3.1 s at the same slot; pounce with two
hounds -> out; blast with a hound 2 m off -> out; blast + a move ordered while down -> the order).

