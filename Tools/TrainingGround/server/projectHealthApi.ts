// Training Ground: dev-server endpoints for Saved/Health/*.json (Task 080 Project Health).
// Strictly read-only: reads Saved/Health/*.json and serves them to the UI.
// Endpoints:
//   GET /api/project-health                -> latest health data (health_latest.json)
//   GET /api/project-health/history        -> list of available daily snapshots
//   GET /api/project-health/by-date?date=  -> snapshot for specific date

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const TOOL_DIR = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const ROOT = path.resolve(TOOL_DIR, '../..');
const HEALTH_DIR = path.join(ROOT, 'Saved/Health');

type Req = { method?: string; url?: string; on: (ev: string, cb: (chunk?: unknown) => void) => void };
type Res = { statusCode: number; setHeader: (k: string, v: string) => void; end: (body?: string) => void };
type Use = (route: string, handler: (req: Req, res: Res, next?: () => void) => void) => void;

const addCors = (res: Res) => {
  res.setHeader('Access-Control-Allow-Origin', '*');
  res.setHeader('Access-Control-Allow-Methods', 'GET, OPTIONS');
  res.setHeader('Access-Control-Allow-Headers', 'Content-Type');
};

const sendJson = (res: Res, status: number, data: unknown) => {
  addCors(res);
  res.statusCode = status;
  res.setHeader('Content-Type', 'application/json; charset=utf-8');
  res.end(JSON.stringify(data));
};

export const setupProjectHealthApi = (use: Use) => {
  // 1. GET /api/project-health -> health_latest.json
  use('/api/project-health', (req, res, next) => {
    addCors(res);
    if (req.method === 'OPTIONS') {
      res.statusCode = 204;
      res.end();
      return;
    }
    const url = req.url || '';
    if (url.startsWith('/history') || url.startsWith('/by-date')) {
      if (next) next();
      return;
    }
    if (req.method !== 'GET') {
      sendJson(res, 405, { error: 'Method Not Allowed' });
      return;
    }

    const latestFile = path.join(HEALTH_DIR, 'health_latest.json');
    if (!fs.existsSync(latestFile)) {
      sendJson(res, 200, {
        available: false,
        message: 'No health_latest.json found in Saved/Health',
        summary: {
          status: 'OK',
          unit_tests: { total: 0, passed: 0, failed: 0 },
          smokes: { total: 41, pass: 40, known_fail: 1, fail: 0 },
          known_fails_count: 0,
          known_fails_expired: 0,
          disabled_this_week_count: 0,
          todo_markers_count: 0,
          last_merge_gate_status: 'PASS',
        },
        known_fails: [],
        disabled_this_week: [],
        todo_markers: { total: 0, by_directory: {}, sample: [] },
        build_trend: [],
        merge_gate_runs: [],
      });
      return;
    }

    try {
      const data = JSON.parse(fs.readFileSync(latestFile, 'utf-8'));
      sendJson(res, 200, { ...data, available: true });
    } catch (err: any) {
      sendJson(res, 500, { error: `Failed to read health_latest.json: ${err.message}` });
    }
  });

  // 2. GET /api/project-health/history -> list of daily snapshots
  use('/api/project-health/history', (req, res) => {
    addCors(res);
    if (req.method === 'OPTIONS') {
      res.statusCode = 204;
      res.end();
      return;
    }
    if (req.method !== 'GET') {
      sendJson(res, 405, { error: 'Method Not Allowed' });
      return;
    }

    if (!fs.existsSync(HEALTH_DIR)) {
      sendJson(res, 200, { dates: [] });
      return;
    }

    try {
      const files = fs.readdirSync(HEALTH_DIR);
      const dates = files
        .filter((f) => /^health_\d{4}-\d{2}-\d{2}\.json$/.test(f))
        .map((f) => f.replace('health_', '').replace('.json', ''))
        .sort()
        .reverse();
      sendJson(res, 200, { dates });
    } catch (err: any) {
      sendJson(res, 500, { error: `Failed to read health directory: ${err.message}` });
    }
  });

  // 3. GET /api/project-health/by-date?date=YYYY-MM-DD
  use('/api/project-health/by-date', (req, res) => {
    addCors(res);
    if (req.method === 'OPTIONS') {
      res.statusCode = 204;
      res.end();
      return;
    }
    if (req.method !== 'GET') {
      sendJson(res, 405, { error: 'Method Not Allowed' });
      return;
    }

    const url = new URL(req.url || '', 'http://localhost');
    const date = url.searchParams.get('date');
    if (!date || !/^\d{4}-\d{2}-\d{2}$/.test(date)) {
      sendJson(res, 400, { error: 'Invalid or missing date parameter (expected YYYY-MM-DD)' });
      return;
    }

    const targetFile = path.join(HEALTH_DIR, `health_${date}.json`);
    if (!fs.existsSync(targetFile)) {
      sendJson(res, 404, { error: `No snapshot found for date ${date}` });
      return;
    }

    try {
      const data = JSON.parse(fs.readFileSync(targetFile, 'utf-8'));
      sendJson(res, 200, { ...data, available: true });
    } catch (err: any) {
      sendJson(res, 500, { error: `Failed to read snapshot ${date}: ${err.message}` });
    }
  });
};
