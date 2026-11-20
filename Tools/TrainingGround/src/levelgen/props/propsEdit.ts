// Training Ground «Пропсы уровней» (T-023): the manual edit of placed props - pick a prop on the plan, move it (drag / arrow-key nudge),
// snap it to the walk surface it stands on, pin it (locked = true) so a re-roll does not take it back, and check the result with the game's
// own static validator + the nav fast gate. Pure (no DOM, no fs): the panel and the tests use the same functions. The props file written by
// the existing «Сохранить» (PUT /api/levelgen/props) is the single source the game reads at OnWorldBeginPlay, so what is moved here is what Unreal spawns.

import type { Vec2 } from '../../movement/geometry.ts';
import type { GenLayout } from '../schema.ts';
import type { LevelPropsFile, PropLayer, PropObject } from './schema.ts';
import type { PropsLayoutView } from './layoutView.ts';
import { applyOverrides } from './placement.ts';
import { variantHalfSize, validateStatic, type PropsValidationIssue } from './validator.ts';
import { runFastGate, type FastGateResult } from './fastGate.ts';
import { PMC_CHARGES_ID } from './rerollKinds.ts';

const clone = <T>(v: T): T => JSON.parse(JSON.stringify(v)) as T;

/** The prop with its layer, or null. */
export function findProp(file: LevelPropsFile, id: string): { layer: PropLayer; obj: PropObject } | null {
  for (const layer of file.layers) {
    const obj = layer.objects.find((o) => o.id === id);
    if (obj) return { layer, obj };
  }
  return null;
}

/** Why a prop cannot be moved by hand (null = it can). */
export function moveRefusal(file: LevelPropsFile, id: string): string | null {
  if (file.frozen) return 'Файл заморожен (frozen): правки запрещены';
  const f = findProp(file, id);
  if (!f) return `Пропс ${id} не найден`;
  if (f.layer.id === PMC_CHARGES_ID) return 'Заряды ЧВК берутся из плана ЧВК раскладки: двигайте их там';
  if (f.layer.locked) return `Слой «${f.layer.id}» закреплён: снимите замок слоя, чтобы двигать его пропсы`;
  return null;
}

/** The plan radius of a prop for picking / the drag ghost (cm). */
export function propRadius(o: PropObject, balance?: Record<string, unknown>): number {
  return Math.max(25, variantHalfSize(o.variant, balance));
}

export interface PickOptions {
  /** Layer visibility from the panel (false = hidden, absent = shown). */
  visibleLayers?: Record<string, boolean>;
  /** The walk-layer filter of the plan ('all' or one walk layer). */
  layerFilter?: string;
  balance?: Record<string, unknown>;
}

/** Whether the plan draws `o` (the same rules as PropsCanvas: layer visible, walk-layer filter). */
export function propShown(layer: PropLayer, o: PropObject, opts: PickOptions = {}): boolean {
  if (opts.visibleLayers?.[layer.id] === false) return false;
  const f = opts.layerFilter ?? 'all';
  return f === 'all' || !o.walk_layer || o.walk_layer === f;
}

/** The nearest shown prop to the plan point `p` (within its own radius + `tolCm`), or null. Ties go to the smaller prop. */
export function pickProp(file: LevelPropsFile, p: Vec2, tolCm: number, opts: PickOptions = {}): { layerId: string; obj: PropObject } | null {
  let best: { layerId: string; obj: PropObject; score: number } | null = null;
  for (const layer of file.layers) {
    for (const obj of layer.objects) {
      if (!propShown(layer, obj, opts)) continue;
      const d = Math.hypot(obj.pos[0] - p[0], obj.pos[1] - p[1]);
      const r = propRadius(obj, opts.balance);
      if (d > r + tolCm) continue;
      const score = d - r * 0.5;
      if (!best || score < best.score) best = { layerId: layer.id, obj, score };
    }
  }
  return best ? { layerId: best.layerId, obj: best.obj } : null;
}

/**
 * The walk layer a prop dropped at `pos` stands on: the layer it came from when the spot is walkable there and not blocked by the layout,
 * else the first other layer (ground, then floors / roofs in the view's order) where the spot is walkable and free, else a layer where it
 * is at least walkable (the validator will flag the block), else null (nothing to stand on: the move is refused).
 */
export function resolveWalkLayer(view: PropsLayoutView, pos: Vec2, preferred: string, halfCm = 0): string | null {
  const order = [preferred, ...view.layers.map((l) => l.name).filter((n) => n !== preferred)];
  for (const name of order) if (view.isWalkable(pos, name) && !view.isBlocked(pos, name, halfCm)) return name;
  for (const name of order) if (view.isWalkable(pos, name)) return name;
  return null;
}

export interface PropMoveOptions {
  /** Pin the moved prop (locked = true). Default true: the lock is the designer's "this one stays where I put it". */
  lock?: boolean;
}

export type PropMoveResult =
  | { ok: true; file: LevelPropsFile; id: string; pos: Vec2; walkLayer: string; changedLayer: boolean; carried: string[]; locked: boolean }
  | { ok: false; reason: string };

/** Straight cable lengths of a generator to its turrets (power.ts): refreshed after the generator or a turret it feeds moved. */
function refreshCables(file: LevelPropsFile, movedIds: ReadonlySet<string>): void {
  const byId = new Map<string, PropObject>();
  for (const l of file.layers) for (const o of l.objects) byId.set(o.id, o);
  for (const g of byId.values()) {
    if (!g.links || g.links.length === 0) continue;
    if (!movedIds.has(g.id) && !g.links.some((id) => movedIds.has(id))) continue;
    g.cable_cm = g.links.map((id) => { const t = byId.get(id); return t ? Math.round(Math.hypot(g.pos[0] - t.pos[0], g.pos[1] - t.pos[1])) : 0; });
  }
}

