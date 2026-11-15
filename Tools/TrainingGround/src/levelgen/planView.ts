// Training Ground / level generator: the 2D plan's world <-> screen transform. It mirrors LayoutView.tsx (UE X forward = up on
// screen, Y right = right, a 10 px pad, uniform scale), so an overlay of the same size (the region-select overlay, task 070)
// draws and picks in the same place as the canvas under it. Pure TS.
import type { Vec2 } from '../movement/geometry.ts';

export interface PlanTransform { s: number; ox: number; oy: number; minY: number; maxX: number }

export function planTransform(bounds: { min: Vec2; max: Vec2 }, w: number, h: number, pad = 10): PlanTransform {
  const [minX, minY] = bounds.min, [maxX, maxY] = bounds.max;
  const s = Math.min((w - 2 * pad) / (maxY - minY), (h - 2 * pad) / (maxX - minX));
  return { s, ox: (w - (maxY - minY) * s) / 2, oy: (h - (maxX - minX) * s) / 2, minY, maxX };
}

export const toScreen = (t: PlanTransform, p: Vec2): [number, number] => [t.ox + (p[1] - t.minY) * t.s, t.oy + (t.maxX - p[0]) * t.s];
export const toWorld = (t: PlanTransform, q: readonly [number, number]): Vec2 => [t.maxX - (q[1] - t.oy) / t.s, (q[0] - t.ox) / t.s + t.minY];
