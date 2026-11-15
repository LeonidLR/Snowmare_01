// Height gate, independent of nav pathfinding so geometry errors keep their object IDs.
import type { GenLayout } from '../schema.ts';
import type { LevelPropsFile } from './schema.ts';
import { classifyObstacleHeight } from '../obstacleHeights.ts';

export interface ObstacleHeightError { id: string; height_cm: number; reason: string }

export function runObstacleHeightGate(
  layout: GenLayout,
  props?: LevelPropsFile,
  kinds: Record<string, Record<string, unknown>> = {},
): { gate: 'pass' | 'fail'; errors: ObstacleHeightError[] } {
  const errors: ObstacleHeightError[] = [];
  const check = (id: string, height: number) => {
    if (!classifyObstacleHeight(height)) errors.push({ id, height_cm: height, reason: `obstacle ${id}: height ${height} cm is outside standard classes` });
  };
  for (const w of layout.walls) check(w.id, w.height);
  for (const c of layout.covers) check(c.id, c.size[2]);
  for (const edge of layout.mantle_edges ?? []) check(edge.id, edge.height_cm);
  for (const b of layout.buildings) {
    check(b.id, b.height);
    for (const p of b.partitions ?? []) check(p.id, p.height ?? 300);
    for (const [i, p] of (b.parapets ?? []).entries()) check(p.wall_id ?? `${b.id}:parapet:${i}`, p.height_cm);
    for (const edge of b.mantle_edges ?? []) check(edge.id, edge.height_cm);
  }
  for (const layer of props?.layers ?? []) {
    if (layer.enabled === false) continue;
    for (const p of layer.objects) {
      const k = kinds[p.variant] ?? {};
      // Layer props inherit their physical size from balance, not their placement record.
      if (!p.variant.startsWith('crate') && !p.variant.startsWith('barrel')) continue;
      const size = k.size_cm;
      const height = typeof k.height_cm === 'number' ? k.height_cm : Array.isArray(size) ? Number(size[2]) : NaN;
      check(p.id, height);
    }
  }
  return { gate: errors.length ? 'fail' : 'pass', errors };
}
