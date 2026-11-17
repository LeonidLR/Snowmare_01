// Training Ground / props - ROTATED BUILDINGS (task 082): the props layout view (props panel T5, fast gate, autogen) tests points
// against a building's real (rotated) footprint via rotation.ts (inFootprint / toBuildingFrame / footprintPoly), not its AABB.
// A 30 deg and a 45 deg building: a point inside the rotated footprint is rejected, one just outside the rotated wall but inside the
// old AABB is accepted; autogen on a 45 deg re-rolled Depot puts no prop inside a closed building. `npm test`.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { describe, it } from 'node:test';
import { fileURLToPath } from 'node:url';
import { generateLayout } from '../generate.ts';
import type { Profile } from '../profile.ts';
import { regenerateRegion } from '../regionRegen.ts';
import { inFootprint, rotationOf, rotPoint, rotVec } from '../rotation.ts';
import type { GenLayout } from '../schema.ts';
import { generatePropsForLayout, loadAutogenConfig, loadPropsBalance } from './autogen.ts';
import { inRect, PropsLayoutView } from './layoutView.ts';
import { applyOverrides } from './placement.ts';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../../..');

const MIN: [number, number] = [-500, -300];
const MAX: [number, number] = [500, 300];
/** A local (building-frame) point as integer world cm. */
const world = (local: [number, number], deg: number): [number, number] => {
  const q = rotPoint(local, [0, 0], deg);
  return [Math.round(q[0]), Math.round(q[1])];
};
const layoutWith = (deg: number, enterable = false) => ({
  schema: 'codex.levelgen_layout/1',
  name: `rot_${deg}`,
  bounds: { min: [-2000, -2000], max: [2000, 2000] },
  spawn: { pos: [-1800, -1800] },
  walls: [],
  buildings: [
    {
      id: 'B_rot',
      footprint: [MIN, MAX],
      rotation: deg,
      floors: 2,
      hollow: false,
      enterable,
      roof_walkable: true,
      doors: [{ pos: world([500, 0], deg), floor: 0 }],
      stairs: [],
      roof_holes: [],
      floor_holes: [{ pos: world([-300, 0], deg), floor: 1 }],
    },
  ],
});

for (const deg of [30, 45]) {
  describe(`props view: building rotated ${deg} deg`, () => {
    const view = PropsLayoutView.build(layoutWith(deg));
    const inside = world([400, 0], deg);
    const insideOutOfAabb = world([480, 250], deg); // the rotated corner sticks out of the old AABB
    const outsideInAabb = world([0, 340], deg); // 40 cm past the rotated long wall, still inside the old AABB

    it('the test points mean what they say', () => {
      assert.ok(!inRect(insideOutOfAabb, MIN, MAX) && inRect(outsideInAabb, MIN, MAX));
    });

    it('ground: a point inside the rotated footprint is blocked, one just outside the rotated wall is free', () => {
      assert.equal(view.isBlocked(inside, 'ground', 0), true);
      assert.equal(view.isBlocked(insideOutOfAabb, 'ground', 0), true);
      assert.equal(view.isFree(outsideInAabb, 'ground', 20), true);
      assert.equal(view.isBlocked(outsideInAabb, 'ground', 60), true, 'clearance reaches the rotated wall');
    });

    it('floor / roof layers walk the rotated footprint (inset and holes in the building frame)', () => {
      assert.equal(view.isWalkable(inside, 'floor:B_rot:1'), true);
      assert.equal(view.isWalkable(insideOutOfAabb, 'floor:B_rot:1'), true);
      assert.equal(view.isWalkable(outsideInAabb, 'floor:B_rot:1'), false);
      assert.equal(view.isWalkable(world([-300, 0], deg), 'floor:B_rot:1'), false, 'floor hole');
      assert.equal(view.isWalkable(world([0, 0], deg), 'roof:B_rot'), true);
      assert.equal(view.isWalkable(world([0, 300 - Math.floor(view.config.roofInsetCm / 2)], deg), 'roof:B_rot'), false, 'roof inset');
      assert.equal(view.isWalkable(outsideInAabb, 'roof:B_rot'), false);
      const ext = view.getLayerExtent('roof:B_rot')!;
      assert.ok(inRect(insideOutOfAabb, ext.min, ext.max), 'the layer extent covers the rotated corners');
    });

    it('perimeter walls and the summary follow the rotated polygon; the door faces out of its rotated wall', () => {
      const b = view.getBuildings()[0];
      assert.equal(b.rotation, deg);
      assert.deepEqual(b.poly[1], world([500, -300], deg));
      const door = view.doorways.find((d) => d.building === 'B_rot')!;
      const out = rotVec([1, 0], deg);
      assert.ok(Math.abs(door.outward[0] - out[0]) < 1e-9 && Math.abs(door.outward[1] - out[1]) < 1e-9);
    });

    it('an enterable rotated building does not block the ground', () => {
      const v = PropsLayoutView.build(layoutWith(deg, true));
      assert.equal(v.isBlocked(inside, 'ground', 0), false);
    });
  });
}

describe('props view: an unrotated building keeps the AABB behaviour', () => {
  const view = PropsLayoutView.build(layoutWith(0));
  it('the AABB corner is blocked, the extent is the footprint', () => {
    assert.equal(view.isBlocked([490, 290], 'ground', 0), true);
    assert.deepEqual(view.getLayerExtent('roof:B_rot'), { min: MIN, max: MAX });
    assert.deepEqual(view.doorways[0].outward, [1, 0]);
  });
});

describe('autogen on a layout with rotated buildings (45 deg Depot re-roll)', () => {
  const profile = JSON.parse(fs.readFileSync(path.join(ROOT, 'Content/Data/LevelGen/refs/ref_01_02_depot.json'), 'utf8')) as Profile;
  const base = generateLayout({ profile, seed: 1 });
  const b = base.bounds;
  const zone: [number, number][] = [[b.min[0], b.min[1]], [b.max[0], b.min[1]], [b.max[0], b.max[1]], [b.min[0], b.max[1]]];
  const r = regenerateRegion({ layout: base, zone, seed: 1, profile, blocks: 'full', chaos: 1, maxRotationDeg: 45 });
  const layout = r.layout as GenLayout;

  it('puts no prop inside a closed rotated building and floor / roof props inside their building', () => {
    assert.equal(r.ok, true, r.message);
    const turned = layout.buildings.filter((x) => rotationOf(x) !== 0);
    assert.ok(turned.length > 0, 'the re-roll rotated some buildings');
    const res = generatePropsForLayout({ layout, balance: loadPropsBalance(ROOT), autogenConfig: loadAutogenConfig(ROOT), seed: 7, layoutName: 'rot45_test', projectRoot: ROOT });
    const objs = applyOverrides(res.props).objects.map((o) => o.object);
    assert.ok(objs.length > 0);
    const bad: string[] = [];
    for (const o of objs) {
      const wl = o.walk_layer ?? 'ground';
      if (wl === 'ground') {
        for (const bd of layout.buildings) {
          if (!bd.enterable && inFootprint(bd, o.pos, -1)) bad.push(`${o.id} @${o.pos} inside closed ${bd.id} (rot ${rotationOf(bd)})`);
        }
      } else {
        const id = wl.startsWith('roof:') ? wl.slice(5) : wl.startsWith('floor:') ? wl.split(':')[1] : '';
        const bd = layout.buildings.find((x) => x.id === id);
        if (bd && !inFootprint(bd, o.pos)) bad.push(`${o.id} @${o.pos} on ${wl} outside its (rot ${rotationOf(bd)}) footprint`);
      }
    }
    assert.deepEqual(bad, []);
  });
});
