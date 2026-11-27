"""Paired weekend validation of a demo-scored candidate vs the approved champion.

Both arms are side A, against the same frozen champion on side B, with identical
seed/sector/scenario. Arm baseline uses champion vs itself. Each job is ONE round
so every reported seed replays independently. Writes only Saved/Trainer/<run-id>.
No promotion or game-file application. Run on an isolated worker checkout.
"""
from __future__ import annotations

import argparse
import copy
import hashlib
import json
from pathlib import Path
import re
import time

from nighttrainer import doctrine
from nighttrainer.adapter import ArenaAdapter
from nighttrainer.fitness import aggregate
from nighttrainer.genome import load_json, write_json
from nighttrainer.runner import Guard, InstanceLock, JobRunner
from nighttrainer.trainer import seed_for

ROOT = Path(__file__).resolve().parents[2]


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def plan(cfg, count=None):
    n = int(cfg['rounds_per_arm'] if count is None else count)
    if n < 1 or (count is None and n < 200):
        raise ValueError('night needs >=200 rounds per arm; trial needs >=1')
    sectors = cfg['sectors']
    if not sectors or any(s not in range(1, 7) for s in sectors):
        raise ValueError('expected X6 sectors 1..6')
    return [{'pair': i, 'sector': sectors[i % len(sectors)],
             'seed': seed_for(int(cfg['seed']), 0, 'weekend-heldout', i),
             'arms': ['baseline', 'candidate'] if i % 2 == 0 else ['candidate', 'baseline']}
            for i in range(n)]


def arena_for(base, cfg, level_path):
    arena = copy.deepcopy(base['arena'])
    arena.update(map=cfg['map'], parallel=int(cfg.get('parallel', 1)), time_limit_s=cfg['time_limit_s'],
                 timeout_s=cfg['timeout_s'], retries=0, require_loaded_weights=True, require_requested_output=True)
    arena['engine_args'] = [a for a in arena['engine_args'] if a != '-FORCELOGFLUSH'
                            and not a.startswith(('-LevelJson=', '-Sector='))]
    arena['engine_args'] += [f'-LevelJson={level_path.as_posix()}', '-Sector=0']
    arena['arg_template'] = ['Map={map}', 'Rounds={rounds}', 'Seed={seed}', 'WeightsA={weights_a}',
                             'Opponent={opponent}', 'WeightsB={weights_b}', 'Time={time_limit_s}',
                             'Out={out}', 'Spawns=random', 'Objective=1', 'Squad=3', f"Scenario={cfg['scenario']}"]
    return arena


def summarize(rows):
    arms = {}
    for arm in ('baseline', 'candidate'):
        pool = [r for r in rows if r['arm'] == arm]
        ok = [r for r in pool if not r.get('crashed')]
        arms[arm] = {'rounds': len(pool), 'crashes': len(pool) - len(ok),
                     'wins': sum(r.get('won') == 1 for r in ok),
                     'draws': sum(r.get('won') == .5 for r in ok),
                     'win_rate': sum(r.get('won') == 1 for r in ok) / len(pool) if pool else None,
                     'captures': sum(r.get('won') == 1 and r.get('role') == 'attacker' and r.get('end_reason') == 'objective' for r in ok),
                     'capture_observed': sum('role' in r and 'end_reason' in r for r in ok),
                     'kills': sum(r.get('kills', 0) for r in ok),
                     'kills_observed': sum('kills' in r for r in ok)}
    pairs = {}
    for r in rows:
        pairs.setdefault(r['pair'], {})[r['arm']] = r
    valid = [p for p in pairs.values() if len(p) == 2 and not any(r.get('crashed') for r in p.values())]
    return {'arms': arms, 'complete_pairs': len(valid),
            'candidate_only_wins': sum(p['candidate'].get('won') == 1 and p['baseline'].get('won') != 1 for p in valid),
            'baseline_only_wins': sum(p['baseline'].get('won') == 1 and p['candidate'].get('won') != 1 for p in valid)}


