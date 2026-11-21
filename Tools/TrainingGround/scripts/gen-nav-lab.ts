// Nav lab (2026-11-21 night, docs/design/levelgen/STORY_SHOWCASE_2026-11-22.md section 'Navmesh'): copies of the L_Gen_BgenPlans kit blocks
// (B_y 37 x 12.6 m 3 storeys, B_b18 18 x 10 m 2, B_b20 20 x 14 m 3) at sub-metre offsets, to tell tile / voxel-alignment navmesh gaps from plan
// defects. The *_o copies stand where gen-bgen-plans-check.ts puts the originals (their plans differ: the plan seed takes the building id).
//   node scripts/gen-nav-lab.ts <out.json>   then copy it to Content/Data/LevelGen/accepted/gen_navlab.json (the islands probe reads it there),
//   Scripts/levelgen_build_map.ps1 -Layout <that> -Target /Game/Maps/NavLab/L_NavLab, LevelGenRampSmoke (ramp nav lines) + LevelGenIslandsProbe.
// Untracked outputs: the map and the level JSON.
import fs from 'node:fs';
import type { Draft, Side } from '../src/levelgen/generate.ts';
import { addBuilding, finalizeDraftToLayout } from '../src/levelgen/generate.ts';
import { anchorLayoutText } from '../src/levelgen/roomAnchors.ts';
import { DEFAULT_PARAMS, LEVEL_HEIGHT, validateGenLayout } from '../src/levelgen/schema.ts';

const out = process.argv[2];
if (!out) { console.error('usage: node scripts/gen-nav-lab.ts <out.json>'); process.exit(2); }
interface Spec { id: string; x: number; y: number; w: number; h: number; floors: number; seed: number; dx: number; dy: number }
const Y = { w: 37.1, h: 12.6, floors: 3, seed: 5 }, B18 = { w: 18.4, h: 10.3, floors: 2, seed: 1 }, B20 = { w: 20.2, h: 14.1, floors: 3, seed: 2 };
const specs: Spec[] = [
  { id: 'Y_o', x: 58, y: 10, ...Y, dx: 0.37, dy: 0.21 },
  { id: 'Y_1', x: 8, y: 10, ...Y, dx: 0.87, dy: 0.61 },
  { id: 'S18_o', x: 8, y: 36, ...B18, dx: 0.37, dy: 0.21 },
  { id: 'S20_o', x: 36, y: 36, ...B20, dx: 0.37, dy: 0.21 },
  { id: 'S20_1', x: 62, y: 36, ...B20, dx: 0.6, dy: 0.3 },
  { id: 'S20_2', x: 86, y: 36, ...B20, dx: 1.5, dy: 0.7 },
  { id: 'S18_1', x: 8, y: 62, ...B18, dx: 0.12, dy: 0.47 },
  { id: 'S18_2', x: 30, y: 62, ...B18, dx: 0.55, dy: 0.0 },
  { id: 'S18_3', x: 52, y: 62, ...B18, dx: 0.8, dy: 0.65 },
  { id: 'S18_4', x: 74, y: 62, ...B18, dx: 0.3, dy: 0.9 },
];
const d: Draft = { walls: [], buildings: [], ladders: [], covers: [], zones: [], routes: [], spawn_points: [], ambush_points: [], patrol_routes: [], mantle_edges: [], keepFree: [], building_plans: true };
for (const s of specs) {
  let n = s.seed * 7919;
  const random = (): number => { n = (n * 1103515245 + 12345) & 0x7fffffff; return n / 0x7fffffff; };
  addBuilding(d, s.id, [[s.x + s.dx, s.y + s.dy], [s.x + s.dx + s.w, s.y + s.dy + s.h]], {
    height: s.floors * LEVEL_HEIGHT, floors: s.floors, enterable: true, roof_walkable: true, stairwell: true, set_piece: 'facade', archetype: 'block',
    doorSides: ['south' as Side], windowSides: ['south', 'north'], partitionRooms: true,
  }, random);
}
d.routes.push({ id: 'R_main', pace: 'walk', points: [[4, 4], [50, 4], [104, 4], [104, 84]], mandatory: true, layers: ['ground', 'ground', 'ground', 'ground'], links: [], set_pieces: ['street'] });
const l = finalizeDraftToLayout(d, [110, 90], { name: 'gen_navlab', profile: 'ref_01_02_depot', seed: 1, params: { ...DEFAULT_PARAMS, size_m: [110, 90], building_plans: true }, start: [4, 4], end: [100, 84] });
const errs = validateGenLayout(l);
if (errs.length) { console.error('schema errors:', errs.slice(0, 10)); process.exit(1); }
fs.writeFileSync(out, anchorLayoutText(JSON.stringify(l, null, 1) + '\n', true));
for (const b of l.buildings) console.log(b.id, b.plan?.template, JSON.stringify(b.footprint), (b.stairs ?? []).length);
