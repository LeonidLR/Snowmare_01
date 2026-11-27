import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import http from 'node:http';
import { parseReplay, unitsAt, eventUnits, smoothRoute, validNotes } from './model.ts';
import { containedFile, demoLeaf, setupReplayApi } from '../../server/replayApi.ts';

const text = (rows: unknown[]) => rows.map(r => JSON.stringify(r)).join('\n');
const u = (x: number, id = 'a') => ({ id, p: [x, 0, 92], yaw: 90 });
const header = { type: 'header', map: 'L_Gen_Depot_1_X6' };
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
