# PMC order policy - Learning Agents probe P0 (2026-11-23)

**Branch `ai/la-probe` only** (off `ai/r1-fallback-cover`). UE-only, no Godot reference. A neural policy (UE `LearningAgents` plugin,
Experimental 0.2 of UE 5.8) can pick the ORDER of each PMC defender in the PMC arena in place of the utility scorer, and a PPO training
loop runs in the arena. Walking, aiming and the cover search stay `UPmcSquadSubsystem::ExecuteOrder`'s. Status: compiles, the unit
tests pass - among them the brain's inference step and the session's PPO loop against the plugin's real trainer object with a stand-in
for the Python process, both in a scratch world - and the train path is wired; **no arena round and no training run was made yet**
(the lead runs it): the Python process, the shared memory exchange and what the policy learns are untested.

## Key files

| File | What |
|---|---|
| `Source/CodexTactics/Public/AI/PmcOrderProvider.h` | `IPmcOrderProvider` + `FPmcOrderRequest`: the commander's hook (one batch call per team decision). |
| `Source/CodexTactics/Public/AI/PmcPolicyRules.h` | Pure rules, no plugin types: the action set, the action mask (the scorer's gates), the observation vector, the step reward. Tests `CodexTactics.AI.PmcPolicy.*`. |
| `Source/CodexTactics/Public/AI/PmcPolicyInteractor.h` | `UPmcPolicyInteractor : ULearningAgentsInteractor` - observation / action schema, action modifier (mask). |
| `Source/CodexTactics/Public/AI/PmcPolicyBrain.h` | `FPmcPolicyBrain : IPmcOrderProvider` - manager + interactor + policy on a transient host actor, the team step, save / load. Inference needs nothing else. |
| `Source/CodexTacticsDev/Private/Debug/PmcPolicyTraining.h` | `UPmcPolicyTrainingEnvironment` (rewards / completions) and `FPmcArenaPolicy` (the arena's session: critic, PPO trainer, the Python process). Dev module only. |
| `Source/CodexTacticsDev/Private/Debug/PmcArenaCommand.cpp` | The arena keys `Policy=` / `PolicyFile=` / `PolicyPython=` / `PolicyMinutes=` / `PolicyRounds=` / `PolicyDevice=`. |
| `Source/CodexTactics/Private/AI/PmcSquadSubsystem.cpp` | `SetTeamOrderProvider`, the two-pass decision of a team with a provider, `IssuePick`. |
| `Scripts/Tools/la_trainer/smart_open.py` | Local-file stand-in for the `smart_open` package, used only when the trainer's Python lacks it. |

Tests (`Scripts/test.ps1 -Filter CodexTactics.AI.PmcPolicy`): `Actions`, `Mask`, `Observation`, `Reward` (pure rules) and `BrainStep`
(`Source/CodexTacticsTests/Private/AI/PmcPolicyRulesTest.cpp`: the brain in a scratch world - masked sampling over 12 decisions, greedy
picks repeat, save / load into a second brain, a file of another network shape refused, a fallen agent leaves); `TrainerLoop`
(in `PmcPolicyTraining.cpp`, because the session lives in CodexTacticsDev: two scripted rounds through `ULearningAgentsPPOTrainer` with a
stand-in `IExternalTrainer` - six episodes / 17 steps in the replay buffer, every reward, the iteration's mean episode reward, the saved
files).

Modules: `CodexTactics` links `LearningAgents` + `Learning` (inference); `CodexTacticsDev` (DeveloperTool, never in Shipping) adds
`LearningAgentsTraining` + `LearningTraining`. Plugin `LearningAgents` is enabled in `CodexTactics.uproject` (it enables `LearningCore`,
`NNERuntimeBasicCpu`, `PythonMLPackages`).

## The hook (and why the default path is unchanged)

`UPmcSquadSubsystem::Decide` scores the orders per fighter (`PmcCommanderRules::ScoreAll`), picks (`PickCommitted`) and executes. The
second half of a fighter's decision (skip when nothing changes, `ExecuteOrder`, the two retries with the scorer's next best, the statistics)
moved verbatim into `IssuePick`. Without a provider (`FTeam::OrderProvider` unset - a null weak pointer) `Decide` calls `IssuePick` for each
fighter right after his `PickCommitted`, i.e. the same statements in the same order as before; the only additions on that path are the
`MatesReloading` counter (no score reads it) and one null check per fighter. One seed still gives one fight.

A team with a provider (`SetTeamOrderProvider(TeamId, Provider)`) decides in two passes: pass 1 builds a `FPmcOrderRequest` per deciding
fighter (view, squad view, scores, commitment, the scorer's pick), then ONE `Provider->PickOrders(TeamId, Weights, Requests)` call (a
neural policy evaluates its agents together), then pass 2 issues the picks. In pass 2 each pick goes through the scorer's gates again
(`PmcPolicyRules::GatePick`), the fighter's view is rebuilt (the mates' orders of this pass claimed their cover slots in between) and
`IssuePick(..., bProviderPick = true)` runs it: a pick that cannot be taken fails and cools down - it is NOT replaced by the scorer's
next best (the experience must say what the policy chose).

