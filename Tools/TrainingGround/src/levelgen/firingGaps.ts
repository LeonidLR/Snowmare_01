// Training Ground / level generator - FIRING GAPS in the roof parapet (designer 2026-11-19; docs/design/levelgen/FIRING_GAPS_HANDOVER.md,
// BUILDING_GENERATOR.md "Firing gaps"). T-05 found no KNEELING roof-edge position on Depot X4: the parapet is 95 cm, a sniper fires kneeling
// (SniperRules::FiringStance, muzzle 85 cm over the floor). A gap is a span cut out of the parapet and refilled with a LOW SILL (config
// sill_cm) a kneeling muzzle clears; the analysis' overwatch spots stand at the roof rim (chokepoints.json threats.overwatch.rim_stand_inset_m)
// behind those gaps.
//   planFiringGaps(l, analysis) -> FiringGap[]   pure, deterministic, every number in Content/Data/LevelGen/firing_gaps.json
//   withFiringGaps(l, analysis)  -> the layout with the TOP-LEVEL `firing_gaps` set (never inside `buildings`: geometryHash hashes them)
// Consumers: «Принять» (LevelGenDepartment accept), scripts/add-firing-gaps.ts, tacticalMap.ts (spot -> gap id, `firing_gaps` list),
// Scripts/Editor/build_levelgen_map.py (cuts the parapet, spawns the sill). Pure TS (browser + node).
import cfgJson from '../../../../Content/Data/LevelGen/firing_gaps.json' with { type: 'json' };
import type { Vec2 } from '../movement/geometry.ts';
import { yawOf } from '../movement/geometry.ts';
import type { LayoutAnalysis } from './chokepoints.ts';
import { footprintPoly, inFootprint } from './rotation.ts';
import type { FiringGap, GenBuilding, GenLayout, RoofParapet } from './schema.ts';
import { NEVER_CLIMBABLE } from './schema.ts';

// ---- config -------------------------------------------------------------------------------------------------------------------

export interface FiringGapsConfig {
  schema: string;
  enabled: boolean;
  sill_cm: number;
  width_cm: number;
  spacing_m: number;
  corner_clear_m: number;
  max_per_side: number;
  max_per_roof: number;
  stand_inset_cm: number;
  street_probe_m: number;
  ladder_clear_cm: number;
  roles: string[];
}
export const FIRING_GAPS = cfgJson as unknown as FiringGapsConfig;
export const FIRING_GAPS_SCHEMA = 'codex.levelgen_firing_gaps/1';
/** The keys the code reads, in the JSON's order; every one has a `<key>_comment` beside it (a test pins both against the file). */
export const FIRING_GAPS_KEYS = ['enabled', 'sill_cm', 'width_cm', 'spacing_m', 'corner_clear_m', 'max_per_side', 'max_per_roof', 'stand_inset_cm', 'street_probe_m', 'ladder_clear_cm', 'roles'] as const;

/** The config's own checks (empty = valid). */
export function validateFiringGapsConfig(c: FiringGapsConfig): string[] {
  const e: string[] = [];
  if (c.schema !== FIRING_GAPS_SCHEMA) e.push(`schema must be "${FIRING_GAPS_SCHEMA}"`);
  if (typeof c.enabled !== 'boolean') e.push('enabled: expected true / false');
  for (const k of ['sill_cm', 'width_cm', 'spacing_m', 'street_probe_m'] as const) if (!(c[k] > 0)) e.push(`${k}: expected a number > 0`);
  for (const k of ['corner_clear_m', 'stand_inset_cm', 'ladder_clear_cm'] as const) if (!(c[k] >= 0)) e.push(`${k}: expected a number >= 0`);
  for (const k of ['max_per_side', 'max_per_roof'] as const) if (!Number.isInteger(c[k]) || c[k] < 1) e.push(`${k}: expected an integer >= 1`);
  if (c.max_per_roof < c.max_per_side) e.push('max_per_roof must be >= max_per_side');
  if (!Array.isArray(c.roles) || c.roles.some((r) => typeof r !== 'string')) e.push('roles: expected a list of building roles');
  return e;
}

// ---- helpers ------------------------------------------------------------------------------------------------------------------

