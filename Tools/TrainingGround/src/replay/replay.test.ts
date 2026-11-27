import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import http from 'node:http';
import { parseReplay, unitsAt, eventUnits, smoothRoute, validNotes, translatePoints, deduplicateAnnotations, unitDisplayName, squadUnitIds, selectedReplayUnit, NOTE_TAGS } from './model.ts';
import type { Annotation } from './model.ts';
import { containedFile, demoLeaf, setupReplayApi } from '../../server/replayApi.ts';

const text = (rows: unknown[]) => rows.map(r => JSON.stringify(r)).join('\n');
const u = (x: number, id = 'a') => ({ id, p: [x, 0, 92], yaw: 90 });
const header = { type: 'header', map: 'L_Gen_Depot_1_X6' };

test('late round marks appear on their target fight, preserving call time; footer is legacy only', () => {
  const rounds = [header, { type: 'round_start', t: 0, round: 1 }, { type: 'round_end', t: 90, round: 1 },
    { type: 'round_start', t: 100, round: 2 }];
  const r = parseReplay(text([...rounds, { type: 'round_mark', t: 105, rt: 107, round: 1, value: 'good' },
    { type: 'footer', mark: 'good' }]));
  const marks = r.events.filter(e => e.type === 'round_mark');
  assert.deepEqual(marks.map(e => [e.t, e.round, e.value, e.recorded_t]), [[0, 1, 'good', 105]]);
  assert.equal(r.rows.find(e => e.type === 'round_mark')?.t, 105);
  const legacy = parseReplay(text([...rounds, { type: 'footer', mark: 'bad' }]));
  assert.deepEqual(legacy.events.filter(e => e.type === 'round_mark').map(e => [e.t, e.round, e.value]), [[0, 1, 'bad'], [100, 2, 'bad']]);
});
const notes = { schema: 'codex.demo_notes/1', coordinates: 'UE_cm_X_north_Y_east', recording: 'real.jsonl', map: header.map,
  annotations: [{ id: 'one', kind: 'route', t: 1, author: 'Designer', unit: 'a', label: 'Flank', color: '#facc15', points: [[0, 0], [100, 200]] }] };

