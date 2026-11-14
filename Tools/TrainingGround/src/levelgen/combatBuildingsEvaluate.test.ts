import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { test } from 'node:test';
import { fileURLToPath } from 'node:url';
import { rng } from '../movement/geometry.ts';
import { configValues } from '../movement/config.ts';
import { criticPrompt, jevState, mockCritic } from './critic.ts';
import { evaluateLayout } from './evaluate.ts';
import type { Draft } from './generate.ts';
import { addBuilding, addWall, finalizeDraftToLayout, generateLayout } from './generate.ts';
import type { Profile } from './profile.ts';
import { PROFILE_SCHEMA } from './profile.ts';
import { DEFAULT_PARAMS, LEVEL_HEIGHT, validateGenLayout } from './schema.ts';
import { openingOutwardDir } from './tacticalEvaluate.ts';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../..');
const depotProfile = JSON.parse(fs.readFileSync(path.join(ROOT, 'Content/Data/LevelGen/refs/ref_01_02_depot.json'), 'utf8')) as Profile;
const config = configValues(JSON.parse(fs.readFileSync(path.join(ROOT, 'Content/Data/FormationLab/formation_config.json'), 'utf8')));

function makeSmallProfile(size_m: [number, number] = [40, 30]): Profile {
  return {
    schema: PROFILE_SCHEMA,
    id: 'test_small_arena',
    title: 'Test Small Arena',
    bounds: { min: [0, 0], max: size_m },
    scale: { recommended: '1:1', ue_ground_plane_m: { recommended: size_m } },
    start: { pos: [4, 15], yaw_deg: 0 },
    end: { pos: [36, 15] },
    ladders: [],
    zones: [],
    buildings: [],
    route: { main: { id: 'R_main', points: [[4, 15], [36, 15]], length_m: 32 }, variants: [] },
    sightlines: { lanes: [], stats_m: { p50: 25, p90: 35, max: 40, short_zone_share: 0 } },
    densities: { covers_per_100m2: {}, enterable_share: 1, roof_access_share: 1, ladders_per_ha: 0 },
    set_pieces: {},
    targets: { route_length_m: [25, 45] },
  };
}

function makeBaseDraft(): Draft {
  return {
    walls: [],
    buildings: [],
    ladders: [],
    covers: [],
    zones: [],
    routes: [{ id: 'R_main', pace: 'walk', points: [[4, 15], [36, 15]], mandatory: true }],
    spawn_points: [],
    ambush_points: [],
    patrol_routes: [],
    mantle_edges: [],
    keepFree: [],
  };
}

test('depot seeds 1..2 pass tactical evaluation checks (rooms, roofs, unblocked windows, firing lines, cover, verticality, mantle)', () => {
  for (const seed of [1, 2]) {
    const layout = generateLayout({ profile: depotProfile, seed });
    const ev = evaluateLayout({ layout, profile: depotProfile, config, noSim: true });
    const tac = ev.tactical;

    assert.equal(tac.unreachableRooms.length, 0, `seed ${seed}: unreachable rooms ${tac.unreachableRooms.join(', ')}`);
    assert.ok(tac.roomsTotal >= 6, `seed ${seed}: expected >= 6 rooms, got ${tac.roomsTotal}`);
    assert.equal(tac.roomsReachable, tac.roomsTotal, `seed ${seed}: all rooms reachable`);
    assert.equal(tac.blockedWindows.length, 0, `seed ${seed}: no blocked windows (${JSON.stringify(tac.blockedWindows)})`);

    for (const br of tac.buildings) {
      if (br.roof_walkable) {
        assert.equal(br.roofReachable, true, `seed ${seed}: walkable roof on ${br.id} must be reachable`);
      }
      if (br.isCombatBuilding) {
        assert.ok(br.score >= 0.45, `seed ${seed}: combat building ${br.id} score ${br.score} too low`);
      }
    }

    assert.ok(tac.firingLines.count >= 4, `seed ${seed}: expected >= 4 firing lines, got ${tac.firingLines.count}`);
    assert.ok(tac.firingLines.openingsWithLos >= 2, `seed ${seed}: expected >= 2 openings with LOS`);
    assert.ok(tac.firingLines.minLength_m > 0 && tac.firingLines.maxLength_m >= tac.firingLines.meanLength_m);
    assert.ok(tac.firingLines.meanLength_m >= tac.firingLines.minLength_m);

    assert.ok(tac.routeCover.share > 0, `seed ${seed}: route cover share ${tac.routeCover.share} must be > 0`);
    assert.ok(tac.routeCover.coversNearRoute > 0, `seed ${seed}: covers near route must be > 0`);

    assert.equal(tac.multiLevelRoutes.exists, true, `seed ${seed}: multi-level route must exist`);
    assert.ok(tac.multiLevelRoutes.levelsUsed.length >= 2, `seed ${seed}: route uses >= 2 levels`);

    assert.ok(tac.mantle.valid >= 20, `seed ${seed}: expected >= 20 valid mantle edges, got ${tac.mantle.valid}`);
    assert.equal(tac.mantle.invalid, 0, `seed ${seed}: expected 0 invalid mantle edges`);
    assert.ok((tac.mantle.byKind.parapet ?? 0) >= 1, `seed ${seed}: expected parapet mantle edges`);

    assert.ok(tac.score >= 0.7 && tac.score <= 1, `seed ${seed}: tactical score ${tac.score} out of range`);
  }
});

