// Training Ground / movement: a series of simulator runs (every route x member count x seed) -> metrics + summary.
// Pure (no file system): the CLI (cli/bot.ts), the tuner (train.ts) and the UI call it.

import type { FormationConfig } from './config.ts';
import type { RunMetrics, SeriesSummary } from './checks.ts';
import { RunCollector, summarize } from './checks.ts';
import type { Layout } from './layout.ts';
import type { NavGrid } from './nav.ts';
import { buildNav, simulate } from './sim.ts';

export interface SeriesOptions {
  layout: Layout;
  config: FormationConfig;
  members: number[];
  seeds: number;
  seedBase: number;
  /** Only these route ids (default: all). */
  routes?: string[];
  nav?: NavGrid;
}

export function runSeries(o: SeriesOptions): { runs: RunMetrics[]; summary: SeriesSummary } {
  const nav = o.nav ?? buildNav(o.layout);
  const runs: RunMetrics[] = [];
  const routes = o.layout.routes.filter((r) => !o.routes || o.routes.includes(r.id));
  for (const route of routes) for (const n of o.members) for (let s = 0; s < o.seeds; s++) {
    const seed = o.seedBase + s;
    const collector = new RunCollector(o.layout, route, n, seed, o.config);
    simulate({ layout: o.layout, route, memberCount: n, config: o.config, seed, nav, onFrame: (f) => collector.onFrame(f) });
    runs.push(collector.finish());
  }
  return { runs, summary: summarize(runs) };
}

/** Short stable hash of the config values (report header, run records). */
export function configHash(config: FormationConfig): string {
  const text = JSON.stringify(Object.keys(config).sort().map((k) => [k, config[k]]));
  let h = 2166136261;
  for (let i = 0; i < text.length; i++) { h ^= text.charCodeAt(i); h = Math.imul(h, 16777619); }
  return (h >>> 0).toString(16).padStart(8, '0');
}
