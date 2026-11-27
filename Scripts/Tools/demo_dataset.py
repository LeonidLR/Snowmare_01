#!/usr/bin/env python3
"""Build a deterministic, round-scoped dataset without modifying source demos.

No tag means not detected, never proof of absence. Notes are authored intent,
not actions performed. Win associations are descriptive, not causal estimates.
"""
from __future__ import annotations

import argparse
from collections import Counter
from dataclasses import asdict
import hashlib
import json
from pathlib import Path

import demo_notes
from demo_tactics import analyze_records, load_jsonl

TACTICS = ('fallback_to_cover_after_contact', 'high_ground', 'distract_and_flank',
           'prone_then_crawl_to_cover', 'first_in_cover_overwatch', 'flank_into_rear',
           'approach_unseen', 'lure_then_snipe', 'unwatched_flank_seconds', 'scan_pitch_range')


def outcome(value):
    value = str(value or '').lower()
    return value if value in ('victory', 'defeat', 'draw') else 'unknown'


def fights(records):
    """Stream boundaries rather than joining round numbers (which can repeat).

    Equal-time end events belong to the ending round. Pre-round snapshots and
    intermission orders cannot become examples for a subsequent fight.
    """
    header = next((r for r in records if r.get('type') == 'header'), {})
    footer = next((r for r in reversed(records) if r.get('type') == 'footer'), {})
    has_rounds = any(r.get('type') in ('round_start', 'round_end') for r in records)
    if not has_rounds:
        yield header, footer, records, 0.0, float(footer.get('duration_s', max((r.get('t', 0) for r in records), default=0))), 'legacy'
        return
    active, start = None, None
    for r in records:
        if r.get('type') == 'round_start':
            if active is not None:
                yield header, {}, active, float(start.get('t', 0)), float(r.get('t', 0)), str(start.get('round', ''))
            active, start = [r], r
        elif r.get('type') == 'round_end':
            if active is None:
                active, start = [], {'t': max(0, float(r.get('t', 0)) - float(r.get('duration_s', 0))), 'round': r.get('round')}
            active.append(r)
            yield header, r, active, float(start.get('t', 0)), float(r.get('t', 0)), str(start.get('round', ''))
            active, start = None, None
        elif active is not None and r.get('type') not in ('header', 'footer'):
            active.append(r)
    if active is not None:
        end = float(footer.get('duration_s', max((r.get('t', 0) for r in active), default=0)))
        yield header, {}, active, float(start.get('t', 0)), end, str(start.get('round', ''))


def recording(path, map_paths=None, notes_directory=None):
    records = load_jsonl(path)
    header = next((r for r in records if r.get('type') == 'header'), None)
    if not header:
        raise ValueError('missing recording header')
    notes_path = (Path(notes_directory) / path.with_suffix('.notes.json').name) if notes_directory else path.with_suffix('.notes.json')
    notes, decoded, notes_error = None, None, None
    if notes_path.exists():
        notes = demo_notes.load_json(notes_path)
        if notes.get('schema') != 'codex.demo_notes/1' or notes.get('coordinates') != 'UE_cm_X_north_Y_east':
            raise ValueError('unsupported notes schema or coordinates')
        if notes.get('recording') != path.name or notes.get('map') != header.get('map'):
            raise ValueError('notes recording/map does not match demo')
        geometry = (map_paths or {}).get(header.get('map'))
        if geometry:
            map_path, index_path = geometry
            decoded = demo_notes.decode(notes, demo_notes.load_map(map_path), demo_notes.load_json(index_path))
        else:
            notes_error = 'No explicit map/index supplied; raw intent retained, geometry unknown'
    rows, assigned_notes = [], set()
    for i, (h, end, segment, t0, t1, rid) in enumerate(fights(records)):
        report = analyze_records([h, *segment, {'type': 'footer', 'duration_s': t1 - t0}])
        for tactic in report.tactics:
            if tactic.tag == 'scan_pitch_range':
                tactic.t_start, tactic.t_end = t0, t1
        annotations = []
        for ni, a in enumerate((notes or {}).get('annotations', [])):
            if ni not in assigned_notes and t0 <= float(a['t']) <= t1 and (a.get('round') is None or str(a['round']) == rid):
                assigned_notes.add(ni)
                annotations.append({'intent': 'should_have', 'raw': a,
                                    'decoded': next((d for d in (decoded or {}).get('annotations', []) if d['id'] == a['id']), None)})
        flags = h.get('flags', {})
        rows.append({'id': f'{path.stem}:{i + 1}', 'recording': path.name, 'round': rid,
                     'map': h.get('map'), 'sector': next((r.get('sector_id') for r in segment if 'sector_id' in r), h.get('sector_id')),
                     'seed': next((r.get('seed') for r in segment if 'seed' in r), None),
                     'start_s': t0, 'end_s': t1, 'duration_s': float(end.get('duration_s', t1 - t0)),
                     'outcome': outcome(end.get('result')), 'completed': outcome(end.get('result')) != 'unknown',
                     'lab': bool(flags.get('immortal') or flags.get('infinite_ammo')),
                     'truncated': bool(next((r.get('truncated', False) for r in reversed(records) if r.get('type') == 'footer'), False)),
                     'kills_player': end.get('kills_player'), 'kills_pmc': end.get('kills_pmc'),
                     'event_counts': dict(sorted(Counter(r.get('type') for r in segment).items())),
                     'tactics': [asdict(t) for t in report.tactics], 'kill_events': [asdict(k) for k in report.kills],
                     'notes': annotations})
    return {'recording': path.name, 'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
            'notes_sha256': hashlib.sha256(notes_path.read_bytes()).hexdigest() if notes_path.exists() else None,
            'notes_geometry_warning': notes_error, 'unassigned_notes': [a for i, a in enumerate((notes or {}).get('annotations', []))
                if i not in assigned_notes], 'fights': rows}


