import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import { parseReplay, validNotes } from '../src/replay/model.ts';

export const REPLAY_MAP = 'L_Gen_Depot_1_X6';
export function demoLeaf(name: string): boolean { return /^[A-Za-z0-9_-]+\.jsonl$/.test(name); }
/** Reject links escaping the configured recording/notes directory as well as path traversal. */
export function containedFile(dir: string, leaf: string): string {
  const file = path.join(dir, leaf);
  const root = fs.realpathSync(dir);
  if (fs.existsSync(file) && path.dirname(fs.realpathSync(file)).toLowerCase() !== root.toLowerCase()) throw Error('File escapes configured directory');
  return file;
}
export function setupReplayApi(use: (route: string, handler: any) => void, project: string) {
  const demos = path.resolve(process.env.TG_DEMOS_DIR || path.join(os.homedir(), 'Documents/Unreal Projects/CodexTactics/Saved/Demos'));
  const notes = path.resolve(process.env.TG_DEMO_NOTES_DIR || demos);
  use('/api/replay', async (req: any, res: any) => {
    const reply = (status: number, data: unknown) => { res.statusCode = status; res.setHeader('Content-Type', 'application/json; charset=utf-8'); res.end(JSON.stringify(data)); };
    try {
      const url = new URL(req.url || '/', 'http://localhost');
      const action = url.pathname.replace(/^\//, '');
      const name = url.searchParams.get('file') || '';
      if (action === 'list' && req.method === 'GET') {
        if (!fs.existsSync(demos)) return reply(200, { demos, notes, recordings: [], warning: 'Recording folder does not exist. Set TG_DEMOS_DIR.' });
        const recordings = fs.readdirSync(demos).filter(demoLeaf).sort().reverse().map(file => {
          try {
            const replay = parseReplay(fs.readFileSync(containedFile(demos, file), 'utf8'));
            return { file, map: replay.header.map, sector: replay.header.sector_id, rounds: replay.events.filter(r => r.type === 'round_start').length,
              result: replay.footer.result ?? 'incomplete', duration: replay.duration, warnings: replay.warnings.length };
          } catch (e) { return { file, error: String(e) }; }
        });
        return reply(200, { demos, notes, recordings });
      }
      if (action === 'map' && req.method === 'GET') {
        if (url.searchParams.get('map') !== REPLAY_MAP) return reply(404, { error: 'Replay geometry currently available only for X6; select an X6 recording.' });
        const level = JSON.parse(fs.readFileSync(path.join(project, 'Content/Data/LevelJson', REPLAY_MAP + '.json'), 'utf8').replace(/^\uFEFF/, ''));
        return reply(200, { map: REPLAY_MAP, layout: JSON.parse(fs.readFileSync(path.resolve(project, level.levelgen_layout), 'utf8').replace(/^\uFEFF/, '')),
          sectors: JSON.parse(fs.readFileSync(path.join(project, 'Content/Data/LevelJson', REPLAY_MAP + '.sectors.json'), 'utf8')).sectors,
          index: JSON.parse(fs.readFileSync(path.join(project, 'Content/Data/LevelJson', REPLAY_MAP + '.buildings_index.json'), 'utf8')) });
      }
      if (!demoLeaf(name)) return reply(400, { error: 'Invalid recording filename' });
      const recordingFile = containedFile(demos, name);
      if (!fs.existsSync(recordingFile)) return reply(404, { error: 'Recording not found' });
      if (action === 'recording' && req.method === 'GET') {
        res.setHeader('Content-Type', 'application/x-ndjson; charset=utf-8');
        return res.end(fs.readFileSync(recordingFile, 'utf8'));
      }
      if (action === 'notes') {
        if (!fs.existsSync(notes)) fs.mkdirSync(notes, { recursive: true });
        const target = containedFile(notes, name.replace(/\.jsonl$/, '.notes.json'));
        if (req.method === 'GET') return reply(200, fs.existsSync(target) ? JSON.parse(fs.readFileSync(target, 'utf8')) : null);
        if (req.method === 'POST') {
          // Local authoring endpoint; a foreign webpage cannot write via form/CORS.
          if (!String(req.headers['content-type'] || '').startsWith('application/json')) return reply(415, { error: 'JSON required' });
          if (req.headers.origin && req.headers.origin !== `http://${req.headers.host}` && req.headers.origin !== `https://${req.headers.host}`) return reply(403, { error: 'Same origin required' });
          const chunks: Buffer[] = []; let bytes = 0;
          for await (const chunk of req) { const buffer = Buffer.from(chunk); bytes += buffer.length; if (bytes > 4_000_000) return reply(413, { error: 'Notes exceed 4 MB' }); chunks.push(buffer); }
          const value = JSON.parse(Buffer.concat(chunks).toString('utf8'));
          const replay = parseReplay(fs.readFileSync(recordingFile, 'utf8'));
          if (!validNotes(value, name, replay.header.map)) return reply(400, { error: 'Invalid notes schema / recording / coordinates' });
          // Replace atomically, never open or alter the recording.
          const temp = target + '.tmp-' + process.pid;
          fs.writeFileSync(temp, JSON.stringify(value, null, 2) + '\n', { flag: 'wx' });
          try { fs.renameSync(temp, target); } finally { if (fs.existsSync(temp)) fs.unlinkSync(temp); }
          return reply(200, { saved: target });
        }
      }
      reply(405, { error: 'Unsupported replay operation' });
    } catch (e) { reply(400, { error: String(e) }); }
  });
}
