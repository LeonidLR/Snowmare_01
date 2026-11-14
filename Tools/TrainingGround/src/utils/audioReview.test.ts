import test from 'node:test';
import assert from 'node:assert/strict';
import {
  isValidAudioId,
  isValidAudioKind,
  validateReviewPayload,
  applyApprovalPatch,
  summarizeAudioCounts,
  extractFilterOptions,
  filterAudioItems,
  type AudioItemEntry,
} from './audioReview.ts';

const SAMPLE_ITEMS: AudioItemEntry[] = [
  {
    key: 'voice/cmd_contact',
    id: 'cmd_contact',
    kind: 'voice',
    wavExists: true,
    streamUrl: '/api/audio/file?kind=voice&id=cmd_contact',
    model: 'gemini-3.8-flash-tts',
    prompt: 'Urgent',
    text: 'Contact!',
    speaker: 'commander',
    voice: 'Charon',
    style: 'Urgent',
    state: '',
    seed: null,
    duration_sec: 1.2,
    cost_usd: 0.0005,
    generated_at: '2026-11-12T09:00:00Z',
  },
  {
    key: 'voice/med_tripwire',
    id: 'med_tripwire',
    kind: 'voice',
    wavExists: true,
    streamUrl: '/api/audio/file?kind=voice&id=med_tripwire',
    model: 'gemini-3.8-flash-tts',
    prompt: 'Urgent warning',
    text: 'Tripwire!',
    speaker: 'medic_sapper',
    voice: 'Orus',
    style: 'Urgent warning',
    state: '',
    seed: null,
    duration_sec: 2.1,
    cost_usd: 0.0006,
    generated_at: '2026-11-12T09:05:00Z',
  },
  {
    key: 'music/explore_calm_v1',
    id: 'explore_calm_v1',
    kind: 'music',
    wavExists: false,
    streamUrl: '/api/audio/file?kind=music&id=explore_calm_v1',
    model: 'lyria-3-clip-preview',
    prompt: 'Arctic drone',
    text: '',
    speaker: '',
    voice: '',
    style: '',
    state: 'explore_calm',
    cue_group: 'explore_calm',
    variant: 'v1',
    loop: true,
    seed: null,
    duration_sec: null,
    cost_usd: null,
    generated_at: null,
  },
];

test('audioReview: validates bare ids and kinds strictly', () => {
  assert.equal(isValidAudioKind('voice'), true);
  assert.equal(isValidAudioKind('music'), true);
  assert.equal(isValidAudioKind('sfx'), false);

  assert.equal(isValidAudioId('cmd_contact'), true);
  assert.equal(isValidAudioId('explore_calm_v1'), true);
  assert.equal(isValidAudioId('../cmd_contact'), false);
  assert.equal(isValidAudioId('voice/cmd_contact'), false);
  assert.equal(isValidAudioId('cmd_contact.wav'), false);
  assert.equal(isValidAudioId(''), false);
});

test('audioReview: validateReviewPayload rejects traversal and invalid status', () => {
  assert.equal(validateReviewPayload({ kind: 'voice', id: '../evil', status: 'approved', notes: '' }).ok, false);
  assert.equal(validateReviewPayload({ kind: 'voice', id: 'cmd_contact', status: 'hacked', notes: '' }).ok, false);
  const okRes = validateReviewPayload({
    kind: 'voice',
    id: 'cmd_contact',
    status: 'approved',
    notes: 'Crisp radio delivery',
  });
  assert.equal(okRes.ok, true);
  if (okRes.ok) {
    assert.equal(okRes.value.status, 'approved');
    assert.equal(okRes.value.notes, 'Crisp radio delivery');
  }
});

test('audioReview: applyApprovalPatch, summarizeAudioCounts & speaker/cue filtering', () => {
  const updated = applyApprovalPatch(
    { version: 1, approvals: {} },
    { kind: 'voice', id: 'cmd_contact', status: 'approved', notes: 'Good', model: 'gemini-3.8-flash-tts' },
    '2026-11-12T10:00:00Z'
  );
  const summary = summarizeAudioCounts(SAMPLE_ITEMS, updated);
  assert.deepEqual(summary, { total: 3, generated: 2, approved: 1, rejected: 0, pending: 2 });

  const opts = extractFilterOptions(SAMPLE_ITEMS);
  assert.deepEqual(opts.speakers, ['commander', 'medic_sapper']);
  assert.deepEqual(opts.cues, ['explore_calm']);

  assert.deepEqual(
    filterAudioItems(SAMPLE_ITEMS, updated, { speaker: 'medic_sapper' }).map((i) => i.id),
    ['med_tripwire']
  );
  assert.deepEqual(
    filterAudioItems(SAMPLE_ITEMS, updated, { cue: 'explore_calm' }).map((i) => i.id),
    ['explore_calm_v1']
  );
  assert.deepEqual(
    filterAudioItems(SAMPLE_ITEMS, updated, { status: 'approved' }).map((i) => i.id),
    ['cmd_contact']
  );
});