def summarize(rows):
    table = []
    for tag in TACTICS:
        tagged = [r for r in rows if any(t['tag'] == tag for t in r['tactics'])]
        eligible = [r for r in tagged if r['completed'] and not r['lab'] and not r['truncated']]
        counts = Counter(r['outcome'] for r in eligible)
        table.append({'tag': tag, 'detections': sum(t['tag'] == tag for r in rows for t in r['tactics']),
                      'fights': len(tagged), 'recordings': len({r['recording'] for r in tagged}),
                      'completed_eligible': len(eligible), 'wins': counts['victory'], 'losses': counts['defeat'],
                      'draws': counts['draw'], 'win_rate': counts['victory'] / len(eligible) if eligible else None})
    return table


def build(directory, map_paths=None, notes_directory=None):
    sources, errors = [], []
    for path in sorted(Path(directory).glob('*.jsonl')):
        try:
            sources.append(recording(path, map_paths, notes_directory))
        except (OSError, ValueError, KeyError, TypeError) as e:
            errors.append({'recording': path.name, 'error': str(e)})
    rows = [r for s in sources for r in s.pop('fights')]
    return {'schema': 'codex.demo_dataset/1', 'sources': sources, 'errors': errors,
            'fights': rows, 'summary': summarize(rows)}


def markdown(data):
    rows = data['fights']
    eligible = [r for r in rows if r['completed'] and not r['lab'] and not r['truncated']]
    outcomes = Counter(r['outcome'] for r in eligible)
    lines = ['# Designer demo dataset', '',
             f"{len(data['sources'])} recordings; {len(rows)} fights/legacy sessions; "
             f"{sum(r['completed'] for r in rows)} completed; {len(data['errors'])} input errors.", '',
             f"Eligible completed baseline: {outcomes['victory']} wins / {outcomes['defeat']} defeats / {outcomes['draw']} draws.", '',
             'Not detected is unknown. Win rates exclude unfinished, lab and truncated fights. Associations do not prove a tactic caused a win.', '',
             '| Tactic | Detections | Fights | Recordings | Eligible W/L/D | Win rate |',
             '|---|---:|---:|---:|---:|---:|']
    for s in data['summary']:
        rate = f"{s['win_rate']:.1%}" if s['win_rate'] is not None else 'unknown'
        lines.append(f"| {s['tag']} | {s['detections']} | {s['fights']} | {s['recordings']} | {s['wins']}/{s['losses']}/{s['draws']} | {rate} |")
    lines += ['', '| Recording | Fights | W/L/D/unknown | Notes |', '|---|---:|---|---:|']
    for source in data['sources']:
        pool = [r for r in rows if r['recording'] == source['recording']]
        c = Counter(r['outcome'] for r in pool)
        lines.append(f"| {source['recording']} | {len(pool)} | {c['victory']}/{c['defeat']}/{c['draw']}/{c['unknown']} | {sum(len(r['notes']) for r in pool)} |")
    return '\n'.join(lines) + '\n'


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('demos', type=Path)
    p.add_argument('--out', type=Path, required=True, help='Output stem (.json and .md)')
    p.add_argument('--map', type=Path, help='Explicit geometry for --map-name; never guesses another map')
    p.add_argument('--map-name')
    p.add_argument('--index', type=Path)
    p.add_argument('--notes-dir', type=Path, help='TG_DEMO_NOTES_DIR when separate from demos')
    args = p.parse_args()
    if any((args.map, args.index, args.map_name)) and not all((args.map, args.index, args.map_name)):
        p.error('--map, --map-name and --index must be supplied together')
    data = build(args.demos, {args.map_name: (args.map, args.index)} if args.map else None, args.notes_dir)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    Path(str(args.out) + '.json').write_text(json.dumps(data, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
    Path(str(args.out) + '.md').write_text(markdown(data), encoding='utf-8')
    print(markdown(data))
    return int(bool(data['errors']))


if __name__ == '__main__':
    raise SystemExit(main())
