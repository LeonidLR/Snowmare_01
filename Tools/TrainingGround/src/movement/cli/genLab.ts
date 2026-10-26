// `npm run lab:gen` - writes Content/Data/FormationLab/formation_lab_01.json, the big formation test lab
// (FORMATION_SECTORS.md "Test lab", coordinator stage 5). Six parallel lanes run along +X (heading north), one feature each:
//   A open field -> dead-end wall with EQUAL room (wall layout, the 4th takes the side he reaches with less travel)
//   B a long wall on the LEFT, open, a long wall on the RIGHT (echelon right / left)
//   C wall-gap-wall on the left: gaps of 1 / 2 / 3 m (shorter than the classifier needs - must NOT flip), then a 24 m gap (open)
//   D a narrow corridor (~4.6 m free -> narrow column) and a wide one (~7.6 m free -> herringbone column, v1.2 user Q3 = A)
//   E an inner 90 deg corner, the wall on the left on both legs (echelon right round the turn)
//   F a dead-end wall with UNEQUAL room (the 4th at the roomier end)
// plus a fast route (run) over the open field, a heat source (hedgehog, manual in game) and the v2 lane (R_v2_*: 4.5 / 6 m gaps
// that v1's time hysteresis flipped; v2's path hold must keep the echelon - FORMATION_CONTRACT.md 8), and the v2.1 lane G
// (FORMATION_CONTRACT.md 10): free-standing walls ACROSS the route - G1 oblique (both sides about as long: no threat -> the
// shortest side; a threat on the left / right -> the side the wall hides it from), G2 square to the route and lopsided (the
// side that hides the threat is ~70 % longer -> too long, the shortest side). Re-run after editing this file; the JSON is the source the
// Unreal builder (Scripts/Editor/build_formation_lab.py) and the simulator read.
import fs from 'node:fs';
import path from 'node:path';
import { LAB_DIR } from './paths.ts';

type V = [number, number];
const H = 300, T = 40;
let wid = 0;
const walls: { id: string; a: V; b: V; height: number; thickness: number }[] = [];
const wall = (lane: string, a: V, b: V) => walls.push({ id: `W_${lane}_${++wid}`, a, b, height: H, thickness: T });
const rect = (x0: number, y0: number, x1: number, y1: number): V[] => [[x0, y0], [x1, y0], [x1, y1], [x0, y1]];
const zones: { id: string; kind: string; poly: V[] }[] = [];
const zone = (id: string, kind: string, poly: V[]) => zones.push({ id, kind, poly });

// Lane y positions (route axis). Neighbouring lanes stay > 10 m apart from each other's walls.
const A = -4500, B = -3000, C = -1200, D = 800, E = 2800, F = 4500, V2 = 5600, G = 2900;

// A - open field, dead end with equal room (wall x 4500, y -5400..-3600; the route ends ON the leader's wall slot: face - 45 cm,
// the click on the cover = where K stands)
wall('A', [4500, A - 900], [4500, A + 900]);
zone('Z_A_open', 'open', rect(-4500, A - 500, 3000, A + 300));
zone('Z_A_end', 'wall_end', rect(4200, A - 300, 4480, A + 300));

// B - wall on the left (-Y) then, after 10 m of open ground, on the right (+Y)
// v2: the open middle is 30 m so the looser class (open) has its hold_distance (5 m) + settling room
wall('B', [-4000, B - 300], [-1500, B - 300]);
wall('B', [1500, B + 300], [4000, B + 300]);
zone('Z_B_left', 'wall_run', rect(-3300, B - 250, -1700, B + 250));
zone('Z_B_mid', 'open', rect(-400, B - 250, 1000, B + 250));
zone('Z_B_right', 'wall_run', rect(1800, B - 250, 3800, B + 250));

