# Night trainer - evolving the PMC squad commander without Claude

`Scripts/Trainer/` trains the PMC squad commander's utility weights (`Content/Data/AI/pmc_commander.json`, one file = one
"personality") overnight / over a weekend from Windows Task Scheduler, in the headless self-play arena
(`CodexTactics.PmcArena`, branch `feat/pmc-squad-ai`). Python 3 stdlib only (+ ffmpeg for highlight clips). It never edits
`Content/` itself: the user reads `report.html` in the morning and decides (`apply_champion.ps1`).

## 1. How it works

- **Genome** = every numeric leaf of the weights JSON, addressed by JSON Pointer (`/utility/flank/weight`, `/skills/0/cooldown_s`).
  Strings / bools / null and the meta keys (`version`, `schema`, `seed`, `id`, keys starting with `_`) are never touched.
  Bounds / mutation step per gene come from the sidecar `pmc_commander.bounds.json` (`codex.trainer_bounds/1`), auto-generated
  next to the weights file when missing: `[0.5x, 1.5x]` of the baseline, clamped at 0 for a non-negative value, `[0, 1]` for 0,
  ints stay ints. Edit `min` / `max` / `step` there, or `"frozen": true` to pin a gene. A gene missing from the sidecar gets the
  defaults, so **a new skill's weights join the genome automatically** (PMC architecture: new skill -> new weights in the JSON ->
  next new run evolves them; regenerate or extend the sidecar if the defaults are not sensible).
- **Evolution** (`trainer_config.json` `evolution`): population 12 (gen 0 = the baseline + 11 mutants), elitism 2, tournament
  k=3, uniform crossover 0.7, gaussian mutation (rate 0.2, sigma = gene step). Every random choice is seeded from
  (run seed, generation), so a resumed run is identical to an uninterrupted one (unit-tested).
- **Evaluation** (`evaluation.matches`): each candidate plays one arena process vs the current **champion** (PMC vs PMC self-play)
  and one vs the **bot**-driven player squad, `rounds_per_job` (4) rounds each; all candidates of a generation share the seeds.
  Up to `arena.parallel` (2) game processes side by side, `timeout_s` 1800, `retries` 1; a crash / timeout / unparsable result
  becomes penalised crash rounds (`fitness.crash_penalty`), never fatal.
- **Fitness** (`fitness`, per round, averaged): `win*won + survivors*surv/squad + hp_lost*lost/hp_max` + a duration term on won
  rounds (1 at `target_s`, 0 beyond `+-tolerance_s`: decisive but not reckless) + capped behaviour bonuses (flanks succeeded,
  suppression seconds, ambushes, retreats survived, heals) + capped penalties (friendly fire, stuck events).
  **Close-range penalty** (`fitness.close_range {weight -4.0, close_range_m 10, shotgun_exempt true}`): "with a rifle you keep as far
  as the weapon allows". The adapter reads the round's track (`nighttrainer/track_stats.py`, BEFORE tracks are pruned) and derives per
  candidate side `close_shot_ratio` (share of its shots fired at a target closer than `close_range_m`; distance from the frames at the
  shot's tick, `units` m default / cm scaled) and `close_hits_taken` (hit shots received at that range, stat only). Round score
  += `weight * close_shot_ratio`. Shots by a shotgun are skipped when the track names the weapon (shot event or fighter `weapon`
  field containing "shotgun"); the current tracks carry no weapon, so the exemption is inactive until the arena adds it. Missing /
  unreadable track or no shots = no penalty. Remove the `close_range` key to disable.
- **Anti-overfit**: each generation the top-3 (`fresh_top_k`) and the champion are re-evaluated on FRESH seeds (same jobs, new
  seed stream); the best contender becomes champion only if its fresh fitness beats the champion's fresh fitness
  (`promotion_margin`, default 0).
- **Highlights** (per night = local date of "now - 12 h"): top-3 rounds by the arena's `highlight_score` (+ the champion's most
  decisive win), deduplicated by (seed, story). Their track files are kept in `highlights/tracks/` (all other per-round tracks are
  pruned after each generation unless `highlights.keep_all_tracks`). When the run ends (or `make_highlights.ps1`), each is re-run
  deterministically with `Record=1` (rendered offscreen + `CodexTactics.ClipRecorder` + `Scripts/Tools/clip_assemble.py`) and cut
  to a phone-friendly mp4 (960 px, crf 30, <= 25 MB) in `highlights/<id>.mp4`.