/**
 * Moves prop `id` by `delta` (UE cm; ArrowUp = +X). Returns a NEW file (the input is never changed). The prop lands on the walk surface under
 * it (resolveWalkLayer; refused when there is none), everything attached to it (a mine on its crate, loot in it) goes along, a hand `move`
 * override of the prop follows, the cables of a generator network are re-measured, and the prop is pinned (`locked: true`) unless asked
 * not to. Whether the new spot breaks a rule is for checkProps (the placement rules are not enforced on a hand move, only reported).
 */
export function moveProp(file: LevelPropsFile, view: PropsLayoutView, id: string, delta: Vec2, balance?: Record<string, unknown>, opts: PropMoveOptions = {}): PropMoveResult {
  const refusal = moveRefusal(file, id);
  if (refusal) return { ok: false, reason: refusal };
  const next = clone(file);
  const f = findProp(next, id)!;
  const to: Vec2 = [Math.round(f.obj.pos[0] + delta[0]), Math.round(f.obj.pos[1] + delta[1])];
  if (to[0] === f.obj.pos[0] && to[1] === f.obj.pos[1]) return { ok: false, reason: 'Пропс остался на месте' };
  const from = f.obj.walk_layer || 'ground';
  const walk = resolveWalkLayer(view, to, from, variantHalfSize(f.obj.variant, balance));
  if (!walk) return { ok: false, reason: `Здесь не на чем стоять: точка [${to[0]}, ${to[1]}] вне карты, в канале или вне этажа` };
  const moved = new Set<string>([id]);
  const apply = (o: PropObject) => {
    o.pos = [to[0], to[1]];
    o.walk_layer = walk;
  };
  apply(f.obj);
  // what is attached to it (mine on a crate, loot in a container) stands on the same spot
  const carried: string[] = [];
  for (const l of next.layers) for (const o of l.objects) if (o.attach === id && !moved.has(o.id)) { apply(o); moved.add(o.id); carried.push(o.id); }
  // a hand override of the moved objects would put them back: it follows
  for (const op of next.overrides ?? []) {
    if (op.op === 'move' && moved.has(op.id)) { op.pos = [to[0], to[1]]; op.walk_layer = walk; }
  }
  refreshCables(next, moved);
  const lock = opts.lock !== false;
  if (lock) f.obj.locked = true;
  return { ok: true, file: next, id, pos: to, walkLayer: walk, changedLayer: walk !== from, carried, locked: f.obj.locked === true };
}

/** Sets / clears the lock of one prop (new file). */
export function setPropLocked(file: LevelPropsFile, id: string, locked: boolean): LevelPropsFile {
  const next = clone(file);
  const f = findProp(next, id);
  if (f) f.obj.locked = locked;
  return next;
}

// ---- validation after an edit -----------------------------------------------------------------------------------------------------------

export interface PropsCheck {
  issues: PropsValidationIssue[];
  gate: FastGateResult;
}

/** The game's static validator (what FPropsValidator would drop at load) + the nav fast gate (route / doorways / ladders) of the file as it is now. */
export function checkProps(file: LevelPropsFile, view: PropsLayoutView, layout: GenLayout, balance: Record<string, unknown>): PropsCheck {
  const resolved = applyOverrides(file);
  const issues = validateStatic(view, resolved.objects, resolved.keepOuts, balance).issues;
  return { issues, gate: runFastGate(layout, file, balance) };
}

/** The issues a move added: the ones on `ids` that the file did not have before (a defect the generator left is not blamed on the move). */
export function newIssues(before: readonly PropsValidationIssue[], after: readonly PropsValidationIssue[]): PropsValidationIssue[] {
  const had = new Set(before.map((i) => `${i.objectId}|${i.reason}`));
  return after.filter((i) => !had.has(`${i.objectId}|${i.reason}`));
}

/** Writes the fast-gate verdict into the reports of `layerIds` (the layer cards show it); returns the same file when nothing changes. */
export function withGate(file: LevelPropsFile, gate: FastGateResult, layerIds: readonly string[]): LevelPropsFile {
  let changed = false;
  const layers = file.layers.map((l) => {
    if (!layerIds.includes(l.id)) return l;
    const r = l.report ?? {};
    const same = r.gate === gate.gate && (r.gate_leg ?? '') === (gate.gate === 'fail' ? gate.gate_leg ?? '' : '') && JSON.stringify(r.gate_blockers ?? []) === JSON.stringify(gate.gate === 'fail' ? gate.gate_blockers ?? [] : []);
    if (same) return l;
    changed = true;
    const report = { ...r, gate: gate.gate };
    delete report.gate_leg;
    delete report.gate_blockers;
    if (gate.gate === 'fail') {
      if (gate.gate_leg) report.gate_leg = gate.gate_leg;
      if (gate.gate_blockers) report.gate_blockers = [...gate.gate_blockers];
    }
    return { ...l, report };
  });
  return changed ? { ...file, layers } : file;
}

/** The grid-snap steps of a prop drag (cm): 50 = the placement lattice (placement.ts LATTICE_CM). */
export const PROP_SNAP_CM: readonly number[] = [10, 25, 50, 100];
export const DEFAULT_PROP_SNAP_CM = 50;
