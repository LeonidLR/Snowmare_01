# Gemini 133 — Fix `VehicleCombatSmoke` Stage 6 Flake (`Medic-Sapper` Killing Dead-Zone Hound)

Model: `Hydrogen`
Branch: `gemini/133-vehicle-smoke-flake` (from `origin/main` `7bcb850c`)

## Root Cause & Fix

- `VehicleCombatSmokeCommand.cpp` Stage 0 placed dismounted `Medic-Sapper` ~32 m from the APC (`(-2500, -3500)`) and set `Squad->SetSquadPosture(ESquadFirePosture::Aggressive)`.
- During Stage 6 (`dead zone: a hound 1.5 m off the hull is never fired at`), `Medic-Sapper` auto-fired at `Close` (`BP_Enemy_Hound_C_1`, 45 HP) with her M16 and intermittently killed him within the 4 s wait (`source 'Medic-Sapper'`), failing `!IsDead(Close)` even though the APC turret had `0 shots` and `11-15 refusals`.
- Set `State.Medic->bHasPostureOverride = true; State.Medic->PostureOverride = ESquadFirePosture::Passive;` in `VehicleCombatSmokeCommand.cpp` Stage 0 (cleared automatically at Stage 9 when `Squad->SetSquadPosture(ESquadFirePosture::Passive)` runs) so dismounted `Medic-Sapper` never interferes with turret Stages 4–8 before she is positioned in front of the APC for the Stage 17–18 braking check.

## Verification

| Check | Scope / Command | Result |
|---|---|---|
| Dispatched build | `machine_dispatch.py build --lane CodexTactics-133` (`worker`) | `Result: Succeeded` (`21.72 s`) |
| Impact map | `Scripts/test.ps1 -CheckMap` | `Impact map OK` |
| Unit tests | `Scripts/test.ps1 -Filter "CodexTactics.Vehicle" -SkipSmokes` | `24 tests, 0 failed` |
| Smoke (3x) | `machine_dispatch.py smoke --lane CodexTactics-133 --smokes VehicleCombatSmoke,VehicleCombatSmoke,VehicleCombatSmoke --jobs 1` (`worker`) | `PASS 3/3` (`45.2 s`, `44.4 s`, `38.7 s`) |
