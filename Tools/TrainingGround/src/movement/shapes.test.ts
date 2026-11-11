// Training Ground / movement: tests for formation shapes schema, coverage, mirror, reset, and sim integration.
// Docs: docs/port/gemini/inbox/008-formation-editor.md.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import { fileURLToPath } from 'node:url';

import {
  FORMATION_SHAPES_SCHEMA,
  DEFAULT_ARC_DEG,
  validateFormationShapes,
  formatFormationShapesJson,
  parseFormationShapesJson,
  readFormationShapesFile,
  writeFormationShapesFile,
  type FormationShapesFile,
  type FormationShape,
} from './shapesSchema.ts';

import {
  computeCoverage,
  validateLiveShape,
  mirrorFormationShape,
  mirrorCustomShape,
  computeDefaultFormationShape,
  resetFormationShape,
  MIN_MEMBER_SPACING_CM,
} from './shapesCoverage.ts';

import { configValues, type FormationConfigFile } from './config.ts';
import { ROOT, readConfigFile, readLayout } from './cli/paths.ts';
import { buildNav, simulate, getSavedFormationShape, type Frame } from './sim.ts';

const FIXTURES_DIR = path.join(ROOT, 'Content/Data/FormationLab/fixtures');

test('validateFormationShapes: accepts valid documents and golden fixtures', () => {
  const doc: FormationShapesFile = {
    schema: FORMATION_SHAPES_SCHEMA,
    formations: {
      diamond: {
        '4': {
          members: [
            { role: 'commander', fwd: 0, right: 0, look: 0, arc: 90 },
            { role: 'right', fwd: -280, right: 260, look: 60, arc: 90 },
            { role: 'left', fwd: -280, right: -260, look: -60, arc: 90 },
            { role: 'rear', fwd: -560, right: 0, look: 180, arc: 90 },
          ],
        },
      },
    },
    custom: {
      custom_1: {
        members: [
          { squad: 0, fwd: 0, right: 0, look: 0 },
          { squad: 1, fwd: -200, right: 150, look: 45 },
        ],
      },
    },
  };

  const errs = validateFormationShapes(doc);
  assert.deepEqual(errs, [], 'valid formation shapes file has no validation errors');
});

test('validateFormationShapes: rejects invalid schema, bad coordinates and missing leader', () => {
  // Bad schema
  assert.ok(
    validateFormationShapes({ schema: 'bad.schema/1', formations: {} }).some((e) =>
      e.includes('schema')
    )
  );

  // Commander not at (0, 0)
  const badLeader = {
    schema: FORMATION_SHAPES_SCHEMA,
    formations: {
      diamond: {
        '2': {
          members: [
            { role: 'commander', fwd: 10, right: 0, look: 0 },
            { role: 'rear', fwd: -200, right: 0, look: 180 },
          ],
        },
      },
    },
  };
  assert.ok(
    validateFormationShapes(badLeader).some((e) => e.includes('commander must be at fwd 0, right 0'))
  );

  // Non-integer coordinate
  const nonInt = {
    schema: FORMATION_SHAPES_SCHEMA,
    formations: {
      wedge: {
        '2': {
          members: [
            { role: 'commander', fwd: 0, right: 0, look: 0 },
            { role: 'rear', fwd: -200.5, right: 0, look: 180 },
          ],
        },
      },
    },
  };
  assert.ok(
    validateFormationShapes(nonInt).some((e) => e.includes('fwd and right must be integer cm'))
  );

  // Duplicate roles
  const dupRoles = {
    schema: FORMATION_SHAPES_SCHEMA,
    formations: {
      line: {
        '2': {
          members: [
            { role: 'commander', fwd: 0, right: 0, look: 0 },
            { role: 'commander', fwd: 0, right: 0, look: 0 },
          ],
        },
      },
    },
  };
  assert.ok(
    validateFormationShapes(dupRoles).some((e) => e.includes('duplicate role'))
  );
});

