# Lanes - no work stops silently (designer 2026-11-19)

Designer: "Процесс, при котором у тебя просто исчезают процессы и останавливаются без триггеров, неприемлемо."
Cause on 11-19: the building-generator session decided on its own to postpone its next task "to the next working window"
(token economy) and went idle; nothing showed it for ~3 hours.

## Rules
1. Every lane of work in flight is a row in `docs/port/LANES.json`: id, title, owner (session / subagent / Grok / worker),
   branch, chat topic, state, `max_quiet_min`. The lead adds the row when he dispatches, and sets `done` when he merges.
2. A lane is either WORKING (commits on its branch or status posts in its topic at least every `max_quiet_min`, default 45)
   or it says why not: state `blocked` / `waiting` + `reason` (e.g. "waiting for the worker", "question to the designer").
   `paused-by-designer` only when the designer said so.
3. Nobody postpones work on their own ("next window", "save tokens") - ask the lead; the lead decides and writes the state.
4. Every executor brief says: post a status line in your topic at each step, push WIP at least every 30-45 min.

## The watchdog
`Scripts/Tools/lane_watchdog.py`, Windows scheduled task `CodexLaneWatchdog` every 10 min (runs without any Claude session).
A stall (active and quiet too long; blocked / waiting without a reason or longer than `max_wait_min` 240) is posted ONCE to
chat topic `lanes` (to all: the designer's task board shows it and it wakes waiting agents), appended to
`Saved/Lanes/alerts.log` (the lead's Monitor tails it) and `Saved/Lanes/status.json` is rewritten each run.
`python Scripts/Tools/lane_watchdog.py --list` shows every lane and its age.
