# G002 - Trainer: behaviour metrics + quality-diversity archive (Claude, 2026-11-17) - MEDIUM-LARGE, Python (no C++)
Branch grok/G002-trainer-qd from the newest main, worktree CodexTactics-grok. Read docs/design/TACTICAL_MAP.md §4 and the week plan
docs/port/WEEK_PLAN_2026-11-17.md part A first.
Today: Scripts/Trainer/nighttrainer (night_trainer.py, trainer_config.json): a genetic search over the PMC commander weights
(Content/Data/AI/pmc_commander.json + .bounds.json), population 8, ONE scalar fitness (win / kills / HP / timeout / no-contact). It learnt to
hide twice. The game side (CodexTactics.PmcArena, run on the second machine) will report per-round BEHAVIOUR metrics - Claude adds them in
C++ (TACTICAL_MAP P3); you build the trainer side now against a schema you define with mock data:
1. Schema: docs/design/TACTICAL_MAP.md §4 metric list -> a `behaviour` object in the arena round result (name, unit, range, direction);
   write it as docs/port/grok/outbox/G002-arena-behaviour-schema.md and in the trainer's adapter (nighttrainer/adapter.py) with defaults when
   the field is missing (old arena builds keep working).
2. Quality-diversity: a MAP-Elites archive (2-3 behaviour axes from config, e.g. aggression = time in contact, mobility = flanks + moves,
   patience = ambush success) - each cell keeps its best weights; selection samples parents from the archive; the scalar fitness stays the
   cell's quality score. Config in trainer_config.json (axes, bins, archive file). Keep the old GA as a mode (config switch) - A/B.
3. League: opponents = champion pool from the archive + the playtest bot squad (existing 'bot' opponent) - config.
4. Report: the morning report lists the archive (cells filled, best per cell, behaviour values) + highlights per cell.
5. Tests in Scripts/Trainer/tests (pytest / unittest as the repo uses) with the mock adapter: archive insert / replace, selection, missing
   metrics fallback, report. All existing trainer tests green.
Gate: the trainer tests, a 3-generation mock run (`mock` in trainer_config) end to end, typesafe audit --agent grok. Report + FINISH.
Allowed paths: Scripts/Trainer/**, docs/port/grok/outbox/G002-*. Do NOT change pmc_commander.json or any C++ / Content.
Note: the second machine runs pmc-v5 tonight with the OLD trainer from main - do not touch its run; your branch is merged only after review.
