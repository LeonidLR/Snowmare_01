// Training Ground / level generator: dev-server endpoints for props layers (codex.levelgen_props/1).
// Docs: docs/design/levelgen/PROPS_LAYERS.md sections 1.2, 4.1, 7; inbox/010-night-queue.md Step 4.
// Endpoints:
//   GET  /api/levelgen/props-list               -> { files: [{ id, name, map, layout, isFixture, filePath, revision, frozen }] }
//   GET  /api/levelgen/props?map=<name>         -> { props: LevelPropsFile, layout?: GenLayout, balance?: any, filePath: string }
//   PUT  /api/levelgen/props?map=<name>         -> { success: true, revision: number, props: LevelPropsFile }
//                                                  (refuses 403 if frozen; refuses 409 if stale revision; atomic write)
//   POST /api/levelgen/props/save               -> same as PUT
//   GET  /api/levelgen/layout?name=<name>       -> GenLayout JSON

import fs from 'node:fs';
import path from 'node:path';

import { ROOT } from '../src/movement/cli/paths.ts';
import type { GenLayout } from '../src/levelgen/schema.ts';
import type { LevelPropsFile, PropLayer, PropOverride } from '../src/levelgen/props/schema.ts';
import { formatPropsCanonical } from '../src/levelgen/props/canonical.ts';

export const PROPS_DIR = path.join(ROOT, 'Content/Data/LevelGen/props');
export const PROPS_FIXTURES_DIR = path.join(PROPS_DIR, 'fixtures');
export const ACCEPTED_DIR = path.join(ROOT, 'Content/Data/LevelGen/accepted');
export const BALANCE_PATH = path.join(ROOT, 'Content/Data/Balance/props_balance.json');

type Req = { method?: string; url?: string; on: (ev: string, cb: (chunk?: unknown) => void) => void };
type Res = { statusCode: number; setHeader: (k: string, v: string) => void; end: (body?: string) => void };
type Use = (route: string, handler: (req: Req, res: Res, next?: () => void) => void) => void;

const addCors = (res: Res) => {
  res.setHeader('Access-Control-Allow-Origin', '*');
  res.setHeader('Access-Control-Allow-Methods', 'GET, PUT, POST, OPTIONS');
  res.setHeader('Access-Control-Allow-Headers', 'Content-Type');
};

const sendJson = (res: Res, status: number, data: unknown) => {
  addCors(res);
  res.statusCode = status;
  res.setHeader('Content-Type', 'application/json; charset=utf-8');
  res.end(JSON.stringify(data));
};

export const readBody = (req: Req): Promise<Record<string, unknown>> =>
  new Promise((resolve, reject) => {
    let body = '';
    req.on('data', (c) => {
      body += String(c);
    });
    req.on('end', () => {
      try {
        resolve(body ? JSON.parse(body) : {});
      } catch (err) {
        reject(err);
      }
    });
  });

export function writeAtomic(targetPath: string, content: string): void {
  const dir = path.dirname(targetPath);
  fs.mkdirSync(dir, { recursive: true });
  const tmpPath = path.join(dir, `.tmp_${path.basename(targetPath)}_${Date.now()}_${Math.random().toString(36).slice(2, 8)}`);
  try {
    const fd = fs.openSync(tmpPath, 'w');
    fs.writeFileSync(fd, content, 'utf8');
    fs.fsyncSync(fd);
    fs.closeSync(fd);
    fs.renameSync(tmpPath, targetPath);
  } catch (err) {
    try {
      if (fs.existsSync(tmpPath)) fs.unlinkSync(tmpPath);
    } catch {
      // ignore unlink errors in cleanup
    }
    throw err;
  }
}

export interface PropsFileInfo {
  id: string;
  name: string;
  map: string;
  layout: string;
  isFixture: boolean;
  filePath: string;
  relativeFilePath: string;
  revision: number;
  frozen: boolean;
}