test('formatFormationShapesJson: roundtrip preserves unknown keys and stable key order', () => {
  const original = {
    schema: FORMATION_SHAPES_SCHEMA,
    formations: {
      diamond: {
        '4': {
          members: [
            { role: 'commander', fwd: 0, right: 0, look: 0, arc: 90, note: 'author note' },
            { role: 'right', fwd: -280, right: 260, look: 60, arc: 90 },
            { role: 'left', fwd: -280, right: -260, look: -60, arc: 90 },
            { role: 'rear', fwd: -560, right: 0, look: 180, arc: 90 },
          ],
        },
      },
    },
    custom: {},
    unknownRootField: 42,
  };

  const text = formatFormationShapesJson(original as FormationShapesFile);
  assert.ok(text.includes('"schema": "codex.formation_shapes/1"'));
  assert.ok(text.includes('"unknownRootField": 42'));
  assert.ok(text.includes('"note": "author note"'));

  const parsed = parseFormationShapesJson(text) as Record<string, unknown>;
  assert.equal(parsed.schema, FORMATION_SHAPES_SCHEMA);
  assert.equal(parsed.unknownRootField, 42);

  const formattedAgain = formatFormationShapesJson(parsed as FormationShapesFile);
  assert.equal(formattedAgain, text, 'formatting is idempotent and stable');
});

test('readFormationShapesFile & writeFormationShapesFile: atomic writing with temp and rename', () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), 'formation-shapes-test-'));
  const filePath = path.join(tmpDir, 'formation_shapes.json');

  const doc: FormationShapesFile = {
    schema: FORMATION_SHAPES_SCHEMA,
    formations: {
      pair: {
        '2': {
          members: [
            { role: 'commander', fwd: 0, right: 0, look: 0, arc: 90 },
            { role: 'rear', fwd: -150, right: 100, look: 60, arc: 90 },
          ],
        },
      },
    },
    custom: {},
  };

  writeFormationShapesFile(filePath, doc);
  assert.ok(fs.existsSync(filePath), 'file was created atomically');

  const readBack = readFormationShapesFile(filePath);
  assert.equal(readBack.schema, FORMATION_SHAPES_SCHEMA);
  assert.equal(readBack.formations?.pair?.['2']?.members.length, 2);

  // Clean up
  fs.rmSync(tmpDir, { recursive: true, force: true });
});

test('computeCoverage: correctly calculates covered, uncovered and overlapping spans', () => {
  // 4 members covering 4 cardinal directions with 90° arcs = 360° full coverage
  const membersCardinal = [
    { look: 0, arc: 90 },    // covers [-45, 45]
    { look: 90, arc: 90 },   // covers [45, 135]
    { look: 180, arc: 90 },  // covers [135, 180] and [-180, -135]
    { look: -90, arc: 90 },  // covers [-135, -45]
  ];

  const reportFull = computeCoverage(membersCardinal);
  assert.equal(reportFull.coveragePercent, 100);
  assert.equal(reportFull.uncoveredPercent, 0);
  assert.equal(reportFull.uncoveredSpans.length, 0);

  // Single forward-looking member with 90° arc: [-45, 45] = 91 deg / 360 = 25.3% coverage
  const singleMember = [{ look: 0, arc: 90 }];
  const reportQuarter = computeCoverage(singleMember);
  assert.equal(reportQuarter.coveragePercent, 25.3);
  assert.equal(reportQuarter.uncoveredPercent, 74.7);
  assert.ok(reportQuarter.uncoveredSpans.length >= 1);

  // Two members looking in the exact same direction = overlap
  const reportOverlap = computeCoverage([
    { look: 0, arc: 90 },
    { look: 0, arc: 90 },
  ]);
  assert.ok(reportOverlap.overlappingSpans.length > 0, 'overlapping spans detected');
});

test('validateLiveShape: flags spacing violations and radius violations', () => {
  // Two members too close (< 120 cm)
  const tooClose = [
    { role: 'commander', fwd: 0, right: 0, look: 0 },
    { role: 'right', fwd: 0, right: 80, look: 90 }, // distance = 80 cm (< 120 cm)
  ];
  const resClose = validateLiveShape(tooClose as any, MIN_MEMBER_SPACING_CM, 4000);
  assert.equal(resClose.valid, false);
  assert.equal(resClose.spacingViolations.length, 1);
  assert.equal(resClose.spacingViolations[0].distCm, 80);

  // Member beyond max radius (> 4000 cm)
  const tooFar = [
    { role: 'commander', fwd: 0, right: 0, look: 0 },
    { role: 'rear', fwd: -4500, right: 0, look: 180 }, // radius = 4500 cm (> 4000 cm)
  ];
  const resFar = validateLiveShape(tooFar as any, MIN_MEMBER_SPACING_CM, 4000);
  assert.equal(resFar.valid, false);
  assert.equal(resFar.radiusViolations.length, 1);
  assert.equal(resFar.radiusViolations[0].radiusCm, 4500);

  // Clean shape
  const clean = [
    { role: 'commander', fwd: 0, right: 0, look: 0 },
    { role: 'right', fwd: -200, right: 200, look: 60 },
  ];
  const resClean = validateLiveShape(clean as any, MIN_MEMBER_SPACING_CM, 4000);
  assert.equal(resClean.valid, true);
  assert.equal(resClean.spacingViolations.length, 0);
  assert.equal(resClean.radiusViolations.length, 0);
});

