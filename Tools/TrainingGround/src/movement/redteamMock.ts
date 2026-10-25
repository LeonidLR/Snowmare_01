// Training Ground / movement: the red team's request payload and its offline stand-in (FORMATION_CONTRACT.md 11). The mock
// "model" designs small adversarial layouts from a few parametric templates (seeded by round and index: deterministic, no
// network, cost 0). Round 1 layout #2 is deliberately broken (a route point inside a wall) so the validate -> one retry path is
// exercised; the retry returns the unbroken template. No imports from ai.ts (ai.ts imports this file).

import type { Vec2 } from './geometry.ts';
import { rng } from './geometry.ts';

/** What the red team sends the model each round (distilled: never raw runs). */
export interface RedteamPayload {
  kind: 'redteam';
  round: number;
  /** How many new layouts to design. */
  count: number;
  /** Attack families with what each already did (tried layouts / layouts that broke something / checks broken). */
  families: { id: string; doc: string; tried: number; broke: number; checks: number[] }[];
  /** Distinct failures already found ("#7 auto n4"): aim at new ones. */
  found: string[];
  /** The previous round's layouts, distilled. */
  previous: { id: string; family: string; valid: boolean; note: string; failures: string[] }[];
  /** Retry: the invalid layouts of this round with their errors (fix them, same order). */
  fix?: { index: number; family: string; errors: string[]; layout: unknown }[];
  /** Retry: why the previous reply could not be used (not JSON, ...). */
  replyError?: string;
}

type V = Vec2;
interface Built { walls: [V, V, number?][]; routes: { id: string; pace: 'walk' | 'run'; points: V[]; threats?: { pos: V; since_s: number }[]; expect_bypass?: { wall: string; wall_on: 'left' | 'right' }[] }[] }
interface Template { family: string; targets: number[]; intent: string; build: (v: number) => Built }

const W = (i: number) => `W${i + 1}`;

/** Parametric attack templates (v in 0..1 varies the geometry a little). Heading +X: left = -Y. */
const TEMPLATES: Template[] = [
  {
    family: 'oblique_walls', targets: [7, 1], intent: 'a wall converging on the left flank at 10-15 deg squeezes the left slot into it',
    build: (v) => ({ walls: [[[-1500, -800 - 200 * v], [1600, -150]]], routes: [{ id: 'R1', pace: 'walk', points: [[-2600, 0], [2600, 0]] }] }),
  },
  {
    family: 'stubs_pillars', targets: [7, 5], intent: 'pillars on the line of the left slots: the chain L-Z and the leader-L line cross them',
    build: (v) => ({
      walls: [0, 1, 2, 3].map((k): [V, V, number] => [[-1200 + k * 700, -260 - 40 * v], [-1140 + k * 700, -260 - 40 * v], 60]),
      routes: [{ id: 'R1', pace: 'walk', points: [[-2600, 0], [2400, 0]] }],
    }),
  },
  {
    family: 'zigzag_corridor', targets: [7, 3], intent: 'a zigzag corridor (~4.5 m) with 30 deg bends: the column tail cuts the inner corners',
    build: (v) => {
      const half = 225 + 25 * v;
      const xs = [-2000, -800, 400, 1600], ys = [0, 600, 0, 600];
      const walls: [V, V][] = [];
      for (let k = 0; k + 1 < xs.length; k++) {
        walls.push([[xs[k], ys[k] - half], [xs[k + 1], ys[k + 1] - half]]);
        walls.push([[xs[k], ys[k] + half], [xs[k + 1], ys[k + 1] + half]]);
      }
      return { walls, routes: [{ id: 'R1', pace: 'walk', points: [[-2800, 0], [-2000, 0], [-800, 600], [400, 0], [1600, 600], [2600, 600]] }] };
    },
  },
  {
    family: 'u_turn', targets: [6, 7], intent: 'walk along a long wall, U-turn round its free end and walk back on the far side',
    build: (v) => ({
      walls: [[[-2000, -300], [1400 + 200 * v, -300]]],
      routes: [{ id: 'R1', pace: 'walk', points: [[-2400, 200], [2100, 200], [2100, -800], [-1800, -800]] }],
    }),
  },
  {
    family: 'gap_vs_width', targets: [7, 5], intent: 'a wall across the route with a gap just narrower than the diamond (~5 m)',
    build: (v) => {
      const g = 230 + 30 * v;
      return { walls: [[[0, -1800], [0, -g]], [[0, g], [0, 1800]]], routes: [{ id: 'R1', pace: 'walk', points: [[-2600, 0], [2600, 0]] }] };
    },
  },
  {
    family: 'threat_behind_thin_wall', targets: [8, 9], intent: 'a thin free-standing wall across the route, a threat behind it on the longer side',
    build: (v) => ({
      walls: [[[0, -700 - 100 * v], [0, 500], 20]],
      routes: [{ id: 'R1', pace: 'walk', points: [[-2400, 0], [2400, 0]], threats: [{ pos: [900, -500], since_s: 0 }], expect_bypass: [{ wall: 'W1', wall_on: 'right' }] }],
    }),
  },
];

function layoutOf(t: Template, v: number, broken: boolean): Record<string, unknown> {
  const b = t.build(v);
  const routes = b.routes.map((r) => ({ ...r, points: r.points.map((p) => [...p] as V) }));
  // The deliberate mistake: a route point on the first wall's middle.
  if (broken) {
    const [a, c] = b.walls[0];
    routes[0].points.splice(1, 0, [(a[0] + c[0]) / 2, (a[1] + c[1]) / 2]);
  }
  return {
    schema: 'codex.formation_layout/1', name: 'mock', bounds: { min: [-4000, -2500], max: [4000, 2500] }, ground_z: 0,
    spawn: { pos: routes[0].points[0], yaw: 0 },
    walls: b.walls.map(([a, c, th], i) => ({ id: W(i), a, b: c, height: 300, thickness: th ?? 40 })),
    zones: [], routes,
  };
}

function design(round: number, index: number, count: number, broken: boolean) {
  const t = TEMPLATES[((round - 1) * count + index) % TEMPLATES.length];
  const v = rng(round * 101 + index * 7)();
  return { family: t.family, target_checks: t.targets, intent: `(mock) ${t.intent}`, layout: layoutOf(t, v, broken) };
}

/** The mock's reply text: strict JSON `{ "layouts": [...] }`, the same shape the real model is asked for. */
export function mockRedteamText(p: RedteamPayload): string {
  if (p.fix) return JSON.stringify({ layouts: p.fix.map((f) => design(p.round, f.index, p.count, false)) });
  return JSON.stringify({ layouts: Array.from({ length: p.count }, (_, i) => design(p.round, i, p.count, p.round === 1 && i === 1)) });
}
