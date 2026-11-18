# 083 - Trainer QD archive fills only 2 of 48 cells (Claude, 2026-11-18) - MEDIUM, Python (+ report), Antigravity day
Base: main. Branch gemini/083-qd-coverage, worktree CodexTactics-gemday. Do it after 082.
Why: the first MAP-Elites trial (pmc-v6-qd, worker 06:00-08:09, L_Gen_Depot_X4 tactical scenario, 5 generations x 8 candidates) ended with
`archive 2/48` every generation. The behaviour space (aggression / mobility / patience, 4x4x3) is not being explored, so QD gives no
diversity over the ga mode. Run data (copied from the worker): Saved/Reports/worker-2026-11-18/pmc-v6-qd/ in the main checkout
(report.html, logs, archive json). Code: Scripts/Trainer/** (night_trainer.py, the qd / archive / descriptor code from Grok G002).
Worker diagnosis already done (docs/port/reports/worker-night-2026-11-18.md section 4): the mobility axis (flanks_completed + moves, range
[0,12]) saturates - moves averages 19, everyone lands in bin 3; aggression [0,180] puts almost all in bins 0-1; patience (ambush_success
[0,3], mean 0.14) all in bin 0. Bot league jobs return 1 round but the trainer asks for 3 and pads 2 as crashes (-5 each).
Do:
1. Confirm the diagnosis (numbers first): the descriptor values of every evaluated candidate (from the run files) - are they clamped, constant, on
   one bin edge, or is the bin mapping wrong? Which per-side `behaviour` metrics (arena, 11 metrics) feed each axis.
2. Fix in Python only: descriptor normalisation / bin edges from the real metric ranges (not guessed), and mutation pressure that pushes
   into empty cells if the code has none. Keep `search.mode` default `ga`; do not touch C++.
2b. League: bot jobs ask for the rounds the arena really plays (rounds_per_job 1 for bot jobs, or read the round count from the
   result) - no padded crashes for missing bot rounds; real crashes still penalised.
3. Unit tests (Scripts/Trainer tests, the existing harness): with the 40 real descriptor rows from this run the new binning fills at least
   8 cells; the old binning reproduces 2/48.
Report docs/port/gemini/outbox/083-qd-coverage.report.md: the diagnosis table, the change, the test numbers, what should change in C++ (if
the arena metrics themselves are flat) as a recommendation only.
Allowed paths: Scripts/Trainer/**, docs/port/gemini/outbox/083-*

Forbidden: C++, Content, Config, worker / ssh actions.
FINISH line in chat topic gemini-083 (status | last commit | done | NOT done | needs Claude | gate | report).
