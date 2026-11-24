import { randomUUID } from 'node:crypto';
import fs from 'node:fs';
import type { IncomingMessage, ServerResponse } from 'node:http';
import path from 'node:path';
import { TRAINING_MAPS, X6 } from '../src/aiTraining/trainingTypes.ts';
import type { TrainingData, TrainingRun, TrainingSector, TrainingSelection } from '../src/aiTraining/trainingTypes.ts';

type ObjectData = Record<string, unknown>;
const mapIds = new Set<string>(TRAINING_MAPS.map(({ id }) => id));
const fallback: TrainingSelection = { map: X6, sector: 0 };
const record = (value: unknown): ObjectData | null => value && typeof value === 'object' && !Array.isArray(value) ? value as ObjectData : null;
const finite = (value: unknown): value is number => typeof value === 'number' && Number.isFinite(value);
const tuple = (value: unknown, length: number): boolean => Array.isArray(value) && value.length === length && value.every(finite);
const sectorId = (value: unknown): value is number => finite(value) && Number.isInteger(value) && value >= 0 && value <= 6;
const parse = (text: string) => record(JSON.parse(text.replace(/^\uFEFF/, '')));
const read = (file: string) => { try { return parse(fs.readFileSync(file, 'utf8')); } catch { return null; } };

export function readTrainingSectors(root: string): TrainingSector[] {
  const data = read(path.join(root, 'Content/Data/LevelJson', X6 + '.sectors.json'));
  if (data?.map !== X6 || !Array.isArray(data.sectors)) return [];
  const valid: TrainingSector[] = [];
  for (const entry of data.sectors) {
    const row = record(entry);
    const box = record(row?.box_cm);
    const squad = record(row?.pmc_squad);
    const contents = record(row?.contents);
    const covers = record(contents?.covers);
    const positions = (list: unknown, posts = false) => Array.isArray(list) && list.length >= 2 && list.length <= 3
      && list.every((entry) => {
        const point = record(entry);
        return tuple(point?.pos, 3) && finite(point?.yaw)
          && (!posts || (typeof point?.id === 'string' && typeof point?.kind === 'string'));
      });
    if (!row || !sectorId(row.id) || row.id === 0 || valid.some((sector) => sector.id === row.id)
      || !tuple(box?.min, 2) || !tuple(box?.max, 2) || typeof row.name !== 'string' || !tuple(row.size_m, 2)
      || !positions(row.starts) || !positions(row.pmc_posts, true)
      || !finite(squad?.count) || typeof squad.posture !== 'string' || typeof squad.weights !== 'string'
      || !finite(contents?.enterable_buildings) || !finite(contents?.fences_m) || !finite(contents?.ladders)
      || !covers || !Object.values(covers).every(finite)) continue;
    valid.push(row as unknown as TrainingSector);
  }
  return valid.sort((a, b) => a.id - b.id);
}

export function validTrainingSelection(data: unknown, sectors: readonly TrainingSector[]): TrainingSelection | null {
  const value = record(data);
  if (!value || typeof value.map !== 'string' || !mapIds.has(value.map) || !sectorId(value.sector)) return null;
  if (value.sector !== 0 && (value.map !== X6 || !sectors.some((sector) => sector.id === value.sector))) return null;
  return { map: value.map as TrainingSelection['map'], sector: value.sector };
}

const selectionFile = (root: string) => path.join(root, 'Saved/Training/active_sector.json');

export function writeTrainingSelection(root: string, data: unknown): TrainingSelection | null {
  const value = validTrainingSelection(data, readTrainingSectors(root));
  if (!value) return null;
  const file = selectionFile(root);
  const existing = read(file);
  if (existing?.map === value.map && existing.sector === value.sector) return value;
  fs.mkdirSync(path.dirname(file), { recursive: true });
  const temp = file + '.' + randomUUID() + '.tmp';
  try {
    fs.writeFileSync(temp, JSON.stringify(value, null, 2) + '\n', { encoding: 'utf8', flag: 'wx' });
    fs.renameSync(temp, file);
  } finally {
    if (fs.existsSync(temp)) fs.unlinkSync(temp);
  }
  return value;
}

