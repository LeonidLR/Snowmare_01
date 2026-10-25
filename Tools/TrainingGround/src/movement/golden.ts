// Training Ground / movement: golden vectors for the C++ parity tests (FORMATION_CONTRACT.md 3). Deterministic: the same
// config gives byte-identical files. C++ CodexTactics.Formation.Parity.<Rule> reads Tools/TrainingGround/golden/formation_<Rule>.json
// and must reproduce every "out" within the tolerance, using "config_values" (a snapshot of formation_config.json).

import type { FormationConfig } from './config.ts';
import type { Segment, Vec2 } from './geometry.ts';
import { round } from './geometry.ts';
import type { Formation } from './layout.ts';
import { FORMATIONS } from './layout.ts';
import type { Mode, Role, TerrainClass } from './rules.ts';
import { FormationSectorRules, FormationShapeRules, FormationTerrainRules, FormationWallRules, rolesFor } from './rules.ts';

export const GOLDEN_SCHEMA = 'codex.formation_golden/1';
export const GOLDEN_RULES = ['Slots', 'HedgehogSlots', 'LookYaw', 'Classify', 'PassageWidth', 'FormationFor', 'WallLayout'] as const;
export type GoldenRule = (typeof GOLDEN_RULES)[number];

export interface GoldenFile {
  schema: string;
  rule: GoldenRule;
  tolerance: { cm: number; deg: number };
  config: string;
  config_values: Record<string, number>;
  notes: string;
  cases: { in: unknown; out: unknown }[];
}

const r2 = (v: Vec2): Vec2 => [round(v[0]), round(v[1])];
const seg = (s: Segment) => ({ a: r2(s.a), b: r2(s.b), thickness: s.thickness ?? 0 });

/** Rotates and moves a scene so the parity tests also cover arbitrary headings / positions. */
function place(walls: Segment[], yaw: number, offset: Vec2): Segment[] {
  const c = Math.cos((yaw * Math.PI) / 180), s = Math.sin((yaw * Math.PI) / 180);
  const t = (p: Vec2): Vec2 => [offset[0] + p[0] * c - p[1] * s, offset[1] + p[0] * s + p[1] * c];
  return walls.map((w) => ({ a: t(w.a), b: t(w.b), thickness: w.thickness }));
}

/** Scenes in the leader's frame (heading +X, left = -Y). */
const SCENES: Record<string, Segment[]> = {
  none: [],
  wall_left: [{ a: [-2000, -300], b: [2000, -300], thickness: 40 }],
  wall_right: [{ a: [-2000, 300], b: [2000, 300], thickness: 40 }],
  corridor: [{ a: [-2000, -250], b: [2000, -250], thickness: 40 }, { a: [-2000, 250], b: [2000, 250], thickness: 40 }],
  wide_street: [{ a: [-2000, -380], b: [2000, -380], thickness: 40 }, { a: [-2000, 380], b: [2000, 380], thickness: 40 }],
  ahead: [{ a: [120, -500], b: [120, 500], thickness: 20 }],
  gap_left: [{ a: [-2000, -300], b: [-100, -300], thickness: 40 }, { a: [100, -300], b: [2000, -300], thickness: 40 }],
  wide_gap_left: [{ a: [-2000, -300], b: [-400, -300], thickness: 40 }, { a: [400, -300], b: [2000, -300], thickness: 40 }],
  corner_left_ahead: [{ a: [-2000, -300], b: [400, -300], thickness: 40 }, { a: [400, -300], b: [400, 1500], thickness: 40 }],
  wall_end_behind_left: [{ a: [-2000, -300], b: [-150, -300], thickness: 40 }],
  far_left: [{ a: [-2000, -700], b: [2000, -700], thickness: 40 }],
  // v1.1 (Q3): a dead end 3 m ahead (beyond at_wall_dist) - the side probes ignore it: open, not corridor.
  deadend_ahead_300: [{ a: [300, -2000], b: [300, 2000], thickness: 40 }],
  // v1.1: a dead end ahead with a wall on the left (lane F) - wall_left, not echelon flips from the wall ahead.
  deadend_ahead_wall_left: [{ a: [300, -2000], b: [300, 2000], thickness: 40 }, { a: [-2000, -300], b: [250, -300], thickness: 40 }],
  // v1.1 edge: a wall 50 deg off the heading on the left is still BESIDE (outside the 30 deg band round perpendicular).
  oblique_left_50: [{ a: [-1200, -1800], b: [400, 107], thickness: 40 }],
  // v1.1 edge: a wall 70 deg off the heading (inside the band) ahead-left is ignored by the side probes.
  oblique_left_70: [{ a: [100, -1700], b: [700, -50], thickness: 40 }],
};

