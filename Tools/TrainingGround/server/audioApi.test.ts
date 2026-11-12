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

function makeTempProject(): { root: string; cleanup: () => void } {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'codex-audio-api-'));
  const audioData = path.join(root, 'Content', 'Data', 'Audio');
  const voiceGen = path.join(root, 'Saved', 'AudioGen', 'voice');
  const musicGen = path.join(root, 'Saved', 'AudioGen', 'music');
  fs.mkdirSync(audioData, { recursive: true });
  fs.mkdirSync(voiceGen, { recursive: true });
  fs.mkdirSync(musicGen, { recursive: true });

  fs.writeFileSync(
    path.join(audioData, 'voice_lines.json'),
    JSON.stringify({
      version: 1,
      model: 'gemini-3.8-flash-tts',
      lines: [
        { id: 'cmd_contact', speaker: 'commander', voice: 'Charon', style: 'Urgent', text: 'Contact!' },
      ],
    }),
    'utf-8'
  );
  fs.writeFileSync(
    path.join(audioData, 'music_cues.json'),
    JSON.stringify({
      version: 1,
      default_model: 'lyria-3-clip-preview',
      cues: [{ id: 'explore_calm', model: 'lyria-3-clip-preview', state: 'explore', prompt: 'Cold drone' }],
    }),
    'utf-8'
  );
  fs.writeFileSync(path.join(voiceGen, 'cmd_contact.wav'), Buffer.from('RIFF0000WAVEfmt ', 'ascii'));
  fs.writeFileSync(
    path.join(root, 'Saved', 'AudioGen', 'catalog.json'),
    JSON.stringify({
      version: 1,
      items: {
        'voice/cmd_contact': { id: 'cmd_contact', kind: 'voice', duration_sec: 1.08, cost_usd: 0.000244 },
      },
    }),
    'utf-8'
  );
  fs.writeFileSync(
    path.join(root, 'Saved', 'AudioGen', 'spend.jsonl'),
    JSON.stringify({ when: '2026-11-12T09:34:22+00:00', usd: 0.000244 }) + '\n',
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

test('audioApi: buildAudioListResponse and applyAudioReview update audio_approvals.json atomically', () => {
  const { root, cleanup } = makeTempProject();
  try {
    const paths = resolveAudioPaths(root);
    const listBefore = buildAudioListResponse(paths);
    assert.equal(listBefore.items.length, 2);
    assert.equal(listBefore.items[0].wavExists, true);
    assert.equal(listBefore.items[0].duration_sec, 1.08);
    assert.equal(listBefore.items[1].wavExists, false);
    assert.equal(listBefore.spendSummary.entries_count, 1);

    const badTraversal = applyAudioReview(
      { kind: 'voice', id: '../oops', status: 'approved', notes: '' },
      paths
    );
    assert.equal(badTraversal.status, 400);

    const unknownItem = applyAudioReview(
      { kind: 'voice', id: 'not_in_manifest', status: 'approved', notes: '' },
      paths
    );
    assert.equal(unknownItem.status, 404);

    const ok = applyAudioReview(
      { kind: 'voice', id: 'cmd_contact', status: 'approved', notes: 'Works great in snow wind.' },
      paths
    );
    assert.equal(ok.status, 200);
    assert.ok(fs.existsSync(paths.approvalsPath));
    const saved = JSON.parse(fs.readFileSync(paths.approvalsPath, 'utf-8'));
    assert.equal(saved.approvals['voice/cmd_contact'].status, 'approved');
    assert.equal(saved.approvals['voice/cmd_contact'].notes, 'Works great in snow wind.');
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
