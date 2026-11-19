// Training Ground / level generator (task T-020): the re-roll of props layers for EVERY kind the autogen supports - crates, barrels,
// turrets, mines, loot and the power layer - with the same follow-up the autogen does (autogen.ts): the mandatory-route clearance
// (task 088, routeClearance.ts), the static validator's drops and the power network rebuild. Pure (no fs): runs in the browser.
// What cannot be re-rolled alone (the locked PMC charges, a kind the placement does not know) is reported with a reason, not thrown.

import type { GenLayout } from '../schema.ts';
import type { LevelPropsFile, PropLayer } from './schema.ts';
import type { PropsLayoutView } from './layoutView.ts';
import type { PropsAutogenConfig } from './autogenConfig.ts';
import { DEFAULT_AUTOGEN_CONFIG } from './autogenConfig.ts';
import { applyOverrides, rerollLayer } from './placement.ts';
import { runFastGate } from './fastGate.ts';
import { validateStatic } from './validator.ts';
import { buildPowerLayer, POWER_LAYER_ID, readPowerConfig, validatePowerLinks } from './power.ts';
import { applyRouteClearance, type RouteClearanceStats } from './routeClearance.ts';

/** The placement kinds, in the order the autogen re-rolls them (rerollAll). */
export const PLACED_KINDS = ['crates', 'barrels', 'turrets', 'mines', 'loot'] as const;
export const PMC_CHARGES_ID = 'pmc_charges';

export type RerollVerdict = { ok: true } | { ok: false; reason: string };

/** Whether a layer can be re-rolled on its own (the UI disables the button and shows `reason`). */
export function rerollSupport(layer: Pick<PropLayer, 'id' | 'kind' | 'locked'>, fileFrozen = false): RerollVerdict {
  if (fileFrozen) return { ok: false, reason: 'Файл заморожен (frozen): переброска запрещена' };
  if (layer.id === PMC_CHARGES_ID) return { ok: false, reason: 'Заряды ЧВК не перебрасываются: они берутся из плана ЧВК раскладки (слой закреплён)' };
  if (layer.locked) return { ok: false, reason: 'Слой закреплён: снимите замок, чтобы перебросить' };
  const kind = layer.kind as string;
  if (kind === POWER_LAYER_ID) return { ok: true };
  if ((PLACED_KINDS as readonly string[]).includes(kind)) return { ok: true };
  return { ok: false, reason: `Тип «${kind}» генератор пропсов пока не умеет перебрасывать отдельно` };
}

export interface RerollRequest {
  /** Layer ids to re-roll; each must pass rerollSupport (the others come back in `skipped`). */
  ids: readonly string[];
  /** The seed of a layer for this re-roll (the UI: the shown seed, or seed + 1 for «Сгенерировать всё»). */
  seedFor: (layer: PropLayer) => number;
}

export interface RerollOutcome {
  rerolled: string[];
  skipped: { id: string; reason: string }[];
  clearance: RouteClearanceStats;
  /** Objects the static validator would drop at game load (left out, like the autogen). */
  droppedByValidator: number;
  /** The power layer was rebuilt (its own re-roll, or its turrets / loot changed). */
  powerRebuilt: boolean;
  /** Warnings worth showing (a locked power layer with stale links, attach orphans, power link issues). */
  notes: string[];
}

const emptyStats = (): RouteClearanceStats => ({ droppedCrates: 0, droppedBarrels: 0, droppedMines: 0, droppedLoot: 0, droppedTotal: 0, pmcChargesInside: [] });

/**
 * Re-rolls the requested layers of `file` IN PLACE (the caller passes a clone): placement per layer in the autogen's kind order, then the
 * route clearance and the validator drops on the re-rolled layers only (hand-locked objects stay), then the power rebuild when needed.
 * Locked layers, `pmc_charges` and every layer not asked for are never touched.
 */
