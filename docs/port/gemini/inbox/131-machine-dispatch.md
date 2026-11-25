# Gemini 131 (router L2 -> Hydrogen; Neon reviews the diff before FINISH) - machine dispatcher: run a lane build / smoke on whichever machine is free (designer 2026-11-25) - after 130

Why: two machines (main PC + codex-worker). Today an agent waited ~20 min for the worker lock (Lead gate) while the main PC was idle.
Each machine already has its own lock (Scripts/agent_lock.ps1 locally; build_remote.ps1 takes the worker's lock) - what is missing is the CHOICE of machine.

Build `Scripts/Tools/machine_dispatch.py` (stdlib only):
1. `machine_dispatch.py build --lane <worktree> [--target ...]` and `machine_dispatch.py smoke --lane <worktree> --smokes A,B`.
2. Pick the machine: read both locks (local agent lock file; worker lock via the same ssh check build_remote.ps1 uses). Both free -> MAIN PC
   (no sync, faster). One free -> that one. Both busy -> append to a shared FIFO queue file (Saved/Dispatch/queue.json, file-locked), wait,
   start on whichever frees first. Print one line per decision: `DISPATCH <job> -> main|worker (reason)`.
3. Main PC is allowed ONLY when `Saved/Lead/MAIN_PC_FREE` exists (the Lead creates it when the designer is away; delete = worker only).
   Never run on main if the designer's CodexTactics editor is open (same command-line check as merge_gate Test-GateEditorCommandLine).
4. Run the existing scripts, do not reimplement them: local = Scripts/build.ps1 in the lane; worker = Scripts/build.ps1 -Remote worker /
   worker_sync.ps1 for smokes. Pass the exit code through; on failure print the log path.
5. `machine_dispatch.py status` -> both machines (free / busy: who, what, since) + the queue.
6. Tests: tests for the pick logic (both free / one busy / both busy / flag missing / editor open) with fake lock readers, no real builds.
   Run once for real: a build of your own lane via the dispatcher, report which machine it chose.
Not in scope: priorities, preemption, smoke splitting across machines. Do not change merge_gate.ps1 or the lock formats.
FINISH line: hash, tests, the real dispatch line, review: Neon. BUILD ONLY via the dispatcher or -Remote worker.
