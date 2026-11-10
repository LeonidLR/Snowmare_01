// Training Ground: dev-server endpoints for Content/Data/UI/hud_labels.json.
// Docs: Step 3 of docs/port/gemini/inbox/010-night-queue.md.
// Endpoints:
//   GET  /api/hud-labels -> { schema, comment, labels: { ... } }
//   POST /api/hud-labels -> { success: true, count: number } (atomic write, comments & formatting preserved)

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

import {
  type HudLabelsFile,
  type HudLabelItem,
  validateHudLabels,
  patchHudLabelsText,
} from '../src/utils/hudLabels.ts';

const TOOL_DIR = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const ROOT = path.resolve(TOOL_DIR, '../..');
const HUD_LABELS_PATH = path.join(ROOT, 'Content/Data/UI/hud_labels.json');

type Req = { method?: string; url?: string; on: (ev: string, cb: (chunk?: unknown) => void) => void };
type Res = { statusCode: number; setHeader: (k: string, v: string) => void; end: (body?: string) => void };
type Use = (route: string, handler: (req: Req, res: Res, next?: () => void) => void) => void;

const addCors = (res: Res) => {
  res.setHeader('Access-Control-Allow-Origin', '*');
  res.setHeader('Access-Control-Allow-Methods', 'GET, POST, OPTIONS');
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

/**
 * Atomically writes text to targetPath via a unique temporary file,
 * syncing to disk before renaming over the target.
 */
export function writeAtomic(targetPath: string, text: string): void {
  const dir = path.dirname(targetPath);
  const tempPath = path.join(
    dir,
    `${path.basename(targetPath)}.tmp.${process.pid}.${Date.now()}.${Math.random().toString(36).slice(2)}`
  );
  try {
    const fd = fs.openSync(tempPath, 'w');
    try {
      fs.writeFileSync(fd, text, 'utf8');
      fs.fsyncSync(fd);
    } finally {
      fs.closeSync(fd);
    }
    fs.renameSync(tempPath, targetPath);
  } catch (err) {
    if (fs.existsSync(tempPath)) {
      try {
        fs.unlinkSync(tempPath);
      } catch {
        // ignore cleanup error
      }
    }
    throw err;
  }
}

export function setupHudLabelsApi(use: Use): void {
  // GET /api/hud-labels and POST /api/hud-labels
  use('/api/hud-labels', async (req: Req, res: Res, next?: () => void) => {
    addCors(res);
    if (req.method === 'OPTIONS') {
      res.statusCode = 204;
      res.end();
      return;
    }

    if (req.method === 'GET') {
      if (!fs.existsSync(HUD_LABELS_PATH)) {
        sendJson(res, 404, { error: 'Content/Data/UI/hud_labels.json not found' });
        return;
      }
      try {
        const raw = fs.readFileSync(HUD_LABELS_PATH, 'utf8').replace(/^\uFEFF/, '');
        const data = JSON.parse(raw);
        sendJson(res, 200, data);
      } catch (err: unknown) {
        const message = err instanceof Error ? err.message : String(err);
        sendJson(res, 500, { error: `Failed to read hud_labels.json: ${message}` });
      }
      return;
    }

    if (req.method === 'POST') {
      let body: Record<string, unknown>;
      try {
        body = await readBody(req);
      } catch {
        sendJson(res, 400, { error: 'invalid JSON' });
        return;
      }

      if (!body || typeof body !== 'object') {
        sendJson(res, 400, { error: 'expected object payload' });
        return;
      }

      // Check current file
      if (!fs.existsSync(HUD_LABELS_PATH)) {
        sendJson(res, 404, { error: 'Content/Data/UI/hud_labels.json not found' });
        return;
      }

      try {
        const currentText = fs.readFileSync(HUD_LABELS_PATH, 'utf8').replace(/^\uFEFF/, '');
        const currentData: HudLabelsFile = JSON.parse(currentText);

        // Merge incoming updates: can receive either a full HudLabelsFile or { labels: ... }
        const updated: HudLabelsFile = {
          ...currentData,
          ...body,
          labels: {
            ...currentData.labels,
            ...((body.labels as Record<string, HudLabelItem>) ?? {}),
          },
        };

        const errors = validateHudLabels(updated);
        if (errors.length > 0) {
          sendJson(res, 400, { error: 'validation failed', details: errors });
          return;
        }

        // Apply changes preserving formatting, comments, and unknown keys
        const patchedText = patchHudLabelsText(currentText, updated);

        // Atomic write
        writeAtomic(HUD_LABELS_PATH, patchedText);

        sendJson(res, 200, {
          success: true,
          count: Object.keys(updated.labels).length,
        });
      } catch (err: unknown) {
        const message = err instanceof Error ? err.message : String(err);
        sendJson(res, 500, { error: `Failed to save hud_labels.json: ${message}` });
      }
      return;
    }

    if (typeof next === 'function') {
      next();
    }
  });
}
