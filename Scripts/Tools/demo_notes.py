#!/usr/bin/env python3
"""Decode designer notes into geometric facts, never inferred tactical success.

World cm, UE +X north/+Y east. Routes are the exact sampled polylines saved by TG.
Pass = outside a building, within 5 m, travelling >= min(2 m, 25% wall length)
along a cardinal side. Endpoint-only/corner approaches do not count as passes.
Open ground = >5 m from any building footprint or cover (geometry proxy, not LOS).
"""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_MAP = ROOT / 'Content/Data/LevelJson/L_Gen_Depot_1_X6.json'
PASS_DISTANCE_CM = 500
COVER_TOUCH_CM = 75
SAMPLE_CM = 25


def load_json(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def load_map(path):
    """Accept either the level config or its referenced layout."""
    path = Path(path).resolve()
    data = load_json(path)
    if 'levelgen_layout' in data:
        for parent in (path.parent, *path.parents):
            layout = parent / data['levelgen_layout']
            if layout.is_file():
                return load_json(layout)
        raise ValueError('Cannot resolve levelgen_layout from map path')
    if 'buildings' not in data:
        raise ValueError('Map has no building geometry')
    return data


def rectangle(building):
    a, b = building['footprint']
    return min(a[0], b[0]), min(a[1], b[1]), max(a[0], b[0]), max(a[1], b[1])


def rect_distance(p, box):
    x0, y0, x1, y1 = box
    return math.hypot(max(x0 - p[0], 0, p[0] - x1), max(y0 - p[1], 0, p[1] - y1))


def building_frame(p, building):
    """Only footprint is building-local; other layout positions are already world cm."""
    x0, y0, x1, y1 = rectangle(building)
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    dx, dy = p[0] - cx, p[1] - cy
    a = math.radians(building.get('rotation', 0))
    return [cx + dx * math.cos(a) + dy * math.sin(a), cy - dx * math.sin(a) + dy * math.cos(a)]


def building_distance(p, building):
    return rect_distance(building_frame(p, building), rectangle(building))


def cardinal_side(local_side, building):
    angle = {'north': 0, 'east': 90, 'south': 180, 'west': 270}[local_side] + building.get('rotation', 0)
    x, y = math.cos(math.radians(angle)), math.sin(math.radians(angle))
    return ('north' if x >= 0 else 'south') if abs(x) >= abs(y) else ('east' if y >= 0 else 'west')


def side_of_building(p, building):
    """Cardinal side only while projected onto that wall; corners/inside -> None."""
    x, y = building_frame(p, building)
    x0, y0, x1, y1 = rectangle(building)
    if x0 <= x <= x1:
        if y > y1:
            return cardinal_side('east', building), y - y1, x, x1 - x0
        if y < y0:
            return cardinal_side('west', building), y0 - y, x, x1 - x0
    if y0 <= y <= y1:
        if x > x1:
            return cardinal_side('north', building), x - x1, y, y1 - y0
        if x < x0:
            return cardinal_side('south', building), x0 - x, y, y1 - y0
    return None


def sample_route(points):
    """(cumulative length, point), spacing <=25cm, preserving every vertex."""
    if not points:
        return []
    out = [(0.0, points[0][:2])]
    total = 0.0
    for a, b in zip(points, points[1:]):
        length = math.dist(a[:2], b[:2])
        steps = max(1, math.ceil(length / SAMPLE_CM))
        for i in range(1, steps + 1):
            alpha = i / steps
            out.append((total + length * alpha, [a[k] + (b[k] - a[k]) * alpha for k in (0, 1)]))
        total += length
    return out


def buildings_passed(points, buildings, index):
    samples = sample_route(points)
    facts = []
    numbers = {identifier: str(number) for number, identifier in index.items()}
    for building in buildings:
        groups, active = [], []
        for length, p in samples:
            side = side_of_building(p, building)
            if side and side[1] <= PASS_DISTANCE_CM:
                if active and active[-1][1][0] != side[0]:
                    groups.append(active)
                    active = []
                active.append((length, side))
            elif active:
                groups.append(active)
                active = []
        if active:
            groups.append(active)
        for group in groups:
            along = [s[2] for _, s in group]
            if max(along) - min(along) + 1e-6 < min(200, group[0][1][3] * .25):
                continue
            facts.append({'building_id': building['id'], 'number': numbers.get(building['id']),
                          'side': group[0][1][0], 'min_wall_distance_m': round(min(s[1] for _, s in group) / 100, 2),
                          'from_m': round(group[0][0] / 100, 2), 'to_m': round(group[-1][0] / 100, 2)})
    return sorted(facts, key=lambda f: (f['from_m'], f['building_id']))


def cover_distance(p, cover):
    # Layout sizes rotate with yaw around UE Z.
    dx, dy = p[0] - cover['pos'][0], p[1] - cover['pos'][1]
    angle = math.radians(cover.get('yaw', 0))
    x, y = dx * math.cos(angle) + dy * math.sin(angle), -dx * math.sin(angle) + dy * math.cos(angle)
    size = cover.get('size', [100, 100, 100])
    return rect_distance([x, y], [-size[0] / 2, -size[1] / 2, size[0] / 2, size[1] / 2])


def decode(notes, layout, index):
    if notes.get('schema') != 'codex.demo_notes/1' or notes.get('coordinates') != 'UE_cm_X_north_Y_east':
        raise ValueError('Unsupported notes schema or coordinate frame')
    buildings, covers = layout.get('buildings', []), layout.get('covers', [])
    numbers = {identifier: str(number) for number, identifier in index.items()}
    result = {'recording': notes['recording'], 'map': notes['map'], 'rules': {
        'axes': '+X north, +Y east; cm', 'pass_distance_cm': PASS_DISTANCE_CM,
        'pass_along_wall_min_cm': 'min(200, wall_length * .25)', 'cover_touch_cm': COVER_TOUCH_CM,
        'sampling_cm': SAMPLE_CM, 'open_ground': '>500cm from building footprint or cover; geometry proxy only, no LOS/height/weapon safety claim'}, 'annotations': []}
    for annotation in notes['annotations']:
        a = {k: annotation[k] for k in ('id', 'kind', 't', 'author', 'unit', 'label')}
        a['round'] = annotation.get('round')
        points = annotation['points']
        if annotation['kind'] in ('route', 'polyline', 'arrow'):
            samples = sample_route(points)
            a['length_m'] = round(samples[-1][0] / 100, 2) if samples else 0
            a['buildings_passed'] = buildings_passed(points, buildings, index)
            a['covers_touched'] = [{'id': c['id'], 'kind': c.get('kind'), 'pos_cm': c['pos'],
                                   'first_at_m': round(next(length for length, p in samples if cover_distance(p, c) <= COVER_TOUCH_CM) / 100, 2)}
                                  for c in covers if any(cover_distance(p, c) <= COVER_TOUCH_CM for _, p in samples)]
            # Only nearby geometry needs a distance calculation for open intervals.
            if samples:
                xs, ys = [p[0] for _, p in samples], [p[1] for _, p in samples]
                route_box = min(xs), min(ys), max(xs), max(ys)
                nearby_b = []
                for b in buildings:
                    x0, y0, x1, y1 = rectangle(b)
                    if rect_distance([(x0 + x1) / 2, (y0 + y1) / 2], route_box) <= math.hypot(x1 - x0, y1 - y0) / 2 + 500:
                        nearby_b.append(b)
                nearby_c = [c for c in covers if rect_distance(c['pos'], route_box) <= max(c.get('size', [100, 100])[:2]) + 500]
                spans, start = [], None
                for length, p in samples:
                    exposed = all(building_distance(p, b) > 500 for b in nearby_b) and all(cover_distance(p, c) > 500 for c in nearby_c)
                    if exposed and start is None:
                        start = length
                    if not exposed and start is not None:
                        if length - start >= 100:
                            spans.append({'from_m': round(start / 100, 2), 'to_m': round(length / 100, 2)})
                        start = None
                if start is not None and samples[-1][0] - start >= 100:
                    spans.append({'from_m': round(start / 100, 2), 'to_m': round(samples[-1][0] / 100, 2)})
                a['open_ground_crossings'] = spans
                a['building_footprints_crossed'] = [b['id'] for b in buildings if any(building_distance(p, b) < 1e-7 for _, p in samples)]
        elif annotation['kind'] == 'zone' and points:
            centre = points[0]
            a['centre_cm'] = centre
            a['radius_m'] = round(math.dist(points[0][:2], points[1][:2]) / 100, 2)
            a['buildings_at_centre'] = []
            for building in buildings:
                if building_distance(centre, building) < 1e-7:
                    height = annotation.get('height_cm')
                    roof = building.get('height')
                    a['buildings_at_centre'].append({'id': building['id'], 'number': numbers.get(building['id']),
                        'roof': height is not None and roof is not None and abs(height - roof) <= 100,
                        'height_evidence_cm': height})
        elif points:
            a['pin_cm'] = points[0]
        result['annotations'].append(a)
    return result


def markdown(report):
    lines = [f"# Replay notes: {report['recording']}", '', 'Designer intent, decoded from drawings. Distances are geometric; open ground is a 5 m proximity proxy, not line of sight.', '']
    for a in report['annotations']:
        timestamp = f"{int(a['t'] // 60):02}:{int(a['t'] % 60):02}"
        prefix = f"t={timestamp} R{a.get('round') or '—'} {a['unit'] or 'Unassigned'} ({a['author']})"
        lines.append(f"- {prefix}: {a['kind']} — {a['label'] or '(no label)'}")
        if 'length_m' in a:
            lines.append(f"  Length {a['length_m']} m.")
            for b in a['buildings_passed']:
                lines.append(f"  Passes building {b['number'] or b['building_id']} on its {b['side'].upper()} side, {b['min_wall_distance_m']} m off the wall (route {b['from_m']}–{b['to_m']} m).")
            for c in a['covers_touched']:
                lines.append(f"  Touches {c['kind']} {c['id']} at {c['pos_cm']} (route {c['first_at_m']} m).")
            for span in a.get('open_ground_crossings', []):
                lines.append(f"  Open-ground proxy crossing: route {span['from_m']}–{span['to_m']} m.")
            if a.get('building_footprints_crossed'):
                lines.append('  Intersects building footprint(s): ' + ', '.join(a['building_footprints_crossed']) + '; floor/path clearance unknown.')
        for b in a.get('buildings_at_centre', []):
            lines.append(f"  Zone centre {'on roof of' if b['roof'] else 'over footprint of (roof unconfirmed)'} building {b['number'] or b['id']}.")
    return '\n'.join(lines) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('notes', type=Path)
    parser.add_argument('--map', type=Path, default=DEFAULT_MAP)
    parser.add_argument('--index', type=Path)
    parser.add_argument('--out', type=Path, help='Output stem; writes <stem>.md and <stem>.json')
    args = parser.parse_args()
    index_path = args.index or args.map.with_suffix('.buildings_index.json')
    if not index_path.exists():
        parser.error('Building index missing; provide --index for a layout input')
    report = decode(load_json(args.notes), load_map(args.map), load_json(index_path))
    stem = args.out or args.notes.with_suffix('.decoded')
    Path(str(stem) + '.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    Path(str(stem) + '.md').write_text(markdown(report), encoding='utf-8')
    print(f"Decoded {len(report['annotations'])} annotations -> {stem}.md + .json")


if __name__ == '__main__':
    main()
