import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {
  resolveAudioPaths,
  resolveSafeWavPath,
  buildAudioListResponse,
  applyAudioReview,
  isSameOriginOrLocal,
} from './audioApi.ts';

function makeTempV1CatalogProject(): { root: string; cleanup: () => void } {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'codex-audio-v1-'));
  const v1Dir = path.join(root, 'Saved', 'Orchestrator', 'audio_v1');
  fs.mkdirSync(path.join(v1Dir, 'voice'), { recursive: true });
  fs.mkdirSync(path.join(v1Dir, 'music'), { recursive: true });

  fs.writeFileSync(path.join(v1Dir, 'voice', 'cmd_contact.wav'), Buffer.from('RIFF0000WAVEfmt ', 'ascii'));
  fs.writeFileSync(path.join(v1Dir, 'music', 'explore_calm_v1.wav'), Buffer.from('RIFF0000WAVEfmt ', 'ascii'));
  fs.writeFileSync(
    path.join(v1Dir, 'catalog.json'),
    JSON.stringify({
      version: 1,
      items: {
        'voice/cmd_contact': {
          id: 'cmd_contact',
          kind: 'voice',
          speaker: 'commander',
          voice: 'Charon',
          text: 'Contact!',
          prompt: 'Urgent',
          model: 'gemini-3.8-flash-tts',
          duration_sec: 1.08,
          cost_usd: 0.000244,
        },
        'music/explore_calm_v1': {
          id: 'explore_calm_v1',
          kind: 'music',
          state: 'explore_calm',
          cue_group: 'explore_calm',
          variant: 'v1',
          loop: true,
          prompt: 'Sparse arctic drone',
          model: 'lyria-3-clip-preview',
          duration_sec: 30.77,
          cost_usd: 0.04,
        },
      },
    }),
    'utf-8'
  );

  return {
    root,
    cleanup: () => fs.rmSync(root, { recursive: true, force: true }),
  };
}

test('audioApi: resolveSafeWavPath allows bare IDs inside fixed folder and blocks escapes', () => {
  const paths = resolveAudioPaths('/tmp/example');
  const safe = resolveSafeWavPath(paths.generatedDir, 'voice', 'cmd_contact');
  assert.ok(safe && safe.endsWith(path.join('Saved', 'AudioGen', 'voice', 'cmd_contact.wav')));

  assert.equal(resolveSafeWavPath(paths.generatedDir, 'voice', '../secret'), null);
  assert.equal(resolveSafeWavPath(paths.generatedDir, 'voice', 'sub/cmd_contact'), null);
  assert.equal(resolveSafeWavPath(paths.generatedDir, 'sfx' as any, 'cmd_contact'), null);
});

test('audioApi: loads standalone audio_v1/catalog.json and writes audio_approvals.json next to catalog', () => {
  const { root, cleanup } = makeTempV1CatalogProject();
  try {
    const paths = resolveAudioPaths(root);
    assert.ok(paths.generatedDir.endsWith(path.join('Saved', 'Orchestrator', 'audio_v1')));
    assert.equal(path.dirname(paths.approvalsPath), path.dirname(paths.catalogPath));

    const listBefore = buildAudioListResponse(paths);
    assert.equal(listBefore.catalogDir, 'Saved/Orchestrator/audio_v1');
    assert.equal(listBefore.items.length, 2);
    assert.equal(listBefore.items[0].wavExists, true);
    assert.equal(listBefore.items[1].cue_group, 'explore_calm');
    assert.equal(listBefore.items[1].wavExists, true);

    const badTraversal = applyAudioReview(
      { kind: 'voice', id: '../oops', status: 'approved', notes: '' },
      paths
    );
    assert.equal(badTraversal.status, 400);

    const unknownItem = applyAudioReview(
      { kind: 'voice', id: 'not_in_catalog', status: 'approved', notes: '' },
      paths
    );
    assert.equal(unknownItem.status, 404);

    const ok = applyAudioReview(
      { kind: 'music', id: 'explore_calm_v1', status: 'rejected', notes: 'Too loud at 0:18.' },
      paths
    );
    assert.equal(ok.status, 200);
    assert.ok(fs.existsSync(paths.approvalsPath));
    const saved = JSON.parse(fs.readFileSync(paths.approvalsPath, 'utf-8'));
    assert.equal(saved.approvals['music/explore_calm_v1'].status, 'rejected');
    assert.equal(saved.approvals['music/explore_calm_v1'].cue_group, 'explore_calm');
    assert.equal(saved.approvals['music/explore_calm_v1'].notes, 'Too loud at 0:18.');
  } finally {
    cleanup();
  }
});

test('audioApi: isSameOriginOrLocal rejects non-local cross-origin POSTs', () => {
  assert.equal(
    isSameOriginOrLocal({ headers: { host: 'localhost:5173', origin: 'http://localhost:5173' }, on: () => {} }),
    true
  );
  assert.equal(
    isSameOriginOrLocal({ headers: { host: 'localhost:5173', origin: 'https://evil.example' }, on: () => {} }),
    false
  );
  assert.equal(
    isSameOriginOrLocal({ headers: { host: 'localhost:5173', origin: 'http://localhost:9999' }, on: () => {} }),
    false
  );
});