test('legacy playback uses game duration, reports damaged lines, preserves round results', () => {
  const r = parseReplay(text([header, { type: 'snap', t: 1, ops: [u(0)] }, { type: 'order', t: 2, dest: [5, 5] }, { type: 'round_end', t: 3, result: 'victory' }, { type: 'footer', duration_s: 999 }]) + '\n{broken');
  assert.equal(r.duration, 3); assert.equal(r.warnings.length, 1); assert.equal(r.events.at(-1)?.result, 'victory');
  assert.equal(unitsAt(r, 1)[0].id, 'a');
});
test('interpolation stays within round and does not bridge respawn', () => {
  const r = parseReplay(text([header, { type: 'snap', t: 1, ops: [u(0)] }, { type: 'snap', t: 2, ops: [u(100)] }, { type: 'round_start', t: 2.2, round: 2 }, { type: 'snap', t: 3, ops: [u(1000, 'b')] }]));
  assert.equal(unitsAt(r, 1.5)[0].p[0], 50); assert.equal(unitsAt(r, 2.1)[0].p[0], 100); assert.deepEqual(unitsAt(r, 2.5), []);
});
test('121 exact who/whom killer/victim/op schema, null victim and state evidence', () => {
  const rows = [header, { type: 'snap', t: 1, ops: [u(0)], threats: [u(100, 'pmc')] },
    { type: 'shot_hit', t: 1.1, who: 'a', whom: 'pmc', dmg: 30 }, { type: 'damage', t: 1.1, who: 'None', whom: 'a', dmg: 5 },
    { type: 'stealth_on', t: 1.2, op: 'a' }, { type: 'cover_enter', t: 1.3, op: 'a', height: 'low' },
    { type: 'climb', t: 1.4, op: 'a', height_cm: 300 }, { type: 'down', t: 1.5, killer: 'a', victim: 'pmc' },
    { type: 'kill', t: 1.6, killer: 'a', victim: 'pmc', was_victim_aware: null }, { type: 'round_end', t: 1.6, result: 'victory' }];
  const r = parseReplay(text(rows)), units = unitsAt(r, 1.6);
  assert.equal(units[0].stealth, true); assert.equal(units[0].in_cover, true); assert.equal(units[0].height, 300); assert.equal(units[1].alive, 0);
  assert.deepEqual(eventUnits(r.events[0]), ['a', 'pmc']); assert.deepEqual(eventUnits({ type: 'kill', killer: 'a', victim: null }), ['a', undefined]);
  assert.equal(r.events.at(-1)?.type, 'round_end');
});
test('smooth drawing retains endpoints and persisted samples validate', () => {
  const points = smoothRoute([[0, 0], [100, 100], [200, 0]]);
  assert.deepEqual(points[0], [0, 0]); assert.deepEqual(points.at(-1), [200, 0]); assert.ok(points.length > 3);
  assert.ok(validNotes(notes, 'real.jsonl', header.map));
  assert.ok(!validNotes({ ...notes, coordinates: 'pixels' }, 'real.jsonl', header.map));
  assert.ok(!validNotes({ ...notes, annotations: [{ ...notes.annotations[0], points: [[NaN, 0]] }] }, 'real.jsonl', header.map));
});
test('recording paths reject traversal', () => {
  for (const name of ['../real.jsonl', 'a/b.jsonl', 'x.notes.json', 'C:\\a.jsonl']) assert.equal(demoLeaf(name), false);
  assert.equal(demoLeaf('2026-11-26_L_Gen_Depot_1_X6_165115.jsonl'), true);
});
test('API list, read-only recording, atomic save/reload and schema/origin rejection', async () => {
  const testParent = path.resolve('../../Saved/Replay148/tests');
  fs.mkdirSync(testParent, { recursive: true });
  const root = fs.mkdtempSync(path.join(testParent, 'replay148-'));
  const previous = { demos: process.env.TG_DEMOS_DIR, notes: process.env.TG_DEMO_NOTES_DIR };
  const original = text([header, { type: 'snap', t: 1, ops: [u(0)] }]);
  fs.writeFileSync(path.join(root, 'real.jsonl'), original);
  process.env.TG_DEMOS_DIR = root; process.env.TG_DEMO_NOTES_DIR = root;
  let handler: any;
  setupReplayApi((_route, h) => { handler = h; }, path.resolve('../..'));
  const server = http.createServer((req, res) => handler(req, res));
  await new Promise<void>(resolve => server.listen(0, '127.0.0.1', resolve));
  const address = server.address() as any, base = `http://127.0.0.1:${address.port}/`;
  try {
    assert.equal((await (await fetch(base + 'list')).json()).recordings[0].file, 'real.jsonl');
    assert.equal(await (await fetch(base + 'recording?file=real.jsonl')).text(), original);
    const post = (body: unknown, origin?: string) => fetch(base + 'notes?file=real.jsonl', { method: 'POST', headers: { 'Content-Type': 'application/json', ...(origin ? { Origin: origin } : {}) }, body: JSON.stringify(body) });
    assert.equal((await post(notes)).status, 200);
    assert.deepEqual(await (await fetch(base + 'notes?file=real.jsonl')).json(), notes);
    const unicodeNotes = { ...notes, annotations: [{ ...notes.annotations[0], label: 'маршрут на крышу' }] };
    const encoded = Buffer.from(JSON.stringify(unicodeNotes));
    const split = encoded.indexOf(Buffer.from('маршрут')) + 1;
    await new Promise<void>((resolve, reject) => {
      const req = http.request(base + 'notes?file=real.jsonl', { method: 'POST', headers: { 'Content-Type': 'application/json' } }, res => {
        res.resume(); res.on('end', () => { try { assert.equal(res.statusCode, 200); resolve(); } catch (e) { reject(e); } });
      });
      req.on('error', reject); req.write(encoded.subarray(0, split));
      setTimeout(() => req.end(encoded.subarray(split)), 20);
    });
    assert.deepEqual(await (await fetch(base + 'notes?file=real.jsonl')).json(), unicodeNotes);
    assert.equal((await post({ ...notes, recording: 'other.jsonl' })).status, 400);
    assert.equal((await post(notes, 'https://foreign.example')).status, 403);
    assert.equal((await fetch(base + 'recording?file=../real.jsonl')).status, 400);
    assert.equal((await fetch(base + 'map?map=unknown')).status, 404);
    assert.equal(fs.readFileSync(path.join(root, 'real.jsonl'), 'utf8'), original);
    assert.ok(containedFile(root, 'real.jsonl').endsWith('real.jsonl'));
  } finally {
    await new Promise<void>(resolve => server.close(() => resolve()));
    for (const [key, value] of Object.entries({ TG_DEMOS_DIR: previous.demos, TG_DEMO_NOTES_DIR: previous.notes })) { if (value === undefined) delete process.env[key]; else process.env[key] = value; }
    fs.rmSync(root, { recursive: true }); // Only the test-created isolated directory.
  }
});

