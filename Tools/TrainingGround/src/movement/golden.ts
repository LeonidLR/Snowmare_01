// Training Ground / movement: golden vectors for the C++ parity tests (FORMATION_CONTRACT.md 3). Deterministic: the same
// config gives byte-identical files. C++ CodexTactics.Formation.Parity.<Rule> reads Tools/TrainingGround/golden/formation_<Rule>.json
// and must reproduce every "out" within the tolerance, using "config_values" (a snapshot of formation_config.json).

import type { FormationConfig } from './config.ts';
import type { Segment, Vec2 } from './geometry.ts';
import { normYaw, round } from './geometry.ts';
import type { Formation } from './layout.ts';
import { FORMATIONS } from './layout.ts';
import type { ClassifyState, CornerState, GuardState, Mode, Role, TerrainClass, Threat } from './rules.ts';
import {
  CORNER_START, FormationAssignRules, FormationBypassRules, FormationCoverRules, FormationCornerRules, FormationGuardRules, FormationSectorRules, FormationShapeRules,
  FormationTerrainRules, FormationWallRules, GUARD_START, rolesFor,
} from './rules.ts';
import { FormationMenuRules, MENU_ITEMS } from './menu.ts';
import type { MenuChoice } from './menu.ts';

export const GOLDEN_SCHEMA = 'codex.formation_golden/1';
export const GOLDEN_RULES = ['Slots', 'HedgehogSlots', 'LookYaw', 'Classify', 'PassageWidth', 'FormationFor', 'WallLayout', 'Menu',
  'Guard', 'TrailSlots', 'SideWidth', 'Bypass', 'Corner', 'Assign', 'TurnAhead', 'ClearLook', 'Cover', 'CoverStatic', 'Slide', 'Decided'] as const;
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

/**
 * Rotates and moves a scene so the parity tests also cover arbitrary headings / positions. The points come back ROUNDED to the
 * precision the files hold (`r2`): every output is computed from exactly the inputs written (a wall whose nearest point lies
 * on the heading line flipped sides between the unrounded and the written coordinates - C++ port, SideWidth cases 37 / 38).
 */
function place(walls: Segment[], yaw: number, offset: Vec2): Segment[] {
  const c = Math.cos((yaw * Math.PI) / 180), s = Math.sin((yaw * Math.PI) / 180);
  const t = (p: Vec2): Vec2 => r2([offset[0] + p[0] * c - p[1] * s, offset[1] + p[0] * s + p[1] * c]);
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
  // v2.1 guard: a wall 4 m to the left - the echelon LEFT's tail is beyond it, the echelon right is whole.
  echelon_cut_left: [{ a: [-2000, -400], b: [2000, -400], thickness: 40 }],
  // v2.1 guard: a passage entrance 3 m ahead (walls from x 300 on): whole here, cut at the lookahead pose.
  entrance_ahead: [{ a: [300, -250], b: [3000, -250], thickness: 40 }, { a: [300, 250], b: [3000, 250], thickness: 40 }],
  // v2.3 ClearLook: two walls left-ahead (-45 deg, 1.9 m) and left-behind (-160 deg, 1.5 m) leave a sliver near -100 (red team 2_8).
  sliver_left: [{ a: [-290, -558], b: [558, 290], thickness: 40 }, { a: [-278, 325], b: [-4, -427], thickness: 40 }],
  // v2.3 ClearLook: a 2 x 2 m pocket - every direction blocked; the longest clear ray is a diagonal.
  pocket: [{ a: [-100, -100], b: [100, -100], thickness: 20 }, { a: [100, -100], b: [100, 100], thickness: 20 },
    { a: [100, 100], b: [-100, 100], thickness: 20 }, { a: [-100, 100], b: [-100, -100], thickness: 20 }],
};

