// Training Ground: dev-server endpoints for Audio v1 review panel.
// Endpoints:
//   GET  /api/audio/list                  -> { catalogDir, approvalsFile, items, approvals, spendSummary }
//   GET  /api/audio/file?kind=...&id=...  -> streams WAV from fixed <audioDir>/<kind>/<id>.wav
//   POST /api/audio/review                -> atomically updates <audioDir>/audio_approvals.json next to catalog.json

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { writeAtomic, readBody } from './hudLabelsApi.ts';
import {
  type AudioApprovalsFile,
  type AudioItemEntry,
  type AudioKind,
  type AudioListResponse,
  type AudioSpendSummary,
  applyApprovalPatch,
  isValidAudioId,
  isValidAudioKind,
  makeAudioKey,
  validateReviewPayload,
} from '../src/utils/audioReview.ts';

const TOOL_DIR = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const DEFAULT_ROOT = path.resolve(TOOL_DIR, '../..');

export interface AudioPaths {
  root: string;
  voiceManifestPath: string;
  musicManifestPath: string;
  approvalsPath: string;
  generatedDir: string;
  catalogPath: string;
  spendLedgerPath: string;
}

export function resolveAudioPaths(rootDir: string = DEFAULT_ROOT): AudioPaths {
  const root = path.resolve(rootDir);
  const v1Dir = path.join(root, 'Saved', 'Orchestrator', 'audio_v1');
  const genDir = path.join(root, 'Saved', 'AudioGen');
  // Prefer Saved/Orchestrator/audio_v1 when present (v1 bundle); fallback to Saved/AudioGen.
  const useV1 = fs.existsSync(path.join(v1Dir, 'catalog.json')) || fs.existsSync(v1Dir);
  const generatedDir = useV1 ? v1Dir : genDir;
  const localLedger = path.join(generatedDir, 'spend.jsonl');
  const spendLedgerPath = fs.existsSync(localLedger) ? localLedger : path.join(genDir, 'spend.jsonl');

  return {
    root,
    voiceManifestPath: path.join(root, 'Content', 'Data', 'Audio', 'voice_lines.json'),
    musicManifestPath: path.join(root, 'Content', 'Data', 'Audio', 'music_cues.json'),
    approvalsPath: path.join(generatedDir, 'audio_approvals.json'),
    generatedDir,
    catalogPath: path.join(generatedDir, 'catalog.json'),
    spendLedgerPath,
  };
}

export type Req = {
  method?: string;
  url?: string;
  headers?: Record<string, string | string[] | undefined>;
  on: (ev: string, cb: (chunk?: unknown) => void) => void;
};
export type Res = {
  statusCode: number;
  setHeader: (k: string, v: string) => void;
  end: (body?: string | Buffer) => void;
};
export type Use = (route: string, handler: (req: Req, res: Res, next?: () => void) => void) => void;

const LOCAL_ORIGIN_RE = /^https?:\/\/(localhost|127\.0\.0\.1)(:\d+)?$/i;

export function isSameOriginOrLocal(req: Req): boolean {
  const originRaw = req.headers?.origin;
  const origin = Array.isArray(originRaw) ? originRaw[0] : originRaw;
  if (origin && !LOCAL_ORIGIN_RE.test(origin)) {
    return false;
  }
  const hostRaw = req.headers?.host;
  const host = Array.isArray(hostRaw) ? hostRaw[0] : hostRaw;
  if (origin && host) {
    try {
      const originHost = new URL(origin).host.toLowerCase();
      if (originHost !== host.toLowerCase()) {
        return false;
      }
    } catch {
      return false;
    }
  }
  return true;
}

function sendJson(res: Res, status: number, data: unknown): void {
  res.statusCode = status;
  res.setHeader('Content-Type', 'application/json; charset=utf-8');
  res.end(JSON.stringify(data));
}

function safeReadJson(filePath: string): Record<string, unknown> | null {
  if (!fs.existsSync(filePath)) return null;
  try {
    const parsed = JSON.parse(fs.readFileSync(filePath, 'utf-8'));
    return parsed && typeof parsed === 'object' ? (parsed as Record<string, unknown>) : null;
  } catch {
    return null;
  }
}

export function readApprovalsFile(approvalsPath: string): AudioApprovalsFile {
  const raw = safeReadJson(approvalsPath);
  if (raw && raw.approvals && typeof raw.approvals === 'object') {
    return {
      version: 1,
      approvals: raw.approvals as AudioApprovalsFile['approvals'],
    };
  }
  return { version: 1, approvals: {} };
}

export function readSpendSummary(ledgerPath: string): AudioSpendSummary {
  if (!fs.existsSync(ledgerPath)) {
    return { total_usd: 0, entries_count: 0 };
  }
  let total = 0;
  let count = 0;
  const lines = fs.readFileSync(ledgerPath, 'utf-8').split(/\r?\n/);
  for (const line of lines) {
    const trimmed = line.trim();
    if (!trimmed) continue;
    try {
      const parsed = JSON.parse(trimmed) as { usd?: unknown };
      total += Number(parsed.usd ?? 0) || 0;
      count += 1;
    } catch {
      // ignore malformed line
    }
  }
  return { total_usd: Math.round(total * 1e6) / 1e6, entries_count: count };
}

