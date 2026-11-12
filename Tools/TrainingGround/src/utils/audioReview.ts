export type AudioKind = 'voice' | 'music';
export type AudioApprovalDecision = 'pending' | 'approved' | 'rejected';

export interface AudioItemEntry {
  key: string;
  id: string;
  kind: AudioKind;
  wavExists: boolean;
  streamUrl: string;
  model: string;
  prompt: string;
  text: string;
  speaker: string;
  voice: string;
  style: string;
  state: string;
  cue_group?: string;
  variant?: string;
  loop?: boolean;
  source?: string;
  category?: string;
  seed: number | null;
  duration_sec: number | null;
  cost_usd: number | null;
  generated_at: string | null;
}

export interface AudioApprovalRecord {
  status: AudioApprovalDecision;
  notes: string;
  updated_at: string;
  model?: string;
  speaker?: string;
  cue_group?: string;
  text?: string;
}

export interface AudioApprovalsFile {
  version: number;
  approvals: Record<string, AudioApprovalRecord>;
}

export interface AudioSpendSummary {
  total_usd: number;
  entries_count: number;
}

export interface AudioListResponse {
  catalogDir?: string;
  approvalsFile?: string;
  items: AudioItemEntry[];
  approvals: AudioApprovalsFile;
  spendSummary: AudioSpendSummary;
}

export interface ReviewPatchInput {
  kind: AudioKind;
  id: string;
  status: AudioApprovalDecision;
  notes: string;
  model?: string;
  speaker?: string;
  cue_group?: string;
  text?: string;
}

export interface AudioFilterCriteria {
  kind?: 'all' | AudioKind;
  status?: 'all' | AudioApprovalDecision;
  speaker?: string;
  cue?: string;
  query?: string;
}

export const AUDIO_ID_RE = /^[a-zA-Z0-9_-]{1,80}$/;

export function isValidAudioKind(value: unknown): value is AudioKind {
  return value === 'voice' || value === 'music';
}

export function isValidAudioId(value: unknown): value is string {
  return typeof value === 'string' && AUDIO_ID_RE.test(value);
}

export function makeAudioKey(kind: AudioKind, id: string): string {
  return `${kind}/${id}`;
}

export function isValidDecision(value: unknown): value is AudioApprovalDecision {
  return value === 'pending' || value === 'approved' || value === 'rejected';
}

export function itemCueName(item: Pick<AudioItemEntry, 'cue_group' | 'state'>): string {
  return (item.cue_group || item.state || '').trim();
}

export function extractFilterOptions(items: AudioItemEntry[]): { speakers: string[]; cues: string[] } {
  const speakerSet = new Set<string>();
  const cueSet = new Set<string>();
  for (const item of items) {
    if (item.speaker && item.speaker.trim()) {
      speakerSet.add(item.speaker.trim());
    }
    const cue = itemCueName(item);
    if (cue) {
      cueSet.add(cue);
    }
  }
  return {
    speakers: Array.from(speakerSet).sort((a, b) => a.localeCompare(b)),
    cues: Array.from(cueSet).sort((a, b) => a.localeCompare(b)),
  };
}

export function filterAudioItems(
  items: AudioItemEntry[],
  approvals: AudioApprovalsFile | null | undefined,
  criteria: AudioFilterCriteria
): AudioItemEntry[] {
  const map = approvals?.approvals ?? {};
  const q = (criteria.query ?? '').trim().toLowerCase();
  const speakerFilter = (criteria.speaker ?? 'all').trim();
  const cueFilter = (criteria.cue ?? 'all').trim();
  const kindFilter = criteria.kind ?? 'all';
  const statusFilter = criteria.status ?? 'all';

  return items.filter((item) => {
    if (kindFilter !== 'all' && item.kind !== kindFilter) return false;
    const dec = map[item.key]?.status ?? 'pending';
    if (statusFilter !== 'all' && dec !== statusFilter) return false;
    if (speakerFilter && speakerFilter !== 'all' && item.speaker !== speakerFilter) return false;
    if (cueFilter && cueFilter !== 'all' && itemCueName(item) !== cueFilter) return false;
    if (!q) return true;
    return (
      item.id.toLowerCase().includes(q) ||
      item.text.toLowerCase().includes(q) ||
      item.prompt.toLowerCase().includes(q) ||
      item.speaker.toLowerCase().includes(q) ||
      item.voice.toLowerCase().includes(q) ||
      itemCueName(item).toLowerCase().includes(q) ||
      (item.source ?? '').toLowerCase().includes(q)
    );
  });
}

export function validateReviewPayload(
  raw: unknown
): { ok: true; value: ReviewPatchInput } | { ok: false; error: string } {
  if (!raw || typeof raw !== 'object') {
    return { ok: false, error: 'Request body must be a JSON object.' };
  }
  const obj = raw as Record<string, unknown>;
  if (!isValidAudioKind(obj.kind)) {
    return { ok: false, error: 'Invalid kind (expected "voice" or "music").' };
  }
  if (!isValidAudioId(obj.id)) {
    return { ok: false, error: 'Invalid bare audio id.' };
  }
  if (!isValidDecision(obj.status)) {
    return { ok: false, error: 'Invalid status (expected "pending", "approved", or "rejected").' };
  }
  const notes = typeof obj.notes === 'string' ? obj.notes.slice(0, 500) : '';
  return {
    ok: true,
    value: {
      kind: obj.kind,
      id: obj.id,
      status: obj.status,
      notes,
      model: typeof obj.model === 'string' ? obj.model : undefined,
      speaker: typeof obj.speaker === 'string' ? obj.speaker : undefined,
      cue_group: typeof obj.cue_group === 'string' ? obj.cue_group : undefined,
      text: typeof obj.text === 'string' ? obj.text : undefined,
    },
  };
}

export function applyApprovalPatch(
  current: AudioApprovalsFile | null | undefined,
  patch: ReviewPatchInput,
  timestampIso: string = new Date().toISOString()
): AudioApprovalsFile {
  const prevApprovals = current?.approvals && typeof current.approvals === 'object' ? current.approvals : {};
  const key = makeAudioKey(patch.kind, patch.id);
  const existing = prevApprovals[key];
  const record: AudioApprovalRecord = {
    status: patch.status,
    notes: patch.notes,
    updated_at: timestampIso,
    ...(patch.model ? { model: patch.model } : existing?.model ? { model: existing.model } : {}),
    ...(patch.speaker ? { speaker: patch.speaker } : existing?.speaker ? { speaker: existing.speaker } : {}),
    ...(patch.cue_group ? { cue_group: patch.cue_group } : existing?.cue_group ? { cue_group: existing.cue_group } : {}),
    ...(patch.text ? { text: patch.text } : existing?.text ? { text: existing.text } : {}),
  };
  return {
    version: 1,
    approvals: {
      ...prevApprovals,
      [key]: record,
    },
  };
}

export function summarizeAudioCounts(
  items: AudioItemEntry[],
  approvals: AudioApprovalsFile | null | undefined
): { total: number; generated: number; approved: number; rejected: number; pending: number } {
  let generated = 0;
  let approved = 0;
  let rejected = 0;
  let pending = 0;
  const map = approvals?.approvals ?? {};
  for (const item of items) {
    if (item.wavExists) generated += 1;
    const rec = map[item.key];
    const st = rec?.status ?? 'pending';
    if (st === 'approved') approved += 1;
    else if (st === 'rejected') rejected += 1;
    else pending += 1;
  }
  return { total: items.length, generated, approved, rejected, pending };
}
