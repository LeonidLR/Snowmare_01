import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { test } from 'node:test';
import {
  readSectorFightStats,
  readTrainingData,
  resetDemoStatsCacheForTesting,
  resolveMainCheckoutRoot,
} from '../../server/trainingApi.ts';
import { X6 } from './trainingTypes.ts';

function writeDemo(dir: string, name: string, lines: string[]): string {
  fs.mkdirSync(dir, { recursive: true });
  const full = path.join(dir, name);
  fs.writeFileSync(full, lines.join('\n') + '\n', 'utf8');
  return full;
}

test('aggregates complete, aborted, no-sector, truncated, and non-X6 demo jsonl files and highlights fewest rounds', (t) => {
  resetDemoStatsCacheForTesting();
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'tg-sector-stats-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const demosDir = path.join(root, 'Saved/Demos');

  // 1. Complete session in Sector 2 (3 rounds: 2W 1L, 180s) + snap lines (must not break if huge)
  writeDemo(demosDir, 's2_complete.jsonl', [
    JSON.stringify({ type: 'header', v: 1, map: X6, sector_id: 2, started_utc: '2026-11-25T10:00:00Z' }),
    JSON.stringify({ type: 'snap', t: 1.0, ops: [{ id: 'a' }] }),
    JSON.stringify({ type: 'round_end', sector_id: 2, round: 1, result: 'victory', duration_s: 60 }),
    JSON.stringify({ type: 'round_end', sector_id: 2, round: 2, result: 'defeat', duration_s: 55 }),
    JSON.stringify({ type: 'round_end', sector_id: 2, round: 3, result: 'victory', duration_s: 65 }),
    JSON.stringify({ type: 'footer', result: 'COMPLETED', rounds: 3, score_victory: 2, score_defeat: 1, score_draw: 0, duration_s: 180 }),
  ]);

  // 2. Aborted session in Sector 3 (2 finished rounds before abort, 120s)
  writeDemo(demosDir, 's3_aborted.jsonl', [
    JSON.stringify({ type: 'header', v: 1, map: X6, sector_id: 3, started_utc: '2026-11-25T11:00:00Z' }),
    JSON.stringify({ type: 'round_start', sector_id: 3, round: 1 }),
    JSON.stringify({ type: 'round_end', sector_id: 3, round: 1, result: 'defeat', duration_s: 40 }),
    JSON.stringify({ type: 'round_end', sector_id: 3, round: 2, result: 'draw', duration_s: 50 }),
    JSON.stringify({ type: 'round_start', sector_id: 3, round: 3 }),
    JSON.stringify({ type: 'footer', result: 'ABORTED', rounds: 2, score_victory: 0, score_defeat: 1, score_draw: 1, duration_s: 120 }),
  ]);

  // 3. Old file without sector_id -> "без сектора"
  writeDemo(demosDir, 'old_no_sector.jsonl', [
    JSON.stringify({ type: 'header', v: 1, map: 'UEDPIE_0_' + X6, started_utc: '2026-11-24T13:00:00Z' }),
    JSON.stringify({ type: 'footer', result: 'ABORTED', duration_s: 300 }),
  ]);

  // 4. Truncated / bad-line file in Sector 5: broken last line + 1 round_end before crash -> still counts 1 round!
  writeDemo(demosDir, 's5_truncated.jsonl', [
    JSON.stringify({ type: 'header', v: 1, map: X6, sector_id: 5, started_utc: '2026-11-25T12:00:00Z' }),
    '{ bad json line in the middle',
    JSON.stringify({ type: 'round_end', sector_id: 5, round: 1, result: 'victory', duration_s: 90 }),
    '{"type":"snap","t":100,"unterminated',
  ]);

  // 5. Completely broken / non-X6 files -> ignored without breaking table
  writeDemo(demosDir, 'corrupt_header.jsonl', ['{ not valid']);
  writeDemo(demosDir, 'other_map.jsonl', [
    JSON.stringify({ type: 'header', v: 1, map: 'L_Gen_StoryShowcase_Kit', sector_id: 1, started_utc: '2026-11-25T09:00:00Z' }),
    JSON.stringify({ type: 'footer', result: 'ABORTED', rounds: 4, score_victory: 4, duration_s: 240 }),
  ]);

  const stats = readSectorFightStats(root);
  assert.equal(stats.sectors.length, 6);
  // Sector 1 has 0 rounds -> recommendedSector = 1 ("сыграйте сюда")
  assert.equal(stats.recommendedSector, 1);
  assert.equal(stats.sectors[0].fewest, true);
  assert.equal(stats.sectors[0].rounds, 0);

  // Sector 2 checks
  const s2 = stats.sectors[1];
  assert.equal(s2.sessions, 1);
  assert.equal(s2.rounds, 3);
  assert.equal(s2.wins, 2);
  assert.equal(s2.losses, 1);
  assert.equal(s2.draws, 0);
  assert.equal(s2.minutes, 3.0);
  assert.equal(s2.lastPlayed, '2026-11-25T10:00:00Z');

  // Sector 3 checks
  const s3 = stats.sectors[2];
  assert.equal(s3.sessions, 1);
  assert.equal(s3.rounds, 2);
  assert.equal(s3.losses, 1);
  assert.equal(s3.draws, 1);
  assert.equal(s3.minutes, 2.0);

  // Sector 5 checks (truncated file recovered 1 round_end)
  const s5 = stats.sectors[4];
  assert.equal(s5.sessions, 1);
  assert.equal(s5.rounds, 1);
  assert.equal(s5.wins, 1);
  assert.equal(s5.minutes, 1.5);

  // "без сектора"
  assert.equal(stats.noSector.label, 'без сектора');
  assert.equal(stats.noSector.sessions, 1);
  assert.equal(stats.noSector.rounds, 0);
  assert.equal(stats.noSector.minutes, 5.0);

  // "всего"
  assert.equal(stats.total.label, 'всего');
  assert.equal(stats.total.sessions, 4);
  assert.equal(stats.total.rounds, 6);
  assert.equal(stats.total.wins, 3);
  assert.equal(stats.total.losses, 2);
  assert.equal(stats.total.draws, 1);
  assert.equal(stats.total.minutes, 11.5);

  // If sectors 1..6 all have >= 1 round and sector 4 has fewest (e.g., 1 vs 2+), sector 4 gets fewest
  for (const sid of [1, 2, 3, 4, 5, 6]) {
    writeDemo(demosDir, `fill_s${sid}.jsonl`, [
      JSON.stringify({ type: 'header', v: 1, map: X6, sector_id: sid, started_utc: '2026-11-25T15:00:00Z' }),
      JSON.stringify({ type: 'footer', rounds: sid === 4 ? 1 : 5, score_victory: 1, duration_s: 60 }),
    ]);
  }
  const updated = readSectorFightStats(root);
  assert.equal(updated.recommendedSector, 4);
  assert.equal(updated.sectors[3].fewest, true);
  assert.equal(updated.sectors[0].fewest, undefined);

  // Also wired into readTrainingData
  assert.equal(readTrainingData(root).sectorStats?.recommendedSector, 4);
});

