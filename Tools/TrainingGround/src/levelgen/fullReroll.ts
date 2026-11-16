// Training Ground / level generator v2 - designer UX round (task 072, item 4): the «Полностью (форма и расстановка)» region re-roll.
// The stage-2 'types' mode gives each block of the zone another catalog type, but a template fills its whole block, so the new
// buildings stand where the old ones stood (same shape, same place - the designer saw "only the points changed"). The 'full' mode
// re-plans the block itself:
//   1. LOT SUBDIVISION: the block (or the whole parent block, when all its parcels from an earlier 'full' re-roll are in the zone)
//      is cut by a seeded BSP into PARCELS. A cut is accepted only when both parts still face a street the block faces (so every
//      door and gate faces a street) and both stay >= min_parcel_m. chaos 0 = even cuts along the longer side (a tidy grid),
//      chaos 1 = uneven ratios, random axis, random skips (irregular).
//   2. FOOTPRINT + PLACEMENT: inside each parcel the template gets a rectangle shrunk / shifted by the chaos (a setback from the
//      street, side gaps, a short back) - so the buildings change size, position and spacing; a corner parcel may face its OTHER
//      street ("turned" 90 degrees: the template orients its doors / gates / buildings to that side). Footprints stay axis-aligned
//      rectangles (the schema and the UE builder have no rotated buildings).
//   3. ORDER: each parcel draws its type from the catalog (weighted, levelgen_v2.json templates.catalog) with its own stream.
// Everything else (the overlap rules against the locks / kept objects / roads, the route visit + gate, the retries) is the
// existing re-roll (regionRegen.ts). Constants: levelgen_v2.json full_reroll. Pure and deterministic per (block, seed, chaos).
import cfg from '../../../../Content/Data/LevelGen/levelgen_v2.json' with { type: 'json' };
import type { Vec2 } from '../movement/geometry.ts';
import { rng } from '../movement/geometry.ts';
import type { Rect, Side } from './generate.ts';

export const FULL = cfg.full_reroll;

/** One parcel of a re-planned block (metres, the plan frame): the parcel, the side its template faces, the template's rectangle. */
export interface Parcel { rect: Rect; side: Side; inner: Rect }

const SIDES: Side[] = ['south', 'north', 'west', 'east'];
const horizontal = (s: Side) => s === 'north' || s === 'south';
const wOf = (r: Rect) => r[1][0] - r[0][0];
const hOf = (r: Rect) => r[1][1] - r[0][1];
const lerp = (a: number, b: number, t: number) => a + (b - a) * t;
const clamp01 = (v: number) => Math.max(0, Math.min(1, v));

/** The gap from side s of r to the street st (negative = overlapping), or Infinity when st does not run along >= 2 m of it. */
function streetGap(r: Rect, s: Side, st: Rect): number {
  const along = horizontal(s) ? Math.min(r[1][0], st[1][0]) - Math.max(r[0][0], st[0][0]) : Math.min(r[1][1], st[1][1]) - Math.max(r[0][1], st[0][1]);
  if (along < 2) return Infinity;
  return s === 'north' ? r[0][1] - st[1][1] : s === 'south' ? st[0][1] - r[1][1] : s === 'west' ? r[0][0] - st[1][0] : st[0][0] - r[1][0];
}

/** The sides of a block with a street within street_reach_m (nearest first); none -> the nearest street's side. */
export function streetSides(r: Rect, streets: readonly Rect[]): Side[] {
  const gaps = SIDES.map((s) => ({ s, g: Math.min(...streets.map((st) => streetGap(r, s, st)).filter((g) => g >= -0.5), Infinity) }));
  const near = gaps.filter((x) => x.g <= FULL.street_reach_m).sort((a, b) => a.g - b.g).map((x) => x.s);
  if (near.length) return near;
  const best = gaps.filter((x) => Number.isFinite(x.g)).sort((a, b) => a.g - b.g)[0];
  return [best?.s ?? 'south'];
}

/** The sides of `sub` that lie on the outer block's street sides (>= 2 m of shared edge): where its doors may face. */
export function touchingSides(sub: Rect, outer: Rect, sides: readonly Side[]): Side[] {
  const eps = 0.01;
  return sides.filter((s) => {
    const on = s === 'north' ? Math.abs(sub[0][1] - outer[0][1]) < eps : s === 'south' ? Math.abs(sub[1][1] - outer[1][1]) < eps : s === 'west' ? Math.abs(sub[0][0] - outer[0][0]) < eps : Math.abs(sub[1][0] - outer[1][0]) < eps;
    return on && (horizontal(s) ? wOf(sub) : hOf(sub)) >= 2;
  });
}

const fitsMin = (r: Rect) => Math.max(wOf(r), hOf(r)) >= Math.max(...FULL.min_parcel_m) && Math.min(wOf(r), hOf(r)) >= Math.min(...FULL.min_parcel_m);