export function resolveSafeWavPath(generatedDir: string, kind: AudioKind, id: string): string | null {
  if (!isValidAudioKind(kind) || !isValidAudioId(id)) {
    return null;
  }
  const kindRoot = path.resolve(generatedDir, kind);
  const candidate = path.resolve(kindRoot, `${id}.wav`);
  if (!candidate.startsWith(kindRoot + path.sep)) {
    return null;
  }
  return candidate;
}

export function buildAudioListResponse(paths: AudioPaths = resolveAudioPaths()): AudioListResponse {
  const catalog = safeReadJson(paths.catalogPath);
  const catalogItems =
    catalog && catalog.items && typeof catalog.items === 'object'
      ? (catalog.items as Record<string, Record<string, unknown>>)
      : {};
  const voiceManifest = safeReadJson(paths.voiceManifestPath);
  const musicManifest = safeReadJson(paths.musicManifestPath);

  const items: AudioItemEntry[] = [];
  const seenKeys = new Set<string>();

  const buildEntry = (kind: AudioKind, id: string, raw: Record<string, unknown>, defaultModel: string): void => {
    if (!isValidAudioId(id)) return;
    const key = makeAudioKey(kind, id);
    if (seenKeys.has(key)) return;
    seenKeys.add(key);

    const cat = catalogItems[key] ?? {};
    const wavPath = resolveSafeWavPath(paths.generatedDir, kind, id);
    const wavExists = Boolean(wavPath && fs.existsSync(wavPath));

    const promptVal = cat.prompt ?? raw.prompt ?? raw.style ?? '';
    const textVal = cat.text ?? raw.text ?? '';
    const speakerVal = cat.speaker ?? raw.speaker ?? '';
    const voiceVal = cat.voice ?? raw.voice ?? '';
    const styleVal = cat.style ?? raw.style ?? '';
    const stateVal = cat.state ?? raw.state ?? '';
    const cueGroupVal = cat.cue_group ?? raw.cue_group ?? stateVal;
    const variantVal = cat.variant ?? raw.variant ?? '';
    const sourceVal = cat.source ?? raw.source;
    const categoryVal = cat.category ?? raw.category;
    const loopVal = typeof cat.loop === 'boolean' ? cat.loop : typeof raw.loop === 'boolean' ? raw.loop : undefined;

    items.push({
      key,
      id,
      kind,
      wavExists,
      streamUrl: `/api/audio/file?kind=${encodeURIComponent(kind)}&id=${encodeURIComponent(id)}`,
      model: String(cat.model ?? raw.model ?? defaultModel),
      prompt: String(promptVal),
      text: String(textVal),
      speaker: String(speakerVal),
      voice: String(voiceVal),
      style: String(styleVal),
      state: String(stateVal),
      cue_group: cueGroupVal ? String(cueGroupVal) : undefined,
      variant: variantVal ? String(variantVal) : undefined,
      loop: loopVal,
      source: typeof sourceVal === 'string' && sourceVal ? sourceVal : undefined,
      category: typeof categoryVal === 'string' && categoryVal ? categoryVal : undefined,
      seed: typeof cat.seed === 'number' ? cat.seed : null,
      duration_sec: typeof cat.duration_sec === 'number' ? cat.duration_sec : null,
      cost_usd: typeof cat.cost_usd === 'number' ? cat.cost_usd : null,
      generated_at: typeof cat.generated_at === 'string' ? cat.generated_at : null,
    });
  };

  // 1. Primary source: catalog.json (works standalone with Saved/Orchestrator/audio_v1/catalog.json)
  for (const [catKey, rawEntry] of Object.entries(catalogItems)) {
    if (!rawEntry || typeof rawEntry !== 'object') continue;
    const kind = isValidAudioKind(rawEntry.kind)
      ? rawEntry.kind
      : catKey.startsWith('music/')
        ? 'music'
        : catKey.startsWith('voice/')
          ? 'voice'
          : null;
    const id = String(rawEntry.id ?? catKey.split('/')[1] ?? '');
    if (!kind || !isValidAudioId(id)) continue;
    buildEntry(kind, id, rawEntry, kind === 'voice' ? 'gemini-3.8-flash-tts' : 'lyria-3-clip-preview');
  }

  // 2. Append any un-generated items declared in Content/Data/Audio manifests (when present)
  const voiceModel = typeof voiceManifest?.model === 'string' ? voiceManifest.model : 'gemini-3.8-flash-tts';
  const voiceLines = Array.isArray(voiceManifest?.lines) ? voiceManifest.lines : [];
  for (const line of voiceLines) {
    if (line && typeof line === 'object') {
      const rec = line as Record<string, unknown>;
      buildEntry('voice', String(rec.id ?? ''), rec, voiceModel);
    }
  }

  const musicModel =
    typeof musicManifest?.default_model === 'string' ? musicManifest.default_model : 'lyria-3-clip-preview';
  const musicCues = Array.isArray(musicManifest?.cues) ? musicManifest.cues : [];
  for (const cue of musicCues) {
    if (cue && typeof cue === 'object') {
      const rec = cue as Record<string, unknown>;
      buildEntry('music', String(rec.id ?? ''), rec, musicModel);
    }
  }

  return {
    catalogDir: path.relative(paths.root, paths.generatedDir).replace(/\\/g, '/'),
    approvalsFile: path.relative(paths.root, paths.approvalsPath).replace(/\\/g, '/'),
    items,
    approvals: readApprovalsFile(paths.approvalsPath),
    spendSummary: readSpendSummary(paths.spendLedgerPath),
  };
}

