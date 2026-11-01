// Training Ground / movement: golden vectors for the C++ parity tests (FORMATION_CONTRACT.md 3). Deterministic: the same
// config gives byte-identical files. C++ CodexTactics.Formation.Parity.<Rule> reads Tools/TrainingGround/golden/formation_<Rule>.json
// and must reproduce every "out" within the tolerance, using "config_values" (a snapshot of formation_config.json).

import type { FormationConfig } from './config.ts';
import type { Segment, Vec2 } from './geometry.ts';
import { normYaw, rng, round } from './geometry.ts';
import type { Formation } from './layout.ts';
import { FORMATIONS } from './layout.ts';
import type { ClassifyState, CornerState, GuardLevel, GuardState, Mode, Role, TerrainClass, Threat } from './rules.ts';
import {
  CORNER_START, FormationAssignRules, FormationBypassRules, FormationCoverRules, FormationCornerRules, FormationGuardRules, FormationSectorRules, FormationShapeRules,
  FormationTerrainRules, FormationWallRules, GUARD_START, rolesFor, wallRoom,
} from './rules.ts';
import { FormationMenuRules, MENU_ITEMS } from './menu.ts';
import type { MenuChoice, MenuItemId } from './menu.ts';
import type { CustomShape } from './custom.ts';
import { CUSTOM_SAMPLE_SHAPE, FormationCustomRules } from './custom.ts';