## 2. Start / stop / pause / resume

```
# quick check without the game (synthetic arena, ~20 s):
python Scripts/Trainer/night_trainer.py --mock --run-id mock-test --max-generations 3
# a real run by hand (resumes when the run id exists):
python Scripts/Trainer/night_trainer.py --run-id pmc-main [--hours 9] [--stop-at 2026-11-09T07:00] [--max-generations 200]
       [--parallel 2] [--yield-to-editor] [--population 12 --rounds-per-job 4 --seed N   (new runs only)]
# scheduler (NOT registered automatically - run one of these yourself):
powershell -ExecutionPolicy Bypass -File Scripts\Trainer\install_night_trainer.ps1 -Mode Nightly -StartTime 22:00 -EndTime 08:00 -RunId pmc-main -YieldToEditor
powershell -ExecutionPolicy Bypass -File Scripts\Trainer\install_night_trainer.ps1 -Mode Weekend -RunId pmc-main    # Fri 20:00 -> Mon 07:00
powershell -ExecutionPolicy Bypass -File Scripts\Trainer\install_night_trainer.ps1 ... -WhatIf                       # print only
powershell -ExecutionPolicy Bypass -File Scripts\Trainer\uninstall_night_trainer.ps1
# watch / control:
powershell -ExecutionPolicy Bypass -File Scripts\Trainer\status.ps1 [-RunId pmc-main] [-Pause | -Resume | -Stop]
```

- The task runs `run_night_trainer.ps1` (computes the stop time = next `-UntilDay`/`-UntilTime`, logs to
  `Saved/Trainer/scheduler.log`), as the current user while logged on, `StartWhenAvailable`, never two at once, cut 3 h after the window.
- **Resume**: the same `--run-id` continues from the last finished generation; finished arena jobs of an unfinished generation are
  reused (`gen_NNNN/jobs/*.rounds.json`). `run.json` snapshots the config at creation: edit `trainer_config.json` for NEW runs.
- **Pause**: `PAUSE` file in the run folder (or `Saved/Trainer/PAUSE` for all runs) - checked before each wave of games.
  **Stop**: `STOP` file - stops cleanly after the running wave. The time budget / stop-at stop the same way.
- **Politeness**: before each wave the trainer waits while `Saved/agent.lock` is held by a live process (the `Scripts/agent_lock.ps1`
  convention) and holds the lock itself (agent `night-trainer`) during the wave, so a build waits instead of failing on locked DLLs.
  With `--yield-to-editor` it also waits while a CodexTactics editor is open (UnrealEditor command line containing
  `\CodexTactics\CodexTactics.uproject` without `-run=` / `-game`). Mock mode ignores the lock and the editor.

## 3. Reading the morning report

`Saved/Trainer/<run_id>/report.html` (self-contained, open it in a browser; the clips are relative files next to it):
fitness curve (best / mean per generation, the champion's fresh-seed line, dashed = a promotion); **highlights** (clips + a one-line
caption "PMC won: flank + suppress, 3-0, 41 s", a **2D top-down replay** of each tracked round: play / pause / scrub, speed, dots per
fighter by side, shot lines, event ticker); champion vs baseline stats (fresh seeds; the "vs bot" columns are the absolute yardstick
because the self-play opponent changes); the **weights diff** champion vs baseline; champion history; leaderboard top 15.
Also `leaderboard.json/.csv`, `champion_weights.json` (a full drop-in weights file), `champion_meta.json`, `champions/<id>.json`
(every champion), `gen_NNNN/generation.json`, `trainer.log`, `status.json`.

A champion whose fresh fitness is far below its generation fitness was lucky; trust promotions confirmed over several generations
and the "vs bot" win rate.

## 4. Applying a champion

```
powershell -ExecutionPolicy Bypass -File Scripts\Trainer\apply_champion.ps1 -RunId pmc-main [-WhatIf]
```
Backs up the current `Content/Data/AI/pmc_commander.json` to `Saved/Trainer/<run>/backups/` (hash-verified), copies
`champion_weights.json` over it (hash-verified). Review `git diff` and commit it yourself. Other personalities: `-Target <file>`.

