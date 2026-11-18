# 083 - Trainer QD archive fills only 2 of 48 cells (Claude, 2026-11-18) - MEDIUM, Python (+ report), Antigravity day
Base: main. Branch gemini/083-qd-coverage, worktree CodexTactics-gemday. Do it after 082.
Why: the first MAP-Elites trial (pmc-v6-qd, worker 06:00-08:09, L_Gen_Depot_X4 tactical scenario, 5 generations x 8 candidates) ended with
`archive 2/48` every generation. The behaviour space (aggression / mobility / patience, 4x4x3) is not being explored, so QD gives no
diversity over the ga mode. Run data (copied from the worker): Saved/Reports/worker-2026-11-18/pmc-v6-qd/ in the main checkout
(report.html, logs, archive json). Code: Scripts/Trainer/** (night_trainer.py, the qd / archive / descriptor code from Grok G002).
Do:
1. Diagnose (numbers first): the descriptor values of every evaluated candidate (from the run files) - are they clamped, constant, on
   one bin edge, or is the bin mapping wrong? Which per-side `behaviour` metrics (arena, 11 metrics) feed each axis.
2. Fix in Python only: descriptor normalisation / bin edges from the real metric ranges (not guessed), and mutation pressure that pushes
   into empty cells if the code has none. Keep `search.mode` default `ga`; do not touch C++.
3. Unit tests (Scripts/Trainer tests, the existing harness): with the 40 real descriptor rows from this run the new binning fills at least
   8 cells; the old binning reproduces 2/48.
Report docs/port/gemini/outbox/083-qd-coverage.report.md: the diagnosis table, the change, the test numbers, what should change in C++ (if
the arena metrics themselves are flat) as a recommendation only.
Allowed: Scripts/Trainer/**, its tests, docs/port/gemini/outbox/083-*. No C++, Content/**, Config/**, no worker / ssh actions.
FINISH line in chat topic gemini-083 (status | last commit | done | NOT done | needs Claude | gate | report).
