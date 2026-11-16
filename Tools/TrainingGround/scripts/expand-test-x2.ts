// Task 023: expand a frozen TEST fixture with the stage-2 API; never reads/writes the designer map.
// From repo root: node Tools/TrainingGround/scripts/expand-test-x2.ts [--verify]
// Creates only Content/Data/LevelGen/TestFixtures/L_Gen_Test_X2.layout.json; refuses overwrite. --verify writes nothing.
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { frameOf } from '../src/levelgen/blockTemplates.ts';
import { checkSeams, expandLayout } from '../src/levelgen/expand.ts';
import type { Profile } from '../src/levelgen/profile.ts';
import type { GenLayout } from '../src/levelgen/schema.ts';
import { validateExpansionState, validateGenLayout } from '../src/levelgen/schema.ts';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const source = 'Content/Data/LevelGen/props/fixtures/golden_depot.layout.json';
const output = 'Content/Data/LevelGen/TestFixtures/L_Gen_Test_X2.layout.json';
const read = (p: string) => fs.readFileSync(path.join(root, p), 'utf8');
const hash = (s: string) => crypto.createHash('sha256').update(s).digest('hex');
const sourceText = read(source);
const base = JSON.parse(sourceText) as GenLayout;
const profile = JSON.parse(read('Content/Data/LevelGen/refs/ref_01_02_depot.json')) as Profile;
const before = JSON.stringify(base);
const r = expandLayout({ layout: base, factor: 2, seed: 7, profile });
assert.ok(r.ok, r.message);
assert.equal(JSON.stringify(base), before, 'source object mutated');
assert.deepEqual(frameOf(r.layout), [250, 175]);
assert.deepEqual(validateExpansionState(r.layout.expansion), []);
const inheritedErrors = validateGenLayout(base);
const errors = validateGenLayout(r.layout);
assert.deepEqual(errors.filter(e => !inheritedErrors.includes(e)), [], 'new schema errors');
assert.ok(r.routes.length && r.routes.every(x => x.completable), 'route not completable');
const seams = checkSeams(r.layout, r.roads);
assert.ok(seams.length && seams.every(x => x.connected && x.ratio <= 1.25), 'disconnected seam');
assert.equal(seams.filter(x => x.kind === 'canal').length, 2);
assert.deepEqual(r.layout.cut!.crossings.slice(0, base.cut!.crossings.length), base.cut!.crossings);
const text = JSON.stringify(r.layout, null, 1) + '\n';
const verify = process.argv.slice(2);
assert.ok(verify.length === 0 || (verify.length === 1 && verify[0] === '--verify'), 'only --verify is supported');
if (verify.length) assert.equal(read(output), text, 'existing output differs');
else fs.writeFileSync(path.join(root, output), text, { flag: 'wx' });
assert.equal(read(source), sourceText, 'source file changed');
assert.equal(JSON.stringify(JSON.parse(read(output))), JSON.stringify(r.layout), 'JSON round trip');
console.log(JSON.stringify({ source, source_sha256: hash(sourceText), output, output_sha256: hash(text),
  name: r.layout.name, factor: 2, seed: r.seed, retries: r.retries, size_m: frameOf(r.layout),
  area_ratio: 250 * 175 / (177 * 124), bounds: r.layout.bounds,
  schema_errors: errors, inherited_schema_errors: inheritedErrors, seams, routes: r.routes,
  base_crossings: base.cut!.crossings.length, expanded_crossings: r.layout.cut!.crossings.length,
  lots_added: r.lots.length, seam_gates: r.seamGates, blocked_seams: r.blockedSeams,
  objects: { buildings: r.layout.buildings.length, walls: r.layout.walls.length,
    ladders: r.layout.ladders.length, covers: r.layout.covers.length } }, null, 2));
