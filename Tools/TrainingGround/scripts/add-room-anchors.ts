#!/usr/bin/env node
// Adds (or refreshes) the walkable room anchors (GenRoom.anchor, src/levelgen/roomAnchors.ts) of an existing layout file, in place:
//   node scripts/add-room-anchors.ts <layout.json>                 # rewrites the file
//   node scripts/add-room-anchors.ts <layout.json> --out <file>    # writes a copy, the layout stays as it is
//   node scripts/add-room-anchors.ts <layout.json> --force         # also for a file not in JSON.stringify style (rewritten normalised)
// Idempotent; nothing but the `anchor` fields of the rooms of enterable buildings changes (the file's BOM / EOL / indent are kept).
// Style: scripts/export-tactical-map.ts. `npm run levelgen:anchors -- <layout.json> [--out <file>]`.

import * as fs from 'node:fs';
import * as path from 'node:path';
import type { GenLayout } from '../src/levelgen/schema.ts';
import { anchorLayoutText } from '../src/levelgen/roomAnchors.ts';

const args = process.argv.slice(2);
const flag = (k: string): string | true | undefined => { const i = args.indexOf(`--${k}`); if (i < 0) return undefined; const n = args[i + 1]; return n !== undefined && !n.startsWith('--') ? n : true; };
const outArg = flag('out');
const force = flag('force') === true;
const file = args.find((a, i) => !a.startsWith('--') && !(i > 0 && args[i - 1] === '--out'));
if (!file || outArg === true) {
  console.error('usage: node scripts/add-room-anchors.ts <layout.json> [--out <file>] [--force]');
  process.exit(2);
}
const target = typeof outArg === 'string' ? path.resolve(outArg) : path.resolve(file);
const text = fs.readFileSync(file, 'utf8');
let next: string;
try {
  next = anchorLayoutText(text, force);
} catch (e) {
  console.error(`[room-anchors] ${file}: ${(e as Error).message}`);
  process.exit(1);
}
const count = (t: string) => {
  const l = JSON.parse(t.replace(/^\uFEFF/, '')) as GenLayout;
  const rooms = l.buildings.filter((b) => b.enterable).flatMap((b) => b.rooms ?? []);
  return { rooms: rooms.length, anchored: rooms.filter((r) => r.anchor).length, buildings: l.buildings.filter((b) => b.enterable).length };
};
const c = count(next);
if (next !== text || target !== path.resolve(file)) fs.writeFileSync(target, next);
console.log(`[room-anchors] ${path.basename(file)} -> ${target}: ${c.buildings} enterable buildings, ${c.rooms} rooms, ${c.anchored} anchored, ${c.rooms - c.anchored} without a walkable point${next === text ? ' (unchanged)' : ''}`);
process.exit(0);