export function buildGolden(config: FormationConfig, configPath: string): Record<GoldenRule, GoldenFile> {
  const base = (rule: GoldenRule, notes: string): GoldenFile => ({
    schema: GOLDEN_SCHEMA, rule, tolerance: { cm: 0.5, deg: 0.5 }, config: configPath,
    config_values: { ...config }, notes, cases: [],
  });

  const slots = base('Slots', 'in: formation, memberCount, leaderPos, leaderYaw -> out.slots [{role, pos}] (index 0 = leader); wall -> [].');
  for (const formation of FORMATIONS) for (const memberCount of [1, 2, 3, 4]) {
    for (const leaderPos of [[0, 0], [1234.5, -678.25]] as Vec2[]) for (const leaderYaw of [0, 37.5, -120, 180]) {
      const out = FormationShapeRules.Slots(formation, memberCount, leaderPos, leaderYaw, config).map((s) => ({ role: s.role, pos: r2(s.pos) }));
      slots.cases.push({ in: { formation, memberCount, leaderPos, leaderYaw }, out: { slots: out } });
    }
  }

  const hedgehog = base('HedgehogSlots', 'in: center, memberCount, yaw -> out.slots [{role, pos, yaw}] (v1.2: the ring round the heat source).');
  for (const memberCount of [1, 2, 3, 4]) for (const center of [[0, 0], [1234.5, -678.25]] as Vec2[]) for (const yaw of [0, 37.5, -120, 180]) {
    const out = FormationShapeRules.HedgehogSlots(center, memberCount, yaw, config).map((s) => ({ role: s.role, pos: r2(s.pos), yaw: round(s.yaw) }));
    hedgehog.cases.push({ in: { center, memberCount, yaw }, out: { slots: out } });
  }

  const look = base('LookYaw', 'in: role, formation, mode, memberCount, timeInPatrol, leaderYaw, passageWidth (null = not in a passage) -> out.yaw (UE yaw, -180..180; compare as angles).');
  const modes: Mode[] = ['walk', 'run', 'halt', 'wall', 'forward'];
  for (const formation of FORMATIONS) for (const mode of modes) for (const memberCount of [2, 3, 4]) {
    for (const role of rolesFor(memberCount) as Role[]) for (const timeInPatrol of [0, 0.37, 0.75, 1.6, 2.9, 4.4]) {
      for (const leaderYaw of [0, -150]) for (const passageWidth of formation === 'column' ? [null, 460, 760] : [null]) {
        const yaw = FormationSectorRules.LookYaw(role, formation, mode, memberCount, timeInPatrol, leaderYaw, passageWidth, config);
        look.cases.push({ in: { role, formation, mode, memberCount, timeInPatrol, leaderYaw, passageWidth }, out: { yaw: round(yaw) } });
      }
    }
  }

  const classify = base('Classify', 'in: walls [{a, b, thickness}], leaderPos, leaderYaw, destination (null = none; v1.2 at_wall only when arriving), previousClass (null = none), timeInClass -> out.class.');
  const previous: (TerrainClass | null)[] = [null, 'open', 'wall_left', 'corridor'];
  for (const [scene, walls] of Object.entries(SCENES)) {
    for (const [yaw, offset] of [[0, [0, 0]], [63, [500, -250]], [-140, [-1200, 800]]] as [number, Vec2][]) {
      const placed = place(walls, yaw, offset);
      const ahead = (d: number): Vec2 => [offset[0] + Math.cos((yaw * Math.PI) / 180) * d, offset[1] + Math.sin((yaw * Math.PI) / 180) * d];
      for (const destination of [null, r2(offset), r2(ahead(1000))] as (Vec2 | null)[]) {
        for (const previousClass of previous) for (const timeInClass of [0.3, 1.5]) {
          const out = FormationTerrainRules.Classify(placed, offset, yaw, destination, previousClass, timeInClass, config);
          classify.cases.push({ in: { scene, walls: placed.map(seg), leaderPos: r2(offset), leaderYaw: yaw, destination, previousClass, timeInClass }, out: { class: out } });
        }
      }
    }
  }

  const passage = base('PassageWidth', 'in: walls, leaderPos, leaderYaw -> out.width (cm, null = a side is open).');
  for (const [scene, walls] of Object.entries(SCENES)) {
    for (const [yaw, offset] of [[0, [0, 0]], [63, [500, -250]], [-140, [-1200, 800]]] as [number, Vec2][]) {
      const placed = place(walls, yaw, offset);
      const w = FormationTerrainRules.PassageWidth(placed, offset, yaw, config);
      passage.cases.push({ in: { scene, walls: placed.map(seg), leaderPos: r2(offset), leaderYaw: yaw }, out: { width: w === null ? null : round(w) } });
    }
  }

  const formationFor = base('FormationFor', 'in: class, memberCount -> out.formation.');
  for (const terrain of ['open', 'wall_left', 'wall_right', 'corridor', 'at_wall'] as TerrainClass[]) for (const memberCount of [2, 3, 4]) {
    formationFor.cases.push({ in: { class: terrain, memberCount }, out: { formation: FormationTerrainRules.FormationFor(terrain, memberCount) as Formation } });
  }

  const wall = base('WallLayout', 'in: wall {a, b, thickness}, clickedSlot, memberCount, roomLeft, roomRight -> out.slots [{role, pos, yaw}].');
  const walls: Segment[] = [
    { a: [-1000, 200], b: [1000, 200], thickness: 40 },
    { a: [300, -800], b: [300, 900], thickness: 30 },
    { a: [-500, -500], b: [700, 700], thickness: 50 },
  ];
  const clicks: Vec2[] = [[0, 0], [600, 100], [-200, 400]];
  for (const w of walls) for (const clickedSlot of clicks) for (const memberCount of [1, 2, 3, 4]) {
    for (const [roomLeft, roomRight] of [[100, 900], [900, 100], [500, 500]]) {
      const out = FormationWallRules.Layout(w, clickedSlot, memberCount, roomLeft, roomRight, config)
        .map((s) => ({ role: s.role, pos: r2(s.pos), yaw: round(s.yaw) }));
      wall.cases.push({ in: { wall: seg(w), clickedSlot, memberCount, roomLeft, roomRight }, out: { slots: out } });
    }
  }

  return { Slots: slots, HedgehogSlots: hedgehog, LookYaw: look, Classify: classify, PassageWidth: passage, FormationFor: formationFor, WallLayout: wall };
}

export const goldenFileName = (rule: GoldenRule): string => `formation_${rule}.json`;
export const goldenText = (file: GoldenFile): string => JSON.stringify(file, null, 1) + '\n';

/** Line endings normalised (a Windows checkout with core.autocrlf may hold CRLF). */
export const sameGoldenText = (onDisk: string, expected: string): boolean => onDisk.replace(/\r\n/g, '\n') === expected;
