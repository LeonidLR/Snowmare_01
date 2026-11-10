// Training Ground department «Генератор уровней» (docs/design/levelgen/LEVEL_GENERATOR.md): dev-server endpoints (vite.config.ts).
// Generation and evaluation run IN THE BROWSER (pure TS); the server only reads the reference profiles, serves the reference
// image, saves accepted layouts, keeps the critic ledger and runs the critics (the API keys live on this machine, never in the page).
//   GET  /api/levelgen/profiles                 -> { profiles: [{ id, title, size_m }] }   (Content/Data/LevelGen/refs/*.json)
//   GET  /api/levelgen/profile?id=<id>          -> the profile JSON
//   GET  /api/levelgen/ref-image?id=<id>        -> image/png: the reference's un-skewed annotated map, 1200 px (made on demand by
//                                                  Scripts/Tools/levelgen_render.py from Saved/MapRefs; 404 when the refs are absent)
//   GET  /api/levelgen/accepted                 -> { accepted: [{ name, seed, profile, at }] }   (Content/Data/LevelGen/accepted)
//   GET  /api/levelgen/ledger                   -> the critic ledger { spentUsd, calls }
//   POST /api/levelgen/critic   { name, layout, evaluation, candidatePng (base64), client: claude|jev|mock, budgetUsd, model }
//                                               -> { verdict } (saved to Saved/TrainingGround/levelgen/<name>.verdicts.json; the ledger updated)
//   POST /api/levelgen/accept   { layout, evaluation, verdicts }  -> { file }  (Content/Data/LevelGen/accepted/<name>.json + <name>.meta.json)
//   POST /api/levelgen/reject   { name, seed, profile, reason }    -> appended to Saved/TrainingGround/levelgen/rejected.jsonl
import fs from 'node:fs';
import path from 'node:path';
import { claudeCritic, emptyLedger, jevCritic, mockCritic } from '../src/levelgen/critic.ts';
import type { CriticClient, CriticVerdict } from '../src/levelgen/critic.ts';
import type { Evaluation } from '../src/levelgen/evaluate.ts';
import { ACCEPTED_DIR, ensureReferencePng, LEVELGEN_OUT, listProfiles, NAME_RE, readLedger, readProfileById, writeLedger } from '../src/levelgen/node/files.ts';
import type { GenLayout } from '../src/levelgen/schema.ts';
import { validateGenLayout } from '../src/levelgen/schema.ts';
import { setupPropsApi } from './propsApi.ts';

type Req = { method?: string; url?: string; on: (ev: string, cb: (chunk?: unknown) => void) => void };
type Res = { statusCode: number; setHeader: (k: string, v: string) => void; end: (body?: string | Buffer) => void };
type Use = (route: string, handler: (req: Req, res: Res, next?: () => void) => void) => void;

const send = (res: Res, status: number, data: unknown) => {
  res.statusCode = status;
  res.setHeader('Content-Type', 'application/json; charset=utf-8');
  res.end(JSON.stringify(data));
};
const readBody = (req: Req): Promise<Record<string, unknown>> => new Promise((resolve) => {
  let body = '';
  req.on('data', (c) => { body += String(c); });
  req.on('end', () => { try { resolve(body ? JSON.parse(body) : {}); } catch { resolve({}); } });
});
const query = (req: Req, key: string) => new URL(req.url ?? '', 'http://x').searchParams.get(key) ?? '';

