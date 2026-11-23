// S2.3 engine fixture: a small test layout (gen_bgen_plans_check) with the planned buildings the generated X6 happens not to contain:
// 36 m blocks (two sections, ids B_x_c1_stair_* for the second core), 18 / 20 m blocks, and 2-storey offices with a 900-1200 core bay
// (L 1000-1200, core at the low / high end). Buildings are placed with addBuilding like the S2.3 unit tests; plans ON.
//   node scripts/gen-bgen-plans-check.ts <out.json>     (then add-room-anchors on the file)
import fs from 'node:fs';
import { buildingToAscii } from '../src/levelgen/buildings/planAscii.ts';
import { validateBuilding } from '../src/levelgen/buildingTraversal.ts';
import type { Draft, Side } from '../src/levelgen/generate.ts';
import { addBuilding, finalizeDraftToLayout } from '../src/levelgen/generate.ts';
import { anchorLayoutText } from '../src/levelgen/roomAnchors.ts';
import { DEFAULT_PARAMS, LEVEL_HEIGHT, validateGenLayout } from '../src/levelgen/schema.ts';

const out = process.argv[2];
if (!out) { console.error('usage: node scripts/gen-bgen-plans-check.ts <out.json>'); process.exit(2); }
interface Spec { id: string; archetype: 'office' | 'block'; x: number; y: number; w: number; h: number; floors: number; seed: number; core?: 'low' | 'high'; sides?: Side[] }
const specs: Spec[] = [
  { id: 'B_x', archetype: 'block', x: 8, y: 10, w: 36.5, h: 12.2, floors: 2, seed: 4 },
  { id: 'B_y', archetype: 'block', x: 58, y: 10, w: 37.1, h: 12.6, floors: 3, seed: 5 },
  { id: 'B_b18', archetype: 'block', x: 8, y: 36, w: 18.4, h: 10.3, floors: 2, seed: 1 },
  { id: 'B_b20', archetype: 'block', x: 36, y: 36, w: 20.2, h: 14.1, floors: 3, seed: 2 },
  { id: 'B_o10', archetype: 'office', x: 8, y: 62, w: 10.2, h: 10.2, floors: 2, seed: 3, core: 'low' },
  { id: 'B_o11', archetype: 'office', x: 26, y: 62, w: 11.1, h: 10.2, floors: 2, seed: 4, core: 'high' },
  { id: 'B_o12', archetype: 'office', x: 45, y: 62, w: 12.0, h: 10.2, floors: 2, seed: 5, core: 'low' },
  { id: 'B_o12h', archetype: 'office', x: 65, y: 62, w: 12.4, h: 11.2, floors: 2, seed: 6, core: 'high' },
  { id: 'B_o12w', archetype: 'office', x: 86, y: 62, w: 10.3, h: 12.0, floors: 2, seed: 7, core: 'low', sides: ['west'] },
];
const d: Draft = { walls: [], buildings: [], ladders: [], covers: [], zones: [], routes: [], spawn_points: [], ambush_points: [], patrol_routes: [], mantle_edges: [], keepFree: [], building_plans: true };
for (const s of specs) {
  let n = s.seed * 7919;
  const random = (): number => { n = (n * 1103515245 + 12345) & 0x7fffffff; return n / 0x7fffffff; };
  addBuilding(d, s.id, [[s.x + 0.37, s.y + 0.21], [s.x + 0.37 + s.w, s.y + 0.21 + s.h]], {
    height: s.floors * LEVEL_HEIGHT, floors: s.floors, enterable: true, roof_walkable: s.archetype === 'block' || s.floors > 1, stairwell: true, set_piece: 'facade', archetype: s.archetype,
    doorSides: s.sides ?? ['south'], windowSides: ['south', 'north'], partitionRooms: true, ...(s.core ? { cores: [s.core] } : {}),
  }, random);
}
d.routes.push({ id: 'R_main', pace: 'walk', points: [[4, 4], [50, 4], [104, 4], [104, 84]], mandatory: true, layers: ['ground', 'ground', 'ground', 'ground'], links: [], set_pieces: ['street'] });
const l = finalizeDraftToLayout(d, [110, 90], { name: 'gen_bgen_plans_check', profile: 'ref_01_02_depot', seed: 1, params: { ...DEFAULT_PARAMS, size_m: [110, 90], building_plans: true }, start: [4, 4], end: [100, 84] });
const errs = validateGenLayout(l);
if (errs.length) { console.error('schema errors:', errs.slice(0, 10)); process.exit(1); }
const text = anchorLayoutText(JSON.stringify(l, null, 1) + '\n', true);
fs.writeFileSync(out, text);
const o = JSON.parse(text);
for (const b of o.buildings) {
  const v = validateBuilding(o, b);
  const core = (b.rooms ?? []).find((r: any) => r.kind === 'stairwell');
  console.log(`${b.id}: ${b.plan?.kind ?? '-'} ${b.plan?.variant ?? ''} fallback=${b.plan?.fallback ?? '-'} floors=${b.floors} rooms=${(b.rooms ?? []).length} stairs=${(b.stairs ?? []).map((s: any) => s.id).join(',')} units=${(b.units ?? []).length} issues=${v.issues.length} core=${core ? JSON.stringify(core.rect ?? core.bounds ?? core) : '-'}`);
}
if (process.argv.includes('--ascii')) for (const b of o.buildings) { console.log(`\n== ${b.id}`); for (let f = 0; f < b.floors; ++f) console.log(buildingToAscii(b, f)); }
