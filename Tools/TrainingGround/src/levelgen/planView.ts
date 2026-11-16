// Training Ground / level generator: the 2D plan's world <-> screen transform. It mirrors LayoutView.tsx (UE X forward = up on
// screen, Y right = right, a 10 px pad, uniform scale), so an overlay of the same size (the region-select overlay, task 070)
// draws and picks in the same place as the canvas under it. Pure TS.
//
// Zoom + pan (levelgen v2 UX round, task 072): a PlanView { zoom, panX, panY } is applied ON TOP of the fitted transform
// (screen = zoom * fitted + pan), so zoom 1 / pan 0 is the "fit" view. zoomAt keeps the world point under the cursor fixed; every
// tool (zone selection, start / goal dragging, playback) converts through viewTransform, so it works at any zoom. Limits:
// levelgen_v2.json plan_view.
import cfg from '../../../../Content/Data/LevelGen/levelgen_v2.json' with { type: 'json' };
import type { Vec2 } from '../movement/geometry.ts';

export interface PlanTransform { s: number; ox: number; oy: number; minY: number; maxX: number }

export function planTransform(bounds: { min: Vec2; max: Vec2 }, w: number, h: number, pad = 10): PlanTransform {
  const [minX, minY] = bounds.min, [maxX, maxY] = bounds.max;
  const s = Math.min((w - 2 * pad) / (maxY - minY), (h - 2 * pad) / (maxX - minX));
  return { s, ox: (w - (maxY - minY) * s) / 2, oy: (h - (maxX - minX) * s) / 2, minY, maxX };
}

export const toScreen = (t: PlanTransform, p: Vec2): [number, number] => [t.ox + (p[1] - t.minY) * t.s, t.oy + (t.maxX - p[0]) * t.s];
export const toWorld = (t: PlanTransform, q: readonly [number, number]): Vec2 => [t.maxX - (q[1] - t.oy) / t.s, (q[0] - t.ox) / t.s + t.minY];

// ---- zoom + pan -----------------------------------------------------------------------------------------------------------------

/** The designer's view on the fitted plan: screen = zoom * fitted + (panX, panY), px. */
export interface PlanView { zoom: number; panX: number; panY: number }
export const FIT_VIEW: PlanView = { zoom: 1, panX: 0, panY: 0 };
export const PLAN_VIEW = cfg.plan_view;

export const clampZoom = (z: number): number => Math.max(PLAN_VIEW.zoom_min, Math.min(PLAN_VIEW.zoom_max, z));

/** The fitted transform with the view applied (zoom 1, pan 0 = the fitted transform itself). */
export function viewTransform(base: PlanTransform, v: PlanView): PlanTransform {
  return { s: base.s * v.zoom, ox: base.ox * v.zoom + v.panX, oy: base.oy * v.zoom + v.panY, minY: base.minY, maxX: base.maxX };
}

/** Zoom by `factor` around the screen point `at` (the world point under it stays under it). The zoom is clamped. */
export function zoomAt(v: PlanView, at: readonly [number, number], factor: number): PlanView {
  const zoom = clampZoom(v.zoom * factor);
  const k = zoom / v.zoom;
  return { zoom, panX: at[0] - (at[0] - v.panX) * k, panY: at[1] - (at[1] - v.panY) * k };
}

/** The zoom factor of one mouse-wheel event (deltaY < 0 = zoom in; a trackpad's small deltas zoom less). */
export function wheelFactor(deltaY: number): number {
  if (!deltaY) return 1;
  const notches = Math.min(3, Math.max(0.1, Math.abs(deltaY) / 100));
  return Math.pow(PLAN_VIEW.wheel_step, deltaY < 0 ? notches : -notches);
}

export const panBy = (v: PlanView, dx: number, dy: number): PlanView => ({ ...v, panX: v.panX + dx, panY: v.panY + dy });

/** The view zoomed to show the world rectangle [a, b] (UE cm) centred in a w x h box (with the fitted transform `base`). */
export function viewOfRect(base: PlanTransform, a: Vec2, b: Vec2, w: number, h: number, pad = 20): PlanView {
  const p0 = toScreen(base, [Math.max(a[0], b[0]), Math.min(a[1], b[1])]), p1 = toScreen(base, [Math.min(a[0], b[0]), Math.max(a[1], b[1])]);
  const zoom = clampZoom(Math.min((w - 2 * pad) / Math.max(1, p1[0] - p0[0]), (h - 2 * pad) / Math.max(1, p1[1] - p0[1])));
  const cx = (p0[0] + p1[0]) / 2, cy = (p0[1] + p1[1]) / 2;
  return { zoom, panX: w / 2 - cx * zoom, panY: h / 2 - cy * zoom };
}