export function listPropsFiles(): PropsFileInfo[] {
  const results: PropsFileInfo[] = [];

  const scanDir = (dir: string, isFixture: boolean) => {
    if (!fs.existsSync(dir)) return;
    const entries = fs.readdirSync(dir, { withFileTypes: true });
    for (const ent of entries) {
      if (!ent.isFile()) continue;
      if (!ent.name.endsWith('.json')) continue;
      if (ent.name.includes('.layout.') || ent.name.includes('golden_cases')) continue;
      const fullPath = path.join(dir, ent.name);
      try {
        const parsed = JSON.parse(fs.readFileSync(fullPath, 'utf8'));
        if (parsed.schema?.startsWith('codex.levelgen_props/')) {
          const id = ent.name.replace(/\.props\.json$/, '').replace(/\.json$/, '');
          results.push({
            id,
            name: ent.name,
            map: parsed.map ?? id,
            layout: parsed.layout ?? '',
            isFixture,
            filePath: fullPath,
            relativeFilePath: path.relative(ROOT, fullPath).replace(/\\/g, '/'),
            revision: typeof parsed.revision === 'number' ? parsed.revision : 1,
            frozen: !!parsed.frozen,
          });
        }
      } catch {
        // Skip unparseable files
      }
    }
  };

  scanDir(PROPS_DIR, false);
  scanDir(PROPS_FIXTURES_DIR, true);

  return results;
}

export const BARE_NAME_RE = /^[A-Za-z0-9_.-]+$/;

export function isValidPropsName(name: string): boolean {
  if (!name || typeof name !== 'string') return false;
  if (name.includes('/') || name.includes('\\') || name.includes('..')) return false;
  return BARE_NAME_RE.test(name);
}

export function resolvePropsFile(mapOrName: string): { fullPath: string; isFixture: boolean } | null {
  if (!isValidPropsName(mapOrName)) {
    return null;
  }

  const clean = mapOrName.replace(/\.props\.json$/, '').replace(/\.json$/, '');
  const candidates: Array<{ p: string; dir: string; isFixture: boolean }> = [
    { p: path.join(PROPS_DIR, `${clean}.props.json`), dir: PROPS_DIR, isFixture: false },
    { p: path.join(PROPS_DIR, `${clean}.json`), dir: PROPS_DIR, isFixture: false },
    { p: path.join(PROPS_FIXTURES_DIR, `${clean}.props.json`), dir: PROPS_FIXTURES_DIR, isFixture: true },
    { p: path.join(PROPS_FIXTURES_DIR, `${clean}.json`), dir: PROPS_FIXTURES_DIR, isFixture: true },
    { p: path.join(PROPS_FIXTURES_DIR, `golden_${clean}.json`), dir: PROPS_FIXTURES_DIR, isFixture: true },
    { p: path.join(PROPS_FIXTURES_DIR, `golden_in_${clean}.props.json`), dir: PROPS_FIXTURES_DIR, isFixture: true },
  ];

  for (const c of candidates) {
    if (fs.existsSync(c.p)) {
      const fullPath = path.resolve(c.p);
      const rel = path.relative(c.dir, fullPath);
      if (rel.startsWith('..') || path.isAbsolute(rel)) {
        continue;
      }
      return { fullPath, isFixture: c.isFixture };
    }
  }

  // Also search by map property in files
  const all = listPropsFiles();
  const match = all.find((f) => f.map === mapOrName || f.id === clean || f.name === mapOrName);
  if (match && fs.existsSync(match.filePath)) {
    const fullPath = path.resolve(match.filePath);
    const expectedDir = match.isFixture ? PROPS_FIXTURES_DIR : PROPS_DIR;
    const rel = path.relative(expectedDir, fullPath);
    if (!rel.startsWith('..') && !path.isAbsolute(rel)) {
      return { fullPath, isFixture: match.isFixture };
    }
  }

  return null;
}

export function resolveLayoutFile(layoutName: string): GenLayout | null {
  if (!layoutName || !isValidPropsName(layoutName)) return null;
  const clean = layoutName.replace(/\.layout\.json$/, '').replace(/\.json$/, '');
  const candidates = [
    { p: path.join(ACCEPTED_DIR, `${clean}.json`), dir: ACCEPTED_DIR },
    { p: path.join(PROPS_FIXTURES_DIR, `${clean}.layout.json`), dir: PROPS_FIXTURES_DIR },
    { p: path.join(PROPS_FIXTURES_DIR, `${clean}.json`), dir: PROPS_FIXTURES_DIR },
    { p: path.join(ROOT, 'Saved/TrainingGround/levelgen', `${clean}.json`), dir: path.join(ROOT, 'Saved/TrainingGround/levelgen') },
    { p: path.join(ROOT, 'Content/Data/LevelGen/refs', `${clean}.json`), dir: path.join(ROOT, 'Content/Data/LevelGen/refs') },
  ];

  for (const c of candidates) {
    if (fs.existsSync(c.p)) {
      const fullPath = path.resolve(c.p);
      const rel = path.relative(c.dir, fullPath);
      if (rel.startsWith('..') || path.isAbsolute(rel)) continue;
      try {
        return JSON.parse(fs.readFileSync(fullPath, 'utf8')) as GenLayout;
      } catch {
        // continue
      }
    }
  }
  return null;
}

