# Switching the lead between the main PC and codex-worker (designer 2026-11-19)

Designer: «хочу, чтобы была возможность переключаться между машинами ... вернуть процесс обратно на вторую машину и обратно.»

## The two leads
| Side | Claude session | Checkout it works in | Pushes to |
|---|---|---|---|
| main PC | the lead session (Claude desktop) | `Documents/Unreal Projects/CodexTactics` (+ lane worktrees) | `main` (CODEX_AGENT=claude, pre-push guard) |
| codex-worker | the stand-in session (Remote Control, the designer drives it from the phone) | `C:\Work\CodexTactics-slot1` | its own `worker/lead-<date>` branch only |
`docs/port/LEAD_ACTIVE.json` on origin says who leads now (`python Scripts/Tools/lead_switch.py --status`).
Only ONE side leads at a time; the other side does not start lanes, worker runs or merges.

## Switch (the designer says «передай управление на вторую машину» / «верни на основную»)
On the side that HANDS OVER, the lead runs:
```
python Scripts/Tools/lead_switch.py --to worker --note "<one line: what is hot>"     (main PC -> worker)
python Scripts/Tools/lead_switch.py --to main   --note "<one line>"                  (worker -> main PC)
```
It commits (tracked files only) and pushes every lane worktree on that machine, writes `LEAD_ACTIVE.json` +
`HANDOVER_LIVE.md` (lanes, branch heads, first steps), commits + pushes them, and prints the line the designer sends to the
other session: «Прими управление: прочитай docs/port/HANDOVER_LIVE.md (ветка <branch>) и продолжай.»
Then the handing-over lead stops its subagents / sessions' work (they are pushed) and stays idle.

## Receiving
- **Worker receives:** `git fetch` in slot1, read HANDOVER_LIVE.md + WORKER_LEAD_<date>.md, continue the lanes from their
  branches (copy branches `worker/<lane>` if the main PC might still push the original), keep LANES.json current on its branch.
- **Main PC receives:** back up the designer's uncommitted files (Saved/Backups/designer-wip-*, hashes), review + merge the
  worker's hand-over branch into main (ff or --no-ff, never rewriting his files), build main (check his editor first), then
  restart the lanes as subagents / sessions from their branches; update LANES.json owners.
- In both directions: the worker's night loop (C:\Work\NightLoop, task "CodexTactics Night Guard", 16:55-07:45, trainer
  05:30-07:30) keeps running; it yields the agent.lock between jobs.

## Watchdog on both sides
Main PC: task `CodexLaneWatchdog` (docs/port/LANES.md). Worker: the night guard + the stand-in lead's own checks
(`Saved\Worker\nightloop\guard.log`, `heartbeat.txt`). A lane that changes machine changes its `machine` / `worktree` /
`owner` in LANES.json in the same commit.