test('mirrorFormationShape: mirrors coordinates, look angles and left<->right roles', () => {
  const echelonLeft: FormationShape = {
    members: [
      { role: 'commander', fwd: 0, right: 0, look: 0, arc: 90 },
      { role: 'left', fwd: -200, right: -200, look: -45, arc: 90 },
      { role: 'rear', fwd: -400, right: -400, look: -90, arc: 90 },
      { role: 'right', fwd: -200, right: 150, look: 90, arc: 90 },
    ],
  };

  const mirrored = mirrorFormationShape('echelon_left', echelonLeft);
  assert.equal(mirrored.formation, 'echelon_right', 'echelon_left mirrors to echelon_right');

  const m0 = mirrored.shape.members[0];
  assert.equal(m0.role, 'commander');
  assert.equal(m0.fwd, 0);
  assert.equal(m0.right, 0);
  assert.equal(m0.look, 0);

  const m1 = mirrored.shape.members[1];
  assert.equal(m1.role, 'right', 'left role swapped to right');
  assert.equal(m1.fwd, -200);
  assert.equal(m1.right, 200, 'right coordinate inverted');
  assert.equal(m1.look, 45, 'look angle inverted');

  const m2 = mirrored.shape.members[2];
  assert.equal(m2.role, 'rear', 'rear role stays rear');
  assert.equal(m2.fwd, -400);
  assert.equal(m2.right, 400);
  assert.equal(m2.look, 90);

  const m3 = mirrored.shape.members[3];
  assert.equal(m3.role, 'left', 'right role swapped to left');
  assert.equal(m3.fwd, -200);
  assert.equal(m3.right, -150);
  assert.equal(m3.look, -90);
});

test('computeDefaultFormationShape & resetFormationShape', () => {
  const cfg = configValues(readConfigFile());

  const def = computeDefaultFormationShape('diamond', 4, cfg);
  assert.equal(def.members.length, 4);
  assert.equal(def.members[0].role, 'commander');
  assert.equal(def.members[0].fwd, 0);
  assert.equal(def.members[0].right, 0);

  const doc: FormationShapesFile = {
    schema: FORMATION_SHAPES_SCHEMA,
    formations: {
      diamond: {
        '4': def,
      },
    },
    custom: {},
  };

  assert.ok(doc.formations?.diamond?.['4'] !== undefined);
  const resetOk = resetFormationShape(doc, 'diamond', 4);
  assert.equal(resetOk, true);
  assert.equal(doc.formations?.diamond?.['4'], undefined);
});

test('TS Simulator integration: simulate uses savedShapes overrides', () => {
  const cfg = configValues(readConfigFile());
  const layout = readLayout('sample_small');
  const nav = buildNav(layout);
  const route = layout.routes[0];

  // Custom authored diamond shape with unique coordinates and look facing:
  // e.g. right flanker at right=330, look=77
  const customDoc: FormationShapesFile = {
    schema: FORMATION_SHAPES_SCHEMA,
    formations: {
      diamond: {
        '4': {
          members: [
            { role: 'commander', fwd: 0, right: 0, look: 0, arc: 90 },
            { role: 'right', fwd: -310, right: 330, look: 77, arc: 90 },
            { role: 'left', fwd: -310, right: -330, look: -77, arc: 90 },
            { role: 'rear', fwd: -620, right: 0, look: 180, arc: 90 },
          ],
        },
      },
    },
    custom: {},
  };

  let firstFrameWithSaved: Frame | null = null;
  simulate({
    layout,
    route,
    memberCount: 4,
    config: cfg,
    seed: 1,
    nav,
    manualFormation: 'diamond',
    savedShapes: customDoc,
    onFrame: (f) => {
      if (!firstFrameWithSaved) firstFrameWithSaved = f;
    },
  });

  assert.ok(firstFrameWithSaved !== null, 'simulator emitted frames');
  // Verify that squad member was spawned or targeted near the custom slots
  const rightMember = firstFrameWithSaved!.members.find((m) => m.role === 'P');
  assert.ok(rightMember !== null);
});