const rN = (v: number, d = 1): number => { const k = 10 ** d; const x = Math.round(v * k) / k; return Object.is(x, -0) ? 0 : x; };
/** The sides in the order the extra gaps are planned (the parapet wall id ends `_N` / `_S` / `_W` / `_E`; anything else goes last, by id). */
const SIDE_ORDER = ['N', 'S', 'W', 'E'];
const sideRank = (id: string): number => { const i = SIDE_ORDER.indexOf(id.slice(id.lastIndexOf('_') + 1)); return i < 0 ? SIDE_ORDER.length : i; };

interface Side {
  p: RoofParapet; id: string; a: Vec2; wv: Vec2; len: number; out: Vec2; rank: number;
  /** Along-wall positions (cm from `a`) of the ladder / stair tops that arrive on this side. */
  tops: number[];
  gaps: number;
}

/** The closest point of segment a-b to p: [parameter t in cm from a (clamped), distance]. */
function nearestOn(p: Vec2, a: Vec2, wv: Vec2, len: number): [number, number] {
  const t = Math.max(0, Math.min(len, (p[0] - a[0]) * wv[0] + (p[1] - a[1]) * wv[1]));
  return [t, Math.hypot(p[0] - (a[0] + wv[0] * t), p[1] - (a[1] + wv[1] * t))];
}

/** The roof's four-or-more parapet sides with their outward normals (away from the footprint centre). */
function sidesOf(b: GenBuilding): Side[] {
  const poly = footprintPoly(b);
  const cx = poly.reduce((s, q) => s + q[0], 0) / poly.length, cy = poly.reduce((s, q) => s + q[1], 0) / poly.length;
  return (b.parapets ?? []).map((p, i): Side => {
    const dx = p.end[0] - p.start[0], dy = p.end[1] - p.start[1], len = Math.hypot(dx, dy) || 1;
    const wv: Vec2 = [dx / len, dy / len];
    let out: Vec2 = [wv[1], -wv[0]];
    if (out[0] * ((p.start[0] + p.end[0]) / 2 - cx) + out[1] * ((p.start[1] + p.end[1]) / 2 - cy) < 0) out = [-out[0], -out[1]];
    const id = p.wall_id ?? `${b.id}_parapet_${i}`;
    return { p, id, a: p.start, wv, len, out, rank: sideRank(id), tops: [], gaps: 0 };
  });
}

// ---- the plan -----------------------------------------------------------------------------------------------------------------

/**
 * The firing gaps of a layout. Roofs: walkable, with parapets, whose building role is in `roles` or that the analysis lists as an
 * overwatch roof. Per roof: one gap at each analysis spot (on its nearest parapet, at the spot's projection), then extra gaps every
 * `spacing_m` along the street-facing parapets (sides N, S, W, E). A position is skipped when within `spacing_m` of another gap, within
 * `ladder_clear_cm` of a ladder / external stair top that arrives on that parapet, or beyond the per-side / per-roof caps.
 */