test('unreachable isolated room fails reachability check and mockCritic flags it', () => {
  const random = rng(10);
  const d = makeBaseDraft();
  addBuilding(d, 'B_house', [[12, 4], [28, 12]], {
    height: LEVEL_HEIGHT,
    floors: 1,
    enterable: true,
    roof_walkable: false,
    partitionRooms: false,
    doorSides: ['south'],
    windowSides: ['south'],
  }, random);

  const layout = finalizeDraftToLayout(d, [40, 30], {
    name: 'broken_room',
    profile: 'test_small_arena',
    seed: 10,
    params: { ...DEFAULT_PARAMS, size_m: [40, 30] },
    start: [4, 15],
    end: [36, 15],
  });

  // Replace rooms with two rooms partitioned inside, but give NO interior partition door to R_isolated
  const b = layout.buildings[0];
  const [[x0, y0], [x1, y1]] = b.footprint;
  const xMid = Math.round((x0 + x1) / 2);
  const extDoorId = (b.openings ?? []).find((o) => o.kind === 'door')?.id;
  b.partitions = [{ id: 'P_solid', floor: 0, a: [xMid, y0], b: [xMid, y1], thickness: 20, height: LEVEL_HEIGHT, openings: [] }];
  b.rooms = [
    { id: 'R_entry', floor: 0, bounds: { min: [x0, y0], max: [xMid, y1] }, kind: 'entry', door_ids: extDoorId ? [extDoorId] : [] },
    { id: 'R_isolated', floor: 0, bounds: { min: [xMid, y0], max: [x1, y1] }, kind: 'room', door_ids: [] },
  ];
  // Re-attach exterior door pos inside R_entry just to be deterministic
  const extDoor = (b.openings ?? []).find((o) => o.kind === 'door');
  if (extDoor) extDoor.pos = [Math.round((x0 + xMid) / 2), extDoor.pos[1]];

  const ev = evaluateLayout({ layout, profile: makeSmallProfile(), config, noSim: true });
  assert.ok(ev.tactical.unreachableRooms.includes('R_isolated'), `expected R_isolated in unreachableRooms, got ${JSON.stringify(ev.tactical.unreachableRooms)}`);
  assert.equal(ev.tactical.roomsTotal, 2);
  assert.equal(ev.tactical.roomsReachable, 1);

  const verdict = mockCritic(ev);
  assert.equal(verdict.fits, false);
  assert.ok(verdict.reasons.some((s) => s.includes('R_isolated') || s.includes('isolated') || s.includes('unreachable')));

  const prompt = criticPrompt({ layoutName: 'broken_room', evaluation: ev, profile: makeSmallProfile() });
  assert.ok(prompt.includes('"tactical"'));
  assert.ok(prompt.includes('R_isolated'));
  const jState = jevState(ev, makeSmallProfile()) as { candidate: { tactical: { unreachable_rooms: number } } };
  assert.equal(jState.candidate.tactical.unreachable_rooms, 1);
});

