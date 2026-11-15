# Review: Task 054 (box select in formation disbands the squad into Solo)

No HIGH findings.

The list below was checked against the diff only. The signatures of `EnterSoloModeInternal`, `SetSelectedGroup`, and the `PlayerTick` / `OnClickReleased` box-gating code were not visible in the patch.

## [MEDIUM]

- **[MEDIUM] Call signatures need checking.** `DisbandFormationToSoloBox` calls `EnterSoloModeInternal(false, false)` and `SetSelectedGroup(BoxedOperatives, true)`.
  - Existing call sites in the diff use one argument each: `EnterSoloModeInternal(false)` and `Squad->SetSelectedGroup(Selected)`.
  - The headers in this patch do not add a `bAnnounce` or `bSwitchLeader` parameter.
  - If those parameters do not already exist, this is a compile error. If they exist with different meanings, the comments (`bForTrap`, `bAnnounce`, `bSwitchLeader`) are wrong.
  - Confirm both signatures.

- **[MEDIUM] The old leader is stopped even when he is on his own order.**
  - The comment says "stop the old leader if moving a formation order", but the code calls `OldLeader->StopOperative()` with no condition.
  - This contradicts the existing `SquadOrderRules::ShouldStopOnBecomingLeader(bOnOwnOrder)` rule: an operative's own order runs on and only formation moves are stopped.
  - If the old leader is among the boxed operatives, his active order is cancelled.
  - Step 4 does the same to every non-boxed member: `StopOperative()` cancels own orders and actions as well as formation-follow moves.
  - Use `IsOnOwnOrder` / `ShouldStopOnBecomingLeader`, and stop only formation-driven movement.

- **[MEDIUM] Boxed followers may keep walking an old formation move.**
  - Only non-boxed members and the old leader are explicitly stopped.
  - Boxed non-leader followers that were mid formation-follow are left alone, unless `EnterSoloModeInternal` already halts followers. That may depend on the `CVarSquadSoloIndependent` path.
  - If it does not, they keep walking toward stale formation slots while selected in Solo.
  - Either verify that `EnterSoloModeInternal` handles this, or apply the same formation-move stop to boxed members.

- **[MEDIUM] The smoke test only passes in headless and does not test the real path.**
  - In headless, `SelectInBox` returns 0, and the test calls `DisbandFormationToSoloBox` directly. The controller integration (`SelectInBox` → `ShouldSwitchToSoloOnBoxSelect` → disband) is never exercised.
  - With a real viewport, the ±10000 box selects all three members and switches to Solo with 3 selected. The checks `GetSelectedGroup().Num() == 2` and `!Contains(Members[2])` would then fail.
  - The test asserts on a hard-coded `Boxed` array instead of whatever the box actually captured.

- **[MEDIUM] Unchecked indexing in the smoke test.**
  - `Members[0]`, `Members[1]` and `Members[2]` are indexed without checking `Members.Num() >= 3` or for null entries.
  - A smaller squad causes an array out-of-bounds crash instead of a failed `Check`.
  - Add a guard that calls `Finish(State, false)`.

- **[MEDIUM] Box-start gating in input code is unverified.**
  - Only a comment changed in `PlayerTick`. In `OnClickReleased`, the `bDragged` branch still returns early with no `SelectInBox` call.
  - The removed comment says that branch existed specifically for formation mode, where box selection was disabled.
  - Confirm that the code which starts and draws the box and calls `SelectInBox` is gated by `IsBoxSelectionAllowed` and not by a direct `IsSoloMode()` check.
  - Otherwise, a drag in formation mode silently does nothing, and the feature never triggers in real play.

## [LOW]

- **[LOW] The BlueprintCallable entry point has no input validation.** `DisbandFormationToSoloBox` does not check that entries are non-null, are squad members, or are alive. It also does not check for turn-based combat. Null or foreign actors are passed straight to `SetSelectedGroup`; filter them first.

- **[LOW] Unused parameter.** `IsBoxSelectionAllowed(bool bSoloMode, ...)` no longer uses `bSoloMode`. This may produce an unused-parameter warning under stricter warning levels. Mark it unnamed or `/*bSoloMode*/`, or drop it.

- **[LOW] Duplicate `LOCTEXT` key.** `LOCTEXT("SquadSender", "SQUAD")` is now used in `SquadSubsystem.cpp` as well. This is fine only if that file defines a `LOCTEXT_NAMESPACE` and the same key is not given different text elsewhere in that namespace.

- **[LOW] Misleading message.** The Solo message says "(N operatives selected)" even if `SetSelectedGroup` drops invalid entries, so the count can be wrong.

- **[LOW] Missing assertion in the smoke test.** No check confirms that the leader after disband is one of the boxed operatives, which is what `bSwitchLeader = true` is meant to guarantee.