export function planFiringGaps(l: GenLayout, analysis: LayoutAnalysis | null | undefined, cfg: FiringGapsConfig = FIRING_GAPS): FiringGap[] {
  if (!cfg.enabled) return [];
  const out: FiringGap[] = [];
  const half = cfg.width_cm / 2, lo = cfg.corner_clear_m * 100 + half, spacing = cfg.spacing_m * 100;
  const roofSpots = new Map((analysis?.overwatchRoofs ?? []).map((r) => [r.building, r.spots] as const));
  for (const b of l.buildings) {
    if (!b.roof_walkable || NEVER_CLIMBABLE.includes(b.set_piece) || !(b.parapets ?? []).length) continue;
    const spots = roofSpots.get(b.id);
    if (!spots && !(b.role && cfg.roles.includes(b.role))) continue;
    const sides = sidesOf(b);
    // ladder / external stair tops: they exclude a stretch of the parapet nearest to them
    const tops: Vec2[] = [
      ...l.ladders.filter((k) => k.to === `roof:${b.id}`).map((k) => k.top),
      ...(b.stairs ?? []).filter((s) => s.kind === 'external' && s.to >= b.floors && s.top).map((s) => s.top as Vec2),
    ];
    for (const tp of tops) {
      let best: { s: Side; t: number; d: number } | null = null;
      for (const s of sides) { const [t, d] = nearestOn(tp, s.a, s.wv, s.len); if (!best || d < best.d) best = { s, t, d }; }
      if (best) best.s.tops.push(best.t);
    }
    const mine: FiringGap[] = [];
    const place = (s: Side, t0: number): boolean => {
      const hi = s.len - cfg.corner_clear_m * 100 - half;
      if (hi < lo) return false;
      const t = Math.max(lo, Math.min(hi, t0));
      if (s.gaps >= cfg.max_per_side || mine.length >= cfg.max_per_roof) return false;
      if (s.tops.some((tt) => Math.abs(t - tt) - half < cfg.ladder_clear_cm)) return false;
      const c: Vec2 = [s.a[0] + s.wv[0] * t, s.a[1] + s.wv[1] * t];
      if (mine.some((g) => Math.hypot(g.center[0] - c[0], g.center[1] - c[1]) < spacing)) return false;
      s.gaps++;
      mine.push({
        id: `FG_${b.id}_${mine.length + 1}`, building: b.id, parapet: s.id, center: [rN(c[0]), rN(c[1])], width_cm: cfg.width_cm, sill_cm: cfg.sill_cm,
        outward: [rN(s.out[0], 4), rN(s.out[1], 4)],
        stand: [rN(c[0] - s.out[0] * cfg.stand_inset_cm), rN(c[1] - s.out[1] * cfg.stand_inset_cm)], facing_yaw: rN(yawOf(s.out)),
      });
      return true;
    };
    // 1. one gap per analysis spot: its nearest parapet, at its projection
    for (const sp of spots ?? []) {
      let best: { s: Side; t: number; d: number } | null = null;
      for (const s of sides) { const [t, d] = nearestOn(sp.pos, s.a, s.wv, s.len); if (!best || d < best.d - 1e-9) best = { s, t, d }; }
      if (best) place(best.s, best.t);
    }
    // 2. extra gaps every spacing_m along the street-facing sides
    const [bmin, bmax] = [l.bounds.min, l.bounds.max];
    const others = l.buildings.filter((o) => o.id !== b.id);
    for (const s of [...sides].sort((x, y) => x.rank - y.rank || (x.id < y.id ? -1 : x.id > y.id ? 1 : 0))) {
      const probe: Vec2 = [s.a[0] + s.wv[0] * s.len / 2 + s.out[0] * cfg.street_probe_m * 100, s.a[1] + s.wv[1] * s.len / 2 + s.out[1] * cfg.street_probe_m * 100];
      const facesStreet = probe[0] >= bmin[0] && probe[0] <= bmax[0] && probe[1] >= bmin[1] && probe[1] <= bmax[1] && !others.some((o) => inFootprint(o, probe));
      const hi = s.len - cfg.corner_clear_m * 100 - half;
      if (!facesStreet || hi < lo) continue;
      const usable = hi - lo, k = Math.floor(usable / spacing) + 1, first = lo + (usable - (k - 1) * spacing) / 2;
      for (let i = 0; i < k; i++) place(s, first + i * spacing);
    }
    out.push(...mine);
  }
  return out;
}

/**
 * The layout with its top-level `firing_gaps` set to planFiringGaps. The key is left out when the config is disabled or no gap was planned
 * (a layout without gaps stays byte-identical). Never touches `buildings`, so geometryHash (and the analysis) stay current.
 */
export function withFiringGaps(l: GenLayout, analysis: LayoutAnalysis | null | undefined = l.analysis, cfg: FiringGapsConfig = FIRING_GAPS): GenLayout {
  const gaps = planFiringGaps(l, analysis, cfg);
  const next = { ...l };
  if (gaps.length) next.firing_gaps = gaps; else delete next.firing_gaps;
  return next;
}

/** The gap whose centre is within width / 2 + `slack_cm` of the point's projection on the gap's parapet - the one a spot stands behind. */
export function gapForSpot(gaps: readonly FiringGap[], building: string, pos: Vec2, slack_cm = 30): FiringGap | null {
  let best: FiringGap | null = null, bd = Infinity;
  for (const g of gaps) {
    if (g.building !== building) continue;
    // the spot's offset from the gap centre along the parapet (perpendicular to `outward`)
    const dx = pos[0] - g.center[0], dy = pos[1] - g.center[1];
    const along = Math.abs(dx * -g.outward[1] + dy * g.outward[0]), across = Math.abs(dx * g.outward[0] + dy * g.outward[1]);
    const behind = Math.hypot(g.stand[0] - g.center[0], g.stand[1] - g.center[1]);
    if (along > g.width_cm / 2 + slack_cm || Math.abs(across - behind) > slack_cm) continue;
    const d = Math.hypot(along, across);
    if (d < bd) { bd = d; best = g; }
  }
  return best;
}