def morning(run_dir, manifest, rows, fitness_cfg):
    result = summarize(rows)
    result.update(mock=manifest['mock'], trial=manifest['trial'], planned_pairs=len(manifest['plan']),
                  same_weights=manifest['champion_sha256'] == manifest['candidate_sha256'],
                  complete=len(rows) == 2 * len(manifest['plan']) and not any(r.get('crashed') for r in rows))
    result['fitness'] = {arm: aggregate([r for r in rows if r['arm'] == arm], fitness_cfg)
                         for arm in ('baseline', 'candidate')}
    result['doctrine'] = [{'pair': r['pair'], 'arm': r['arm'], **doctrine.evaluate(r, fitness_cfg.get('doctrine', {}))} for r in rows]
    # Three deterministic examples: a candidate win, a candidate non-win, then the longest remaining round.
    chosen = []
    pool = [r for r in rows if r['arm'] == 'candidate' and not r.get('crashed')]
    for condition in (lambda r: r.get('won') == 1, lambda r: r.get('won') != 1, lambda r: True):
        available = [r for r in pool if r not in chosen and condition(r)]
        if available:
            chosen.append(max(available, key=lambda r: r.get('duration_s', 0)))
    for row in sorted(pool, key=lambda r: r.get('duration_s', 0), reverse=True):
        if len(chosen) >= 3:
            break
        if row not in chosen:
            chosen.append(row)
    result['examples'] = chosen
    write_json(str(run_dir / 'morning.json'), result)
    lines = ['# Weekend paired trainer', '', f"Mock={result['mock']}; trial={result['trial']}; complete={result['complete']}; paired rounds={result['complete_pairs']}/{result['planned_pairs']}; same weights (A/A)={result['same_weights']}.", '',
             '| Arm | Rounds | Wins | Draws | Win rate | Captures (coverage) | Kills (coverage) | Crashes |',
             '|---|---:|---:|---:|---:|---:|---:|---:|']
    for arm, s in result['arms'].items():
        rate = f"{s['win_rate']:.1%}" if s['win_rate'] is not None else 'unknown'
        lines.append(f"| {arm} | {s['rounds']} | {s['wins']} | {s['draws']} | {rate} | {s['captures']} ({s['capture_observed']}) | {s['kills']} ({s['kills_observed']}) | {s['crashes']} |")
    lines += ['', f"Discordant wins: candidate only {result['candidate_only_wins']}; baseline only {result['baseline_only_wins']}.", '',
              'Examples (each job contains exactly one round; replay with the frozen weights/config and seed):']
    for r in chosen:
        lines.append(f"- Pair {r['pair']}, sector {r['sector']}, seed {r['seed']}, won={r.get('won')}, duration={r.get('duration_s')}s: `{r['result_file']}`; track `{r.get('track', 'not recorded')}`")
    lines += ['', 'Missing doctrine metrics receive no credit; see morning.json for per-round coverage.',
              'Spawn/objective boxes use the X6 sector geometry; this is the PMC arena, not interactive player sector rounds.',
              'The trainer does not apply a champion. Lead approval is required for apply_champion.ps1.']
    (run_dir / 'morning.md').write_text('\n'.join(lines) + '\n', encoding='utf-8')
    return result