export const GOLDEN_SCHEMA = 'codex.formation_golden/1';
export const GOLDEN_RULES = ['Slots', 'HedgehogSlots', 'LookYaw', 'Classify', 'PassageWidth', 'FormationFor', 'WallLayout', 'Menu',
  'Guard', 'TrailSlots', 'SideWidth', 'Bypass', 'Corner', 'Assign', 'TurnAhead', 'ClearLook', 'Cover', 'CoverStatic', 'Slide', 'Decided', 'WallEnd', 'Approach',
  'CustomSlots', 'CustomGuard', 'CustomLook', 'CustomPlace', 'RearCatchUp'] as const;
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
  // v2.3 part 8 ClearLook: the end of a wall to the right, 0.6 m ahead and 0.9 m aside - he walks past it (the anticipation).
  end_right_ahead: [{ a: [60, 90], b: [60, 1000], thickness: 20 }],
  // Red team v2.5 (16_4): a T stem's free tip 1 m straight ahead, end-on (the wall runs along the heading from x 120; its end face at
  // x 100) - the forward rays hit only that END face: at_wall on arrival (they used to see nothing: open).
  stem_tip_ahead: [{ a: [120, 0], b: [2000, 0], thickness: 40 }],
  // v2.6 (game designer 2026-11-01, Q1 = b): short obstacles beside the path are no side walls (classify.side_min_length 150 along the
  // heading, pieces within classify.side_join_gap 50 are one obstacle). A 60 cm pillar 2.4 m to the left (red team 14_1): open.
  pillar_left: [{ a: [-30, -240], b: [30, -240], thickness: 40 }],
  // v2.6: a 1.2 m stub along the path 2.5 m to the right: open.
  stub_right_120: [{ a: [-40, 250], b: [40, 250], thickness: 40 }],
  // v2.6: a 2 m wall 2.5 m to the left - long enough: wall_left.
  short_wall_left_200: [{ a: [-80, -250], b: [80, -250], thickness: 40 }],
  // v2.6: a wall of touching 1 m pieces on the left (each alone 1.2 m) is one 5.2 m obstacle: wall_left.
  pieces_left: [{ a: [-250, -260], b: [-150, -260], thickness: 20 }, { a: [-150, -260], b: [-50, -260], thickness: 20 },
    { a: [-50, -260], b: [50, -260], thickness: 20 }, { a: [50, -260], b: [150, -260], thickness: 20 }, { a: [150, -260], b: [250, -260], thickness: 20 }],
  // v2.6: posts 1.4 m apart on the right (gaps 1.1 m > the join gap): each a short obstacle - open.
  posts_right: [{ a: [-200, 260], b: [-200, 261], thickness: 30 }, { a: [-60, 260], b: [-60, 261], thickness: 30 },
    { a: [80, 260], b: [80, 261], thickness: 30 }, { a: [220, 260], b: [220, 261], thickness: 30 }],
  // v2.6: a long wall on the left and a pillar on the right: wall_left (no corridor for the pillar).
  pillar_in_corridor: [{ a: [-2000, -250], b: [2000, -250], thickness: 40 }, { a: [-30, 240], b: [30, 240], thickness: 40 }],
  // v2.6: a short stub touching a long wall (a T joint) belongs to the long obstacle: the side probes still see the long wall.
  stub_on_wall_right: [{ a: [-2000, 380], b: [2000, 380], thickness: 40 }, { a: [0, 360], b: [0, 260], thickness: 40 }],
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

  const menu = base('Menu', 'in: op (itemAt | release | resolve; 17.7: customAt | hitAt) + its inputs -> out (FormationMenuRules; screen px, y down, 12 = up). release also takes filled [bool x 3] + edit (absent = [false, false, false] / false; effect may be edit); resolve out.customSlot is absent in the old cases (= null) and written in the custom ones; hitAt = customAt ?? itemAt.');
  for (let deg = 1.3; deg < 360; deg += 7.5) for (const r of [20, 41, 120]) { // off the sector borders (22.5 + 45k): parity-safe
    const dx = round(Math.sin((deg * Math.PI) / 180) * r, 4), dy = round(-Math.cos((deg * Math.PI) / 180) * r, 4);
    menu.cases.push({ in: { op: 'itemAt', dx, dy, deadZonePx: 40 }, out: { item: FormationMenuRules.ItemAt(dx, dy, 40) } });
  }
  for (const highlighted of [null, ...MENU_ITEMS.map((i) => i.id)]) for (const choice of ['auto', 'column'] as MenuChoice[]) for (const memberCount of [1, 2, 4]) {
    const r = FormationMenuRules.Release({ open: true, choice, highlighted }, memberCount);
    menu.cases.push({ in: { op: 'release', state: { open: true, choice, highlighted }, memberCount }, out: r });
  }
  for (const choice of ['auto', ...MENU_ITEMS.map((i) => i.id)] as MenuChoice[]) for (const memberCount of [1, 2, 3, 4]) {
    const { customSlot: _none, ...old } = FormationMenuRules.Resolve(choice, memberCount); // the first cases predate customSlot (always null here)
    menu.cases.push({ in: { op: 'resolve', choice, memberCount }, out: old });
  }
  // --- 17.7 (the custom discs; appended after the old cases, which stay unchanged: the old resolve cases keep the old `out` without customSlot)
  const CUSTOM_PROBES: [number, number][] = [ // [dx, dy]: the disc centres, rings round them (inside / on / outside the radius), the gaps, the ring side
    [-100, 280], [0, 280], [100, 280], [-100, 319], [-100, 321], [-100, 241], [-60, 280], [-59, 280], [-140, 280], [-141, 280], [-39.9, 280], [-40.1, 280], [0, 240], [0, 320], [0, 320.5],
    [40, 280], [40.1, 280], [-40, 280], [60, 280], [59.9, 280], [140, 280], [140.5, 280], [80, 280], [-80, 280], [100, 250], [130, 300], [-130, 300], [0, 200], [0, 100], [250, 280], [0, 0],
  ];
  for (const [dx, dy] of CUSTOM_PROBES) menu.cases.push({ in: { op: 'customAt', dx, dy }, out: { item: FormationMenuRules.CustomItemAt(dx, dy) } });
  for (const [dx, dy] of CUSTOM_PROBES) menu.cases.push({ in: { op: 'hitAt', dx, dy, deadZonePx: 40 }, out: { item: FormationMenuRules.HitAt(dx, dy, 40) } });
  const FILLED: boolean[][] = [[false, false, false], [true, false, true], [false, true, false], [true, true, true]];
  for (const highlighted of ['custom_1', 'custom_2', 'custom_3'] as MenuItemId[]) for (const choice of ['auto', 'custom_2'] as MenuChoice[]) for (const memberCount of [1, 2, 4]) {
    for (const filled of FILLED) for (const edit of [false, true]) {
      const r = FormationMenuRules.Release({ open: true, choice, highlighted }, memberCount, filled, edit);
      menu.cases.push({ in: { op: 'release', state: { open: true, choice, highlighted }, memberCount, filled, edit }, out: r });
    }
  }
  for (const highlighted of [null, 'column', 'all_forward'] as (MenuItemId | null)[]) for (const edit of [false, true]) { // `edit` and `filled` change nothing off the custom discs
    const r = FormationMenuRules.Release({ open: true, choice: 'custom_1', highlighted }, 4, [true, true, true], edit);
    menu.cases.push({ in: { op: 'release', state: { open: true, choice: 'custom_1', highlighted }, memberCount: 4, filled: [true, true, true], edit }, out: r });
  }
  for (const choice of ['custom_1', 'custom_2', 'custom_3'] as MenuChoice[]) for (const memberCount of [1, 2, 3, 4]) {
    menu.cases.push({ in: { op: 'resolve', choice, memberCount }, out: FormationMenuRules.Resolve(choice, memberCount) });
  }
  for (const choice of ['auto', 'column', 'diamond', 'all_forward'] as MenuChoice[]) for (const memberCount of [2, 4]) {
    menu.cases.push({ in: { op: 'resolve', choice, memberCount }, out: FormationMenuRules.Resolve(choice, memberCount) });
  }

  // --- v2.1 (FORMATION_CONTRACT.md 10)
  const guard = base('Guard', 'in: walls, requested, memberCount, leaderPos, leaderYaw, ahead ({pos, yaw} | null), turnAhead (deg, v2.1.1), state {requested, level, effective, candidate, candidateDistance, cause (wall | turn | null; v2.3 part 9)}, stepDistance, allInSight (v2.3) -> out.blocked (BlockedRoles at the leader) + out.state (Resolve).');
  const guardFormations: Formation[] = ['diamond', 'line', 'column', 'echelon_right', 'echelon_left', 'hedgehog', 'blizzard'];
  for (const scene of GUARD_SCENES) {
    for (const [yaw, offset] of [[0, [0, 0]], [63, [500, -250]]] as [number, Vec2][]) {
      const placed = place(SCENES[scene], yaw, offset);
      const ahead = { pos: r2([offset[0] + Math.cos((yaw * Math.PI) / 180) * 500, offset[1] + Math.sin((yaw * Math.PI) / 180) * 500]), yaw };
      for (const requested of guardFormations) for (const memberCount of requested.startsWith('echelon') ? [3, 4] : [4]) {
        const states: GuardState[] = [
          GUARD_START,
          { requested, level: 2, effective: 'column', candidate: null, candidateDistance: 0, cause: 'wall' },
          { requested, level: 2, effective: 'column', candidate: 0, candidateDistance: hold - 30, cause: 'turn' },
        ];
        // v2.1.1: turnAhead 40 (> turn.fold_deg) only without the lookahead pose (keeps the file small).
        // v2.3: allInSight false (a member cut off) only with the lookahead pose (keeps the file small).
        for (const [aheadIn, turn, allInSight] of [[null, 0, true], [ahead, 0, true], [null, 40, true], [ahead, 0, false]] as [typeof ahead | null, number, boolean][]) for (const state of states) {
          const blocked = FormationGuardRules.BlockedRoles(placed, requested, memberCount, offset, yaw, config);
          const out = FormationGuardRules.Resolve(placed, requested, memberCount, offset, yaw, aheadIn, turn, state, 60, config, allInSight);
          guard.cases.push({ in: { scene, walls: placed.map(seg), requested, memberCount, leaderPos: r2(offset), leaderYaw: yaw, ahead: aheadIn, turnAhead: turn, state, stepDistance: 60, allInSight },
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
  const shoulderMax = config['look.over_shoulder_max_deg'];
  const clear = base('ClearLook', 'in: walls, pos, yaw, currentYaw, held (null = none), limit ({yaw, half} | null; v2.3 part 7), ahead ([x, y] | null; v2.3 part 8) -> out.yaw (FormationSectorRules.ClearLook: compare as angles).');
  for (const scene of ['none', 'wall_left', 'wall_right', 'corridor', 'ahead', 'corner_left_ahead', 'wall_end_behind_left', 'far_left', 'sliver_left', 'pocket', 'end_right_ahead']) {
    for (const [yaw0, offset] of [[0, [0, 0]], [63, [500, -250]]] as [number, Vec2][]) {
      const placed = place(SCENES[scene], yaw0, offset);
      // v2.3 part 8: his position look.anticipate_s ahead at 100 cm/s along the scene heading (rounded as written).
      const aheadPt = r2([offset[0] + Math.cos((yaw0 * Math.PI) / 180) * 100 * config['look.anticipate_s'], offset[1] + Math.sin((yaw0 * Math.PI) / 180) * 100 * config['look.anticipate_s']]);
      for (let rel = -165; rel <= 180; rel += 30) {
        const yaw = round(yaw0 + rel + 2.5);
        for (const turned of [0, 70, -70]) for (const heldRel of [null, -40, 100]) {
          const currentYaw = round(yaw + turned);
          const held = heldRel === null ? null : round(yaw + heldRel); // v2.3: the previous result (hysteresis)
          // v2.3 part 7: the over-the-shoulder limit (his travel = the scene's heading) only without a turn (keeps the file small).
          // v2.3 part 8: the anticipation (ahead) on the cases without a turn and without a limit.
          for (const limit of turned === 0 ? [null, { yaw: yaw0, half: shoulderMax }] : [null]) for (const ahead of turned === 0 && !limit ? [null, aheadPt] : [null]) {
            clear.cases.push({ in: { scene, walls: placed.map(seg), pos: r2(offset), yaw, currentYaw, held, limit, ahead },
              out: { yaw: round(FormationSectorRules.ClearLook(placed, offset, yaw, currentYaw, config, held, limit, ahead)) } });
          }
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

  // v2.6 (game designer 2026-11-01, Q2 = b): the lagging rear guard turns round to catch up (hysteresis move.rear_turn_lag / rear_turn_back).
  const rearG = base('RearCatchUp', 'in: op turn {lag (cm behind his slot along the heading), turned (the last answer), wayBlocked} -> out.turned (FormationSectorRules.RearCatchUp); op way {pos, slot, others [[x, y]]} -> out.blocked (FormationSectorRules.RearWayBlocked).');
  const lagKey = config['move.rear_turn_lag'], backKey = config['move.rear_turn_back'];
  for (const lag of [0, 20, backKey - 1, backKey, backKey + 1, 100, lagKey - 1, lagKey, lagKey + 1, 250, 600]) for (const turned of [false, true]) {
    for (const wayBlocked of [false, true]) {
      rearG.cases.push({ in: { op: 'turn', lag, turned, wayBlocked }, out: { turned: FormationSectorRules.RearCatchUp(lag, turned, wayBlocked, config) } });
    }
  }
  // The way: his slot 4 m ahead (+X, then rotated / moved); a mate on it, beside it within / beyond the reach, behind him, past the slot.
  const WAY_OTHERS: Vec2[][] = [[], [[200, 0]], [[200, 90]], [[200, 115]], [[200, -60]], [[-60, 0]], [[430, 0]], [[520, 0]], [[200, 130], [300, -10]]];
  for (const [yaw, offset] of [[0, [0, 0]], [-115, [640, -300]]] as [number, Vec2][]) {
    const tp = (p: Vec2): Vec2 => r2(place([{ a: p, b: p }], yaw, offset)[0].a);
    const pos = tp([0, 0]), slot = tp([400, 0]);
    for (const others of WAY_OTHERS) {
      const placed = others.map(tp);
      rearG.cases.push({ in: { op: 'way', pos, slot, others: placed }, out: { blocked: FormationSectorRules.RearWayBlocked(pos, slot, placed, config) } });
    }
  }

  // v2.2: the coverage allocator (walk / halt, fixed looks) and the static allocation round a standing group.
  const cover = base('Cover', 'in: walls, leaderPos, heading, members [{role, pos, back, travel?}] (K first; travel = world yaw, absent = the heading; v2.3 part 7), mode walk|halt, t, fixed [{role, yaw}] -> out {rearGuard, looks [{role, yaw}], shares [{role, from, to, primary}], shoulder [{role, yaw}]} (FormationCoverRules.Allocate; yaws compare as angles).');
  const COVER_SCENES = ['none', 'wall_left', 'wall_right', 'corridor', 'wide_street', 'gap_left', 'corner_left_ahead', 'far_left'];
  for (const scene of COVER_SCENES) for (const [yaw, offset] of [[0, [0, 0]], [-120, [800, 300]]] as [number, Vec2][]) {
    const placed = place(SCENES[scene], yaw, offset);
    for (const f of ['diamond', 'column', 'echelon_right', 'line'] as Formation[]) for (const n of [3, 4]) {
      const mem = FormationShapeRules.Slots(f, n, offset, yaw, config).map((x) => {
        const p = r2(x.pos), h: Vec2 = [Math.cos((yaw * Math.PI) / 180), Math.sin((yaw * Math.PI) / 180)];
        return { role: x.role, pos: p, back: round(-((p[0] - offset[0]) * h[0] + (p[1] - offset[1]) * h[1])) };
      });
      // v2.3 part 7: the last case gives the followers a travel direction 35 deg off the heading (the trail column at a bend).
      for (const [mode, t, fixed, bend] of [['halt', 0, [], false], ['walk', 0.7, [], false], ['walk', 2.9, [], false], ['walk', 1.3, [{ role: 'K', yaw: round(yaw - 95) }], false], ['walk', 2.1, [], true]] as ['walk' | 'halt', number, { role: Role; yaw: number }[], boolean][]) {
        const members = bend ? mem.map((m, k) => (k === 0 ? m : { ...m, travel: round(yaw + 35) })) : mem;
        const out = FormationCoverRules.Allocate(placed, offset, yaw, members, mode, t, config, fixed)!;
        cover.cases.push({ in: { scene, formation: f, walls: placed.map(seg), leaderPos: r2(offset), heading: yaw, members, mode, t, fixed },
          out: { rearGuard: out.rearGuard, looks: out.looks.map((l) => ({ role: l.role, yaw: round(l.yaw) })), shares: out.shares.map((x) => ({ ...x, primary: round(x.primary) })),
            shoulder: out.shoulder.map((x) => ({ role: x.role, yaw: round(x.yaw) })) } });
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

  // v2.4 (user bug report 2026-10-26): the wall layout's looks at a wall END (FormationWallRules.EndLooks). Scenes in the wall's
  // frame (the leader's wall slot near the origin); every scene is placed at three headings / offsets and the Layout slots for
  // 2-4 members are written ROUNDED (the outputs are computed from exactly those).
  const wallEnd = base('WallEnd', 'in: walls, slots [{role, pos, yaw}] (FormationWallRules.Layout, K first), fixed (roles whose looks stay besides K; missing = []) -> out {spread, basePct, pct, slots [{role, pos, yaw}]} (FormationWallRules.EndLooks; spread exactly, basePct / pct within 0.01, yaws as angles).');
  const END_SCENES: Record<string, { walls: Segment[]; wall: number; click: Vec2 }> = {
    // The user's report: a thin free-standing wall, the squad at its end face (the slot on the wall's axis, 45 cm off the face).
    end_face: { walls: [{ a: [-1500, 0], b: [0, 0], thickness: 40 }], wall: 0, click: [65, 0] },
    end_face_thin: { walls: [{ a: [-1500, 0], b: [0, 0], thickness: 20 }], wall: 0, click: [55, 0] },
    // A 3 m stub: the ring sees past its far end too.
    end_face_stub: { walls: [{ a: [-300, 0], b: [0, 0], thickness: 40 }], wall: 0, click: [65, 0] },
    // Wall - gap - wall on one line: at the end face of one wall, the next wall 2.5 m beyond the gap.
    end_face_gap: { walls: [{ a: [-1500, 0], b: [0, 0], thickness: 40 }, { a: [250, 0], b: [1800, 0], thickness: 40 }], wall: 0, click: [65, 0] },
    // A face (unchanged), a face whose left man stands past the wall's end, an inner corner (unchanged).
    face_long: { walls: [{ a: [0, -1500], b: [0, 1500], thickness: 40 }], wall: 0, click: [65, 0] },
    face_near_end: { walls: [{ a: [0, -60], b: [0, 1500], thickness: 40 }], wall: 0, click: [65, 0] },
    inner_corner: { walls: [{ a: [0, -1500], b: [0, 1500], thickness: 40 }, { a: [0, -150], b: [1500, -150], thickness: 40 }], wall: 0, click: [65, 0] },
  };
  for (const [scene, sc] of Object.entries(END_SCENES)) for (const [yaw, offset] of [[0, [0, 0]], [63, [500, -250]], [-140, [-1200, 800]]] as [number, Vec2][]) {
    const placed = place(sc.walls, yaw, offset);
    const click = r2(place([{ a: sc.click, b: sc.click }], yaw, offset)[0].a);
    for (const memberCount of [2, 3, 4]) {
      const room = wallRoom(placed[sc.wall], click);
      const slots = FormationWallRules.Layout(placed[sc.wall], click, memberCount, room.roomLeft, room.roomRight, config)
        .map((x) => ({ role: x.role, pos: r2(x.pos), yaw: round(x.yaw) }));
      // v2.4 + the corner stack: also with the second slot's look fixed (a look a higher rule decided - the stack's point man / rear man).
      for (const fixed of memberCount >= 3 ? [[], [slots[1].role]] : [[]]) {
        const out = FormationWallRules.EndLooks(placed, slots, config, fixed);
        wallEnd.cases.push({ in: { scene, walls: placed.map(seg), slots, fixed },
          out: { spread: out.spread, basePct: round(out.basePct), pct: round(out.pct), slots: out.slots.map((x) => ({ role: x.role, pos: r2(x.pos), yaw: round(x.yaw) })) } });
      }
    }
  }

  // v2.5 (user rule 2026-10-26): the covered approach to the wall (FormationWallRules.Approach). A wall face along Y at x 0 (outward +X;
  // inner_corner: a side wall at y -250 too), the Layout slots for 2-4 members, the members at stages of the approach (local frame),
  // placed at two headings; state null (the roles assigned from those positions) and the roles assigned at 'start' held.
  const approachG = base('Approach', 'in: walls, slots [{role, pos, yaw}] (Layout, K first), normalYaw, members [{role, pos, yaw}] (yaw = his look now), state ({runner, rear, flanks [{role, side}], alongSide, done} | null) -> out {state, looks [{role, duty, yaw (null = his travel)}]} (FormationWallRules.Approach; yaws as angles).');
  const APPROACH_WALLS: Record<string, Segment[]> = {
    face: [{ a: [0, -1500], b: [0, 1500], thickness: 40 }],
    inner_corner: [{ a: [0, -1500], b: [0, 1500], thickness: 40 }, { a: [0, -250], b: [1500, -250], thickness: 40 }],
  };
  const STAGE_POS: Record<string, Record<Role, Vec2 | 'slot'>> = {
    start: { K: [400, 0], P: [680, 260], L: [680, -260], Z: [960, 0] },
    runner_in: { K: 'slot', P: [520, 200], L: [520, -200], Z: [800, 0] },
    flank_in: { K: 'slot', P: 'slot', L: [300, -150], Z: [600, 0] },
    all_but_rear: { K: 'slot', P: 'slot', L: 'slot', Z: [400, 30] },
    all_in: { K: 'slot', P: 'slot', L: 'slot', Z: 'slot' },
    along_wall: { K: [65, -500], P: [325, -780], L: [-200, -780], Z: [65, -920] },
    all_forward: { K: [400, 0], P: [680, 260], L: [680, -260], Z: [960, 0] },
  };
  for (const [scene, ws] of Object.entries(APPROACH_WALLS)) for (const [yaw, offset] of [[0, [0, 0]], [-120, [800, 300]]] as [number, Vec2][]) {
    const placed = place(ws, yaw, offset);
    const tp = (p: Vec2) => r2(place([{ a: p, b: p }], yaw, offset)[0].a);
    for (const memberCount of [2, 3, 4]) {
      const slots = FormationWallRules.Layout(placed[0], tp([65, 0]), memberCount, 500, 500, config).map((x) => ({ role: x.role, pos: r2(x.pos), yaw: round(x.yaw) }));
      const normalYaw = round(normYaw(yaw));
      const at = (stage: string) => slots.map((s) => {
        const p = STAGE_POS[stage][s.role];
        // the triad's Z is the wedge's rear man; the pair's Z stands where P would (behind the right shoulder)
        const local: Vec2 | 'slot' = memberCount === 2 && s.role === 'Z' && p !== 'slot' && stage !== 'all_but_rear' ? (STAGE_POS[stage].P as Vec2 | 'slot') : p;
        // their looks now: the leader towards the wall, the patrol's flankers scanning, the rear man back (the pair's partner too); all_forward: everyone towards the wall
        const rel = stage === 'all_forward' ? (s.role === 'P' || (memberCount === 2 && s.role === 'Z') ? 170 : 180) : ({ K: 180, P: -135, L: 135, Z: 0 } as Record<Role, number>)[s.role];
        return { role: s.role, pos: local === 'slot' ? s.pos : r2(tp(local)), yaw: round(normYaw(yaw + rel)) };
      });
      const held = FormationWallRules.Approach(placed, slots, normalYaw, at('start'), null, config).state;
      for (const stage of Object.keys(STAGE_POS)) for (const state of stage === 'start' || stage === 'along_wall' || stage === 'all_forward' ? [null] : [null, held]) {
        const members = at(stage);
        const out = FormationWallRules.Approach(placed, slots, normalYaw, members, state, config);
        approachG.cases.push({ in: { scene, stage, walls: placed.map(seg), slots, normalYaw, members, state },
          out: { state: out.state, looks: out.looks.map((l) => ({ role: l.role, duty: l.duty, yaw: l.yaw === null ? null : round(l.yaw) })) } });
      }
    }
  }

  // --- FORMATION_CONTRACT.md 17 (the custom formation)
  const S = (...m: [number, number, number, number][]): CustomShape => ({ members: m.map(([squad, fwd, right, look]) => ({ squad, fwd, right, look })) });
  const CUSTOM_SHAPES: Record<string, CustomShape> = {
    anchor_only: S([0, 0, 0, 0]),
    pair: S([0, 0, 0, 0], [2, -312.4, 187.6, 33.3]),
    triad: S([0, 0, 0, 0], [1, -250, 300, 60], [3, -100, -410.5, -135]),
    sample: CUSTOM_SAMPLE_SHAPE,
    anchor_squad_2: S([2, 0, 0, 0], [0, -180, -220, -90], [3, -400, 100.2, 175.5]),
  };
  const HEADS: [Vec2, number][] = [[[0, 0], 0], [[1234.5, -678.25], 37.5], [[-300, 50.5], -120], [[0, 0], 180]];
  const customSlots = base('CustomSlots', 'in: name (informational), shape {members [{squad, fwd, right, look}]} (members[0] = the anchor), present [squad] (the living members, the leader FIRST then the followers in squad order), leaderPos, leaderYaw -> out.slots [{squad, role, pos, yaw, fixed}] (FormationCustomRules.CustomSlots; output order = the leader, then present order; present beyond 4 is cut; yaws compare as angles; empty shape or empty present -> []).');
  const pushSlots = (name: string, shape: CustomShape, present: number[], leaderPos: Vec2, leaderYaw: number) => {
    const slotsOut = FormationCustomRules.CustomSlots(shape, present, leaderPos, leaderYaw, config).map((s) => ({ squad: s.squad, role: s.role, pos: r2(s.pos), yaw: round(s.yaw), fixed: s.fixed }));
    customSlots.cases.push({ in: { name, shape, present, leaderPos, leaderYaw }, out: { slots: slotsOut } });
  };
  // the leader (any of squads 0..3) x who else is alive (every subset of the other three, squad order); a shape with the leader as the
  // anchor / another stored member / not stored, a stored member dead, extras
  const presents: number[][] = [];
  for (const leader of [0, 1, 2, 3]) {
    const rest = [0, 1, 2, 3].filter((q) => q !== leader);
    for (let mask = 0; mask < 8; mask++) presents.push([leader, ...rest.filter((_, k) => mask & (1 << k))]);
  }
  let headIdx = 0;
  for (const [name, shape] of Object.entries(CUSTOM_SHAPES)) for (const present of presents) {
    if (name === 'sample') for (const [pos, yaw] of HEADS.slice(0, 3)) pushSlots(name, shape, present, pos, yaw);
    else { const [pos, yaw] = HEADS[headIdx++ % HEADS.length]; pushSlots(name, shape, present, pos, yaw); }
  }
  for (const [name, shape, present] of [
    ['sample_five_present', CUSTOM_SAMPLE_SHAPE, [0, 1, 2, 3, 4]], ['sample_five_leader_3', CUSTOM_SAMPLE_SHAPE, [3, 0, 1, 2, 4]],
    ['sample_extra_4', CUSTOM_SAMPLE_SHAPE, [0, 4, 1]], ['sample_leader_not_stored', CUSTOM_SAMPLE_SHAPE, [4, 0, 1]],
    ['sample_leader_not_stored_anchor_dead', CUSTOM_SAMPLE_SHAPE, [4, 1, 2, 3]], ['sample_all_extras', CUSTOM_SAMPLE_SHAPE, [5, 6, 7]],
    ['pair_two_extras', CUSTOM_SHAPES.pair, [1, 3, 4]], ['anchor2_leader_not_stored', CUSTOM_SHAPES.anchor_squad_2, [1, 0, 2, 3]],
    ['empty_shape', { members: [] }, [0, 1, 2]], ['empty_shape_alone', { members: [] }, [0]], ['empty_present', CUSTOM_SAMPLE_SHAPE, []],
  ] as [string, CustomShape, number[]][]) pushSlots(name, shape, present, ...HEADS[1]);

  const customGuard = base('CustomGuard', 'in: name (informational), walls, shape, present (leader first), leaderPos, leaderYaw, ahead ({pos, yaw} | null), state {requested, level, effective, candidate, candidateDistance, cause (wall | null)}, stepDistance, allInSight -> out.blocked (BlockedRoles at the leader pose for CustomSlots) + out.state (FormationCustomRules.Resolve: requested custom, effective custom at level 0 / column at level 2, cause wall | null).');
  const CUSTOM_SCENES: Record<string, Segment[]> = {
    none: SCENES.none, wall_left: SCENES.wall_left, wall_right: SCENES.wall_right, corridor: SCENES.corridor, entrance_ahead: SCENES.entrance_ahead, far_left: SCENES.far_left,
    narrow: [{ a: [-2000, -130], b: [2000, -130], thickness: 40 }, { a: [-2000, 130], b: [2000, 130], thickness: 40 }],
    behind: [{ a: [-200, -600], b: [-200, 600], thickness: 20 }],
  };
  const heldWall = (candidate: GuardLevel | null, candidateDistance: number, requested: Formation = 'custom', level: GuardLevel = 2): GuardState =>
    ({ requested, level, effective: level === 0 ? requested : 'column', candidate, candidateDistance, cause: level === 0 ? null : 'wall' });
  const guardStates: GuardState[] = [GUARD_START, heldWall(null, 0), heldWall(0, hold - 30)];
  const pushGuard = (scene: string, walls: Segment[], present: number[], offset: Vec2, yaw: number, aheadIn: { pos: Vec2; yaw: number } | null, state: GuardState, stepDistance: number, allInSight: boolean) => {
    const slotsAt = FormationCustomRules.CustomSlots(CUSTOM_SAMPLE_SHAPE, present, offset, yaw, config);
    const blocked = FormationCustomRules.BlockedRoles(walls, slotsAt, offset, config);
    const out = FormationCustomRules.Resolve(walls, CUSTOM_SAMPLE_SHAPE, present, offset, yaw, aheadIn, state, stepDistance, config, allInSight);
    customGuard.cases.push({ in: { name: scene, walls: walls.map(seg), shape: CUSTOM_SAMPLE_SHAPE, present, leaderPos: r2(offset), leaderYaw: yaw, ahead: aheadIn, state, stepDistance, allInSight },
      out: { blocked, state: { ...out, candidateDistance: round(out.candidateDistance) } } });
  };
  for (const [scene, scn] of Object.entries(CUSTOM_SCENES)) for (const [yaw, offset] of [[0, [0, 0]], [63, [500, -250]]] as [number, Vec2][]) {
    const placed = place(scn, yaw, offset);
    const ahead = { pos: r2([offset[0] + Math.cos((yaw * Math.PI) / 180) * 500, offset[1] + Math.sin((yaw * Math.PI) / 180) * 500]), yaw };
    for (const present of [[0, 1, 2, 3], [1, 0, 2]]) {
      for (const [aheadIn, allInSight] of [[null, true], [ahead, true], [ahead, false]] as [typeof ahead | null, boolean][]) {
        for (const state of guardStates) pushGuard(scene, placed, present, offset, yaw, aheadIn, state, 60, allInSight);
      }
    }
  }
  // the hold: waiting for level 0 (the distance walked so far + the step), a step of 0 / negative, the exact release, a new request
  for (const scene of ['none', 'wall_left']) {
    const placed = place(CUSTOM_SCENES[scene], 0, [0, 0]);
    for (const [state, step] of [
      [heldWall(0, 100), 60], [heldWall(0, 100), 0], [heldWall(0, 100), -20], [heldWall(null, 100), 60], [heldWall(0, hold - 30), 30], [heldWall(0, hold - 30), 29.5],
      [heldWall(0, 0, 'column'), 60], [heldWall(0, 0, 'custom', 0), 60], [heldWall(0, 0, 'custom', 0), 6],
    ] as [GuardState, number][]) pushGuard(scene, placed, [0, 1, 2, 3], [0, 0], 0, null, state, step, true);
  }

  const customLook = base('CustomLook', "in: name (informational), walls, pos, yaw, currentYaw, held (null = none) -> out.yaw (FormationCustomRules.Look: the player's yaw kept unless it points into a wall within look.wall_dist / look.wall_angle, then ClearLook(walls, pos, yaw, currentYaw, held) without limit / ahead; compare as angles).");
  for (const scene of ['none', 'wall_left', 'corridor', 'ahead', 'pocket', 'sliver_left', 'corner_left_ahead']) {
    for (const [yaw0, offset] of [[0, [0, 0]], [63, [500, -250]]] as [number, Vec2][]) {
      const placed = place(SCENES[scene], yaw0, offset);
      for (const rel of [-150, -90, -30, 30, 90, 150]) {
        const yaw = round(yaw0 + rel + 2.5);
        for (const [turned, heldRel] of [[0, null], [70, null], [0, -40]] as [number, number | null][]) {
          const currentYaw = round(yaw + turned);
          const held = heldRel === null ? null : round(yaw + heldRel);
          customLook.cases.push({ in: { name: scene, walls: placed.map(seg), pos: r2(offset), yaw, currentYaw, held },
            out: { yaw: round(FormationCustomRules.Look(placed, offset, yaw, currentYaw, config, held)) } });
        }
      }
    }
  }

  const customPlace = base('CustomPlace', 'in: op (dragLook | canPlace | shapeFrom) + its inputs. dragLook: ghostPos, cursorPos, defaultYaw -> out.yaw (compare as angles). canPlace: ghosts [{squad, pos}], anchorSquad, squad, pos -> out.result (ok | too_far | too_close). shapeFrom: ghosts [{squad, pos, yaw}], anchorSquad -> out.shape {members [{squad, fwd, right, look}]} (RoundShape applied: 0.1 cm / 0.1 deg; look compares as an angle; empty members = no anchor ghost).');
  // dragLook: a click (< 50 cm), the exact threshold, a drag in several directions
  const DRAG_DIRS: [number, number[]][] = [[0, [0]], [10, [30]], [49.9, [-60]], [50, [0, 90, 180, -90]], [50.1, [0, 90, 180, -90]], [300, [0, 45, 90, 135, 180, -135, -90, -45]]];
  const dragCase = (ghostPos: Vec2, d: number, dirDeg: number, defaultYaw: number) => {
    const cursorPos = r2([ghostPos[0] + Math.cos((dirDeg * Math.PI) / 180) * d, ghostPos[1] + Math.sin((dirDeg * Math.PI) / 180) * d]);
    customPlace.cases.push({ in: { op: 'dragLook', ghostPos, cursorPos, defaultYaw }, out: { yaw: round(FormationCustomRules.DragLook(ghostPos, cursorPos, defaultYaw)) } });
  };
  for (const defaultYaw of [0, -135, 180]) for (const [d, dirs] of DRAG_DIRS) for (const dirDeg of dirs) dragCase([100, 100], d, dirDeg, defaultYaw);
  for (const dirDeg of DRAG_DIRS[5][1]) dragCase([-420.5, 77.25], 300, dirDeg, 37.5);
  // canPlace: ghosts (squad 0 = the anchor unless said otherwise) x a placement: new / moved / the anchor moved / too close / too far
  type Ghost = { squad: number; pos: Vec2 };
  const g = (squad: number, x: number, y: number): Ghost => ({ squad, pos: [x, y] });
  const GHOST_SETS: [string, Ghost[], number[]][] = [ // [name, ghosts, anchor squads to try]
    ['none', [], [0]], ['anchor', [g(0, 0, 0)], [0]], ['three', [g(0, 0, 0), g(1, -250, 300), g(2, -250, -300)], [0, 2]],
    ['four', [g(0, 0, 0), g(1, -250, 300), g(2, -250, -300), g(3, -450, 0)], [0, 2]], ['far_one', [g(0, 0, 0), g(1, 900, 0)], [0]], ['far_two', [g(0, 0, 0), g(1, 3900, 0)], [0]], ['no_anchor', [g(1, -250, 300), g(2, -250, -300)], [0]],
  ];
  const PLACEMENTS: [number, number, number][] = [ // [squad, x, y]
    [1, -250, 300], [1, -250, 340], [1, 10, 10], [1, 1000, 0], [1, 1000.1, 0], [2, 79.9, 0], [2, 80.1, 0], [3, -450, 20], [0, 0, 0], [0, 600, 0], [0, 1500, 0], [4, 1000.1, 0],
    [1, 1500, 0], [1, 4000, 0], [1, 4000.1, 0], [0, 4500, 0], [4, 4000.1, 0], [0, -200, 0],
  ];
  for (const [setName, ghosts, anchors] of GHOST_SETS) for (const anchorSquad of anchors) for (const [squad, x, y] of PLACEMENTS) {
    const pos: Vec2 = [x, y];
    customPlace.cases.push({ in: { op: 'canPlace', set: setName, ghosts, anchorSquad, squad, pos }, out: { result: FormationCustomRules.CanPlace(ghosts, anchorSquad, squad, pos, config) } });
  }
  // shapeFrom: hand-made (ordering, the cut at 4, the anchor squad, the look wrap, the rounding) + seeded random scenes
  const gy = (squad: number, x: number, y: number, yaw: number) => ({ squad, pos: [x, y] as Vec2, yaw });
  const SHAPE_CASES: [string, { squad: number; pos: Vec2; yaw: number }[], number][] = [
    ['anchor_only', [gy(0, 100, 100, 30)], 0],
    ['sample_world', [gy(0, 0, 0, 0), gy(1, -250, 300, 60), gy(2, -250, -300, -60), gy(3, -450, 0, 180)], 0],
    ['sample_rotated', [gy(3, 1000, 2000, 90), gy(0, 2000, 500, 90), gy(1, 1800, 500, 90), gy(2, 2000, 700, 0)], 3],
    ['scrambled_order', [gy(2, -100, 50, 10), gy(0, 0, 0, 0), gy(3, -200, -30, -170), gy(1, -50, 120, 95)], 0],
    ['anchor_squad_2', [gy(0, 130.25, -88.5, 12), gy(2, 500, 500, 45), gy(3, 420, 600.75, 45)], 2],
    ['five_ghosts_cut', [gy(0, 0, 0, 0), gy(1, -100, 0, 0), gy(2, -200, 0, 0), gy(3, -300, 0, 0), gy(4, -400, 0, 0)], 0],
    ['five_ghosts_anchor_4', [gy(0, -400, 0, 0), gy(1, -100, 0, 0), gy(2, -200, 0, 0), gy(3, -300, 0, 0), gy(4, 0, 0, 0)], 4],
    ['look_wrap', [gy(0, 0, 0, 170), gy(1, 0, 100, -170), gy(2, 0, -100, 170), gy(3, 50, 50, 10)], 0],
    ['look_180', [gy(0, 0, 0, -10), gy(1, -100, 0, 170), gy(2, -100, 50, 170.04), gy(3, -100, -50, 169.96)], 0],
    ['rounding_half_away', [gy(0, 0, 0, 0), gy(1, 0.05, -0.05, 0.05), gy(2, 0.15, -0.15, -0.15), gy(3, 123.456, -123.456, 10.04)], 0],
    ['no_anchor', [gy(1, 0, 0, 0), gy(2, 100, 0, 0)], 0],
    ['empty', [], 0],
  ];
  for (const [name, ghosts, anchorSquad] of SHAPE_CASES) customPlace.cases.push({ in: { op: 'shapeFrom', name, ghosts, anchorSquad }, out: { shape: FormationCustomRules.ShapeFrom(ghosts, anchorSquad) } });
  const rand = rng(1601);
  for (let i = 0; i < 24; i++) {
    const squads = [0, 1, 2, 3].filter(() => rand() < 0.8);
    if (!squads.length) squads.push(0);
    const ghosts = squads.map((squad) => gy(squad, round((rand() - 0.5) * 3000, 1), round((rand() - 0.5) * 3000, 1), round((rand() - 0.5) * 720, 1)));
    for (let k = ghosts.length - 1; k > 0; k--) { const j = Math.floor(rand() * (k + 1)); [ghosts[k], ghosts[j]] = [ghosts[j], ghosts[k]]; } // the input order is scrambled
    const anchorSquad = squads[Math.floor(rand() * squads.length)];
    customPlace.cases.push({ in: { op: 'shapeFrom', name: `random_${i}`, ghosts, anchorSquad }, out: { shape: FormationCustomRules.ShapeFrom(ghosts, anchorSquad) } });
  }

  return { RearCatchUp: rearG, CustomSlots: customSlots, CustomGuard: customGuard, CustomLook: customLook, CustomPlace: customPlace, Approach: approachG, WallEnd: wallEnd, Slots: slots, HedgehogSlots: hedgehog, LookYaw: look, Menu: menu, Classify: classify, PassageWidth: passage, FormationFor: formationFor, WallLayout: wall,
    Guard: guard, TrailSlots: trail, SideWidth: side, Bypass: bypass, Corner: corner, Assign: assign, TurnAhead: turnAhead, ClearLook: clear, Cover: cover, CoverStatic: coverStatic, Slide: slide, Decided: decided };
}

export const goldenFileName = (rule: GoldenRule): string => `formation_${rule}.json`;
export const goldenText = (file: GoldenFile): string => JSON.stringify(file, null, 1) + '\n';

/** Line endings normalised (a Windows checkout with core.autocrlf may hold CRLF). */
export const sameGoldenText = (onDisk: string, expected: string): boolean => onDisk.replace(/\r\n/g, '\n') === expected;