export function rerollPropsLayers(
  file: LevelPropsFile,
  balance: Record<string, unknown>,
  view: PropsLayoutView,
  layout: GenLayout,
  req: RerollRequest,
  cfg: PropsAutogenConfig = DEFAULT_AUTOGEN_CONFIG,
): RerollOutcome {
  const out: RerollOutcome = { rerolled: [], skipped: [], clearance: emptyStats(), droppedByValidator: 0, powerRebuilt: false, notes: [] };
  const wanted: PropLayer[] = [];
  for (const id of req.ids) {
    const layer = file.layers.find((l) => l.id === id);
    if (!layer) { out.skipped.push({ id, reason: 'слой не найден' }); continue; }
    const v = rerollSupport(layer, file.frozen);
    if (!v.ok) { out.skipped.push({ id, reason: v.reason }); continue; }
    wanted.push(layer);
  }

  // 1. placement of the placed kinds, in the autogen's order
  const placed = new Set<string>();
  for (const kind of PLACED_KINDS) {
    for (const layer of wanted) {
      if ((layer.kind as string) !== kind) continue;
      rerollLayer(file, balance, view, layer.id, req.seedFor(layer), layout);
      placed.add(layer.id);
      out.rerolled.push(layer.id);
    }
  }

  // 2. the 088 route clearance + the static validator, on the re-rolled layers only (the designer's pinned objects stay)
  if (placed.size > 0) {
    out.clearance = applyRouteClearance(file, layout, cfg, { only: placed, keepLockedObjects: true });
    const resolved = applyOverrides(file);
    const drop = validateStatic(view, resolved.objects, resolved.keepOuts, balance).dropped;
    const mine = new Set<string>();
    for (const l of file.layers) if (placed.has(l.id)) for (const o of l.objects) mine.add(o.id);
    const ids = new Set(drop.filter((r) => mine.has(r.object.id) && !r.object.locked).map((r) => r.object.id));
    if (ids.size > 0) {
      for (const l of file.layers) if (placed.has(l.id)) l.objects = l.objects.filter((o) => !ids.has(o.id));
      out.droppedByValidator = ids.size;
    }
  }

  // 3. the power network: its own re-roll, or the turrets / loot it links were re-rolled
  const powerLayer = file.layers.find((l) => (l.kind as string) === POWER_LAYER_ID);
  const powerAsked = wanted.some((l) => (l.kind as string) === POWER_LAYER_ID);
  const upstream = wanted.some((l) => l.kind === 'turrets' || l.kind === 'loot');
  if (powerLayer && (powerAsked || upstream)) {
    if (powerLayer.locked) {
      out.notes.push('Слой power закреплён: связи генераторов с турелями могли устареть');
    } else {
      const fuelTable = readPowerConfig(balance).fuelTable;
      for (const l of file.layers) {
        if (l.kind === 'loot' && !l.locked) for (const o of l.objects) if (!o.locked && o.loot_table === fuelTable) delete o.loot_table;
      }
      const seed = powerAsked ? req.seedFor(powerLayer) : powerLayer.seed;
      buildPowerLayer(file, view, balance, seed);
      out.powerRebuilt = true;
      if (powerAsked) out.rerolled.push(POWER_LAYER_ID);
      const kindOfLayer = new Map(file.layers.map((l) => [l.id, l.kind as string]));
      const issues = validatePowerLinks(applyOverrides(file).objects, kindOfLayer, balance, view.spawnPos);
      if (issues.length > 0) out.notes.push(`Сеть питания: ${issues.length} замечаний (${issues.slice(0, 2).map((i) => i.reason).join('; ')})`);
    }
  }

  // 4. the fast gate again on what changed (the route must stay walkable): the layers' reports show it
  if (out.rerolled.length > 0) {
    const g = runFastGate(layout, file, balance);
    for (const l of file.layers) {
      if (!out.rerolled.includes(l.id)) continue;
      const report = { ...(l.report ?? {}), gate: g.gate };
      delete report.gate_leg;
      delete report.gate_blockers;
      if (g.gate === 'fail') {
        report.gate_leg = g.gate_leg;
        if (g.gate_blockers) report.gate_blockers = [...g.gate_blockers];
      }
      l.report = report;
    }
  }
  return out;
}
