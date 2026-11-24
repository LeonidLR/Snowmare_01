# Codex 108-merge-gate-smoke-mapping (after 107b)

Lead 2026-11-24: Scripts/Tools/merge_gate.ps1 printed "Smokes (all mapped): DropLinkReachSmoke, LevelGenBuildingsSmoke, ..." in the
local test triage, yet the worker step said "Worker smokes: none mapped (0,0s)" for two real C++ merges today:
- codex/103-low-cover-click (OperativeCharacterVault.cpp + DepotVaultSmokeCommand.cpp) - log Saved/lead_gate_103.log
- grok/G014-pmc-grenade (PmcSquadSubsystem.cpp, PmcCommanderRules.*) - log Saved/lead_gate_G014.log
while the 4-branch gate (Saved/lead_gate_1124b.log) did map 5 worker smokes. So C++ gameplay merges can pass the gate with NO smoke.
Also: smokes whose test_map.json map is ARCHIVED (DepotVaultSmoke on L_Gen_Depot_1, the worker_daily_smokes.json list - 18 of them on
L_MovementTest) are refused by smoke_parallel; the gate must either retarget them (-Map X6 -ForceMap where valid) or say so loudly.
Fix: find why the worker list comes out empty, make the gate fail (or warn red) when Source/ changed and no worker smoke ran, and
update Scripts/Tools/worker_daily_smokes.json to smokes on the 5 kept maps. Tests for the selection logic. Branch codex/108-gate-smokes.

Also (Lead 2026-11-24 19:35): worker_sync -ShipHead bundles unpushed commits but NOT their LFS objects - the worker reset failed
("smudge filter lfs failed" on docs/port/codex/outbox/107-ai-training.png). Fix: run `git lfs push origin <ref>` for the shipped ref before
the worker reset (objects only, refs stay unpushed), and print LFS-FAILED instead of RESET-FAILED when the smudge is the cause.
