// Usable slot balconies (designer 2026-11-22: "balconies on every building - step out, a position behind the parapet, vault the rail and drop";
// buildingStories.ts applySlotBalconies), checked on the accepted story showcase layout the generator writes (gen-story-showcase.ts --plans).
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { test } from 'node:test';
import { applySlotBalconies, SLOT_BALCONY_DEPTH } from './buildingStories.ts';
import { obstacleHeight } from './obstacleHeights.ts';
import type { GenLayout } from './schema.ts';
import { unifyOpenings } from './schema.ts';

const SHOWCASE = path.join(import.meta.dirname, '../../../../Content/Data/LevelGen/accepted/gen_story_showcase.json');

test('slot balconies on the showcase: every house has usable balconies, each behind a balcony door that replaced its slot window', () => {
  const l = JSON.parse(fs.readFileSync(SHOWCASE, 'utf8')) as GenLayout;
  for (const b of l.buildings.filter((x) => x.enterable)) {
    const balconies = b.balconies ?? [];
    assert.ok(balconies.length >= 2, `${b.id}: at least two balconies (${balconies.length})`);
    const openings = unifyOpenings(b);
    for (const bal of balconies) {
      const door = openings.find((o) => o.id === bal.door_id);
      assert.ok(door && door.kind === 'door' && (door.floor ?? 0) === bal.floor, `${b.id} ${bal.id}: its door on its storey`);
      assert.ok(bal.floor >= 1 && bal.floor < b.floors, `${b.id} ${bal.id}: on an upper storey`);
      assert.equal(bal.rail_cm, obstacleHeight('low_vault'), `${b.id} ${bal.id}: a low-vault rail (cover + vault over)`);
      if (bal.id.includes('_bal_') && !bal.id.includes('_esc_')) {
        const [[ax, ay], [bx, by]] = bal.rect;
        const depth = Math.min(Math.abs(bx - ax), Math.abs(by - ay));
        assert.ok(Math.abs(depth - SLOT_BALCONY_DEPTH) <= 1, `${b.id} ${bal.id}: ${SLOT_BALCONY_DEPTH} cm deep (${depth})`);
        assert.ok(!openings.some((o) => o.kind === 'window' && (o.floor ?? 0) === bal.floor && door && Math.hypot(o.pos[0] - door.pos[0], o.pos[1] - door.pos[1]) < 30),
          `${b.id} ${bal.id}: no window left where the door went`);
      }
    }
  }
});

test('slot balconies leave a house with balconies (story 4 escape) alone and are not added twice', () => {
  const l = JSON.parse(fs.readFileSync(SHOWCASE, 'utf8')) as GenLayout;
  for (const b of l.buildings) {
    const before = JSON.stringify(b);
    assert.equal(applySlotBalconies(l, b, 2), 0, `${b.id}: nothing added again`);
    assert.equal(JSON.stringify(b), before, `${b.id}: unchanged`);
  }
});
