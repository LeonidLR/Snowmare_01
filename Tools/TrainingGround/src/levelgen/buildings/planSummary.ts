// Training Ground / level generator v3 - the PLAN SUMMARY of a map (S2_ROOMS_PLAN.md step S2.7): how many buildings are planned (band plan), fell back to the
// BSP, or never went through the planner (legacy / BSP-only archetypes); per archetype counts; the validator's issue counts; stories per building. The page's
// «Здания» panel and the CLI (`npm run levelgen -- --plans`) print the same numbers from here (planSummaryLine). Also the plan signature that tells two plans of
// one building apart ("другой план" must change it). Pure, deterministic, no DOM.
import type { BuildingTraversal, TraversalIssue } from '../buildingTraversal.ts';
import { validateBuildings } from '../buildingTraversal.ts';
import type { GenBuilding, GenLayout } from '../schema.ts';
import { planSeedFor } from '../generate.ts';

/** How a building's interior was made: 'bands' = the band plan, 'bsp' = the plan fell back (`plan.fallback`), 'legacy' = the planner never ran (toggle off, other archetypes). */
export type PlanKind = 'bands' | 'bsp' | 'legacy';

export const planKindOf = (b: GenBuilding): PlanKind => (b.plan?.kind === 'bands' ? 'bands' : b.plan?.kind === 'bsp' ? 'bsp' : 'legacy');

export interface PlanArchetypeRow { archetype: string; buildings: number; planned: number; fallback: number; legacy: number; rooms: number }

export interface PlanMapSummary {
  /** Enterable buildings (the ones with interiors). */
  enterable: number;
  planned: number;
  fallback: number;
  /** Enterable buildings the planner did not touch (the toggle was off, or an archetype it does not plan yet). */
  legacy: number;
  /** Per archetype, by name. */
  archetypes: PlanArchetypeRow[];
  /** Fallback reasons of the BSP buildings (plan.fallback), counted. */
  fallbacks: Record<string, number>;
  /** Re-planned buildings ("другой план"): the plan seed carries a counter (replanCounter). */
  replanned: number;
  /** Rooms (all storeys) of the planned buildings and the dead-end flats among them. */
  rooms: number;
  dead_end_units: number;
  /** Issue counts of the planned buildings by code and severity (validateBuildings; absent report = computed here). */
  issues: { error: number; warning: number; by_code: Record<string, number> };
  /** Stories per planned building: the applied story ids (b.stories), counted over the planned buildings. */
  stories: Record<string, number>;
  /** Planned buildings with a story / the map's buildings with a story at all. */
  storied_planned: number;
  /** Planned buildings whose validator report has an error. */
  failing: number;
}

/** A stable string of the plan of a building: its rooms (id, storey, bounds, kind, unit) and the openings on partitions and walls (id, position, role). Two plans differ iff this does. */
export function planSignature(b: GenBuilding): string {
  const rooms = [...(b.rooms ?? [])].sort((p, q) => (p.id < q.id ? -1 : 1)).map((r) => `${r.id}:${r.floor}:${r.bounds.min.join(',')}:${r.bounds.max.join(',')}:${r.kind ?? ''}:${r.unit ?? ''}`);
  const ops = [...(b.openings ?? []), ...(b.partitions ?? []).flatMap((p) => p.openings ?? [])].map((o) => `${o.id ?? ''}:${o.floor}:${o.kind}:${o.pos.join(',')}:${o.role ?? ''}`).sort();
  return `${rooms.join(';')}|${ops.join(';')}`;
}

/** The issues of the planned buildings of a report (a planned building = plan.kind 'bands'), by id. */
function plannedIssues(layout: GenLayout, report: readonly BuildingTraversal[]): Map<string, readonly TraversalIssue[]> {
  const planned = new Set(layout.buildings.filter((b) => b.plan?.kind === 'bands').map((b) => b.id));
  return new Map(report.filter((r) => planned.has(r.id)).map((r) => [r.id, r.issues] as const));
}