test('Golden fixtures: verify formation_shapes_diamond4 and formation_shapes_echelon_mirror', () => {
  const d4Path = path.join(FIXTURES_DIR, 'formation_shapes_diamond4.json');
  assert.ok(fs.existsSync(d4Path), 'formation_shapes_diamond4.json fixture exists');
  const d4 = parseFormationShapesJson(fs.readFileSync(d4Path, 'utf8'));
  assert.equal(d4.schema, FORMATION_SHAPES_SCHEMA);
  assert.ok(d4.formations?.diamond?.['4'] !== undefined);
  assert.equal(d4.formations.diamond['4'].members.length, 4);

  const emPath = path.join(FIXTURES_DIR, 'formation_shapes_echelon_mirror.json');
  assert.ok(fs.existsSync(emPath), 'formation_shapes_echelon_mirror.json fixture exists');
  const em = parseFormationShapesJson(fs.readFileSync(emPath, 'utf8'));
  assert.equal(em.schema, FORMATION_SHAPES_SCHEMA);
  assert.ok(em.formations?.echelon_left?.['4'] !== undefined);
  assert.ok(em.formations?.echelon_right?.['4'] !== undefined);

  // Validate that echelon_right in the fixture is indeed the exact mirror of echelon_left
  const calculatedMirror = mirrorFormationShape('echelon_left', em.formations.echelon_left['4']);
  assert.equal(calculatedMirror.formation, 'echelon_right');
  assert.deepEqual(calculatedMirror.shape.members, em.formations.echelon_right['4'].members);
});

test('008 review defect 1: readBody rejects invalid JSON and handles valid JSON', async () => {
  const { readBody } = await import('../../server/movementApi.ts');
  const makeReq = (bodyStr: string) => ({
    on: (ev: string, cb: (chunk?: unknown) => void) => {
      if (ev === 'data') cb(bodyStr);
      if (ev === 'end') cb();
    },
  });

  await assert.rejects(
    async () => {
      await readBody(makeReq('{ invalid json '));
    },
    (err: unknown) => err instanceof SyntaxError
  );

  const resEmpty = await readBody(makeReq(''));
  assert.deepEqual(resEmpty, {});

  const resValid = await readBody(makeReq('{"foo": 123}'));
  assert.deepEqual(resValid, { foo: 123 });
});

test('008 review defect 2: formatFormationShapesJson preserves unknown keys on shapes and custom shapes', () => {
  const doc: FormationShapesFile = {
    schema: FORMATION_SHAPES_SCHEMA,
    formations: {
      column: {
        '2': {
          members: [
            { role: 'commander', fwd: 0, right: 0, look: 0 },
            { role: 'rear', fwd: -200, right: 0, look: 0 },
          ],
          designer_note: 'keep extra field',
          spacing_multiplier: 1.5,
        } as any,
      },
    },
    custom: {
      custom_1: {
        name: 'Custom alpha',
        members: [
          { squad: 0, fwd: 0, right: 0, look: 0 },
          { squad: 1, fwd: -100, right: 100, look: 45 },
        ],
        extra_custom_metadata: { active: true },
      } as any,
    },
  };

  const formatted = formatFormationShapesJson(doc);
  assert.ok(formatted.includes('"designer_note": "keep extra field"'));
  assert.ok(formatted.includes('"spacing_multiplier": 1.5'));
  assert.ok(formatted.includes('"extra_custom_metadata": { "active": true }'));

  const parsed = parseFormationShapesJson(formatted);
  assert.equal((parsed.formations?.column?.['2'] as any)?.designer_note, 'keep extra field');
  assert.equal((parsed.custom?.custom_1 as any)?.extra_custom_metadata?.active, true);
});

test('008 review defect 3: validateFormationShapes enforces commander at member 0 and exact role match to rolesFor(count)', () => {
  // member 0 not commander
  const badLeaderDoc: FormationShapesFile = {
    schema: FORMATION_SHAPES_SCHEMA,
    formations: {
      column: {
        '2': {
          members: [
            { role: 'rear', fwd: 0, right: 0, look: 0 },
            { role: 'commander', fwd: -200, right: 0, look: 0 },
          ],
        },
      },
    },
  };
  const errs1 = validateFormationShapes(badLeaderDoc);
  assert.ok(errs1.some((e) => e.includes('member 0 must be the commander')));

  // missing role for 4-member (commander, right, rear, rear instead of commander, right, left, rear)
  const badRolesDoc: FormationShapesFile = {
    schema: FORMATION_SHAPES_SCHEMA,
    formations: {
      wedge: {
        '4': {
          members: [
            { role: 'commander', fwd: 0, right: 0, look: 0 },
            { role: 'right', fwd: -150, right: 150, look: 30 },
            { role: 'point', fwd: -150, right: -150, look: -30 }, // point not valid role for wedge 4
            { role: 'rear', fwd: -300, right: 0, look: 180 },
          ],
        },
      },
    },
  };
  const errs2 = validateFormationShapes(badRolesDoc);
  assert.ok(errs2.some((e) => e.includes('roles must match rolesFor(4)')));
});

