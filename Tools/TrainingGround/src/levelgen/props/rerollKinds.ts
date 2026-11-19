// Training Ground / level generator (task T-020): the re-roll of props layers for EVERY kind the autogen supports - crates, barrels,
// turrets, mines, loot and the power layer - with the same follow-up the autogen does (autogen.ts): the mandatory-route clearance
// (task 088, routeClearance.ts), the static validator's drops and the power network rebuild. Pure (no fs): runs in the browser.
// What cannot be re-rolled alone (the locked PMC charges, a kind the placement does not know) is reported with a reason, not thrown.

import type { GenLayout } from '../schema.ts';
import type { LevelPropsFile, PropLayer, PropObject } from './schema.ts';
import type { PropsLayoutView } from './layoutView.ts';
import type { PropsAutogenConfig } from './autogenConfig.ts';
import { DEFAULT_AUTOGEN_CONFIG } from './autogenConfig.ts';
import { applyOverrides, rerollLayer } from './placement.ts';
import { runFastGate } from './fastGate.ts';
import { validateStatic } from './validator.ts';
import { buildPowerLayer, POWER_LAYER_ID, readPowerConfig, validatePowerLinks } from './power.ts';
import { applyRouteClearance, type RouteClearanceStats } from './routeClearance.ts';
import type { Vec2 } from '../../movement/geometry.ts';
import { rerollLayerInZone } from './zoneReroll.ts';

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
  /**
   * T-023: re-roll only INSIDE this polygon (UE cm; a box / polygon / lasso of the region tools). Objects outside it, locked / hand /
   * pinned objects, locked layers and `pmc_charges` stay exactly as they are; the power network is not rebuilt (it is shared by the
   * whole map) - the power layer is skipped and its links are only checked. Absent = the whole layer (the normal re-roll).
   */
  zone?: readonly Vec2[];
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
  /** A zone re-roll's totals over the re-rolled layers (absent for a whole-layer re-roll). */
  zone?: { removed: number; added: number; droppedSpacing: number; keptOutside: number; keptPinned: number };
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
    if (req.zone && (layer.kind as string) === POWER_LAYER_ID) { out.skipped.push({ id, reason: 'сеть питания общая для всей карты: по зоне не перебрасывается (перебросьте слой power целиком)' }); continue; }
    wanted.push(layer);
  }
  const zone = req.zone;
  if (zone) out.zone = { removed: 0, added: 0, droppedSpacing: 0, keptOutside: 0, keptPinned: 0 };
  // the ids of the objects this call creates (a zone re-roll's clearance / validator only judge those)
  const freshIds = new Set<string>();

  // 1. placement of the placed kinds, in the autogen's order
  const placed = new Set<string>();
  for (const kind of PLACED_KINDS) {
    for (const layer of wanted) {
      if ((layer.kind as string) !== kind) continue;
      if (zone) {
        const z = rerollLayerInZone(file, balance, view, layer.id, req.seedFor(layer), zone);
        for (const id of z.added) freshIds.add(id);
        out.zone!.removed += z.removed;
        out.zone!.added += z.added.length;
        out.zone!.droppedSpacing += z.droppedSpacing;
        out.zone!.keptOutside += z.keptOutside;
        out.zone!.keptPinned += z.keptPinned;
        if (z.orphaned.length > 0) {
          out.notes.push(`${layer.id}: объекты ${z.orphaned.slice(0, 3).join(', ')}${z.orphaned.length > 3 ? ` и ещё ${z.orphaned.length - 3}` : ''} были привязаны к убранным в зоне; перебросьте и их слой (игра их уберёт при загрузке)`);
          const orphanSet = new Set(z.orphaned);
          for (const l of file.layers) {
            if (l.id === layer.id || !l.objects.some((o) => orphanSet.has(o.id))) continue;
            l.report = { ...(l.report ?? {}), warnings: [...(l.report?.warnings ?? []), ...z.orphaned.filter((id) => l.objects.some((o) => o.id === id)).map((id) => `attach_orphaned ${id}`)] };
          }
        }
      } else {
        rerollLayer(file, balance, view, layer.id, req.seedFor(layer), layout);
      }
      placed.add(layer.id);
      out.rerolled.push(layer.id);
    }
  }

  // 2. the 088 route clearance + the static validator, on the re-rolled layers only (the designer's pinned objects stay)
  if (placed.size > 0) {
    // a zone re-roll: everything it did not create is pinned for the clearance (temporarily), so only the fresh objects are judged
    const pins: { o: PropObject; had: boolean; was: unknown }[] = [];
    if (zone) {
      for (const l of file.layers) {
        if (!placed.has(l.id)) continue;
        for (const o of l.objects) if (!freshIds.has(o.id) && o.locked !== true) { pins.push({ o, had: Object.prototype.hasOwnProperty.call(o, 'locked'), was: o.locked }); o.locked = true; }
      }
    }
    try {
      out.clearance = applyRouteClearance(file, layout, cfg, { only: placed, keepLockedObjects: true });
    } finally {
      for (const p of pins) { if (p.had) p.o.locked = p.was as boolean; else delete p.o.locked; }
    }
    const resolved = applyOverrides(file);
    const drop = validateStatic(view, resolved.objects, resolved.keepOuts, balance).dropped;
    const mine = new Set<string>();
    for (const l of file.layers) if (placed.has(l.id)) for (const o of l.objects) if (!zone || freshIds.has(o.id)) mine.add(o.id);
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
  if (zone && powerLayer && upstream) {
    // the generators' links to the turrets / loot of the zone could now be stale: only reported, the network is not rebuilt (outside stays)
    const kindOfLayer = new Map(file.layers.map((l) => [l.id, l.kind as string]));
    const issues = validatePowerLinks(applyOverrides(file).objects, kindOfLayer, balance, view.spawnPos);
    if (issues.length > 0) out.notes.push(`Сеть питания: ${issues.length} замечаний после зоны (${issues.slice(0, 2).map((i) => i.reason).join('; ')}); перебросьте слой power целиком`);
  } else if (powerLayer && (powerAsked || upstream)) {
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
  // 5. an "attach_orphaned" warning (placement.ts rerollLayer) that a later re-roll of the attached layer already settled is stale: drop it
  const byId = new Map<string, PropObject>();
  for (const l of file.layers) for (const o of l.objects) byId.set(o.id, o);
  for (const l of file.layers) {
    const w = l.report?.warnings;
    if (!w || !l.report) continue;
    l.report = {
      ...l.report,
      warnings: w.filter((x) => {
        const m = /^attach_orphaned (.+)$/.exec(x);
        if (!m) return true;
        const o = byId.get(m[1]);
        return o !== undefined && o.attach !== undefined && !byId.has(o.attach);
      }),
    };
  }
  return out;
}