export function readPropsData(mapOrName: string): {
  props: LevelPropsFile;
  layout?: GenLayout | null;
  balance?: Record<string, unknown> | null;
  filePath: string;
} {
  if (!isValidPropsName(mapOrName)) {
    throw {
      status: 400,
      message: `Invalid props map name "${mapOrName}". Bare name matching /^[A-Za-z0-9_.-]+$/ required.`,
    };
  }

  const resolved = resolvePropsFile(mapOrName);
  if (!resolved) {
    throw { status: 404, message: `Props file not found for "${mapOrName}"` };
  }

  const text = fs.readFileSync(resolved.fullPath, 'utf8');
  const props = JSON.parse(text) as LevelPropsFile;

  let layout: GenLayout | null = null;
  if (props.layout) {
    layout = resolveLayoutFile(props.layout);
  }

  let balance: Record<string, unknown> | null = null;
  if (fs.existsSync(BALANCE_PATH)) {
    try {
      balance = JSON.parse(fs.readFileSync(BALANCE_PATH, 'utf8'));
    } catch {
      // ignore
    }
  }

  return {
    props,
    layout,
    balance,
    filePath: path.relative(ROOT, resolved.fullPath).replace(/\\/g, '/'),
  };
}

export interface SavePropsResult {
  success: boolean;
  revision: number;
  props: LevelPropsFile;
}

export function savePropsData(
  mapOrName: string,
  baseRevision: number,
  layers: PropLayer[],
  overrides?: PropOverride[]
): SavePropsResult {
  if (!isValidPropsName(mapOrName)) {
    throw {
      status: 400,
      message: `Invalid props map name "${mapOrName}". Only bare names matching /^[A-Za-z0-9_.-]+$/ without path separators are allowed.`,
    };
  }

  // Refuse fixtures and golden files
  if (
    mapOrName.startsWith('golden_') ||
    mapOrName.includes('fixture') ||
    mapOrName.startsWith('golden')
  ) {
    throw {
      status: 400,
      message: `Refused: "${mapOrName}" is a test fixture. PUT/save cannot modify fixtures.`,
    };
  }

  // Refuse extensions other than .props.json if an extension was explicitly provided
  if (mapOrName.includes('.') && !mapOrName.endsWith('.props.json')) {
    throw {
      status: 400,
      message: `Refused: target file "${mapOrName}" must end with .props.json.`,
    };
  }

  const resolved = resolvePropsFile(mapOrName);
  if (resolved?.isFixture || resolved?.fullPath.includes('fixtures') || path.basename(resolved?.fullPath ?? '').startsWith('golden_')) {
    throw {
      status: 400,
      message: `Refused: "${mapOrName}" resolves to a test fixture. Edits to fixtures are forbidden.`,
    };
  }

  const clean = mapOrName.replace(/\.props\.json$/, '').replace(/\.json$/, '');
  const targetPath = resolved ? resolved.fullPath : path.join(PROPS_DIR, `${clean}.props.json`);

  // Ensure targetPath is strictly inside PROPS_DIR
  const rel = path.relative(PROPS_DIR, targetPath);
  if (rel.startsWith('..') || path.isAbsolute(rel) || targetPath.includes('fixtures')) {
    throw {
      status: 400,
      message: 'Path traversal or fixture modification refused: target file must reside directly in PROPS_DIR.',
    };
  }

  if (!targetPath.endsWith('.props.json')) {
    throw {
      status: 400,
      message: `Invalid file extension: target "${targetPath}" must end with .props.json.`,
    };
  }

  if (path.basename(targetPath).startsWith('golden_')) {
    throw {
      status: 400,
      message: 'Refused: cannot save to golden files.',
    };
  }

  if (!fs.existsSync(targetPath)) {
    throw { status: 404, message: `Props file "${mapOrName}" not found at ${targetPath}` };
  }

  const currentRaw = fs.readFileSync(targetPath, 'utf8');
  const currentFile = JSON.parse(currentRaw) as LevelPropsFile;

  // Stale revision check (§4.1): refuses when frozen
  if (currentFile.frozen) {
    throw { status: 403, message: 'Props file is frozen; edits refused.' };
  }

  // Stale revision check (§4.1): refuses when base_revision !== file.revision
  if (currentFile.revision !== baseRevision) {
    throw {
      status: 409,
      message: `Stale revision: expected ${currentFile.revision}, got ${baseRevision}`,
      current_revision: currentFile.revision,
      current_file: currentFile,
    };
  }

  // Update only layers; bump revision; preserve overrides unless explicitly provided
  currentFile.layers = layers;
  if (overrides !== undefined) {
    currentFile.overrides = overrides;
  }
  currentFile.revision = (currentFile.revision ?? 1) + 1;

  // Canonical formatting
  const formatted = formatPropsCanonical(currentFile);

  // Atomic write
  writeAtomic(targetPath, formatted);

  return {
    success: true,
    revision: currentFile.revision,
    props: currentFile,
  };
}