export function setupLevelgenApi(use: Use): void {
  setupPropsApi(use);

  use('/api/levelgen/profiles', (_req, res) => {
    try { send(res, 200, { profiles: listProfiles() }); } catch (e) { send(res, 500, { error: String(e) }); }
  });

  use('/api/levelgen/profile', (req, res) => {
    const id = query(req, 'id');
    if (!NAME_RE.test(id)) return send(res, 400, { error: 'bad profile id' });
    try { send(res, 200, readProfileById(id)); } catch (e) { send(res, 404, { error: e instanceof Error ? e.message : String(e) }); }
  });

  use('/api/levelgen/ref-image', (_req, res) => {
    const png = ensureReferencePng();
    if (!png) return send(res, 404, { error: 'reference images not found (Saved/MapRefs) or python/PIL missing' });
    res.statusCode = 200;
    res.setHeader('Content-Type', 'image/jpeg');
    res.setHeader('Cache-Control', 'no-cache');
    res.end(fs.readFileSync(png));
  });

  use('/api/levelgen/accepted', (_req, res) => {
    if (!fs.existsSync(ACCEPTED_DIR)) return send(res, 200, { accepted: [] });
    const accepted = fs.readdirSync(ACCEPTED_DIR).filter((f) => f.endsWith('.json') && !f.endsWith('.meta.json')).map((f) => {
      const l = JSON.parse(fs.readFileSync(path.join(ACCEPTED_DIR, f), 'utf8')) as GenLayout;
      const metaFile = path.join(ACCEPTED_DIR, f.replace(/\.json$/, '.meta.json'));
      const meta = fs.existsSync(metaFile) ? JSON.parse(fs.readFileSync(metaFile, 'utf8')) as { at?: string } : {};
      return { name: l.name, seed: l.seed, profile: l.profile, at: meta.at ?? '' };
    });
    send(res, 200, { accepted });
  });

  use('/api/levelgen/ledger', (_req, res) => send(res, 200, readLedger() ?? emptyLedger()));

  use('/api/levelgen/critic', async (req, res, next) => {
    if (req.method !== 'POST') return next ? next() : send(res, 405, { error: 'POST only' });
    const b = await readBody(req);
    const name = String(b.name ?? '');
    if (!NAME_RE.test(name)) return send(res, 400, { error: 'bad layout name' });
    const evaluation = b.evaluation as Evaluation | undefined;
    if (!evaluation || typeof evaluation !== 'object') return send(res, 400, { error: 'evaluation missing' });
    const client = (['claude', 'jev', 'mock'].includes(String(b.client)) ? String(b.client) : 'mock') as CriticClient;
    let verdict: CriticVerdict;
    try {
      const profile = readProfileById(String(b.profile ?? evaluation.profile));
      const ledger = readLedger() ?? emptyLedger();
      if (client === 'claude') {
        const candidatePng = String(b.candidatePng ?? '').replace(/^data:image\/png;base64,/, '');
        if (!candidatePng) return send(res, 400, { error: 'candidatePng (base64) missing - the page renders the canvas' });
        const ref = ensureReferencePng();
        verdict = await claudeCritic({ layoutName: name, evaluation, profile, candidatePng, referencePng: ref ? fs.readFileSync(ref).toString('base64') : null,
          budgetUsd: Math.max(0, Math.min(50, Number(b.budgetUsd) || 2)), ledger, model: typeof b.model === 'string' ? b.model : undefined });
        if (verdict.costUsd > 0) {
          ledger.spentUsd = Math.round((ledger.spentUsd + verdict.costUsd) * 1e6) / 1e6;
          ledger.calls.push({ at: verdict.at, layout: name, client, model: verdict.model, costUsd: verdict.costUsd, fits: verdict.fits });
          writeLedger(ledger);
        }
      } else if (client === 'jev') verdict = await jevCritic({ evaluation, profile });
      else verdict = mockCritic(evaluation);
      fs.mkdirSync(LEVELGEN_OUT, { recursive: true });
      if (b.layout) fs.writeFileSync(path.join(LEVELGEN_OUT, `${name}.json`), JSON.stringify(b.layout, null, 1) + '\n');
      fs.writeFileSync(path.join(LEVELGEN_OUT, `${name}.eval.json`), JSON.stringify(evaluation, null, 1) + '\n');
      const vf = path.join(LEVELGEN_OUT, `${name}.verdicts.json`);
      const all: CriticVerdict[] = fs.existsSync(vf) ? JSON.parse(fs.readFileSync(vf, 'utf8')) : [];
      all.push(verdict);
      fs.writeFileSync(vf, JSON.stringify(all, null, 1) + '\n');
      send(res, 200, { verdict, ledger: readLedger() ?? emptyLedger() });
    } catch (e) {
      send(res, 500, { error: e instanceof Error ? e.message : String(e) });
    }
  });

  use('/api/levelgen/accept', async (req, res, next) => {
    if (req.method !== 'POST') return next ? next() : send(res, 405, { error: 'POST only' });
    const b = await readBody(req);
    const layout = b.layout as GenLayout | undefined;
    const errors = validateGenLayout(layout);
    if (errors.length) return send(res, 400, { error: errors.join('; ') });
    if (!NAME_RE.test(layout!.name)) return send(res, 400, { error: 'bad layout name' });
    fs.mkdirSync(ACCEPTED_DIR, { recursive: true });
    const file = path.join(ACCEPTED_DIR, `${layout!.name}.json`);
    fs.writeFileSync(file, JSON.stringify(layout, null, 1) + '\n');
    const ev = b.evaluation as Evaluation | undefined;
    fs.writeFileSync(path.join(ACCEPTED_DIR, `${layout!.name}.meta.json`), JSON.stringify({ at: new Date().toISOString(), score: ev?.score, summary: ev?.summary, verdicts: b.verdicts ?? [] }, null, 1) + '\n');
    send(res, 200, { file: path.relative(path.resolve(ACCEPTED_DIR, '../../../..'), file).split(path.sep).join('/') });
  });

  use('/api/levelgen/reject', async (req, res, next) => {
    if (req.method !== 'POST') return next ? next() : send(res, 405, { error: 'POST only' });
    const b = await readBody(req);
    fs.mkdirSync(LEVELGEN_OUT, { recursive: true });
    fs.appendFileSync(path.join(LEVELGEN_OUT, 'rejected.jsonl'), JSON.stringify({ at: new Date().toISOString(), name: b.name, seed: b.seed, profile: b.profile, reason: b.reason ?? '' }) + '\n');
    send(res, 200, { ok: true });
  });
}
