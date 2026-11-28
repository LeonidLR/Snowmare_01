import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { test } from 'node:test';
import { syncTrainingLevels, TRAINING_SYNC_FILES } from '../../server/trainingSync.ts';

test('sync allowlist main -> build only, byte comparison, backups, idempotence and missing companions', t => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'tg-sync-test-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const main = path.join(root, 'main'), build = path.join(root, 'play');
  const put = (base: string, name: string, data: string) => {
    const file = path.join(base, 'Content/Data/LevelJson', name);
    fs.mkdirSync(path.dirname(file), { recursive: true }); fs.writeFileSync(file, data); return file;
  };
  const source = put(main, TRAINING_SYNC_FILES[0], '{"environment":{"start_hour":12}}');
  const destination = put(build, TRAINING_SYNC_FILES[0], '{"environment":{"start_hour":2}}');
  put(main, TRAINING_SYNC_FILES[1], 'new sectors'); put(build, TRAINING_SYNC_FILES[1], 'old sectors');
  put(main, TRAINING_SYNC_FILES[2], 'same index'); put(build, TRAINING_SYNC_FILES[2], 'same index');
  put(main, 'other.json', 'main other'); const other = put(build, 'other.json', 'play other');
  assert.equal(syncTrainingLevels(main, main), '');
  assert.ok(!fs.existsSync(path.join(main, 'Saved')));
  assert.match(syncTrainingLevels(main, build), /синхронизировано: start_hour 2 -> 12/);
  assert.equal(fs.readFileSync(destination, 'utf8'), fs.readFileSync(source, 'utf8'));
  assert.equal(fs.readFileSync(other, 'utf8'), 'play other');
  const backups = path.join(build, 'Saved/Backups');
  const backup = path.join(backups, fs.readdirSync(backups)[0]);
  assert.deepEqual(fs.readdirSync(backup).sort(), TRAINING_SYNC_FILES.slice(0, 2).sort());
  assert.equal(fs.readFileSync(path.join(backup, TRAINING_SYNC_FILES[0]), 'utf8'), '{"environment":{"start_hour":2}}');
  assert.equal(syncTrainingLevels(main, build), '');
  assert.equal(fs.readdirSync(backups).length, 1);
  fs.unlinkSync(source);
  assert.equal(syncTrainingLevels(main, build), '');
  assert.ok(fs.existsSync(destination));
  assert.ok(!fs.existsSync(source));
});

test('backup failure leaves destination untouched', t => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'tg-sync-failure-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const main = path.join(root, 'main'), build = path.join(root, 'play');
  for (const base of [main, build]) fs.mkdirSync(path.join(base, 'Content/Data/LevelJson'), { recursive: true });
  const relative = path.join('Content/Data/LevelJson', TRAINING_SYNC_FILES[0]);
  fs.writeFileSync(path.join(main, relative), 'new');
  fs.writeFileSync(path.join(build, relative), 'old');
  fs.mkdirSync(path.join(build, 'Saved'), { recursive: true });
  fs.writeFileSync(path.join(build, 'Saved/Backups'), 'blocked');
  assert.throws(() => syncTrainingLevels(main, build));
  assert.equal(fs.readFileSync(path.join(build, relative), 'utf8'), 'old');
});