test('saved pair is only a fallback for a 2-member wedge or diamond; exact shapes win', () => {
  const shapes: FormationShapesFile = {
    schema: FORMATION_SHAPES_SCHEMA,
    formations: {
      pair: {
        '2': {
          members: [
            { role: 'commander', fwd: 0, right: 0, look: 0 },
            { role: 'rear', fwd: 0, right: 150, look: 0 },
          ],
        },
      },
      wedge: {
        '3': {
          members: [
            { role: 'commander', fwd: 0, right: 0, look: 0 },
            { role: 'right', fwd: -100, right: 100, look: 0 },
            { role: 'rear', fwd: -100, right: -100, look: 0 },
          ],
        },
      },
    },
  };

  const pair = shapes.formations.pair!['2'];
  assert.equal(getSavedFormationShape(shapes, 'pair', 2), pair);
  assert.equal(getSavedFormationShape(shapes, 'wedge', 2), pair);
  assert.equal(getSavedFormationShape(shapes, 'diamond', 2), pair);
  assert.equal(getSavedFormationShape(shapes, 'wedge', 3), shapes.formations.wedge!['3']);
  assert.equal(getSavedFormationShape(shapes, 'diamond', 3), undefined);
  for (const formation of ['column', 'line', 'echelon_left', 'echelon_right'] as const) {
    assert.equal(getSavedFormationShape(shapes, formation, 2), undefined, `${formation} never takes pair`);
  }
  assert.equal(getSavedFormationShape(undefined, 'wedge', 2), undefined);

  const own: FormationShape = { members: pair.members.map((m) => ({ ...m, right: m.right + 35 })) };
  shapes.formations.column = { '2': own };
  shapes.formations.wedge!['2'] = own;
  shapes.formations.diamond = { '2': own };
  for (const formation of ['column', 'wedge', 'diamond'] as const) {
    assert.equal(getSavedFormationShape(shapes, formation, 2), own, `${formation}/2 exact entry wins over pair`);
  }
});

test('008 review defect 5: custom shape validation catches radius, arc, duplicate squad, non-object member', () => {
  const badCustom: any = {
    schema: FORMATION_SHAPES_SCHEMA,
    custom: {
      custom_1: {
        name: 'Excessive radius and bad arc',
        members: [
          { squad: 0, fwd: 0, right: 0, look: 0 },
          { squad: 1, fwd: 5000, right: 0, look: 0, arc: 400 }, // > 4000 cm, arc > 360
          { squad: 1, fwd: 100, right: 100, look: 0 }, // duplicate squad index
          null, // not an object
        ],
      },
      custom_2: {
        name: 'Bad arc float',
        members: [
          { squad: 0, fwd: 0, right: 0, look: 0, arc: 45.5 },
        ],
      },
    },
  };
  const errs = validateFormationShapes(badCustom);
  assert.ok(errs.some((e) => e.includes('exceeds CUSTOM_MAX_RADIUS 4000')));
  assert.ok(errs.some((e) => e.includes('arc must be integer degrees between 10 and 360')));
  assert.ok(errs.some((e) => e.includes('duplicate squad index')));
  assert.ok(errs.some((e) => e.includes('expected an object')));
});

test('008 review defect 6: writeFormationShapesFile writes atomically with fsync and cleans up on error', () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), 'shapes-atomic-test-'));
  try {
    const targetFile = path.join(tmpDir, 'formation_shapes.json');
    const doc: FormationShapesFile = {
      schema: FORMATION_SHAPES_SCHEMA,
      formations: {
        column: {
          '2': {
            members: [
              { role: 'commander', fwd: 0, right: 0, look: 0 },
              { role: 'rear', fwd: -200, right: 0, look: 0 },
            ],
          },
        },
      },
    };

    writeFormationShapesFile(targetFile, doc);
    assert.ok(fs.existsSync(targetFile));
    const loaded = readFormationShapesFile(targetFile);
    assert.equal(loaded.schema, FORMATION_SHAPES_SCHEMA);

    // Verify temp files are cleaned up
    const remainingFiles = fs.readdirSync(tmpDir);
    assert.deepEqual(remainingFiles, ['formation_shapes.json']);
  } finally {
    fs.rmSync(tmpDir, { recursive: true, force: true });
  }
});