const realDir = process.env.TG_DEMOS_DIR || path.join(os.homedir(), 'Documents/Unreal Projects/CodexTactics/Saved/Demos');
test('all locally available real recordings parse with finite frame positions', { skip: !fs.existsSync(realDir) }, () => {
  const files = fs.readdirSync(realDir).filter(demoLeaf);
  assert.ok(files.length > 0);
  let count = 0;
  for (const file of files) {
    const r = parseReplay(fs.readFileSync(path.join(realDir, file), 'utf8'));
    assert.equal(r.warnings.length, 0, file); assert.ok(r.frames.length > 0, file);
    for (const t of [0, r.duration / 2, r.duration]) for (const u of unitsAt(r, t)) assert.ok(u.p.every(Number.isFinite), file);
    count += r.rows.length;
  }
  console.log(`Real recordings: ${files.length}, timed rows: ${count}`);
});

test('translatePoints shifts 2D and 3D coordinates for whole-shape dragging', () => {
  assert.deepEqual(translatePoints([[100, 200], [300, -400, 92]], 50.4, -20.6), [[150, 179], [350, -421, 92]]);
});

test('deduplicateAnnotations keeps pointed note over time-only duplicate within 2s and preserves distinct points', () => {
  const base: Omit<Annotation, 'id' | 'points' | 't'> = { kind: 'comment', author: 'Designer', unit: '', label: 'Cover bug', color: '#facc15' };
  const input: Annotation[] = [
    { ...base, id: '1', t: 64.5, points: [] },
    { ...base, id: '2', t: 64.5, points: [[-2812, -12943]] },
    { ...base, id: '3', t: 66.0, points: [] },
    { ...base, id: '4', t: 64.5, points: [[-6434, -18735]] },
    { ...base, id: '5', t: 68.5, points: [] },
    { ...base, id: '6', t: 69.0, points: [] },
    { ...base, id: '7', t: 64.5, points: [[-2812, -12943]] },
  ];
  const { annotations, removed } = deduplicateAnnotations(input);
  assert.equal(removed, 4);
  assert.deepEqual(annotations.map(a => a.id), ['2', '4', '5']);
});

test('real 51-note designer session copies to temp dir, validates legacy schema, and deduplicates cleanly', () => {
  const candidates = [
    path.join(os.homedir(), 'Documents/Unreal Projects/CodexTactics-play/Saved/Demos/2026-11-27_L_Gen_Depot_1_X6_085013.notes.json'),
    path.join(os.homedir(), 'Documents/Unreal Projects/CodexTactics/Saved/Demos/2026-11-27_L_Gen_Depot_1_X6_085013.notes.json'),
  ];
  const src = candidates.find(p => fs.existsSync(p));
  if (!src) return;
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), 'replay154-notes-'));
  try {
    const copyPath = path.join(tmpDir, path.basename(src));
    fs.copyFileSync(src, copyPath);
    const parsed = JSON.parse(fs.readFileSync(copyPath, 'utf8').replace(/^\uFEFF/, ''));
    assert.ok(validNotes(parsed, '2026-11-27_L_Gen_Depot_1_X6_085013.jsonl', 'L_Gen_Depot_1_X6'));
    const { annotations, removed } = deduplicateAnnotations(parsed.annotations);
    assert.ok(removed > 0);
    assert.ok(annotations.length < parsed.annotations.length);
    const savedCopy = { ...parsed, annotations };
    fs.writeFileSync(copyPath, JSON.stringify(savedCopy, null, 2));
    assert.ok(validNotes(JSON.parse(fs.readFileSync(copyPath, 'utf8')), '2026-11-27_L_Gen_Depot_1_X6_085013.jsonl', 'L_Gen_Depot_1_X6'));
  } finally {
    fs.rmSync(tmpDir, { recursive: true, force: true });
  }
});