/** v2.1 scenes for the guard (the leader's frame). */
const GUARD_SCENES = ['none', 'wall_left', 'wall_right', 'corridor', 'gap_left', 'corner_left_ahead', 'wall_end_behind_left',
  'far_left', 'echelon_cut_left', 'entrance_ahead'];

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

  const classify = base('Classify', 'in: walls [{a, b, thickness}], leaderPos, leaderYaw, destination (null = none; v1.2 at_wall only when arriving), state {current, candidate, candidateDistance} (v2), stepDistance -> out.state.');
  // v2 states: none; settled classes; a pending looser candidate short of / just under the hold distance; leaving at_wall.
  const hold = config['classify.hold_distance'];
  const STATES: ClassifyState[] = [
    { current: null, candidate: null, candidateDistance: 0, passageWidth: null },
    { current: 'open', candidate: null, candidateDistance: 0, passageWidth: null },
    { current: 'wall_left', candidate: null, candidateDistance: 0, passageWidth: null },
    { current: 'corridor', candidate: null, candidateDistance: 0, passageWidth: 460 },
    { current: 'wall_left', candidate: 'open', candidateDistance: 100, passageWidth: null },
    { current: 'corridor', candidate: 'open', candidateDistance: hold - 30, passageWidth: 460 },
    { current: 'at_wall', candidate: null, candidateDistance: 0, passageWidth: null },
  ];
  for (const [scene, walls] of Object.entries(SCENES)) {
    for (const [yaw, offset] of [[0, [0, 0]], [63, [500, -250]], [-140, [-1200, 800]]] as [number, Vec2][]) {
      const placed = place(walls, yaw, offset);
      const ahead = (d: number): Vec2 => [offset[0] + Math.cos((yaw * Math.PI) / 180) * d, offset[1] + Math.sin((yaw * Math.PI) / 180) * d];
      for (const destination of [null, r2(offset), r2(ahead(1000))] as (Vec2 | null)[]) {
        for (const state of STATES) for (const stepDistance of [6, 60]) {
          const out = FormationTerrainRules.Classify(placed, offset, yaw, destination, state, stepDistance, config);
          classify.cases.push({ in: { scene, walls: placed.map(seg), leaderPos: r2(offset), leaderYaw: yaw, destination, state, stepDistance },
            out: { state: { ...out, candidateDistance: round(out.candidateDistance), passageWidth: out.passageWidth === null ? null : round(out.passageWidth) } } });
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

  const formationFor = base('FormationFor', 'in: class, memberCount, blizzard -> out.formation.');
  for (const terrain of ['open', 'wall_left', 'wall_right', 'corridor', 'at_wall'] as TerrainClass[]) for (const memberCount of [2, 3, 4]) {
    for (const blizzard of [false, true]) {
      formationFor.cases.push({ in: { class: terrain, memberCount, blizzard }, out: { formation: FormationTerrainRules.FormationFor(terrain, memberCount, blizzard) as Formation } });
    }
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

  const menu = base('Menu', 'in: op (itemAt | release | resolve) + its inputs -> out (FormationMenuRules; screen px, y down, 12 = up).');
  for (let deg = 1.3; deg < 360; deg += 7.5) for (const r of [20, 41, 120]) { // off the sector borders (22.5 + 45k): parity-safe
    const dx = round(Math.sin((deg * Math.PI) / 180) * r, 4), dy = round(-Math.cos((deg * Math.PI) / 180) * r, 4);
    menu.cases.push({ in: { op: 'itemAt', dx, dy, deadZonePx: 40 }, out: { item: FormationMenuRules.ItemAt(dx, dy, 40) } });
  }
  for (const highlighted of [null, ...MENU_ITEMS.map((i) => i.id)]) for (const choice of ['auto', 'column'] as MenuChoice[]) for (const memberCount of [1, 2, 4]) {
    const r = FormationMenuRules.Release({ open: true, choice, highlighted }, memberCount);
    menu.cases.push({ in: { op: 'release', state: { open: true, choice, highlighted }, memberCount }, out: r });
  }
  for (const choice of ['auto', ...MENU_ITEMS.map((i) => i.id)] as MenuChoice[]) for (const memberCount of [1, 2, 3, 4]) {
    menu.cases.push({ in: { op: 'resolve', choice, memberCount }, out: FormationMenuRules.Resolve(choice, memberCount) });
  }

  // --- v2.1 (FORMATION_CONTRACT.md 10)
  const guard = base('Guard', 'in: walls, requested, memberCount, leaderPos, leaderYaw, ahead ({pos, yaw} | null), turnAhead (deg, v2.1.1), state {requested, level, effective, candidate, candidateDistance}, stepDistance -> out.blocked (BlockedRoles at the leader) + out.state (Resolve).');
  const guardFormations: Formation[] = ['diamond', 'line', 'column', 'echelon_right', 'echelon_left', 'hedgehog', 'blizzard'];
  for (const scene of GUARD_SCENES) {
    for (const [yaw, offset] of [[0, [0, 0]], [63, [500, -250]]] as [number, Vec2][]) {
      const placed = place(SCENES[scene], yaw, offset);
      const ahead = { pos: r2([offset[0] + Math.cos((yaw * Math.PI) / 180) * 500, offset[1] + Math.sin((yaw * Math.PI) / 180) * 500]), yaw };
      for (const requested of guardFormations) for (const memberCount of requested.startsWith('echelon') ? [3, 4] : [4]) {
        const states: GuardState[] = [
          GUARD_START,
          { requested, level: 2, effective: 'column', candidate: null, candidateDistance: 0 },
          { requested, level: 2, effective: 'column', candidate: 0, candidateDistance: hold - 30 },
        ];
        // v2.1.1: turnAhead 40 (> turn.fold_deg) only without the lookahead pose (keeps the file small).
        for (const [aheadIn, turn] of [[null, 0], [ahead, 0], [null, 40]] as [typeof ahead | null, number][]) for (const state of states) {
          const blocked = FormationGuardRules.BlockedRoles(placed, requested, memberCount, offset, yaw, config);
          const out = FormationGuardRules.Resolve(placed, requested, memberCount, offset, yaw, aheadIn, turn, state, 60, config);
          guard.cases.push({ in: { scene, walls: placed.map(seg), requested, memberCount, leaderPos: r2(offset), leaderYaw: yaw, ahead: aheadIn, turnAhead: turn, state, stepDistance: 60 },
            out: { blocked, state: { ...out, candidateDistance: round(out.candidateDistance) } } });
        }
      }
    }
  }

  const trail = base('TrailSlots', 'in: trail [[x, y]] (oldest first, last = the leader), leaderYaw, memberCount, walls (v2.3; [] = none) -> out.slots [{role, pos, yaw}] (the column along the path; yaw = the trail direction there).');
  const TRAILS: Record<string, Vec2[]> = {
    straight: [[-2000, 0], [-1000, 0], [0, 0]],
    turn_left_90: [[-1500, 600], [-600, 600], [-300, 300], [0, 0]],
    short: [[-100, 0], [0, 0]],
    single: [[0, 0]],
    zigzag: [[-1200, -200], [-800, 200], [-400, -200], [0, 0]],
  };
  // v2.3: walls round the trail - the stagger shrinks to keep column.stagger_clear (none = the full stagger).
  const TRAIL_WALLS: Record<string, Segment[]> = {
    none: [],
    corridor_200: [{ a: [-3000, -100], b: [1000, -100], thickness: 20 }, { a: [-3000, 100], b: [1000, 100], thickness: 20 }],
    gap_oblique: [{ a: [-700, -1500], b: [-560, -80], thickness: 40 }, { a: [-540, 80], b: [-400, 1500], thickness: 40 }],
  };
  for (const [name, pts] of Object.entries(TRAILS)) for (const [yaw, offset] of [[0, [0, 0]], [-120, [800, 300]]] as [number, Vec2][]) {
    const tr = pts.map((p) => r2([offset[0] + p[0] * Math.cos((yaw * Math.PI) / 180) - p[1] * Math.sin((yaw * Math.PI) / 180), offset[1] + p[0] * Math.sin((yaw * Math.PI) / 180) + p[1] * Math.cos((yaw * Math.PI) / 180)]));
    for (const [wname, ws] of Object.entries(TRAIL_WALLS)) {
      if (wname !== 'none' && name !== 'straight' && name !== 'zigzag') continue;
      const placed = place(ws, yaw, offset);
      for (const memberCount of [2, 3, 4]) {
        const out = FormationShapeRules.TrailSlots(tr, yaw, memberCount, config, placed).map((x) => ({ role: x.role, pos: r2(x.pos), yaw: round(x.yaw) }));
        trail.cases.push({ in: { trail: name, points: tr, leaderYaw: yaw, memberCount, scene: wname, walls: placed.map(seg) }, out: { slots: out } });
      }
    }
  }

  const side = base('SideWidth', 'in: walls, pos, yaw -> out.width (cm; null = no wall body within classify.probe_side on either side).');
  for (const [scene, walls] of Object.entries(SCENES)) for (const [yaw, offset] of [[0, [0, 0]], [63, [500, -250]], [-140, [-1200, 800]]] as [number, Vec2][]) {
    const placed = place(walls, yaw, offset);
    const w = FormationTerrainRules.SideWidth(placed, offset, yaw, config);
    side.cases.push({ in: { scene, walls: placed.map(seg), pos: r2(offset), yaw }, out: { width: w === null ? null : round(w) } });
  }

  const bypass = base('Bypass', 'in: wall {a, b, thickness}, leaderPos, destination, threats [{pos, ageS}] -> out {wallOn, detour, shortest, reason, path} (ChooseSide; compare path points in cm, detour to 1e-3).');
  const BYPASS: { name: string; wall: Segment; from: Vec2; to: Vec2 }[] = [
    { name: 'oblique', wall: { a: [600, -100], b: [2000, 200], thickness: 40 }, from: [0, 0], to: [3200, 0] },
    { name: 'square_lopsided', wall: { a: [900, -200], b: [900, 1100], thickness: 40 }, from: [0, 0], to: [1700, 0] },
    { name: 'parallel_offset', wall: { a: [500, -80], b: [2500, 60], thickness: 30 }, from: [0, 0], to: [3000, 0] },
  ];
  const memory = config['bypass.threat_memory_s'];
  for (const b of BYPASS) for (const [yaw, offset] of [[0, [0, 0]], [63, [500, -250]]] as [number, Vec2][]) {
    const [w] = place([b.wall], yaw, offset);
    const tp = (p: Vec2) => place([{ a: p, b: p }], yaw, offset)[0].a;
    const threatSets: Record<string, Threat[]> = {
      none: [],
      left: [{ pos: r2(tp([1300, -900])), ageS: 0 }],
      right: [{ pos: r2(tp([1300, 900])), ageS: 3 }],
      right_far: [{ pos: r2(tp([1600, 700])), ageS: 1 }],
      forgotten: [{ pos: r2(tp([1300, -900])), ageS: memory + 1 }],
    };
    for (const [threatName, threats] of Object.entries(threatSets)) {
      const out = FormationBypassRules.ChooseSide({ a: r2(w.a), b: r2(w.b), thickness: w.thickness }, r2(tp(b.from)), r2(tp(b.to)), threats, config);
      bypass.cases.push({ in: { scene: b.name, threat: threatName, wall: seg(w), leaderPos: r2(tp(b.from)), destination: r2(tp(b.to)), threats },
        out: { wallOn: out.wallOn, detour: round(out.detour, 4), shortest: out.shortest, reason: out.reason, path: out.path.map(r2) } });
    }
  }

  const corner = base('Corner', 'in: walls, leaderPos, leaderYaw, members [{role, pos}] (K first), state {left, right: {wall, end, role} | null} -> out {state, looks [{role, yaw}]} (Watch).');
  const CORNER_SCENES: Record<string, Segment[]> = {
    wall_end_left: [{ a: [-2000, -300], b: [200, -300], thickness: 40 }],
    wall_end_right: [{ a: [-2000, 300], b: [150, 300], thickness: 40 }],
    corridor_exit: [{ a: [-2000, -250], b: [100, -250], thickness: 40 }, { a: [-2000, 250], b: [250, 250], thickness: 40 }],
    room_corner: [{ a: [-2000, -300], b: [200, -300], thickness: 40 }, { a: [200, -300], b: [200, -1500], thickness: 40 }],
  };
  for (const [scene, walls] of Object.entries(CORNER_SCENES)) for (const [yaw, offset] of [[0, [0, 0]], [-120, [700, 400]]] as [number, Vec2][]) {
    const placed = place(walls, yaw, offset);
    const tp = (p: Vec2) => r2(place([{ a: p, b: p }], yaw, offset)[0].a);
    for (const leaderX of [-400, 0, 150, 300, 500]) for (const memberCount of [2, 4]) {
      const local: Record<Role, Vec2> = { K: [leaderX, 0], P: [leaderX - 280, 200], L: [leaderX - 280, -200], Z: [leaderX - 560, 0] };
      const members = rolesFor(memberCount).map((role) => ({ role, pos: tp(local[role]) }));
      const states: CornerState[] = [CORNER_START, { left: { wall: 0, end: 1, role: memberCount >= 4 ? 'L' : 'Z' }, right: null }];
      for (const state of states) {
        const out = FormationCornerRules.Watch(placed, tp([leaderX, 0]), yaw, members, state, config);
        corner.cases.push({ in: { scene, walls: placed.map(seg), leaderPos: tp([leaderX, 0]), leaderYaw: yaw, members, state },
          out: { state: out.state, looks: out.looks.map((l) => ({ role: l.role, yaw: round(l.yaw) })) } });
      }
    }
  }

  // v2.1 fix: minimal-travel places on a formation change (every pair of formations, from their slots, 3 and 4 members; plus
  // tie / identity cases). out.places[i] = the index into in.places for member i.
  const assign = base('Assign', 'in: members [[x, y]] (the followers), places [[x, y]] -> out.places [index into places per member] (AssignPlaces = GroupCoverRules::AssignMinimalTravel: DFS member-major, a full assignment wins only when cheaper by > 1e-3 cm).');
  const switchable: Formation[] = ['diamond', 'line', 'column', 'echelon_right', 'echelon_left', 'hedgehog', 'blizzard'];
  for (const memberCount of [3, 4]) for (const from of switchable) for (const to of switchable) {
    for (const [yaw, offset, turn] of [[0, [0, 0], 0], [63, [500, -250], 35]] as [number, Vec2, number][]) {
      const a = FormationShapeRules.Slots(from, memberCount, offset, yaw, config).slice(1).map((x) => r2(x.pos));
      const b = FormationShapeRules.Slots(to, memberCount, r2([offset[0] + 150, offset[1]]), yaw + turn, config).slice(1).map((x) => r2(x.pos));
      assign.cases.push({ in: { from, to, memberCount, members: a, places: b }, out: { places: FormationAssignRules.AssignPlaces(a, b) } });
    }
  }
  for (const [members, places] of [
    [[[0, 0], [100, 0]], [[0, 0], [100, 0]]], [[[0, 0], [0, 0]], [[50, 0], [50, 0]]], [[[0, 0], [100, 0], [200, 0]], [[200, 0], [100, 0], [0, 0]]],
  ] as Vec2[][][]) assign.cases.push({ in: { from: 'tie', to: 'tie', memberCount: members.length + 1, members, places }, out: { places: FormationAssignRules.AssignPlaces(members, places) } });

  // v2.1.1: the turn ahead on a path, and the look turned out of walls.
  const turnAhead = base('TurnAhead', 'in: path [[x, y]] (path[0] = the leader), heading -> out.deg (largest |heading - piece direction| over the pieces starting within turn.lookahead).');
  const PATHS: Record<string, Vec2[]> = {
    straight: [[0, 0], [2000, 0]], bend_30_near: [[0, 0], [200, 0], [1200, 577]], bend_90_far: [[0, 0], [700, 0], [700, 900]],
    zigzag: [[0, 0], [100, 50], [200, 0], [300, 50]], single: [[0, 0]], u_turn: [[0, 0], [150, 0], [150, 300], [-500, 300]],
  };
  for (const [name, pts] of Object.entries(PATHS)) for (const [yaw, offset] of [[0, [0, 0]], [-120, [800, 300]]] as [number, Vec2][]) {
    const tp = (p: Vec2) => r2(place([{ a: p, b: p }], yaw, offset)[0].a);
    const path = pts.map(tp);
    for (const heading of [yaw, yaw + 12]) turnAhead.cases.push({ in: { path: name, points: path, heading }, out: { deg: round(FormationGuardRules.TurnAhead(path, heading, config)) } });
  }
  const clear = base('ClearLook', 'in: walls, pos, yaw, currentYaw, held (null = none) -> out.yaw (FormationSectorRules.ClearLook: compare as angles).');
  for (const scene of ['none', 'wall_left', 'wall_right', 'corridor', 'ahead', 'corner_left_ahead', 'wall_end_behind_left', 'far_left', 'sliver_left', 'pocket']) {
    for (const [yaw0, offset] of [[0, [0, 0]], [63, [500, -250]]] as [number, Vec2][]) {
      const placed = place(SCENES[scene], yaw0, offset);
      for (let rel = -165; rel <= 180; rel += 30) {
        const yaw = round(yaw0 + rel + 2.5);
        for (const turned of [0, 70, -70]) for (const heldRel of [null, -40, 100]) {
          const currentYaw = round(yaw + turned);
          const held = heldRel === null ? null : round(yaw + heldRel); // v2.3: the previous result (hysteresis)
          clear.cases.push({ in: { scene, walls: placed.map(seg), pos: r2(offset), yaw, currentYaw, held }, out: { yaw: round(FormationSectorRules.ClearLook(placed, offset, yaw, currentYaw, config, held)) } });
        }
      }
    }
  }

  // v2.3 (1A clarified): which formation looks are user decisions the allocator keeps as fixed.
  const decided = base('Decided', 'in: role, formation, memberCount, passageWidth (null = none) -> out.decided (FormationSectorRules.Decided).');
  for (const formation of ['column', 'line', 'diamond', 'echelon_right', 'echelon_left', 'wedge', 'blizzard', 'hedgehog', 'wall'] as const) {
    for (const memberCount of [2, 3, 4]) for (const passageWidth of [null, 200, 599, 600, 900]) for (const role of ['K', 'P', 'L', 'Z'] as const) {
      if (memberCount === 2 && (role === 'P' || role === 'L')) continue;
      if (memberCount === 3 && role === 'L') continue;
      decided.cases.push({ in: { role, formation, memberCount, passageWidth }, out: { decided: FormationSectorRules.Decided(role, formation, memberCount, passageWidth, config) } });
    }
  }

  // v2.2: the coverage allocator (walk / halt, fixed looks) and the static allocation round a standing group.
  const cover = base('Cover', 'in: walls, leaderPos, heading, members [{role, pos, back}] (K first), mode walk|halt, t, fixed [{role, yaw}] -> out {rearGuard, looks [{role, yaw}], shares [{role, from, to, primary}]} (FormationCoverRules.Allocate; yaws compare as angles).');
  const COVER_SCENES = ['none', 'wall_left', 'wall_right', 'corridor', 'wide_street', 'gap_left', 'corner_left_ahead', 'far_left'];
  for (const scene of COVER_SCENES) for (const [yaw, offset] of [[0, [0, 0]], [-120, [800, 300]]] as [number, Vec2][]) {
    const placed = place(SCENES[scene], yaw, offset);
    for (const f of ['diamond', 'column', 'echelon_right', 'line'] as Formation[]) for (const n of [3, 4]) {
      const mem = FormationShapeRules.Slots(f, n, offset, yaw, config).map((x) => {
        const p = r2(x.pos), h: Vec2 = [Math.cos((yaw * Math.PI) / 180), Math.sin((yaw * Math.PI) / 180)];
        return { role: x.role, pos: p, back: round(-((p[0] - offset[0]) * h[0] + (p[1] - offset[1]) * h[1])) };
      });
      for (const [mode, t, fixed] of [['halt', 0, []], ['walk', 0.7, []], ['walk', 2.9, []], ['walk', 1.3, [{ role: 'K', yaw: round(yaw - 95) }]]] as ['walk' | 'halt', number, { role: Role; yaw: number }[]][]) {
        const out = FormationCoverRules.Allocate(placed, offset, yaw, mem, mode, t, config, fixed)!;
        cover.cases.push({ in: { scene, formation: f, walls: placed.map(seg), leaderPos: r2(offset), heading: yaw, members: mem, mode, t, fixed },
          out: { rearGuard: out.rearGuard, looks: out.looks.map((l) => ({ role: l.role, yaw: round(l.yaw) })), shares: out.shares.map((x) => ({ ...x, primary: round(x.primary) })) } });
      }
    }
  }
  const coverStatic = base('CoverStatic', 'in: walls, members [{role, pos}], fixed [{role, yaw}], free [roles] -> out.looks [{role, yaw}] (FormationCoverRules.AllocateStatic).');
  const coverWall: Segment = { a: [0, 0], b: [3000, 0], thickness: 40 };
  for (const [yaw, offset] of [[0, [0, 0]], [63, [500, -250]]] as [number, Vec2][]) {
    const [w] = place([coverWall], yaw, offset);
    const tp = (p: Vec2) => r2(place([{ a: p, b: p }], yaw, offset)[0].a);
    for (const n of [3, 4]) {
      const slots = FormationWallRules.Layout(coverWall, [400, 300], n, 400, 2600, config).map((s2) => ({ role: s2.role, pos: tp(s2.pos) }));
      for (const fixedYaw of [180, 135, 90]) {
        const fixed = [{ role: 'K' as Role, yaw: round(fixedYaw + yaw) }];
        const free = slots.map((s2) => s2.role).filter((r) => r !== 'K');
        const out = FormationCoverRules.AllocateStatic([w], slots, fixed, free, config);
        coverStatic.cases.push({ in: { walls: [seg(w)], members: slots, fixed, free }, out: { looks: out.map((l) => ({ role: l.role, yaw: round(l.yaw) })) } });
      }
      const all = slots.map((s2) => s2.role);
      coverStatic.cases.push({ in: { walls: [seg(w)], members: slots, fixed: [], free: all }, out: { looks: FormationCoverRules.AllocateStatic([w], slots, [], all, config).map((l) => ({ role: l.role, yaw: round(l.yaw) })) } });
    }
  }

  // v2.2: the wall slide.
  const slide = base('Slide', 'in: wall, startSlots [{role, pos, yaw}] (FormationWallRules.Layout, K first), leaderPos, destination, t -> out {active, leaderTarget, targets [{role, pos, yaw}], lead, rearGuard, normalYaw, order} (FormationWallRules.Slide).');
  for (const [yaw, offset] of [[0, [0, 0]], [63, [500, -250]]] as [number, Vec2][]) {
    const [w] = place([{ a: [0, 0], b: [3000, 0], thickness: 40 }], yaw, offset);
    const tp = (p: Vec2) => r2(place([{ a: p, b: p }], yaw, offset)[0].a);
    for (const n of [2, 3, 4]) {
      const start = FormationWallRules.Layout({ a: [0, 0], b: [3000, 0], thickness: 40 }, [1500, 300], n, 1500, 1500, config)
        .map((s2) => ({ role: s2.role, pos: tp(s2.pos), yaw: round(normYaw(s2.yaw + yaw)) }));
      const kLocal = FormationWallRules.Layout({ a: [0, 0], b: [3000, 0], thickness: 40 }, [1500, 300], n, 1500, 1500, config)[0].pos;
      for (const [dx, dy, progress, t] of [[600, 100, 0, 0], [600, 100, 300, 1.3], [-900, 60, 450, 2.2], [600, 400, 0, 0], [600, -150, 0, 0], [20, 100, 0, 0]] as number[][]) {
        const destination = tp([1500 + dx, dy]);
        const leaderPos = tp([kLocal[0] + Math.sign(dx) * progress, kLocal[1]]);
        const out = FormationWallRules.Slide(w, start, leaderPos, destination, t, config);
        slide.cases.push({ in: { wall: seg(w), startSlots: start, leaderPos, destination, t },
          out: { ...out, leaderTarget: r2(out.leaderTarget), normalYaw: round(out.normalYaw), targets: out.targets.map((x) => ({ role: x.role, pos: r2(x.pos), yaw: round(x.yaw) })) } });
      }
    }
  }

  return { Slots: slots, HedgehogSlots: hedgehog, LookYaw: look, Menu: menu, Classify: classify, PassageWidth: passage, FormationFor: formationFor, WallLayout: wall,
    Guard: guard, TrailSlots: trail, SideWidth: side, Bypass: bypass, Corner: corner, Assign: assign, TurnAhead: turnAhead, ClearLook: clear, Cover: cover, CoverStatic: coverStatic, Slide: slide, Decided: decided };
}

export const goldenFileName = (rule: GoldenRule): string => `formation_${rule}.json`;
export const goldenText = (file: GoldenFile): string => JSON.stringify(file, null, 1) + '\n';

/** Line endings normalised (a Windows checkout with core.autocrlf may hold CRLF). */
export const sameGoldenText = (onDisk: string, expected: string): boolean => onDisk.replace(/\r\n/g, '\n') === expected;