export function setupPropsApi(use: Use): void {
  // CORS preflight
  use('/api/levelgen/props', (req, res, next) => {
    if (req.method === 'OPTIONS') {
      addCors(res);
      res.statusCode = 204;
      return res.end();
    }
    if (next) next();
  });

  // GET /api/levelgen/props-list
  use('/api/levelgen/props-list', (_req, res) => {
    try {
      const files = listPropsFiles();
      sendJson(res, 200, { files });
    } catch (err) {
      sendJson(res, 500, { error: err instanceof Error ? err.message : String(err) });
    }
  });

  // GET /api/levelgen/layout?name=<name>
  use('/api/levelgen/layout', (req, res) => {
    const u = new URL(req.url ?? '', 'http://x');
    const name = u.searchParams.get('name') ?? '';
    if (!name) return sendJson(res, 400, { error: 'Missing name parameter' });
    const layout = resolveLayoutFile(name);
    if (!layout) return sendJson(res, 404, { error: `Layout "${name}" not found` });
    sendJson(res, 200, layout);
  });

  // GET / PUT / POST for /api/levelgen/props
  use('/api/levelgen/props', async (req, res) => {
    const u = new URL(req.url ?? '', 'http://x');
    let map = u.searchParams.get('map') ?? '';
    if (!map && u.pathname.length > 1) {
      map = u.pathname.replace(/^\/+/, '').split('/')[0] ?? '';
    }

    if (req.method === 'GET') {
      if (!map) {
        // Return files list if no map specified
        return sendJson(res, 200, { files: listPropsFiles() });
      }
      try {
        const data = readPropsData(map);
        sendJson(res, 200, data);
      } catch (err) {
        sendJson(res, 404, { error: err instanceof Error ? err.message : String(err) });
      }
      return;
    }

    if (req.method === 'PUT' || req.method === 'POST') {
      try {
        const body = await readBody(req);
        const baseRevision = Number(body.base_revision);
        if (isNaN(baseRevision)) {
          return sendJson(res, 400, { error: 'base_revision must be a valid number' });
        }
        const layers = body.layers as PropLayer[];
        if (!Array.isArray(layers)) {
          return sendJson(res, 400, { error: 'layers must be an array' });
        }
        const overrides = body.overrides as PropOverride[] | undefined;

        const result = savePropsData(map || String(body.map ?? ''), baseRevision, layers, overrides);
        sendJson(res, 200, result);
      } catch (err: any) {
        const status = err && typeof err.status === 'number' ? err.status : 500;
        sendJson(res, status, {
          error: err?.message ?? (err instanceof Error ? err.message : String(err)),
          current_revision: err?.current_revision,
          current_file: err?.current_file,
        });
      }
      return;
    }

    sendJson(res, 405, { error: `Method ${req.method} not allowed` });
  });

  // POST /api/levelgen/props/save
  use('/api/levelgen/props/save', async (req, res) => {
    if (req.method !== 'POST') return sendJson(res, 405, { error: 'POST only' });
    try {
      const body = await readBody(req);
      const u = new URL(req.url ?? '', 'http://x');
      const map = u.searchParams.get('map') || String(body.map ?? '');
      const baseRevision = Number(body.base_revision);
      if (isNaN(baseRevision)) {
        return sendJson(res, 400, { error: 'base_revision must be a valid number' });
      }
      const layers = body.layers as PropLayer[];
      if (!Array.isArray(layers)) {
        return sendJson(res, 400, { error: 'layers must be an array' });
      }
      const overrides = body.overrides as PropOverride[] | undefined;

      const result = savePropsData(map, baseRevision, layers, overrides);
      sendJson(res, 200, result);
    } catch (err: any) {
      const status = err && typeof err.status === 'number' ? err.status : 500;
      sendJson(res, status, {
        error: err?.message ?? (err instanceof Error ? err.message : String(err)),
        current_revision: err?.current_revision,
        current_file: err?.current_file,
      });
    }
  });
}