## 5. A new weights file / personality

Set `baseline_weights` in `trainer_config.json` (or a copy of the config passed with `--config`) to the new file and start a new
run id; the bounds sidecar is generated next to it on first use. Apply with `apply_champion.ps1 -Target <that file>`.

## 6. The adapter contract with the arena (`Scripts/Trainer/nighttrainer/adapter.py`, config `arena`)

Everything the trainer assumes about the arena is in `adapter.py` + `trainer_config.json` `arena` (most changes are config-only):

- **Launch**: `UnrealEditor-Cmd.exe "<uproject>" <map> <engine_args> "-ExecCmds=<console_command> <arg_template>" "-abslog=<log>"`,
  default `CodexTactics.PmcArena Map={map} Rounds={rounds} Seed={seed} WeightsA={weights_a} Opponent={Bot|Pmc} WeightsB={weights_b|None} TimeLimit={time_limit_s} Out={out}`.
  Paths are **project-relative with forward slashes** (no spaces; -ExecCmds splits on spaces, commas separate commands) - resolve
  them against `FPaths::ProjectDir()`. The arena must **quit the game when done** (like the smokes) and write the result to `Out`
  (fallback: the newest `Saved/Arena/*.json` written after start - only safe with `parallel` 1).
- **Result** `codex.pmc_arena/1` (sample: `Scripts/Trainer/fixtures/sample_arena_result.json`): `rounds[]` with `winner`
  (`"A"` = the WeightsA squad, `"B"`, or `"draw"`), `duration_s`, `seed` (the round's own seed), `index`, `end_reason`,
  `highlight_score` (optional), `track` (optional, path of the round's track file relative to the result JSON), and
  `sides.A` / `sides.B` (dict, or a list of objects with `side`) with `survivors, squad_size, hp_lost, hp_max, kills,
  flanks_attempted, flanks_succeeded, suppression_s, ambushes, retreats, retreats_survived, heals, friendly_fire, stuck_events`.
  Key names are mapped in `arena.result.fields` (dotted paths allowed); missing metrics count as 0; fewer rounds than requested
  are padded as crashes.
- **Track** `codex.pmc_arena_track/1` (one file per round, optional): `dt` (0.2 s), `units`, `fighters[] {id, side, team}`,
  `frames[]` = per tick one `[x, y]` (or `null` when dead) per fighter in `fighters` order, `events[] {t, type: shot|kill|down|heal|tactic,
  from/to/hit | by/target | side/decision}`. The viewer auto-fits the bounds, so any planar unit works.
- **Record re-run** (`arena.record`): `<arena command with Rounds=1 Seed=<round seed>> Record=1,CodexTactics.ClipRecorder <clip> seconds=<n>`
  rendered offscreen (`-RenderOffscreen -ResX=1280 -ResY=720 -fps=30`); the recorder's PNG frames in `Saved/Screenshots/Clips/<clip>/`
  are encoded by `clip_assemble.py watch`, then concatenated and scaled to 960 px. The same seed must replay the same round.

**Left to wire when the arena lands**: check the real arg syntax / key names against `arena.arg_template` and `arena.result`;
confirm `Out=` is honoured (needed for `parallel` > 1) and the game quits after the last round; confirm the round `seed` replays
the round with `Record=1` and that ClipRecorder works with the arena camera; pick the arena map (`arena.map`); then run
`night_trainer.py --run-id smoke --max-generations 1 --population 2 --rounds-per-job 1` once by hand before installing the task.

## 7. Files and tests

`Scripts/Trainer/`: `night_trainer.py` (CLI), `trainer_config.json`, `nighttrainer/` (`genome`, `evolution`, `fitness`,
`adapter`, `runner` - processes / guards / lock, `trainer` - loop / state, `highlights`, `report` + `replay_viewer.js`,
`mock_arena` - the synthetic arena), PowerShell `run_night_trainer`, `install_night_trainer`, `uninstall_night_trainer`, `status`,
`apply_champion`, `make_highlights`; `fixtures/` (sample weights + bounds, sample arena result); `tests/`.

```
python -m unittest discover -s Scripts/Trainer/tests      # 37 tests, ~15 s (genome, operators, fitness, adapter, guards, loop, resume, highlights)
```
