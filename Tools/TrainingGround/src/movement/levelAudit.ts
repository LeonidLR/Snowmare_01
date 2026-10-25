// Training Ground / level design: a layout audit (0 tokens) - the first checks of the level-design bot. It finds layout
// mistakes before any squad walks: schema errors, route points inside walls, unreachable legs, zones no route crosses, and
// expectations a route cannot satisfy because it does not stay in the zone long enough for the classifier to settle.

import type { FormationConfig } from './config.ts';
import { cfg } from './config.ts';
import type { Vec2 } from './geometry.ts';
import { dist, pointInPoly, pointInWall } from './geometry.ts';
import type { Layout } from './layout.ts';
import { validateLayout, wallSegments } from './layout.ts';
import { AGENT_RADIUS, NavGrid } from './nav.ts';

export interface AuditIssue {
  severity: 'error' | 'warning';
  where: string;
  message: string;
}

/** Samples a route polyline every `step` cm (start and end included). */
function samplePath(points: Vec2[], step: number): Vec2[] {
  const out: Vec2[] = [points[0]];
  for (let i = 1; i < points.length; i++) {
    const a = points[i - 1], b = points[i], L = dist(a, b);
    for (let s = step; s < L; s += step) out.push([a[0] + ((b[0] - a[0]) * s) / L, a[1] + ((b[1] - a[1]) * s) / L]);
    out.push(b);
  }
  return out;
}

export function auditLayout(data: unknown, config: FormationConfig, nav?: NavGrid): AuditIssue[] {
  const schema = validateLayout(data);
  if (schema.length) return schema.map((message) => ({ severity: 'error' as const, where: 'schema', message }));
  const layout = data as Layout;
  const issues: AuditIssue[] = [];
  const walls = wallSegments(layout);
  const grid = nav ?? new NavGrid(walls, layout.bounds.min, layout.bounds.max);

  if (pointInWall(layout.spawn.pos, walls, AGENT_RADIUS)) issues.push({ severity: 'error', where: 'spawn', message: 'spawn point inside a wall' });

  const crossed = new Set<string>();
  for (const route of layout.routes) {
    route.points.forEach((p, i) => {
      if (pointInWall(p, walls, AGENT_RADIUS)) issues.push({ severity: 'error', where: `${route.id}.points[${i}]`, message: 'route point inside a wall' });
    });
    for (let i = 1; i < route.points.length; i++) {
      const path = grid.path(route.points[i - 1], route.points[i]);
      const straight = dist(route.points[i - 1], route.points[i]);
      if (!path) issues.push({ severity: 'error', where: `${route.id} leg ${i}`, message: 'unreachable: walls block the leg completely' });
      else if (path.length > 2) {
        const L = path.slice(1).reduce((acc, p, k) => acc + dist(path[k], p), 0);
        issues.push({ severity: 'warning', where: `${route.id} leg ${i}`, message: `the leg crosses a wall; the leader detours ${Math.round(L - straight)} cm (add a route point?)` });
      }
    }
    // Time inside each expected zone at the route's pace vs. what the classifier needs to settle.
    const speed = route.pace === 'run' ? cfg(config, 'pace.run_speed') : cfg(config, 'pace.walk_speed') * cfg(config, 'pace.patrol');
    const step = 25;
    const samples = samplePath(route.points, step);
    const inZone = new Map<string, number>();
    for (const p of samples) for (const z of layout.zones) if (pointInPoly(p, z.poly)) {
      crossed.add(z.id);
      inZone.set(z.id, (inZone.get(z.id) ?? 0) + step);
    }
    const need = (cfg(config, 'classify.hold_time') + 0.5) * speed + 100;
    for (const e of route.expect ?? []) {
      const len = inZone.get(e.zone) ?? 0;
      const zone = layout.zones.find((z) => z.id === e.zone)!;
      const endsHere = pointInPoly(route.points[route.points.length - 1], zone.poly); // the squad halts there: time to settle
      if (len === 0) issues.push({ severity: 'error', where: `${route.id} expect ${e.zone}`, message: 'the route never enters this zone' });
      else if (len < need && !endsHere) issues.push({ severity: 'warning', where: `${route.id} expect ${e.zone}`, message: `only ${Math.round(len)} cm of the route in the zone; the classifier needs ~${Math.round(need)} cm to settle at ${route.pace} pace` });
    }
  }
  for (const z of layout.zones) if (!crossed.has(z.id)) issues.push({ severity: 'warning', where: z.id, message: 'no route crosses this zone' });
  return issues;
}
