import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { test } from 'node:test';
import { fileURLToPath } from 'node:url';
import { configValues } from '../movement/config.ts';
import { mockCritic } from './critic.ts';
import { evaluateLayout } from './evaluate.ts';
import type { GenLayout } from './schema.ts';
import { validateGenLayout } from './schema.ts';
import { buildAllTrainingArenas } from './trainingArenas.ts';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../..');
const ACCEPTED_DIR = path.join(ROOT, 'Content/Data/LevelGen/accepted');
const config = configValues(JSON.parse(fs.readFileSync(path.join(ROOT, 'Content/Data/FormationLab/formation_config.json'), 'utf8')));

test('L_Gen_Train_1..3 pass schema validation, full simulation, tactical checks, and mockCritic', () => {
  const arenas = buildAllTrainingArenas();
  assert.equal(arenas.length, 3);

  for (const arena of arenas) {
    assert.deepEqual(validateGenLayout(arena.layout), [], `${arena.name} must pass validateGenLayout`);

    const ev = evaluateLayout({ layout: arena.layout, profile: arena.profile, config });
    assert.equal(ev.route.completable, true, `${arena.name} mandatory route failed: ${ev.route.fail}`);
    for (const v of ev.variants) {
      assert.equal(v.completable, true, `${arena.name} variant ${v.id} failed: ${v.fail}`);
    }

    assert.equal(ev.tactical.unreachableRooms.length, 0, `${arena.name} unreachable rooms: ${ev.tactical.unreachableRooms.join(', ')}`);
    assert.equal(ev.tactical.blockedWindows.length, 0, `${arena.name} blocked windows: ${JSON.stringify(ev.tactical.blockedWindows)}`);
    for (const b of ev.tactical.buildings) {
      assert.equal(b.roofReachable, true, `${arena.name} building ${b.id} roof unreachable`);
    }
    assert.ok(ev.tactical.firingLines.count >= 4, `${arena.name} expected >= 4 firing lines, got ${ev.tactical.firingLines.count}`);
    assert.ok(ev.tactical.multiLevelRoutes.exists, `${arena.name} expected multi-level route`);
    assert.ok(ev.tactical.mantle.valid >= 6, `${arena.name} expected >= 6 valid mantle edges, got ${ev.tactical.mantle.valid}`);
    assert.equal(ev.tactical.mantle.invalid, 0, `${arena.name} expected 0 invalid mantle edges`);
    assert.ok(ev.tactical.score >= 0.75, `${arena.name} tactical score ${ev.tactical.score} too low`);

    const verdict = mockCritic(ev);
    assert.equal(verdict.fits, true, `${arena.name} mockCritic rejected: ${verdict.reasons.join(' | ')} (fixes: ${verdict.fixes.join('; ')})`);

    // Accepted artifact exists on disk and matches the builder output
    const diskPath = path.join(ACCEPTED_DIR, `${arena.name}.json`);
    assert.equal(fs.existsSync(diskPath), true, `${diskPath} must exist`);
    const onDisk = JSON.parse(fs.readFileSync(diskPath, 'utf8')) as GenLayout;
    assert.deepEqual(validateGenLayout(onDisk), []);
    assert.deepEqual(onDisk, JSON.parse(JSON.stringify(arena.layout)), `${arena.name}.json on disk must match trainingArenas builder`);
  }
});

test('training arenas cover distinct combat building archetypes requested in 027 item 5', () => {
  const [a1, a2, a3] = buildAllTrainingArenas();

  // Train 1: partitioned house block, street, windows, stairwell, roof parapet
  const house = a1.layout.buildings.find((b) => b.id === 'B_house')!;
  assert.ok((house.partitions?.length ?? 0) >= 2);
  assert.ok((house.rooms?.length ?? 0) >= 4);
  assert.ok((house.openings ?? []).some((o) => o.kind === 'window'));
  assert.ok(house.stairs.some((s) => s.kind === 'stairwell'));
  assert.equal(house.parapets?.length, 4);

  // Train 2: fortified checkpoint, narrow loopholes, sandbags, bunker, watch tower with hatch & ladder
  const cp = a2.layout.buildings.find((b) => b.id === 'B_checkpoint')!;
  const tower = a2.layout.buildings.find((b) => b.id === 'B_tower')!;
  assert.ok((cp.openings ?? []).some((o) => o.kind === 'loophole'));
  assert.ok(a2.layout.covers.some((c) => c.kind === 'sandbag'));
  assert.ok(a2.layout.buildings.some((b) => b.set_piece === 'bunker'));
  assert.equal(tower.set_piece, 'tower');
  assert.ok((tower.hatches?.length ?? 0) >= 1);
  assert.ok(a2.layout.ladders.some((l) => l.to === `roof:${tower.id}`));

  // Train 3: warehouse yard, 2nd-floor roof catwalk/gap landing, parapet mantle edges, courtyard obstacles
  const cat = a3.layout.buildings.find((b) => b.id === 'B_catwalk')!;
  assert.ok((cat.floors_detail?.[0].gaps?.length ?? 0) >= 2, 'catwalk annex must have floor gaps / landings');
  assert.ok((a3.layout.mantle_edges ?? []).some((me) => me.kind === 'parapet'));
  assert.ok((a3.layout.mantle_edges ?? []).some((me) => me.kind === 'obstacle' || me.kind === 'crate'));
  assert.ok(a3.layout.covers.length >= 4);
});
