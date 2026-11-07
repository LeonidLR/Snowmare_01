"""Night trainer CLI: evolves the PMC squad commander weights in the headless arena (docs/tools/night-trainer.md).

  python Scripts/Trainer/night_trainer.py --run-id pmc-main [--mock] [--max-generations 50] [--hours 10]
         [--stop-at 2026-11-09T07:00] [--parallel 2] [--yield-to-editor] [--config Scripts/Trainer/trainer_config.json]

The same --run-id resumes (from the last finished generation; finished arena jobs of an unfinished generation are reused).
Population / rounds / seed overrides only apply when the run is created (a resume keeps run.json's snapshot).
Stop: create Saved/Trainer/<run_id>/STOP. Pause: create Saved/Trainer/<run_id>/PAUSE (or Saved/Trainer/PAUSE).
Single instance (2026-11-06): a real (non --mock) run holds Saved/Trainer/night_trainer.lock (its PID); a second start
while that PID is alive prints the holder and exits 0 without touching anything (a watchdog restart is harmless).
"""
from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from nighttrainer.runner import InstanceLock  # noqa: E402
from nighttrainer.trainer import Trainer  # noqa: E402

REPO_ROOT = os.path.dirname(os.path.dirname(HERE))


def parse_args(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--run-id", default=None, help="run folder name under Saved/Trainer (default: pmc-<date>, mock-<date>)")
    ap.add_argument("--config", default=os.path.join(HERE, "trainer_config.json"))
    ap.add_argument("--mock", action="store_true", help="synthetic arena (no game): tests the loop / report / scheduler")
    ap.add_argument("--max-generations", type=int)
    ap.add_argument("--hours", type=float, help="time budget for this invocation")
    ap.add_argument("--stop-at", help="local date-time to stop at, e.g. 2026-11-09T07:00")
    ap.add_argument("--parallel", type=int, help="game processes side by side (default from config: 2)")
    ap.add_argument("--yield-to-editor", action="store_true", help="wait while a CodexTactics editor is open")
    ap.add_argument("--population", type=int)
    ap.add_argument("--rounds-per-job", type=int)
    ap.add_argument("--seed", type=int)
    ap.add_argument("--poll-s", type=float, default=30.0, help="seconds between PAUSE / lock / editor checks")
    ap.add_argument("--make-highlights", action="store_true",
                    help="only (re)make the highlight clips of --run-id and rewrite its report")
    ap.add_argument("--force", action="store_true", help="with --make-highlights: re-render existing clips too")
    ap.add_argument("--repo-root", default=REPO_ROOT)
    return ap.parse_args(argv)


def deadline_from(args):
    ends = []
    if args.hours:
        ends.append(time.time() + args.hours * 3600.0)
    if args.stop_at:
        ends.append(dt.datetime.fromisoformat(args.stop_at).timestamp())
    return min(ends) if ends else None


def main(argv=None) -> int:
    args = parse_args(argv)
    with open(args.config, "r", encoding="utf-8-sig") as f:
        cfg = json.load(f)
    if args.population:
        cfg["evolution"]["population"] = args.population
    if args.rounds_per_job:
        cfg["evaluation"]["rounds_per_job"] = args.rounds_per_job
    if args.seed is not None:
        cfg["seed"] = args.seed
    run_id = args.run_id or (("mock-" if args.mock else "pmc-") + dt.date.today().strftime("%Y%m%d"))
    lock = None
    if not args.mock:
        lock = InstanceLock(args.repo_root, run_id)
        if not lock.acquire():
            holder = lock.holder() or {}
            print(f"night_trainer: another instance is running (pid {holder.get('pid')}, run {holder.get('run_id')!r}, "
                  f"since {holder.get('started')}) - exiting ({lock.path})")
            return 0
    try:
        return _run(args, cfg, run_id)
    finally:
        if lock:
            lock.release()


def _run(args, cfg, run_id) -> int:
    trainer = Trainer(cfg, run_id, args.repo_root, mock=args.mock, deadline=deadline_from(args),
                      max_generations=args.max_generations, parallel=args.parallel,
                      yield_to_editor=args.yield_to_editor, poll_s=args.poll_s)
    if args.make_highlights:
        made = trainer.make_highlights(force=args.force)
        print(f"clips made: {made}; report: {os.path.join(trainer.run_dir, 'report.html')}")
        return 0
    outcome = trainer.run()
    print(f"report: {os.path.join(trainer.run_dir, 'report.html')}")
    return 0 if outcome in ("finished", "stopped") else 1


if __name__ == "__main__":
    sys.exit(main())
