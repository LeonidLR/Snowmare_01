# Session State — CodexTactics (UE 5.8 port)

<!-- STATUS -->
Epic: Godot → UE port (vertical slice per docs/port/REFERENCE_PLAYTHROUGH_01.md)
Feature: playtest fixes (tremor / facing / stances) + Gemini's TANDEM requests 1-3 (enemy upper-body hits, pause abuse, Marksman enemy)
Task: Unreal is the reference: level JSON at runtime, Wave Editor in Tools/WaveEditor, playtest bot + run telemetry (done); next = balance with the bot (user decision), more parity items from PORT_MATRIX
<!-- /STATUS -->

**The full handoff (state, system map, traps, next steps, change log) is `docs/port/HANDOFF.md`. Read it first.**

## Mode (user, 2026-09-28)
- Full port with identical behaviour; work autonomously through PORT_MATRIX / HANDOFF §8, commit each verified
  system, ask only on real design forks. Godot project is read-only.
- Gemini may take over at any time: keep HANDOFF.md, PORT_MATRIX.md and TANDEM.md current with every commit.
- The user may close / reopen the Unreal Editor for builds (graceful close only).

## Checkpoint 2026-10-09 (end of day, main 763ca9c pushed)
- Done today: F9 quick load + black load cover (4b31144); Sniper-pack death clips for operatives / Marksman (4c75003);
  Female Soldier = Medic-Sapper body + sniper rifle only for her, fires kneeling / prone only (c2478dd, ac61f69);
  TB facing snap-back, cover-entry pops, kneel reload lift (5816996); TB shots resolve after turn / kneel (763ca9c).
- User WIP NOT committed (do not touch / commit without asking): BP_Operative, BP_Enemy_Marksman, L_MovementTest,
  DefaultEditor.ini, DefaultScalability.ini, ABP_Operative_Rifle2 deletion. Backups: Saved/Backups/user_wip_20261009_*.
  L_PatrolTest is the committed (old) version; the user may restore Saved/Autosaves/.../L_PatrolTest_Auto1.umap themselves.
- Open (ask the user): enemy TB shots with the same turn -> shot order; sniper pose in cover (now M4 cover clips);
  real sniper rifle mesh (DA_Weapon_sniper_rifle.HandMesh); KIA in saves; save-slot playtime; Options screen.
- Rule: back up the user's uncommitted files and compare hashes before every merge into main (memory backup-before-merge-main).

## Last verified
2026-10-05: ABP_Operative_Rifle2 — build OK, `test.ps1 -Smart` 23/0, RifleLocomotion 2/0, setup script OK (6 states / 18 transitions, compile 0 errors, clips 8/16/16/8/6), rendered HudShot rifle2 OK, Jev 88 %.
2026-10-05: Sprint 10 defense line — build OK, `test.ps1 -Smart` 43/0, SquadAutonomy + SquadROE 9/0, smokes Defend / CommanderMode / RealtimeSelect / ClickRules / CameraZone / Tripwire / Sight PASS, Jev ROE 81 %.
2026-10-05: Sprint 09 tripwire — build OK, `test.ps1 -Smart` 4/0, `Interactables.Tripwire.*` 2/0, smokes Tripwire / Inventory / Deployable / Relocation / PrepDeploy / TurnBasedDeploy / EnemyAI / Vault / EnemyTactics / Grenade / Barrel PASS; CombatFlow fixed (4/4).
2026-10-05: Sprint 08 line of sight — build OK, `test.ps1 -Smart` 0 failed, `Combat.Sight.*` 2/0, smokes Sight / SquadFire / LevelWave / WaveCombat / EnemyAI / EnemyHunt / EnemyTactics / Facing / FlankBreach / Cutter / BarricadeContact / Vault / Marksman / MarksmanAdvance / Turret / TargetedShot / CommanderMode / Deployable / Stance / BotMarksman / TurnBased / TurnBasedBarricade / EnemyDeath PASS; CombatFlow fails also with Codex.Sight=0 (free pause 17.5 s left vs 18-20 expected — open, not Sprint 08).
2026-10-05: preparation set-up by the closest running operative — build OK, `test.ps1 -Smart` 4/0, smokes PrepDeploy / Deployable / TurnBasedDeploy / Relocation / Inventory PASS.
2026-10-05: real-time order lock + «АВТО» button + 1-4 camera — build OK, `test.ps1 -Smart` 40/0, smokes RealtimeSelect / ClickRules / ActionBar / GroupSelect / Guard / Inventory / TargetedShot / WeaponSelector / CameraZone / TurnBasedCamera / TurnSelect PASS.
2026-10-05: Sprint 07 Commander Mode — build OK, `test.ps1 -Smart` 40/0 (+ SquadAutonomy 6/0), smokes CommanderMode / Movement PASS.
2026-10-04: Sprint 06-E..H — build OK, `test.ps1 -Smart` 162/0, smokes Relocation / Deployable / TurnBasedDeploy / BarricadeTurnContact / Marksman / MarksmanAdvance / BotMarksman / LevelWave PASS.
2026-10-01: 155 automation tests, all smokes ALL GREEN (`verify_all.ps1`, parallel x3) after the Marksman / enemy hit layer / pause cooldown work.

## Open questions
- none (L_MovementTest re-laid by the user is kept; smokes are layout-relative)