// C - gaps on the left: 1 m, 2 m, 3 m (bridged), then a 16 m gap (open), then the wall again
const cw = C - 300;
wall('C', [-4500, cw], [-3000, cw]);
wall('C', [-2900, cw], [-1900, cw]);  // 1 m gap before
wall('C', [-1700, cw], [-700, cw]);   // 2 m gap before
wall('C', [-400, cw], [600, cw]);     // 3 m gap before
wall('C', [3000, cw], [4500, cw]);    // 24 m gap before (v2: room for the 5 m hold + settling)
zone('Z_C_gaps', 'gap_run', rect(-3800, C - 250, 500, C + 250));
zone('Z_C_biggap', 'open', rect(1600, C - 250, 2600, C + 250));
zone('Z_C_after', 'wall_run', rect(3100, C - 250, 4300, C + 250));

// D - narrow corridor (walls 500 cm apart -> 460 free) then a wide one (800 apart -> 760 free)
wall('D', [-3500, D - 250], [-1000, D - 250]);
wall('D', [-3500, D + 250], [-1000, D + 250]);
wall('D', [1000, D - 400], [3500, D - 400]);
wall('D', [1000, D + 400], [3500, D + 400]);
zone('Z_D_narrow', 'corridor', rect(-2700, D - 200, -1100, D + 200));
zone('Z_D_wide', 'corridor', rect(1800, D - 350, 3400, D + 350));

// E - inner corner on the left: north along y = E (wall at E - 300), turn right (east, +Y) at x 0 (wall at x 300)
wall('E', [-4000, E - 300], [300, E - 300]);
wall('E', [300, E - 300], [300, 5300]);
zone('Z_E_leg1', 'wall_run', rect(-3300, E - 250, -700, E + 250));
zone('Z_E_corner', 'corner', rect(-500, E - 250, 250, E + 400));
zone('Z_E_leg2', 'wall_run', rect(-250, 3700, 250, 5000));

// F - dead end with unequal room: wall x 4500, y 4300..6000 (facing it: 200 cm on the left, 1500 on the right)
wall('F', [4500, F - 200], [4500, 5990]);
zone('Z_F_end', 'wall_end', rect(4200, F - 150, 4480, F + 150));

// V2 lane (FORMATION_CONTRACT.md 8), west half north of lane E: 4.5 m and 6 m gaps on the left - v1's time hysteresis flips
// there (known, v2 fixes)
const vw = V2 - 300;
wall('V2', [-4500, vw], [-3500, vw]);
wall('V2', [-3050, vw], [-2400, vw]);
wall('V2', [-1800, vw], [-800, vw]);
zone('Z_V2_gaps', 'gap_run', rect(-3900, V2 - 250, -1000, V2 + 250));

// G lane (v2.1, east of lane E's corner wall): G1 oblique across the route (1 m to the left of the axis at its near end, 2 m
// to the right at its far end: round the near end = the wall on the right is the shorter side), G2 square to the route at
// x 4800, 2 m past the axis on the left (-Y), 11 m on the right (+Y).
wall('G', [1700, G - 100], [3100, G + 200]);
wall('G', [4800, G - 200], [4800, G + 1100]);
zone('Z_G1', 'wall_run', rect(1800, G - 450, 3400, G + 450));
// v2.4 (user bug report 2026-10-26): the squad stops at W_G_20's free END face (R_G_end) - the wall layout must hold the open arc
// round the end, both flanks along its faces (bot check 13). Listed before Z_G2: zoneAt takes the first zone a point lies in.
zone('Z_G_end', 'wall_end', rect(4650, G - 340, 4950, G - 210));
zone('Z_G2', 'wall_end', rect(4500, G - 500, 5100, G + 300));