test('window facing a wall within 150 cm is reported as blocked and flagged by mockCritic', () => {
  const random = rng(11);
  const d = makeBaseDraft();
  addBuilding(d, 'B_blocked', [[14, 4], [24, 11]], {
    height: LEVEL_HEIGHT,
    floors: 1,
    enterable: true,
    roof_walkable: false,
    doorSides: ['east'],
    windowSides: ['south'],
  }, random);

  const layout = finalizeDraftToLayout(d, [40, 30], {
    name: 'blocked_win_fixture',
    profile: 'test_small_arena',
    seed: 11,
    params: { ...DEFAULT_PARAMS, size_m: [40, 30] },
    start: [4, 15],
    end: [36, 15],
  });

  const b = layout.buildings[0];
  const win = (b.openings ?? []).find((o) => o.kind === 'window');
  assert.ok(win, 'expected generated window');
  const dir = openingOutwardDir(b, win!);
  const perp: [number, number] = [-dir[1], dir[0]];
  // Place a solid wall 80 cm outside the window opening perpendicular to its firing line
  const cx = win!.pos[0] + dir[0] * 80;
  const cy = win!.pos[1] + dir[1] * 80;
  layout.walls.push({
    id: 'W_blocker_close',
    a: [Math.round(cx - perp[0] * 250), Math.round(cy - perp[1] * 250)],
    b: [Math.round(cx + perp[0] * 250), Math.round(cy + perp[1] * 250)],
    thickness: 30,
    height: 300,
    floor: 0,
    kind: 'fence',
  });

  assert.deepEqual(validateGenLayout(layout), []);
  const ev = evaluateLayout({ layout, profile: makeSmallProfile(), config, noSim: true });
  assert.ok(ev.tactical.blockedWindows.length >= 1, 'blocked window must be detected');
  assert.equal(ev.tactical.blockedWindows[0].building, 'B_blocked');

  const verdict = mockCritic(ev);
  assert.equal(verdict.fits, false);
  assert.ok(verdict.reasons.some((s) => s.toLowerCase().includes('blocked')));
});

test('window firing lines see clear mandatory route and drop to 0 when tall wall blocks LOS', () => {
  const makeFixture = (withTallScreen: boolean) => {
    const random = rng(12);
    const d = makeBaseDraft();
    addBuilding(d, 'B_bunker', [[14, 4], [24, 11]], {
      height: LEVEL_HEIGHT,
      floors: 1,
      enterable: true,
      roof_walkable: false,
      set_piece: 'bunker',
      doorSides: ['north'],
      windowSides: ['south'],
    }, random);
    if (withTallScreen) {
      // Place tall opaque wall across the full corridor at y = 13 m (north of the y=15m street, 2m south of the y=11m facade)
      addWall(d, 'W_tall_screen', [2, 13], [38, 13], 350, 30, 'wall');
    }
    return finalizeDraftToLayout(d, [40, 30], {
      name: withTallScreen ? 'los_blocked' : 'los_clear',
      profile: 'test_small_arena',
      seed: 12,
      params: { ...DEFAULT_PARAMS, size_m: [40, 30] },
      start: [4, 15],
      end: [36, 15],
    });
  };

  const clearEv = evaluateLayout({ layout: makeFixture(false), profile: makeSmallProfile(), config, noSim: true });
  assert.ok(clearEv.tactical.firingLines.count >= 1, 'clear south window must have firing line onto R_main');
  assert.ok(clearEv.tactical.firingLines.maxLength_m >= 3);

  const blockedEv = evaluateLayout({ layout: makeFixture(true), profile: makeSmallProfile(), config, noSim: true });
  assert.equal(blockedEv.tactical.firingLines.count, 0, 'tall wall across corridor must block all window firing lines to R_main');
  assert.equal(blockedEv.tactical.blockedWindows.length, 0, '2m distance keeps opening clear of the 150cm adjacency check');
});

test('walkable roof with no stair, ladder, hatch, or roof hole is flagged unreachable', () => {
  const random = rng(13);
  const d = makeBaseDraft();
  addBuilding(d, 'B_stranded_roof', [[12, 4], [22, 11]], {
    height: LEVEL_HEIGHT,
    floors: 1,
    enterable: true,
    roof_walkable: true,
    doorSides: ['south'],
    windowSides: ['south'],
  }, random);

  const layout = finalizeDraftToLayout(d, [40, 30], {
    name: 'stranded_roof',
    profile: 'test_small_arena',
    seed: 13,
    params: { ...DEFAULT_PARAMS, size_m: [40, 30] },
    start: [4, 15],
    end: [36, 15],
  });

  const ev = evaluateLayout({ layout, profile: makeSmallProfile(), config, noSim: true });
  const rep = ev.tactical.buildings.find((b) => b.id === 'B_stranded_roof')!;
  assert.equal(rep.roofReachable, false, 'walkable roof without any access link must be unreachable');
  const verdict = mockCritic(ev);
  assert.equal(verdict.fits, false);
  assert.ok(verdict.reasons.some((s) => s.includes('B_stranded_roof')));
});