test('role display names map squad roles to Russian names while preserving IDs and selection defaults', () => {
  const squadHeader = {
    type: 'header',
    map: 'L_Gen_Depot_1_X6',
    squad: [
      { id: 'BP_Operative_C_0', role: 'EOperativeRole::Commander' },
      { id: 'BP_Operative_C_1', role: 'EOperativeRole::Engineer' },
      { id: 'BP_Operative_C_2', role: 'EOperativeRole::MedicSapper' },
      { id: 'BP_Operative_C_9', role: 'EOperativeRole::Recruit' },
    ],
  };
  const r = parseReplay(text([
    squadHeader,
    { type: 'round_start', t: 0.5, round: 1 },
    { type: 'snap', t: 1, ops: [{ id: 'BP_Operative_C_0', p: [0, 0], sel: true, lead: true }, { id: 'BP_Operative_C_1', p: [100, 0], sel: false }], threats: [{ id: 'BP_Operative_C_7', p: [500, 0] }] },
    { type: 'snap', t: 3, ops: [{ id: 'BP_Operative_C_0', p: [0, 0], sel: false }, { id: 'BP_Operative_C_1', p: [100, 0], sel: true }] },
    { type: 'round_start', t: 10, round: 2 },
    { type: 'snap', t: 11, ops: [{ id: 'BP_Operative_C_6', p: [0, 0], sel: true, lead: true }, { id: 'BP_Operative_C_70', p: [100, 0], sel: false }, { id: 'BP_Operative_C_8', p: [200, 0], sel: false }] },
  ]));
  assert.deepEqual(squadUnitIds(r), ['BP_Operative_C_0', 'BP_Operative_C_1', 'BP_Operative_C_2', 'BP_Operative_C_9']);
  assert.deepEqual(squadUnitIds(r, 11), ['BP_Operative_C_6', 'BP_Operative_C_70', 'BP_Operative_C_8']);
  assert.equal(unitDisplayName(r, 'BP_Operative_C_0'), 'Командир');
  assert.equal(unitDisplayName(r, 'BP_Operative_C_1'), 'Инженер');
  assert.equal(unitDisplayName(r, 'BP_Operative_C_2'), 'Медик-сапёр');
  assert.equal(unitDisplayName(r, 'BP_Operative_C_9'), 'Новобранец');
  assert.equal(unitDisplayName(r, 'BP_Operative_C_6'), 'Командир');
  assert.equal(unitDisplayName(r, 'BP_Operative_C_70'), 'Инженер');
  assert.equal(unitDisplayName(r, 'BP_Operative_C_8'), 'Медик-сапёр');
  assert.equal(unitDisplayName(r, 'BP_Operative_C_7'), 'PMC 7');
  assert.equal(selectedReplayUnit(r, 0), 'BP_Operative_C_0');
  assert.equal(selectedReplayUnit(r, 1), 'BP_Operative_C_0');
  assert.equal(selectedReplayUnit(r, 3), 'BP_Operative_C_1');
  assert.equal(selectedReplayUnit(r, 11), 'BP_Operative_C_6');
});

test('tag field validates allowed values and maintains backward compatibility for untagged notes', () => {
  for (const tag of NOTE_TAGS) {
    const withTag = { ...notes, annotations: [{ ...notes.annotations[0], tag }] };
    assert.ok(validNotes(withTag, 'real.jsonl', header.map), tag);
  }
  assert.ok(validNotes(notes, 'real.jsonl', header.map));
  assert.ok(!validNotes({ ...notes, annotations: [{ ...notes.annotations[0], tag: 'unknown_tag' }] }, 'real.jsonl', header.map));
});