/** The map summary; `report` = the validator's per-building results (the page already holds them), else computed here. */
export function planMapSummary(layout: GenLayout, report?: readonly BuildingTraversal[]): PlanMapSummary {
  const rep = report ?? validateBuildings(layout).buildings;
  const enterable = layout.buildings.filter((b) => b.enterable && !b.hollow);
  const byArch = new Map<string, PlanArchetypeRow>();
  const fallbacks: Record<string, number> = {};
  const stories: Record<string, number> = {};
  let planned = 0, fallback = 0, legacy = 0, replanned = 0, rooms = 0, dead = 0, storiedPlanned = 0;
  for (const b of enterable) {
    const kind = planKindOf(b), arch = b.archetype ?? 'none';
    const row = byArch.get(arch) ?? { archetype: arch, buildings: 0, planned: 0, fallback: 0, legacy: 0, rooms: 0 };
    byArch.set(arch, row);
    row.buildings++;
    if (kind === 'bands') {
      planned++; row.planned++;
      row.rooms += (b.rooms ?? []).length; rooms += (b.rooms ?? []).length;
      dead += (b.units ?? []).filter((u) => u.dead_end).length;
      if (replanCounter(b) > 0) replanned++;
      const s = b.stories?.at(-1)?.id;
      if (s) { stories[s] = (stories[s] ?? 0) + 1; if (s !== 'open') storiedPlanned++; }
    } else if (kind === 'bsp') {
      fallback++; row.fallback++;
      const why = b.plan?.fallback ?? 'plan_fallback';
      fallbacks[why] = (fallbacks[why] ?? 0) + 1;
    } else { legacy++; row.legacy++; }
  }
  const byCode: Record<string, number> = {};
  let error = 0, warning = 0, failing = 0;
  for (const issues of plannedIssues(layout, rep).values()) {
    let bad = false;
    for (const i of issues) {
      byCode[i.code] = (byCode[i.code] ?? 0) + 1;
      if (i.severity === 'error') { error++; bad = true; } else warning++;
    }
    if (bad) failing++;
  }
  return {
    enterable: enterable.length, planned, fallback, legacy,
    archetypes: [...byArch.values()].sort((a, b) => (a.archetype < b.archetype ? -1 : 1)),
    fallbacks, replanned, rooms, dead_end_units: dead,
    issues: { error, warning, by_code: byCode }, stories, storied_planned: storiedPlanned, failing,
  };
}

/** The most re-plans of one building the counter inference looks for (a tool cap, far above what a designer clicks). */
const COUNTER_MAX = 200;

/**
 * How many times the building was re-planned: the plan seed is fnv1a(id | L x D | storeys | archetype [| r<n>]), so n is found by trying the counters (no schema field:
 * the counter is the seed). 0 = the generated plan, or a seed this version cannot explain (the footprint was edited by hand): the next re-plan then starts from 1, and
 * a plan equal to the current one is skipped anyway (replanBuilding (replan.ts)), so it still ends on a different plan.
 */
export function replanCounter(b: GenBuilding): number {
  if (!b.plan || (b.archetype !== 'office' && b.archetype !== 'block')) return 0;
  const [[x0, y0], [x1, y1]] = b.footprint;
  const snap = (cm: number): number => Math.floor((cm + 0.5) / 100) * 100;
  const w = snap(Math.abs(x1 - x0)), h = snap(Math.abs(y1 - y0)), L = Math.max(w, h), D = Math.min(w, h);
  for (let n = 0; n <= COUNTER_MAX; n++) if (planSeedFor(b.id, L, D, b.floors, b.archetype, n) === b.plan.seed) return n;
  return 0;
}

/** Does the layout carry any plan (a building with `plan`)? A plans-off map has none. */
export const hasPlans = (l: GenLayout): boolean => l.buildings.some((b) => !!b.plan);

const countText = (m: Record<string, number>): string => Object.entries(m).sort(([a], [b]) => (a < b ? -1 : 1)).map(([k, n]) => `${k} ${n}`).join(', ');

/** The one-line text of the summary (the CLI report line; the page shows the same numbers in its «Планировки» block). */
export function planSummaryLine(s: PlanMapSummary): string {
  const arch = s.archetypes.map((a) => `${a.archetype} ${a.planned}/${a.buildings}`).join(', ');
  const fb = countText(s.fallbacks);
  const issues = countText(s.issues.by_code);
  return `plans: ${s.enterable} enterable, ${s.planned} planned, ${s.fallback} BSP fallback${fb ? ` (${fb})` : ''}, ${s.legacy} legacy; by archetype: ${arch || '-'}; rooms ${s.rooms}, dead-end flats ${s.dead_end_units}, re-planned ${s.replanned}; `
    + `issues: ${s.issues.error} errors / ${s.issues.warning} warnings${issues ? ` (${issues})` : ''}, failing ${s.failing}; stories on planned: ${countText(s.stories) || '-'}`;
}
