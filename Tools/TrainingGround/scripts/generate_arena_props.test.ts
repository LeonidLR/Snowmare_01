import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import { ROOT } from '../src/movement/cli/paths.ts';
import { formatPropsCanonical } from '../src/levelgen/props/canonical.ts';
import { PropsLayoutView } from '../src/levelgen/props/layoutView.ts';
import type { GenLayout } from '../src/levelgen/schema.ts';
import { checkArenaProps, generateArenaProps, layoutHash, requireArenaName } from './generate_arena_props.ts';

const balance = JSON.parse(fs.readFileSync(path.join(ROOT, 'Content/Data/Balance/props_balance.json'), 'utf8'));
const layout = (n: number): GenLayout => JSON.parse(fs.readFileSync(path.join(ROOT, `Content/Data/LevelGen/accepted/L_Gen_Train_${n}.json`), 'utf8'));

test('arena props CLI: only the three assigned arena names are accepted', () => {
  for (const name of ['L_Gen_Depot_1', '../L_Gen_Train_1', 'L_Gen_Train_4', '--unknown']) assert.throws(() => requireArenaName(name));
  requireArenaName('L_Gen_Train_1');
});

test('arena props CLI: repeatable seed, different seed changes placement, inputs stay untouched', () => {
  const l = layout(1);
  const before = JSON.stringify({ l, balance });
  const a = formatPropsCanonical(generateArenaProps(l, balance));
  assert.equal(a, formatPropsCanonical(generateArenaProps(l, balance)));
  assert.equal(JSON.stringify({ l, balance }), before);
  l.seed++;
  assert.notEqual(a, formatPropsCanonical(generateArenaProps(l, balance)));
});

for (const n of [1, 2, 3]) {
  test(`arena props CLI: Train ${n} passes schemas, mine safety and fastGate with unchanged presets`, () => {
    const l = layout(n);
    const file = generateArenaProps(l, balance);
    const result = checkArenaProps(l, file, balance);
    assert.deepEqual(result.errors, []);
    assert.equal(result.fastGate.gate, 'pass');
    assert.ok(file.layers.every(layer => layer.objects.length > 0));
    assert.deepEqual(file.layers.map(layer => layer.kind), ['crates', 'barrels', 'turrets', 'mines', 'loot']);
    assert.ok(file.layers.every(layer => Object.keys(layer.rules).length === 0));
    // No cut layer exists: keep the documented shortage instead of placing mines elsewhere.
    assert.equal(file.layers.find(layer => layer.kind === 'mines')!.report!.field!.placed, 0);
    assert.equal(file.layers.find(layer => layer.kind === 'mines')!.report!.short_by, 4);
  });
}

test('arena props CLI: validator catches mandatory doorway mines and missing attachment IDs', () => {
  const l = layout(1);
  const file = generateArenaProps(l, balance);
  const view = PropsLayoutView.build(JSON.stringify(l), PropsLayoutView.readConfig(balance));
  const door = view.doorways.find(d => d.bOnMandatoryRoute)!;
  assert.ok(door);
  const mine = file.layers.find(layer => layer.kind === 'mines')!.objects.find(o => o.door)!;
  mine.door = door.id;
  const attached = file.layers.find(layer => layer.kind === 'mines')!.objects.find(o => o.attach)!;
  attached.attach = 'missing_crate';
  const { errors } = checkArenaProps(l, file, balance);
  assert.ok(errors.some(error => error.includes(`mandatory/missing doorway: ${mine.id}`)));
  assert.ok(errors.some(error => error.includes(`missing attachment: ${attached.id}`)));
});

test('arena props CLI: geometry hash ignores metadata and detects changed obstacle geometry', () => {
  const l = layout(1);
  const hash = layoutHash(l);
  assert.match(hash, /^[a-f0-9]{40}$/);
  l.comment = 'metadata only';
  assert.equal(layoutHash(l), hash);
  l.walls[0].height++;
  assert.notEqual(layoutHash(l), hash);
});
