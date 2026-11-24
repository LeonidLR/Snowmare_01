# G019 plan — X6 sector rounds + ammo resupply

Level 3 (model router). Base: local `main` (`TrainingSectorSubsystem` / `TrainingSectorRules`).

## Diagnosis

An active sector picks one start and one PMC post (`TrainingSectorRules::Pick`) and leaves that fight running. A wiped player squad still takes the death cinematic and fails the mission (`AOperativeCharacter::HandleDied` -> `MissionRules::ShouldFailMission`). Ammo resupply exists only inside the dev arena (`PmcArenaCommand::ResupplyAmmo`): empty reserve and a low clip, no delay. Session recordings have no round events.

## Files

- `TrainingSectorRules` — round end (wipe / time limit / draw), seeded start+post pair that is never the previous pair, resupply timing. Defaults: restart 8 s, limit 600 s, `ammo_mode` resupply, delay 20 s.
- `L_Gen_Depot_1_X6.sectors.json` — those four fields at the top (line edit).
- `UTrainingSectorSubsystem` — while a sector is active: round clock, HUD line for 3 s, remove and respawn both squads after the delay, refill an empty reserve after the quiet delay. Grenades only on the fresh spawn. Cold and weather untouched.
- `OperativeCharacterHealth` — sector mode skips the death cinematic and the mission-failed path.
- `SessionRecordingRules` — `round_start` / `round_end` (result, sector, seed, duration, kills, score).
- `CodexTacticsHUD` — draws the round line.
- Tests `CodexTactics.Training.Sector` + `SessionRecording`. Smoke `SectorRoundsSmoke` on X6 sector 5. `SectorBoundarySmoke` stays in the same rule.

## Proof

Unit tests for the pure rules. `Scripts/build.ps1`. `Scripts/test.ps1 -Smart -SkipSmokes` (smokes are worker-only). Worker slot2: `worker_sync.ps1 -RemoteRoot C:/Work/CodexTactics-slot2 -ShipHead -ShareMachine -Commands SectorRoundsSmoke,SectorBoundarySmoke`. `typesafe_triage.py --audit-diff --agent grok`. No push.