const layout = {
  schema: 'codex.formation_layout/1',
  name: 'formation_lab_01',
  comment: 'Generated by Tools/TrainingGround/src/movement/cli/genLab.ts (npm run lab:gen) - edit there. Lanes A-F + V2 along +X, one feature each. Rules v2 (path hold, classify.hold_distance). Open stretches between walls are longer than the hold + settling (B middle 30 m, C big gap 24 m).',
  bounds: { min: [-6000, -6000], max: [6000, 6000] },
  ground_z: 0,
  spawn: { pos: [-5500, A], yaw: 0 },
  walls,
  zones,
  props: [{ id: 'HEAT_A', kind: 'heat_source', pos: [-3000, A - 1000] }],
  routes: [
    { id: 'R_A_open', pace: 'walk', points: [[-5500, A], [4435, A]],
      expect: [{ zone: 'Z_A_open', formation: 'diamond' }, { zone: 'Z_A_end', formation: 'wall' }] },
    { id: 'R_A_run', pace: 'run', points: [[-5500, A - 300], [3000, A - 300]],
      expect: [{ zone: 'Z_A_open', formation: 'diamond' }] },
    { id: 'R_B_walls', pace: 'walk', points: [[-5500, B], [5500, B]],
      expect: [{ zone: 'Z_B_left', formation: 'echelon_right' }, { zone: 'Z_B_mid', formation: 'diamond' }, { zone: 'Z_B_right', formation: 'echelon_left' }] },
    { id: 'R_C_gaps', pace: 'walk', points: [[-5500, C], [5500, C]],
      expect: [{ zone: 'Z_C_gaps', formation: 'echelon_right' }, { zone: 'Z_C_biggap', formation: 'diamond' }, { zone: 'Z_C_after', formation: 'echelon_right' }] },
    { id: 'R_D_corridors', pace: 'walk', points: [[-5500, D], [5500, D]],
      expect: [{ zone: 'Z_D_narrow', formation: 'column' }, { zone: 'Z_D_wide', formation: 'column' }] },
    { id: 'R_E_corner', pace: 'walk', points: [[-5500, E], [0, E], [0, 5200]],
      expect: [{ zone: 'Z_E_leg1', formation: 'echelon_right' }, { zone: 'Z_E_corner', formation: 'echelon_right' }, { zone: 'Z_E_leg2', formation: 'echelon_right' }] },
    { id: 'R_F_deadend', pace: 'walk', points: [[1200, F], [4435, F]],
      expect: [{ zone: 'Z_F_end', formation: 'wall' }] },
    { id: 'R_v2_gaps', pace: 'walk', points: [[-5500, V2], [-300, V2]],
      expect: [{ zone: 'Z_V2_gaps', formation: 'echelon_right' }] },
    // v2.2 wall slide (check 11): to the lane-A dead end (the wall layout), then 6 m along the wall one way, 12 m back the other
    { id: 'R_A_slide', pace: 'walk', points: [[2000, A], [4435, A]], slide: [[4435, A - 600], [4435, A + 600]] },
    // v2.1 bypass (check 8): no threat -> the shortest side; a threat seen behind the wall -> the side it hides; too long -> shortest
    { id: 'R_G_free', pace: 'walk', points: [[1100, G], [4300, G]], expect_bypass: [{ wall: 'W_G_19', wall_on: 'right' }] },
    { id: 'R_G_threat_right', pace: 'walk', points: [[1100, G], [4300, G]], threats: [{ pos: [2600, G + 900], since_s: 0 }],
      expect_bypass: [{ wall: 'W_G_19', wall_on: 'right' }] },
    { id: 'R_G_threat_left', pace: 'walk', points: [[1100, G], [4300, G]], threats: [{ pos: [2600, G - 900], since_s: 0 }],
      expect_bypass: [{ wall: 'W_G_19', wall_on: 'left' }] },
    { id: 'R_G_far', pace: 'walk', points: [[3900, G], [5900, G]], threats: [{ pos: [5500, G + 700], since_s: 0 }],
      expect_bypass: [{ wall: 'W_G_20', wall_on: 'right' }] },
    // v2.4 (user bug report 2026-10-26): up to the free end face of W_G_20 (a thin free-standing wall) - the leader's wall slot is
    // 45 cm off the end face on the wall's axis; the last leg comes in 4 deg off the axis (a ray along the axis slides between the
    // faces of the simulator's walls - the game's trace hits the end face; the slot is the same).
    { id: 'R_G_end', pace: 'walk', points: [[4700, 1300], [4800, G - 265]],
      expect: [{ zone: 'Z_G_end', formation: 'wall' }] },
  ],
};

const file = path.join(LAB_DIR, 'formation_lab_01.json');
fs.writeFileSync(file, JSON.stringify(layout, null, 2) + '\n');
console.log(`wrote ${path.relative(process.cwd(), file)}: ${walls.length} walls, ${zones.length} zones, ${layout.routes.length} routes`);
