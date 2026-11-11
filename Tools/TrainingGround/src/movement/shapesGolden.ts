// Training Ground / movement: golden vectors for the saved formation shapes (FORMATION_CONTRACT.md 19; formation editor 008).
// C++ CodexTactics.Formation.Parity.SavedShapes reads Tools/TrainingGround/golden/formation_SavedShapes.json, parses the same fixtures
// (Content/Data/FormationLab/fixtures/formation_shapes_*.json) with its own reader (FormationShapes::Parse) and must reproduce every
// "out": the shape the simulator takes for (formation, member count) and its slots / looks / arcs (sim.ts savedShapeTargets), and the
// editor's mirror (shapesCoverage.ts mirrorFormationShape). Deterministic: the same fixtures give a byte-identical file.
// Regenerate: `node src/movement/cli/shapesGolden.ts` (--check only compares).

import path from 'node:path';
import type { Vec2 } from './geometry.ts';
import { round } from './geometry.ts';
import { getSavedFormationShape, savedShapeTargets } from './sim.ts';
import { mirrorFormationShape } from './shapesCoverage.ts';
import { readFormationShapesFile, type EditableFormation, type FormationShapesFile } from './shapesSchema.ts';

export const SHAPES_GOLDEN_RULE = 'SavedShapes';
export const SHAPES_GOLDEN_FILE = `formation_${SHAPES_GOLDEN_RULE}.json`;
/** The fixtures the cases are built from (Content/Data/FormationLab/fixtures). */
export const SHAPES_GOLDEN_FIXTURES = ['formation_shapes_diamond4.json', 'formation_shapes_echelon_mirror.json'] as const;

/**
 * The lookups checked: every built-in shape the editor offers, for 2..4 members. 'pair' is left out on purpose - the C++ maps the
 * editor's pair onto the game's pair (wedge / diamond with 2 members, FORMATION_CONTRACT.md 19.2), the simulator's lookup onto 'pair'
 * and a 2-man column; the fixtures hold no pair either way.
 */
const LOOKUP_FORMATIONS: readonly EditableFormation[] = ['diamond', 'wedge', 'line', 'echelon_left', 'echelon_right', 'column'];
const LEADER_POSES: readonly { pos: Vec2; yaw: number }[] = [
  { pos: [0, 0], yaw: 0 },
  { pos: [1234.5, -678.25], yaw: 37.5 },
  { pos: [1234.5, -678.25], yaw: -120 },
  { pos: [-300, 450], yaw: 180 },
];

export interface ShapesGoldenFile {
  schema: string;
  rule: string;
  tolerance: { cm: number; deg: number };
  fixtures: string[];
  notes: string;
  cases: { in: unknown; out: unknown }[];
}

const r2 = (v: Vec2): Vec2 => [round(v[0]), round(v[1])];

/** fixtures: file name -> its parsed document (the SHAPES_GOLDEN_FIXTURES). */
export function buildShapesGolden(fixtures: Record<string, FormationShapesFile>): ShapesGoldenFile {
  const file: ShapesGoldenFile = {
    schema: 'codex.formation_golden/1',
    rule: SHAPES_GOLDEN_RULE,
    tolerance: { cm: 0.5, deg: 0.5 },
    fixtures: [...SHAPES_GOLDEN_FIXTURES],
    notes: 'op slots: in fixture, formation, memberCount, leaderPos, leaderYaw -> out.slots [{role, pos, yaw, arc}] in the shape\'s member order (null = no saved shape: the formula); op mirror: in fixture, formation, memberCount -> out {formation, members [{role, fwd, right, look, arc}]}.',
    cases: [],
  };
  for (const name of SHAPES_GOLDEN_FIXTURES) {
    const doc = fixtures[name];
    if (!doc) throw new Error(`fixture ${name} missing`);
    for (const formation of LOOKUP_FORMATIONS) for (const memberCount of [2, 3, 4]) {
      const shape = getSavedFormationShape(doc, formation, memberCount);
      for (const pose of shape ? LEADER_POSES : LEADER_POSES.slice(0, 1)) {
        const slots = shape
          ? savedShapeTargets(shape, pose.pos, pose.yaw).map((s) => ({ role: s.role, pos: r2(s.pos), yaw: round(s.yaw), arc: s.arc }))
          : null;
        file.cases.push({ in: { op: 'slots', fixture: name, formation, memberCount, leaderPos: pose.pos, leaderYaw: pose.yaw }, out: { slots } });
      }
    }
    for (const [formation, byCount] of Object.entries(doc.formations ?? {})) {
      for (const [countKey, shape] of Object.entries(byCount ?? {})) {
        if (!shape) continue;
        const mirrored = mirrorFormationShape(formation as EditableFormation, shape);
        file.cases.push({
          in: { op: 'mirror', fixture: name, formation, memberCount: Number(countKey) },
          out: {
            formation: mirrored.formation,
            members: mirrored.shape.members.map((m) => ({ role: m.role, fwd: m.fwd, right: m.right, look: m.look, arc: m.arc ?? null })),
          },
        });
      }
    }
  }
  return file;
}

/** Reads the SHAPES_GOLDEN_FIXTURES from labDir/fixtures (labDir = Content/Data/FormationLab). */
export function readShapesFixtures(labDir: string): Record<string, FormationShapesFile> {
  const out: Record<string, FormationShapesFile> = {};
  for (const name of SHAPES_GOLDEN_FIXTURES) out[name] = readFormationShapesFile(path.join(labDir, 'fixtures', name));
  return out;
}

export const shapesGoldenText = (file: ShapesGoldenFile): string => JSON.stringify(file, null, 1) + '\n';