def run(args):
    if not re.fullmatch(r'[A-Za-z0-9_-]+', args.run_id):
        raise ValueError('run-id must be a simple folder name')
    cfg, trainer_cfg = load_json(args.config), load_json(args.trainer_config)
    if not args.mock and args.trial is None and not args.plan_only:
        gate = ROOT / 'Saved/Trainer/gate_pass.json'
        if not gate.exists() or not load_json(str(gate)).get('passed') or time.time() - gate.stat().st_mtime > 86400:
            raise RuntimeError('A full night requires the existing trainer gate pass, fresh within 24h')
    run_dir = ROOT / 'Saved/Trainer' / args.run_id
    run_dir.mkdir(parents=True, exist_ok=True)
    requested = {'schema': 'codex.weekend_run/1', 'mock': args.mock, 'trial': args.trial,
                 'config': cfg, 'trainer_config': trainer_cfg,
                 'champion_sha256': digest(args.champion), 'candidate_sha256': digest(args.candidate),
                 'level_sha256': digest(ROOT / cfg['level']), 'sectors_sha256': digest(ROOT / cfg['sector_definitions']),
                 'plan': plan(cfg, args.trial)}
    manifest_path = run_dir / 'run.json'
    if manifest_path.exists() and load_json(str(manifest_path)) != requested:
        raise ValueError('resume inputs differ from frozen run.json; use a new run-id')
    write_json(str(manifest_path), requested)
    for name, source in [('champion', args.champion), ('candidate', args.candidate)]:
        target = run_dir / f'{name}.json'
        if not target.exists():
            target.write_bytes(Path(source).read_bytes())
        if digest(target) != requested[f'{name}_sha256']:
            raise ValueError(f'frozen {name} changed')
    sectors = load_json(str(ROOT / cfg['sector_definitions']))['sectors']
    for sector in sectors:
        level = load_json(str(ROOT / cfg['level']))
        level['pmc_squads'] = []
        level.setdefault('pmc_arena', {})['play_area'] = {'include_box_cm': sector['box_cm']}
        write_json(str(run_dir / f"sector_{sector['id']}.json"), level)
    if args.plan_only:
        print(f'{len(requested["plan"])} pairs planned in {manifest_path}')
        return 0
    lock = InstanceLock(str(ROOT), args.run_id)
    if not args.mock and not lock.acquire():
        raise RuntimeError('another trainer is running')
    deadline = time.time() + args.hours * 3600
    guard = Guard(str(run_dir), str(ROOT), deadline, check_lock=not args.mock,
                  hold_lock=not args.mock, yield_to_editor=not args.mock, poll_s=5)
    rows = []
    try:
        for pair in requested['plan']:
            arena = arena_for(trainer_cfg, cfg, run_dir / f"sector_{pair['sector']}.json")
            if args.mock:
                arena['require_loaded_weights'] = False  # mock has no engine file loader
            mock_cfg = dict(trainer_cfg.get('mock', {}), reference_weights=str(run_dir / 'champion.json'), crash_rate=0, tracks=True) if args.mock else None
            adapter = ArenaAdapter(arena, str(ROOT), mock_cfg, trainer_cfg['fitness'].get('close_range'))
            runner = JobRunner(adapter, arena, int(cfg.get('parallel', 1)), mock_in_process=args.mock, mock_cfg=mock_cfg)
            jobs = [{'id': f"p{pair['pair']:04}_{arm}", 'dir': str(run_dir / 'jobs'),
                     'weights_a': str(run_dir / ('champion.json' if arm == 'baseline' else 'candidate.json')),
                     'weights_b': str(run_dir / 'champion.json'), 'opponent': 'champion',
                     'rounds': 1, 'seed': pair['seed'], 'map': cfg['map'], 'time_limit_s': cfg['time_limit_s']}
                    for arm in pair['arms']]
            results = runner.run_jobs(jobs, guard)
            # Preserve a completed half pair on STOP; cached jobs resume later.
            for arm, job in zip(pair['arms'], jobs):
                path = Path(job['dir']) / (job['id'] + '.rounds.json')
                for row in load_json(str(path))['rounds'] if path.exists() else []:
                    rows.append(dict(row, arm=arm, pair=pair['pair'], sector=pair['sector'], seed=pair['seed'],
                                     result_file=str(path.relative_to(ROOT)).replace('\\', '/')))
            result = morning(run_dir, requested, rows, trainer_cfg['fitness'])
            if results is None or any(r.get('crashed') for r in rows):
                break
        print(f"Morning report: {run_dir / 'morning.md'}")
        return 0 if result['complete'] else 1
    finally:
        if not args.mock:
            lock.release()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--config', default=str(Path(__file__).with_suffix('.json')))
    p.add_argument('--trainer-config', required=True, help='Prepared demo trainer config')
    p.add_argument('--champion', required=True, help='Current Lead-approved champion weights (read only)')
    p.add_argument('--candidate', required=True, help='Candidate weights produced with demo scoring')
    p.add_argument('--run-id', required=True)
    p.add_argument('--trial', type=int, metavar='PAIRS', help='Short trial instead of >=200 pairs')
    p.add_argument('--mock', action='store_true', help='Synthetic script validation, not gameplay evidence')
    p.add_argument('--plan-only', action='store_true')
    p.add_argument('--hours', type=float, default=10)
    args = p.parse_args()
    if args.hours <= 0:
        p.error('--hours must be positive')
    return run(args)


if __name__ == '__main__':
    raise SystemExit(main())
