"""Freeze a demo-derived proxy profile into a NEW trainer config; never edits game weights."""
import argparse
import json
from pathlib import Path

from nighttrainer.doctrine import profile
from nighttrainer.genome import generate_bounds
from weekend_trainer import ROOT, arena_for


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('dataset', type=Path)
    p.add_argument('--base', type=Path, default=Path(__file__).with_name('trainer_config.json'))
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--weekend', type=Path, help='Prepare X6 sector training matches from weekend_trainer.json')
    p.add_argument('--champion', type=Path, help='Freeze this approved baseline and generated bounds beside --out')
    a = p.parse_args()
    cfg = json.loads(a.base.read_text(encoding='utf-8-sig'))
    cfg['fitness']['doctrine'] = profile(json.loads(a.dataset.read_text(encoding='utf-8')))
    a.out.parent.mkdir(parents=True, exist_ok=True)
    if a.champion:
        baseline = a.out.parent.resolve() / 'approved_champion.json'
        bounds = a.out.parent.resolve() / 'approved_champion.bounds.json'
        baseline.write_bytes(a.champion.read_bytes())
        bounds.write_text(json.dumps(generate_bounds(json.loads(baseline.read_text(encoding='utf-8-sig'))), indent=2) + '\n', encoding='utf-8')
        cfg['baseline_weights'], cfg['bounds'] = str(baseline), str(bounds)
    if a.weekend:
        wc = json.loads(a.weekend.read_text(encoding='utf-8-sig'))
        cfg['seed'] = wc['seed']
        definitions = json.loads((ROOT / wc['sector_definitions']).read_text(encoding='utf-8-sig'))
        matches = []
        for s in definitions['sectors']:
            if s['id'] not in wc['sectors']:
                continue
            level = json.loads((ROOT / wc['level']).read_text(encoding='utf-8-sig'))
            level['pmc_squads'] = []
            level.setdefault('pmc_arena', {})['play_area'] = {'include_box_cm': s['box_cm']}
            level_path = a.out.parent.resolve() / f"sector_{s['id']}.json"
            level_path.write_text(json.dumps(level, indent=2) + '\n', encoding='utf-8')
            matches.append({'opponent': 'champion', 'jobs': 1, 'rounds': 1,
                            'engine_args': [f'-LevelJson={level_path.as_posix()}']})
        cfg['arena'] = arena_for(cfg, wc, a.out.parent.resolve() / 'sector_1.json')
        cfg['arena']['engine_args'] = [x for x in cfg['arena']['engine_args'] if not x.startswith('-LevelJson=')]
        cfg['evaluation']['matches'] = matches
        cfg['search']['mode'] = 'ga'
        cfg['gate_scenarios'] = ['skirmish', 'chokepoint', 'street', 'roofs', 'blizzard', 'tactical']
        cfg['highlights']['auto_clips'] = False
    a.out.write_text(json.dumps(cfg, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(cfg['fitness']['doctrine'], indent=2))


if __name__ == '__main__':
    main()
