# 009 - Review of the night trainer run 2026-11-10 -> 11 (READ-ONLY analysis)

No code changes. Do not edit any tracked file. Allowed writes: your report `docs/port/gemini/outbox/009-night-trainer-review.report.md`
and generated pages under `Saved/Orchestrator/replay/` (main checkout). Run tools with `python -I`.

## Input
- The night's run: the NEWEST folder under `Saved/Trainer/` that is not `check-*`, `mock-*`, `smoke-*` (scheduler.log / latest.json say
  which run started at 2026-11-10 22:15). Inside: generations `gen_*/jobs/*.arena/round_*.track.json` + arena result JSONs, the
  trainer's own summaries / champion genes, and clips (mp4) - you do not need to watch the clips.
- Baselines: the older runs `Saved/Trainer/pmc-v3` (before today's fixes) and `Saved/Trainer/check-2026-11-10` (the fix check run).
- Today's arena changes (docs/port/CHANGELOG.md 2026-11-10 rows, docs/systems/pmc-squad-ai.md sections 11-12): order commitment,
  the push after 30 s, defender wins on time only on the point, no-contact = draw (-3), Ammo=resupply, team colours / camera, the play
  area without the outer strip, rocket bursts count as contact, objective reachable from both spawns. Known open bug: the on-point /
  arrival checks are 2D (a roof objective counts men on the ground under it).
- Tools: `python -I Scripts/Trainer/nighttrainer/stalemate_stats.py <tracks...> --json <out>`;
  `python -I Scripts/Tools/arena_replay/build_replay.py <tracks...> --out Saved/Orchestrator/replay` (your tool).

## Output (the report, Russian for the designer's part, compact)
1. Numbers, tonight vs pmc-v3 vs the check run: rounds, no-contact rounds, longest quiet spells (median / max), shots per minute,
   outcomes (attackers win / defender on point / draw / no_contact), order re-decisions per fighter-minute, rocket shots.
2. Fitness / genes: best fitness per generation, which genes moved and where (commander weights, commitment, push, rpg_strike ...).
3. Anomalies with round ids: stuck fighters, roof-objective draws (the 2D bug), anybody spawned in the outer strip (must be 0),
   rounds that hit the time limit, weird outcomes.
4. 3-5 replay pages built (the most decisive round, the worst stalemate, a typical one) - list the HTML paths.
5. The top 3 recommended fixes (no code), each with the evidence (round id + numbers). Mark guesses as guesses.
Post one line in the agent chat when done.
