// Mission goals runtime file (designer 2026-11-17, F-4 «а»): layout objectives -> Content/Data/LevelGen/objectives/<Map>.objectives.json.
import { describe, it } from 'node:test';
import * as assert from 'node:assert/strict';
import * as fs from 'node:fs';
import * as os from 'node:os';
import * as path from 'node:path';
import type { GenLayout } from './schema.ts';
import { findProjectRoot } from './props/autogen.ts';
import {
  DEFAULT_FLOOR_TOLERANCE_CM, DEFAULT_GOAL_RADIUS_CM, MISSION_GOALS_SCHEMA, formatMissionGoals, layerZ, missionGoalsFor, missionGoalsPath, writeMissionGoalsFile,
} from './missionGoals.ts';

const root = findProjectRoot();
const x4 = JSON.parse(fs.readFileSync(path.join(root, 'Content/Data/LevelGen/accepted/gen_ref_01_02_depot_1_x4.json'), 'utf8')) as GenLayout;

const tiny = (over: Partial<GenLayout> = {}): GenLayout => ({
  name: 'tiny', bounds: { min: [-5000, -5000], max: [5000, 5000] }, ground_z: 40, spawn: { pos: [0, 0], yaw: 0 }, end: [100, 100],
  walls: [], zones: [], buildings: [
    { id: 'B_1', footprint: [[1000, 1000], [3000, 2000]], floors: 2, enterable: true, rotation: 90,
      rooms: [{ id: 'R_a', floor: 0, bounds: { min: [1000, 1000], max: [2000, 2000] } }, { id: 'R_b', floor: 0, bounds: { min: [2000, 1000], max: [3000, 2000] } }] },
  ],
  ladders: [], covers: [], routes: [], spawn_points: [], ambush_points: [], patrol_routes: [], mantle_edges: [],
  objectives: { version: 1, seed: 3, order: 'free', start: { pos: [0, 0], yaw: 0, layer: 'ground', source: 'spawn' }, goals: [
    { id: 'GOAL_1', pos: [-2000, 0], layer: 'ground', kind: 'street', zone: 'Z_ST_1' },
    { id: 'GOAL_2', pos: [2000, 1250], layer: 'ground', kind: 'room', building: 'B_1' },
    { id: 'GOAL_3', pos: [2000, 1500], layer: 'roof:B_1', kind: 'yard' },
  ] },
  ...over,
} as unknown as GenLayout);

describe('mission goals runtime file (F-4)', () => {
  it('maps every goal to world cm with the default radius, the start is not a goal', () => {
    const f = missionGoalsFor(tiny(), 'L_Gen_Tiny')!;
    assert.equal(f.schema, MISSION_GOALS_SCHEMA);
    assert.equal(f.map, 'L_Gen_Tiny');
    assert.equal(f.layout, 'tiny');
    assert.equal(f.order, 'free');
    assert.equal(f.default_radius_cm, DEFAULT_GOAL_RADIUS_CM);
    assert.equal(f.floor_tolerance_cm, DEFAULT_FLOOR_TOLERANCE_CM);
    assert.deepEqual(f.goals.map((g) => g.id), ['GOAL_1', 'GOAL_2', 'GOAL_3']);
    assert.deepEqual(f.goals[0].pos, [-2000, 0, 40]);
    assert.equal(f.goals[0].zone, 'Z_ST_1');
    assert.equal(f.goals[0].area, undefined);
    assert.ok(f.goals.every((g) => g.radius_cm === DEFAULT_GOAL_RADIUS_CM));
  });

  it('z follows the layer: ground_z + level x 300 (a roof of a 2-floor building = level 2)', () => {
    const l = tiny();
    assert.equal(layerZ(l, 'ground'), 40);
    assert.equal(layerZ(l, 'roof:B_1'), 640);
    assert.equal(layerZ(l, 'floor:B_1:1'), 340);
    assert.equal(missionGoalsFor(l, 'M')!.goals[2].pos[2], 640);
  });

  it('a room goal carries its room polygon (the building rotation applied)', () => {
    const f = missionGoalsFor(tiny(), 'M')!;
    const area = f.goals[1].area!;
    assert.equal(area.length, 4);
    // the building turns 90 deg about its centre (2000, 1500): the goal (2000, 1250) is (1750, 1500) in its own frame -> room R_a,
    // whose frame rectangle [1000..2000] x [1000..2000] stands at x [1500..2500], y [500..1500] in the world
    const xs = area.map((p) => p[0]), ys = area.map((p) => p[1]);
    assert.deepEqual([Math.min(...xs), Math.max(...xs), Math.min(...ys), Math.max(...ys)], [1500, 2500, 500, 1500]);
  });

  it('a layout without objectives writes nothing (the game then has no goals)', () => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'mgoals-'));
    assert.equal(missionGoalsFor(tiny({ objectives: undefined }), 'M'), null);
    assert.equal(writeMissionGoalsFile(tiny({ objectives: undefined }), dir, 'M'), null);
    assert.equal(fs.existsSync(missionGoalsPath(dir, 'M')), false);
    const file = writeMissionGoalsFile(tiny(), dir, 'M')!;
    assert.equal(file, missionGoalsPath(dir, 'M'));
    assert.equal(fs.readFileSync(file, 'utf8'), formatMissionGoals(missionGoalsFor(tiny(), 'M')!));
    fs.rmSync(dir, { recursive: true, force: true });
  });

  it('the committed x4 Depot file matches its accepted layout', () => {
    const want = formatMissionGoals(missionGoalsFor(x4, 'L_Gen_Depot_X4')!);
    const have = fs.readFileSync(path.join(root, 'Content/Data/LevelGen/objectives/L_Gen_Depot_X4.objectives.json'), 'utf8').split('\r\n').join('\n'); // autocrlf checkouts
    assert.equal(have, want);
    const f = JSON.parse(have);
    assert.equal(f.goals.length, 3);
    assert.ok(f.goals[0].area.length === 4, 'GOAL_1 is a room goal with its room polygon');
  });
});
