"""Real full-length gate probe / freeze baseline. Never evolves or applies weights.

Run the baseline (30+ rounds per scenario) only in the worker trainer window.
Each cached job has its own replayable seed, raw result and engine log.
"""
import argparse
from collections import Counter
import copy
import datetime
from pathlib import Path

from nighttrainer.adapter import ArenaAdapter
from nighttrainer.gate import check_metric_liveness, extract_used_metrics, extract_penalty_metrics
from nighttrainer.gate_identity import freeze_sha, identity
from nighttrainer.genome import load_json, write_json
from nighttrainer.runner import Guard, JobRunner

ROOT = Path(__file__).resolve().parents[2]
SCENARIOS = ['skirmish', 'chokepoint', 'street', 'roofs', 'blizzard', 'tactical']


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--config', required=True)
    p.add_argument('--run-id', required=True)
    p.add_argument('--rounds', type=int, default=2, help='per scenario; >=30 is a baseline')
    p.add_argument('--freeze-sha', required=True)
    p.add_argument('--stop-at')
    p.add_argument('--parallel', type=int, default=2)
    a = p.parse_args(argv)
    if not a.run_id.replace('-', '').replace('_', '').isalnum() or a.rounds < 1:
        p.error('invalid run id / round count')
    if freeze_sha(ROOT) != a.freeze_sha:
        raise ValueError('Checkout differs from requested freeze SHA')
    if a.rounds >= 30 and datetime.time(8) <= datetime.datetime.now().time() < datetime.time(16, 30):
        raise ValueError('Baseline starts only in trainer window 16:30-08:00')
    cfg = load_json(a.config)
    directory = ROOT / 'Saved/Trainer' / a.run_id
    directory.mkdir(parents=True, exist_ok=True)
    manifest = {'identity': identity(cfg, a.freeze_sha), 'rounds_per_scenario': a.rounds,
                'scenarios': cfg.get('gate_scenarios', SCENARIOS)}
    path = directory / 'campaign.json'
    if path.exists() and load_json(path) != manifest:
        raise ValueError('Resume differs from frozen campaign')
    write_json(str(path), manifest)
    arena = copy.deepcopy(cfg['arena'])
    if float(arena['time_limit_s']) < 120:
        raise ValueError('Probe requires full-length rounds (>=120s)')
    arena['retries'] = 0
    arena['require_requested_output'] = True
    arena['require_loaded_weights'] = True
    arena['arg_template'] = [x for x in arena['arg_template'] if not x.lower().startswith('scenario=')]
    arena['arg_template'].append('Scenario={scenario}')
    adapter = ArenaAdapter(arena, str(ROOT), close_range_cfg=cfg['fitness'].get('close_range'))
    runner = JobRunner(adapter, arena, a.parallel)
    deadline = datetime.datetime.fromisoformat(a.stop_at).timestamp() if a.stop_at else None
    # Parent night-loop queue holds its worker lock. This isolated checkout also
    # gets its own normal trainer guard, so day callers can yield between jobs.
    guard = Guard(str(directory), str(ROOT), deadline, True, True, False, poll_s=10)
    jobs = []
    # Placement may explicitly fall back to skirmish. A baseline tries up to
    # three seed sets to reach the actual per-scenario minimum; no relabelling.
    for index in range(a.rounds * (3 if a.rounds >= 30 else 1)):
        for s, scenario in enumerate(manifest['scenarios']):
            job = {'id': f'{scenario}-{index:03}', 'dir': str(directory / 'jobs'),
                   'rounds': 1, 'seed': 149000 + index * 7 + s,
                   'weights_a': cfg['baseline_weights'], 'weights_b': cfg['baseline_weights'],
                   'opponent': 'champion', 'scenario_requested': scenario,
                   'engine_args': cfg['evaluation']['matches'][index % len(cfg['evaluation']['matches'])].get('engine_args', [])}
            jobs.append(job)
    rows = []
    for start in range(0, len(jobs), a.parallel):
        if freeze_sha(ROOT) != a.freeze_sha:
            raise ValueError('Freeze changed during campaign')
        if a.rounds >= 30:
            now = datetime.datetime.now()
            if datetime.time(8) <= now.time() < datetime.time(16, 30):
                break
        counts = Counter(r.get('scenario') for r in rows if not r.get('crashed'))
        wave = [job for job in jobs[start:start + a.parallel]
                if a.rounds < 30 or counts[job['scenario_requested']] < a.rounds]
        batch = runner.run_jobs(wave, guard)
        if batch is None:
            break
        for job in wave:
            for row in batch[job['id']]:
                row = dict(row, job_id=job['id'], scenario_requested=job['scenario_requested'])
                # Count actual scenario: placement fallback cannot satisfy G3.
                rows.append(row)
        counts = Counter(r.get('scenario') for r in rows if not r.get('crashed'))
        live, issues = check_metric_liveness(rows, extract_used_metrics(cfg),
                                           penalty_metrics=extract_penalty_metrics(cfg))
        write_json(str(directory / 'evidence.json'), dict(manifest, rounds=rows, counts=dict(counts),
                   g2={'passed': live, 'issues': issues},
                   complete=(all(counts[s] >= a.rounds for s in manifest['scenarios'])
                             if a.rounds >= 30 else len(rows) == len(jobs)),
                   g3_sufficient=all(counts[s] >= 30 for s in manifest['scenarios'])))
        if any(r.get('crashed') for r in rows):
            return 1
        if a.rounds >= 30 and all(counts[s] >= a.rounds for s in manifest['scenarios']):
            return 0
    return 0 if a.rounds < 30 and len(rows) == len(jobs) else 1


if __name__ == '__main__':
    raise SystemExit(main())

