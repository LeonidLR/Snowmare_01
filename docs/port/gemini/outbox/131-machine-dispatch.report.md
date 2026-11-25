# Task 131 — Machine Dispatcher (`Scripts/Tools/machine_dispatch.py`)

## Summary
Added stdlib-only `Scripts/Tools/machine_dispatch.py` and unit tests `Scripts/Tools/test_machine_dispatch.py` to dispatch lane builds and smokes onto whichever machine (`main` PC or `codex-worker`) is free.

## Behavior Implemented
- **CLI subcommands**:
  - `machine_dispatch.py build --lane <worktree> [--target <Target>] [--config <Config>]`
  - `machine_dispatch.py smoke --lane <worktree> --smokes A,B [--map <Map>] [--jobs <N>]`
  - `machine_dispatch.py status [--lane <worktree>]`
- **Machine selection (`pick_machine`)**:
  - Reads local `Saved/agent.lock` across main + git worktree roots (PID-liveness / `manual` check matching `agent_lock.ps1`) and worker `Saved/agent.lock` + running `UnrealEditor*` processes over SSH (matching `worker_sync.ps1`).
  - Both free -> chooses `main` (`DISPATCH <job> -> main (both free)`).
  - One free -> chooses the free machine (`worker busy: ...` or `main busy: ...`).
  - Both busy -> enqueues into file-locked (`msvcrt.locking` / `fcntl.flock` on `.lock` + atomic replace) shared FIFO queue `<main_root>/Saved/Dispatch/queue.json`, pruning crashed PIDs and polling until head of queue + a machine is free.
- **Main PC safety guards**:
  - Requires `<main_root>/Saved/Lead/MAIN_PC_FREE` to exist; otherwise `main` is disallowed and jobs go to `worker`.
  - Checks local `UnrealEditor.exe` command lines using `test_gate_editor_command_line` (mirroring `merge_gate.ps1` `Test-GateEditorCommandLine`); blocks `main` whenever the designer's interactive editor is open (still allowing `-game` / `-run=` headless runs).
- **Delegates to existing scripts**:
  - Build on `main`: `Scripts/build.ps1` in `--lane`. Build on `worker`: `Scripts/build.ps1 -Remote worker`.
  - Smoke on `main`: `Scripts/smoke_parallel.ps1 -Commands ... -Remote off`. Smoke on `worker`: `Scripts/Tools/worker_sync.ps1 -ShipHead -BinariesFrom <lane> -Commands ...`.
  - Preserves process exit code and prints failure log path (`Saved/Logs/Build.log`, `Saved/Logs/SmokeParallel-last.json`, or worker `SmokeParallel-worker.json`).

## Verification
- `python -m unittest Scripts/Tools/test_machine_dispatch.py -v`: **8/8 tests PASS** (`both_free_picks_main`, `worker_busy_picks_main`, `main_busy_picks_worker`, `both_busy_returns_none`, `flag_missing_disables_main`, `editor_open_blocks_main`, `stale_vs_live_local_lock`, `both_busy_waits_fifo_until_one_frees`).
- Real lane build via dispatcher:
  - `python -u Scripts/Tools/machine_dispatch.py build --lane "C:/Work/Lanes/CodexTactics-131"`
  - Initially both `main` (`claude | Lead merge gate`) and `worker` (`night-loop`) were busy, so it queued `#1` in `Saved/Dispatch/queue.json`.
  - Once `main` freed first while `worker` remained busy, dispatched:
    `DISPATCH build CodexTactics-131 -> main (worker busy: night-loop (night loop job))`
  - Build succeeded (`Exit code 0`, `Total execution time: 1057.81 seconds`).
