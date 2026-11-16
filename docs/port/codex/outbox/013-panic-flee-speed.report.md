# 013 - Panic flee speed checkpoint

Status: INCOMPLETE; no gameplay edits or implementation checks. Stopped before code edits because weekly usage reached 95% (5% remaining), invoking the user low-limit stop instruction relayed by the coordinating agent. Branch: codex/013-panic-speed.

## Diagnosis

- `Source/CodexTactics/Private/Characters/PanicComponent.cpp:218` forces Standing when panic begins, discarding the operative's current stance.
- `Source/CodexTactics/Private/Characters/PanicComponent.cpp:304` directly writes `MaxWalkSpeed` from `Config.FleeSpeed * 100` (or the current speed times `Config.SpeedMultiplier`). This does not use the normal stance and wound speed calculation and its fallback can compound across ticks.
- Normal movement computes state-specific speed in `Source/CodexTactics/Private/Characters/OperativeCharacterMovement.cpp:621-633`, using `OperativeMovementRules::ComputeMaxSpeed`, cold scaling and `GetWoundTierSpeedScale`. Reuse existing configurations and rules rather than introducing speed constants.

## Next steps

1. Read the remaining targeted panic/runtime/smoke files and normal movement/wound configs; implement pure `PanicFleeSpeed` in PanicRules with stance/wound/config inputs and focused unit cases.
2. Integrate in PanicComponent, preserve the starting stance and clamp flight to its normal allowed speed. Check recovery stance behavior as part of the same allowed file.
3. Extend PanicSmoke with measured prone badly-wounded movement, including a nonzero-motion assertion and the configured crawl-speed ceiling times 1.05.
4. Run `Scripts/build.ps1`, `Scripts/test.ps1 -Filter CodexTactics.Characters.Panic`, and `Scripts/smoke_parallel.ps1 -Commands CodexTactics.PanicSmoke,CodexTactics.InjuredLocomotionSmoke,CodexTactics.ProneCrawlSmoke -ExtraArgs ''-Multiprocess''`. Preserve logs, then run the TypeSafe diff audit before each explicit-path commit.

## Validation

No source changes, build, unit tests or smokes performed at this checkpoint. Documentation-only pre-commit audit recorded in the checkpoint commit workflow. All requested implementation checks remain pending.

## Router trial

2 | gpt-6.1-sol | checkpoint (not pass; implementation and validation pending)