## Actions and the mask

One exclusive discrete action over nine defender orders, in this order: Hold, TakeCover, Suppress, Reload, FallBack, Objective, Ambush,
Crouch, RpgStrike (`PmcPolicyRules::ActionOrders`; Advance / Flank / Heal / Relocate / Sidearm are not given by the policy).

The scorer's safety gates are the action mask (`PmcPolicyRules::AllowedOrderMask` -> `MaskedActions` -> the interactor's
`MakeAgentActionModifier`, which the plugin applies when it samples and which the PPO trainer receives):

- an order is allowed only while the scorer scores it above 0 and it is not cooling down after a failure;
- a running active order (not Hold / Ambush / Crouch) that still scores and is younger than `order_min_hold_s` (3 s) is the only one
  allowed (no walk re-decided twice a second);
- Hold scoring 0 next to a scored order is ruled out too (a posted defender before contact must walk to his post: only Ambush);
- nothing allowed = Hold. A fighter that does not decide this tick (ladder, mid-drop) can only sample Hold and nothing is executed.

The policy does not see the scores, only which orders are open.

## Observations

`PmcPolicyRules::BuildObservation` - 48 floats from `FPmcFighterView` / `FPmcSquadView` / the objective view only, all 0..1 (the bearing
pair -1..1): health, clip, ammo, reserve, in cover, cover quality, cover available, exposed, crouching, prone, moving, under fire,
suppressed, fire line, launcher score, at post, has a defence post, distance to the point (/50 m), on the point, inside the zone, threat
known / seen / distance (/100 m) / in range / age (/10 s), the threat's bearing off his line to the point (cos, sin), attention off,
enemies known / seen (/6), mates alive (/5) / reloading / suppressing (/3), contact, shots exchanged, quiet seconds (/30), time fraction,
hold fraction, enemy on the point, and the running order as a one-hot over the nine actions. `FPmcSquadView::MatesReloading` is new
(counted in `Decide`, read by no score). The enum `PmcPolicyRules::EObservation` is the layout; a saved policy only loads into the same
layout (the snapshot's compatibility hash is checked).

## Rewards and episodes

`PmcPolicyRules::StepReward` (the P0 numbers): **+1** per attacker killed (to every living defender - no kill attribution), **-1** the
agent's own death, **+3** the round won, **-3** the point captured, **-0.02** per second an attacker stands on the point. One episode =
one defender in one round: it ends (a termination) with his death or with the round. The round's end terms go to the survivors only.
A step = one commander decision of the team (`decision_interval`, 0.5 s).

## Arena keys

`Policy=off|train|infer` (default `off` = today's arena). The policy always plays the round's DEFENDING PMC team (side A or B by the
seed's parity with `Opponent=Pmc`; always A with `Opponent=Script`); the attackers keep the scorer. It needs `Objective=1` and a PMC
side B. An unknown mode refuses the run, like an unknown key.

| Key | Default | Meaning |
|---|---|---|
| `PolicyFile=<base path>` | `Saved/Arena/Policy/pmc_policy` | Project-relative or absolute. Files `<base>.encoder.bin`, `.policy.bin`, `.decoder.bin` (+ `.critic.bin`). `train` goes on from existing files, `infer` needs them. |
| `PolicyPython=<python.exe>` | env `CODEX_LA_PYTHON`, else `<project>/Intermediate/PipInstall/Scripts/python.exe` | The trainer's interpreter (needs `torch` and `numpy`; `smart_open` or the project's stand-in). |
| `PolicyMinutes=<m>` | 0 | `train`: wall-clock budget; the run ends after the round in which it passed. |
| `PolicyRounds=<n>` | 2 | `train`: rounds per PPO iteration (the replay buffer holds exactly their `squad x n` episodes). |
| `PolicyDevice=cpu\|gpu` | cpu | Where Python trains (gpu falls back to cpu without CUDA). |

The result JSON gets `"policy"` (the run's mode) and `"policy_run"` (file, python, iterations, episodes, steps,
`last_mean_episode_reward`, failed) at the root, and per round `"policy": "<mode>"` + `"policy_side": "A" | "B" | ""`.

## How to start a training run

```
& "C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "<repo>/CodexTactics.uproject" /Game/Maps/Generated/L_Gen_Depot_1 `
  -game -nullrhi -nosound -nosplash -unattended -NoTelemetry "-LevelJson=L_PmcArenaDepot.json" -UseFixedTimeStep -FPS=60 -FixedSeed `
  "-ExecCmds=CodexTactics.PmcArena Rounds=100000 Seed=1060 Opponent=Script Scenario=street Squad=3 TimeLimit=120 Policy=train PolicyMinutes=10 PolicyFile=Saved/Arena/Policy/p0 PolicyPython=C:/Work/Lanes/la-venv/Scripts/python.exe" `
  "-abslog=<repo>/Saved/Logs/PmcPolicyTrain.log"
```

- Use forward slashes in `PolicyPython=` / `PolicyFile=` (the console splits the command at spaces; no spaces in the paths).
- Per PPO iteration the log has `[PmcPolicy] iteration N: mean episode reward R over K episodes (S steps), ...` (C++ side) and the Python
  trainer's `Iter: N | Avg Rew: .. | Avg Rew Sum: .. | Avg Return: .. | Avg Episode Len: ..` (`LogLearning`). At the end
  `[PmcPolicy] training over: ...` and `[PmcPolicy] picks ...` (how often each order was picked).
- Evaluate: the same command with `Policy=infer` (no `PolicyPython` / `PolicyMinutes`), fixed `Rounds=` and the baseline's seeds.
- **Which Python.** The plugin's own spawn only knows `<project>/Intermediate/PipInstall/Scripts/python.exe` (the editor's pip-install
  venv). The arena builds the trainer process object itself (`UE::Learning::FSharedMemoryTrainerServerProcess`, which takes any
  interpreter) and runs LearningCore's `Content/Python/train.py` with `learning_core.train_ppo` in `PolicyPython`. Before spawning it
  runs `python -c "import numpy, torch"`: a missing package fails the arena with that message instead of a timeout. The plugin's
  `train_common.py` also imports `smart_open` (it only opens local files with it here): a Python without that package gets
  `Scripts/Tools/la_trainer` (a local-file stand-in module) on the trainer process's `PYTHONPATH`, with a `[PmcPolicy] smart_open is not
  installed ...` line; once the real package is installed the stand-in is not used. Checked by hand with the lead's venv
  (`C:/Work/Lanes/la-venv`, torch 2.5.1+cu124, numpy 1.26.4, no smart_open): `learning_core.train_ppo`, the shared memory communicator
  and `nne_runtime_basic_cpu_pytorch` import with the stand-in on the path.
  `-LearningAgentsUseExternalTrainingProcess` (the plugin's switch) skips the spawn and attaches to a trainer started by hand.
- The trainer's working files (configs, logs) go to `<project>/Intermediate/LearningCore/PmcPolicy<N>/`.
- While Python trains an iteration the game thread waits (the fixed-step world clock does not move), so the fights stay as they are.

## Traps

- **No automatic pip install.** `PythonMLPackages` (pulled in by the plugin) lists torch 2.5.1+cu124 and more as pip requirements; with
  the editor's default the next editor start (also `-game` runs and the test runner) would download ~2.5 GB into
  `Intermediate/PipInstall`. `Config/DefaultEngine.ini` sets `bRunPipInstallOnStartup=False`. The editor still creates the small
  `Intermediate/PipInstall` venv itself (offline, from the engine's wheels) and lists the missing packages in the log.
- The trainer's Python needs `numpy` and `smart_open` besides torch (`learning_core/train_common.py` imports them at the top); the
  arena covers a missing `smart_open` with its stand-in (above), nothing covers numpy / torch.
- `ULearningAgentsPPOTrainer::EndTraining` re-applies the game user settings (`UGameUserSettings::ApplySettings`: resolution request,
  scalability levels, a save of GameUserSettings.ini) as its "revert" - at the arena's end that is harmless; the `TrainerLoop` test
  puts the editor process's scalability levels back afterwards.
- `ULearningAgentsManager` must be registered BEFORE `SetMaxAgentNum` (sized first, `OnRegister` adds every id twice).
- The replay buffer cuts an episode that does not fit (its end, with the round's reward, is lost) and drops the rest of the batch: the
  buffer is sized by EPISODES (`squad x PolicyRounds`), with room for full-length ones, so it fills exactly at a round's end.
- `ULearningAgentsPPOTrainer::BeginTraining` applies its game settings (fixed step, physics step, max FPS, VSync); the session passes
  the launcher's own values so the arena's clock is untouched.
- A policy file is tied to the observation layout, the action count and the network shape (2 x 64, no memory cell); loading checks
  input / output sizes, the compatibility hash and the byte count before any network is replaced.
- Shipping / packaged targets were not built with the plugin (the runtime module now links `LearningAgents`).

## Open questions for P1

- The reward is individual for deaths and shared for kills (no kill attribution in the arena); the round's end goes to survivors only.
- The policy sees no scorer scores; feeding them as observations would let it start from the scorer's behaviour.
- Heal / Relocate (the sector shift) are not in the action set.