/**
 * Cut a block into parcels (metres). `key` names the block's random stream (its id), `seed` the click. Every parcel touches one of
 * the block's street sides; parcels never overlap; together with the gaps they tile the block (the union bbox = the block).
 */
export function subdivideBlock(block: Rect, streets: readonly Rect[], seed: number, key: string, chaos: number): Parcel[] {
  const ch = clamp01(chaos);
  const random = rng(((Math.imul(seed | 0, 1000003) >>> 0) ^ hashKey(`${key}:full`)) >>> 0);
  const sides = streetSides(block, streets);
  const splitChance = lerp(FULL.split_chance_tidy, FULL.split_chance_chaos, ch);
  const leaves: Rect[] = [];
  const split = (r: Rect, depth: number): void => {
    const big = Math.max(wOf(r), hOf(r)) >= FULL.split_min_m;
    // the first cut of a big block always happens (a re-planned block is never the old single set-piece); deeper ones by chance
    if (depth >= FULL.max_depth || !big || (depth > 0 && random() >= splitChance)) { leaves.push(r); return; }
    const longX = wOf(r) >= hOf(r);
    const axes: ('x' | 'y')[] = random() < ch * 0.5 ? [longX ? 'y' : 'x', longX ? 'x' : 'y'] : [longX ? 'x' : 'y', longX ? 'y' : 'x'];
    for (const axis of axes) {
      const ratio = FULL.split_ratio_tidy + (random() * 2 - 1) * FULL.split_ratio_spread * ch;
      const gap = FULL.gap_m + random() * FULL.gap_chaos_m * ch;
      const len = axis === 'x' ? wOf(r) : hOf(r);
      const at = (axis === 'x' ? r[0][0] : r[0][1]) + len * ratio;
      const a: Rect = axis === 'x' ? [r[0], [at - gap / 2, r[1][1]]] : [r[0], [r[1][0], at - gap / 2]];
      const b: Rect = axis === 'x' ? [[at + gap / 2, r[0][1]], r[1]] : [[r[0][0], at + gap / 2], r[1]];
      if (!fitsMin(a) || !fitsMin(b)) continue;
      if (!touchingSides(a, block, sides).length || !touchingSides(b, block, sides).length) continue;
      split(a, depth + 1); split(b, depth + 1);
      return;
    }
    leaves.push(r);
  };
  split(block, 0);
  return leaves.map((r) => {
    const touching = touchingSides(r, block, sides);
    const ordered = touching.length ? touching : sides;
    // a corner parcel may face its other street (the template turns 90 degrees)
    const side = ordered.length > 1 && random() < FULL.turn_chance * ch ? ordered[1 + Math.floor(random() * (ordered.length - 1))] : ordered[0];
    return { rect: r, side, inner: innerRect(r, side, ch, random) };
  });
}

/** The template's rectangle inside a parcel: a setback from the street side, side gaps and a short back, all by the chaos. */
function innerRect(r: Rect, side: Side, ch: number, random: () => number): Rect {
  const minLong = Math.max(...FULL.min_parcel_m), minShort = Math.min(...FULL.min_parcel_m);
  const len = horizontal(side) ? wOf(r) : hOf(r), dep = horizontal(side) ? hOf(r) : wOf(r);
  const minLen = len >= dep ? minLong : minShort, minDep = len >= dep ? minShort : minLong;
  const slackLen = Math.max(0, len - minLen), slackDep = Math.max(0, dep - minDep);
  const lenCut = Math.min(slackLen, len * FULL.shrink_chaos * ch * random());
  const left = lenCut * random();
  const setback = Math.min(slackDep, FULL.setback_chaos_m * ch * random());
  const back = Math.min(slackDep - setback, dep * FULL.shrink_chaos * ch * random());
  // (along0, along1) on the street side, (front, rear) inwards from it
  const a0 = left, a1 = len - (lenCut - left), f = setback, b = dep - back;
  const [[x0, y0], [x1, y1]] = r;
  if (side === 'south') return [[x0 + a0, y1 - b], [x0 + a1, y1 - f]];
  if (side === 'north') return [[x0 + a0, y0 + f], [x0 + a1, y0 + b]];
  if (side === 'west') return [[x0 + f, y0 + a0], [x0 + b, y0 + a1]];
  return [[x1 - b, y0 + a0], [x1 - f, y0 + a1]];
}

function hashKey(s: string): number {
  let h = 0x811c9dc5;
  for (let i = 0; i < s.length; i++) { h ^= s.charCodeAt(i); h = Math.imul(h, 0x01000193) >>> 0; }
  return h >>> 0;
}

/** The union bounding box of rectangles (UE or metres). */
export const unionRect = (rs: readonly (readonly [Vec2, Vec2])[]): [Vec2, Vec2] => [[Math.min(...rs.map((r) => r[0][0])), Math.min(...rs.map((r) => r[0][1]))], [Math.max(...rs.map((r) => r[1][0])), Math.max(...rs.map((r) => r[1][1]))]];