export function applyAudioReview(
  rawBody: unknown,
  paths: AudioPaths = resolveAudioPaths()
): { status: number; body: Record<string, unknown> } {
  const check = validateReviewPayload(rawBody);
  if (!check.ok) {
    return { status: 400, body: { error: check.error } };
  }
  const listing = buildAudioListResponse(paths);
  const key = makeAudioKey(check.value.kind, check.value.id);
  const matched = listing.items.find((item) => item.key === key);
  if (!matched) {
    return { status: 404, body: { error: `Unknown catalog/manifest audio item: ${key}` } };
  }

  fs.mkdirSync(path.dirname(paths.approvalsPath), { recursive: true });
  const current = readApprovalsFile(paths.approvalsPath);
  const next = applyApprovalPatch(current, {
    ...check.value,
    model: matched.model,
    speaker: matched.speaker || undefined,
    cue_group: matched.cue_group || matched.state || undefined,
    text: matched.text || undefined,
  });
  writeAtomic(paths.approvalsPath, JSON.stringify(next, null, 2) + '\n');
  return {
    status: 200,
    body: {
      success: true,
      key,
      record: next.approvals[key],
      approvals: next,
    },
  };
}

export function setupAudioApi(use: Use, resolvePathsFn: () => AudioPaths = () => resolveAudioPaths()): void {
  use('/api/audio/list', (req, res) => {
    if (req.method !== 'GET') {
      sendJson(res, 405, { error: 'Method Not Allowed' });
      return;
    }
    try {
      sendJson(res, 200, buildAudioListResponse(resolvePathsFn()));
    } catch (err: unknown) {
      sendJson(res, 500, { error: String(err) });
    }
  });

  use('/api/audio/file', (req, res) => {
    if (req.method !== 'GET') {
      sendJson(res, 405, { error: 'Method Not Allowed' });
      return;
    }
    try {
      const paths = resolvePathsFn();
      const parsedUrl = new URL(req.url || '/', 'http://localhost');
      const kindParam = parsedUrl.searchParams.get('kind');
      const idParam = parsedUrl.searchParams.get('id');
      if (!isValidAudioKind(kindParam) || !isValidAudioId(idParam)) {
        sendJson(res, 400, { error: 'Invalid kind or bare audio id.' });
        return;
      }
      const wavPath = resolveSafeWavPath(paths.generatedDir, kindParam, idParam);
      if (!wavPath || !fs.existsSync(wavPath)) {
        sendJson(res, 404, { error: 'Audio file not found in active catalog directory.' });
        return;
      }
      const buf = fs.readFileSync(wavPath);
      const rangeHeader = req.headers?.range;
      const rangeStr = Array.isArray(rangeHeader) ? rangeHeader[0] : rangeHeader;
      res.setHeader('Content-Type', 'audio/wav');
      res.setHeader('Accept-Ranges', 'bytes');
      if (rangeStr && /^bytes=\d+-\d*$/i.test(rangeStr)) {
        const [startStr, endStr] = rangeStr.replace(/^bytes=/i, '').split('-');
        const start = Number.parseInt(startStr, 10);
        const end = endStr ? Math.min(buf.length - 1, Number.parseInt(endStr, 10)) : buf.length - 1;
        if (start >= 0 && start <= end && end < buf.length) {
          res.statusCode = 206;
          res.setHeader('Content-Range', `bytes ${start}-${end}/${buf.length}`);
          res.setHeader('Content-Length', String(end - start + 1));
          res.end(buf.subarray(start, end + 1));
          return;
        }
      }
      res.statusCode = 200;
      res.setHeader('Content-Length', String(buf.length));
      res.end(buf);
    } catch (err: unknown) {
      sendJson(res, 500, { error: String(err) });
    }
  });

  use('/api/audio/review', async (req, res) => {
    if (req.method !== 'POST') {
      sendJson(res, 405, { error: 'Method Not Allowed' });
      return;
    }
    if (!isSameOriginOrLocal(req)) {
      sendJson(res, 403, { error: 'Cross-origin POST rejected.' });
      return;
    }
    try {
      const body = await readBody(req);
      const result = applyAudioReview(body, resolvePathsFn());
      sendJson(res, result.status, result.body);
    } catch (err: unknown) {
      sendJson(res, 400, { error: String(err) });
    }
  });
}
