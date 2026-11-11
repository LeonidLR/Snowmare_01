// The saved-shape golden vectors stay in step with the fixtures and the simulator (FORMATION_CONTRACT.md 19): a change to
// sim.ts savedShapeTargets / getSavedFormationShape, mirrorFormationShape or a fixture without regenerating fails here
// (`node src/movement/cli/shapesGolden.ts`), before the C++ parity test CodexTactics.Formation.Parity.SavedShapes sees a stale file.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { sameGoldenText } from './golden.ts';
import { SHAPES_GOLDEN_FILE, buildShapesGolden, readShapesFixtures, shapesGoldenText } from './shapesGolden.ts';
import { GOLDEN_DIR, LAB_DIR } from './cli/paths.ts';

test('formation_SavedShapes.json is up to date (run node src/movement/cli/shapesGolden.ts)', () => {
  const file = path.join(GOLDEN_DIR, SHAPES_GOLDEN_FILE);
  assert.ok(fs.existsSync(file), `${SHAPES_GOLDEN_FILE} missing`);
  assert.ok(sameGoldenText(fs.readFileSync(file, 'utf8'), shapesGoldenText(buildShapesGolden(readShapesFixtures(LAB_DIR)))), `${SHAPES_GOLDEN_FILE} is stale`);
});

test('saved-shape golden: the diamond-4 fixture moves the slots off the formula and the echelon pair are mirrors', () => {
  const golden = buildShapesGolden(readShapesFixtures(LAB_DIR));
  type SlotsCase = { in: { op: string; fixture: string; formation: string; memberCount: number; leaderYaw: number }; out: { slots: unknown[] | null; formation?: string; members?: unknown[] } };
  const cases = golden.cases as SlotsCase[];
  const d4 = cases.find((c) => c.in.op === 'slots' && c.in.fixture === 'formation_shapes_diamond4.json' && c.in.formation === 'diamond' && c.in.memberCount === 4 && c.in.leaderYaw === 0);
  assert.ok(d4?.out.slots, 'diamond-4 found');
  assert.deepEqual((d4!.out.slots as { pos: number[] }[]).map((s) => s.pos), [[0, 0], [-280, 260], [-280, -260], [-560, 0]]);
  const miss = cases.find((c) => c.in.op === 'slots' && c.in.fixture === 'formation_shapes_diamond4.json' && c.in.formation === 'diamond' && c.in.memberCount === 3);
  assert.equal(miss?.out.slots, null, 'diamond-3 not saved -> the formula');
  const right = readShapesFixtures(LAB_DIR)['formation_shapes_echelon_mirror.json'].formations.echelon_right!['4'].members;
  const mirror = cases.find((c) => c.in.op === 'mirror' && c.in.formation === 'echelon_left');
  assert.equal(mirror?.out.formation, 'echelon_right');
  assert.deepEqual(mirror?.out.members, right.map((m) => ({ role: m.role, fwd: m.fwd, right: m.right, look: m.look, arc: m.arc ?? null })));
});