test('mtime cache avoids re-parsing unchanged files and invalidates when modified; worktree .git resolves main root', (t) => {
  resetDemoStatsCacheForTesting();
  const mainRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'tg-main-root-'));
  const worktreeRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'tg-lane-root-'));
  t.after(() => {
    fs.rmSync(mainRoot, { recursive: true, force: true });
    fs.rmSync(worktreeRoot, { recursive: true, force: true });
  });

  // Simulate git worktree .git file pointing into mainRoot/.git/worktrees/CodexTactics-129
  const gitWorktreeDir = path.join(mainRoot, '.git/worktrees/CodexTactics-129');
  fs.mkdirSync(gitWorktreeDir, { recursive: true });
  fs.writeFileSync(path.join(worktreeRoot, '.git'), `gitdir: ${gitWorktreeDir}\n`, 'utf8');
  assert.equal(path.resolve(resolveMainCheckoutRoot(worktreeRoot)), path.resolve(mainRoot));

  const demosDir = path.join(mainRoot, 'Saved/Demos');
  const demoFile = writeDemo(demosDir, 's1.jsonl', [
    JSON.stringify({ type: 'header', v: 1, map: X6, sector_id: 1, started_utc: '2026-11-25T14:00:00Z' }),
    JSON.stringify({ type: 'footer', rounds: 1, score_victory: 1, duration_s: 60 }),
  ]);

  const first = readSectorFightStats(worktreeRoot);
  assert.equal(first.sectors[0].rounds, 1);

  // Overwrite with different rounds & bump mtime forward so cache invalidates
  fs.writeFileSync(
    demoFile,
    [
      JSON.stringify({ type: 'header', v: 1, map: X6, sector_id: 1, started_utc: '2026-11-25T14:00:00Z' }),
      JSON.stringify({ type: 'footer', rounds: 4, score_victory: 3, score_defeat: 1, duration_s: 120 }),
    ].join('\n') + '\n',
    'utf8',
  );
  const later = new Date(Date.now() + 5000);
  fs.utimesSync(demoFile, later, later);

  const second = readSectorFightStats(worktreeRoot);
  assert.equal(second.sectors[0].rounds, 4);
  assert.equal(second.sectors[0].wins, 3);
  assert.equal(second.sectors[0].losses, 1);
});