function demoHeader(file: string): ObjectData | null {
  const fd = fs.openSync(file, 'r');
  try {
    const bytes = Buffer.alloc(32 * 1024);
    const count = fs.readSync(fd, bytes, 0, bytes.length, 0);
    const newline = bytes.subarray(0, count).indexOf(10);
    return newline < 0 ? null : parse(bytes.toString('utf8', 0, newline));
  } finally {
    fs.closeSync(fd);
  }
}

function recentRuns(root: string): TrainingRun[] {
  const result: TrainingRun[] = [];
  for (const source of ['demo', 'arena'] as const) {
    const dir = path.join(root, 'Saved', source === 'demo' ? 'Demos' : 'Arena');
    if (!fs.existsSync(dir)) continue;
    const ext = source === 'demo' ? '.jsonl' : '.json';
    const files = fs.readdirSync(dir, { withFileTypes: true }).filter((entry) => entry.isFile() && entry.name.endsWith(ext))
      .map(({ name }) => name).sort().reverse().slice(0, 12);
    for (const file of files) {
      try {
        const full = path.join(dir, file);
        const stat = fs.statSync(full);
        const data = source === 'demo' ? demoHeader(full) : stat.size <= 4 * 1024 * 1024 ? read(full) : null;
        if (!data || typeof data.map !== 'string'
          || (source === 'demo' ? data.type !== 'header' || data.v !== 1 : data.schema !== 'codex.pmc_arena/1')) continue;
        const map = data.map.split('/').at(-1)?.replace(/^UEDPIE_\d+_/, '') ?? data.map;
        const time = typeof data.started_utc === 'string' && Number.isFinite(Date.parse(data.started_utc))
          ? data.started_utc : stat.mtime.toISOString();
        result.push({ file, source, time, map, sector: sectorId(data.sector_id) ? data.sector_id : null });
      } catch { /* The recorder can rotate a file while it is being scanned. */ }
    }
  }
  return result.sort((a, b) => Date.parse(b.time) - Date.parse(a.time)).slice(0, 12);
}

export function readTrainingData(root: string): TrainingData {
  const sectors = readTrainingSectors(root);
  const layout = read(path.join(root, 'Content/Data/LevelGen/accepted/gen_ref_01_02_depot_1_x6.json'));
  const validLayout = layout?.bounds && Array.isArray(layout.buildings) && Array.isArray(layout.walls)
    ? layout as unknown as TrainingData['layout'] : null;
  return { maps: TRAINING_MAPS, active: validTrainingSelection(read(selectionFile(root)), sectors) ?? fallback,
    sectors, layout: validLayout, runs: recentRuns(root) };
}

type Use = (route: string, handler: (req: IncomingMessage, res: ServerResponse) => void | Promise<void>) => void;

export function setupTrainingApi(use: Use, root: string) {
  use('/api/ai-training', async (req, res) => {
    const send = (status: number, value: unknown) => {
      res.statusCode = status;
      res.setHeader('Content-Type', 'application/json; charset=utf-8');
      res.end(JSON.stringify(value));
    };
    try {
      if (req.method === 'GET') return send(200, readTrainingData(root));
      if (req.method !== 'POST') return send(405, { error: 'GET or POST required' });
      if (!/^application\/json(?:\s*;|$)/i.test(req.headers['content-type'] ?? '')) {
        return send(415, { error: 'JSON required' });
      }
      const chunks: Buffer[] = [];
      let size = 0;
      for await (const chunk of req) {
        size += chunk.length;
        if (size <= 4096) chunks.push(chunk);
      }
      if (size > 4096) return send(413, { error: 'Selection is too large' });
      let value: ObjectData | null;
      try { value = parse(Buffer.concat(chunks).toString('utf8')); }
      catch { return send(400, { error: 'Invalid JSON' }); }
      const active = writeTrainingSelection(root, value);
      return active ? send(200, { active }) : send(400, { error: 'Сектор недоступен для выбранной карты' });
    } catch (error) {
      return send(500, { error: error instanceof Error ? error.message : String(error) });
    }
  });
}
