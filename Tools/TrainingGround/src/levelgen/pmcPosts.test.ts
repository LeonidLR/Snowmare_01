// PMC posts (docs/design/PMC_DEFENCE.md D2, task 087): the generator's seeded choice of PMC squad posts at the strategic places of the
// tactical map - never inside a chokepoint, never at the player start / on the mandatory route, spaced, deterministic per seed.
import { describe, it } from 'node:test';
import * as assert from 'node:assert/strict';
import {
  expansionFactor, inChokepoint, mandatorySegments, PMC_POST_KINDS, PMC_POSTS, planPmcPosts, postCandidates, routeAnchor, routeDistOnLayer, startOf,
} from './pmcPosts.ts';
import { loadX4, tinyLayout, tinyTactical } from './pmcTestFixtures.ts';

const d2 = (a: readonly number[], b: readonly number[]) => Math.hypot(a[0] - b[0], a[1] - b[1]);

describe('PMC posts - the choice rules (task 087)', () => {
  it('rejects posts in a chokepoint, at the start, on the route and too far; keeps the rest', () => {
    const cfg = { ...PMC_POSTS, max_from_route_m: 40, squad_spacing_m: 10 };
    const plan = planPmcPosts(tinyLayout(), tinyTactical(), 7, { config: cfg, count: 10 });
    assert.deepEqual(plan.report.rejected, { chokepoint: 1, start: 1, route: 1, far: 1 });
    assert.deepEqual(plan.posts.map((p) => p.source).sort(), ['HS_ok1', 'HS_ok2', 'HS_ok3']);
    const ok1 = plan.posts.find((p) => p.source === 'HS_ok1')!;
    assert.equal(ok1.kind, 'ambush_exit');
    assert.equal(ok1.place, 'CP_01');
    assert.deepEqual(ok1.anchor, [1000, 0, 0], 'the attack anchor = the nearest mandatory-route point');
    assert.equal(ok1.route_m, 12);
    assert.equal(plan.posts.find((p) => p.source === 'HS_ok2')!.kind, 'hiding_spot');
  });

  it('keeps squad_spacing_m between posts and one post per building', () => {
    const cfg = { ...PMC_POSTS, max_from_route_m: 40, squad_spacing_m: 20 };
    const plan = planPmcPosts(tinyLayout(), tinyTactical(), 3, { config: cfg, count: 10 });
    for (const a of plan.posts) for (const b of plan.posts) if (a !== b) assert.ok(d2(a.pos, b.pos) >= 2000, `${a.id} ${b.id} spaced`);
    assert.ok(plan.posts.length < 3, 'HS_ok1 / HS_ok3 are 15.3 m apart - not all three fit a 20 m spacing');
  });

  it('a kind with weight 0 is never drawn; posture / delay / squad size come from the config', () => {
    const cfg = { ...PMC_POSTS, max_from_route_m: 40, squad_spacing_m: 1, kinds: { ...PMC_POSTS.kinds, ambush_exit: { weight: 0, squad: 3 } },
      posture_weights: { defend: 0, hunt: 1 } };
    const plan = planPmcPosts(tinyLayout(), tinyTactical(), 11, { config: cfg, count: 10 });
    assert.ok(plan.posts.length > 0);
    for (const p of plan.posts) {
      assert.notEqual(p.kind, 'ambush_exit');
      assert.equal(p.posture, 'hunt');
      assert.equal(p.delay_s, PMC_POSTS.delay_s.hunt);
      assert.equal(p.count, PMC_POSTS.kinds[p.kind].squad);
    }
  });

  it('chokepoint margin: a point just outside the outline counts as inside within the margin', () => {
    const poly = [[0, 0], [1000, 0], [1000, 1000], [0, 1000]];
    assert.equal(inChokepoint([1050, 500], [poly], 100), true);
    assert.equal(inChokepoint([1150, 500], [poly], 100), false);
    assert.equal(inChokepoint([500, 500], [poly], 0), true);
  });
});

describe('PMC posts on the accepted X4 layout (task 087)', () => {
  const { layout, tactical } = loadX4();
  const segs = mandatorySegments(layout);
  const polys = tactical.chokepoints.map((c) => c.poly);
  const start = startOf(layout);

  it('every candidate kind is present on X4', () => {
    const kinds = new Set(postCandidates(tactical).map((c) => c.kind));
    for (const k of PMC_POST_KINDS) assert.ok(kinds.has(k), k);
  });

  it('25 seeds: the count by the expansion factor, never in a chokepoint / at the start / on the route, spaced, one per building', () => {
    const [lo, hi] = PMC_POSTS.count_by_factor[String(expansionFactor(layout))];
    const counts = new Set<number>();
    for (let seed = 1; seed <= 25; seed++) {
      const plan = planPmcPosts(layout, tactical, seed);
      counts.add(plan.posts.length);
      assert.ok(plan.posts.length >= lo && plan.posts.length <= hi, `seed ${seed}: ${plan.posts.length} posts in [${lo}, ${hi}]`);
      const buildings = plan.posts.filter((p) => p.building).map((p) => p.building);
      assert.equal(new Set(buildings).size, buildings.length, `seed ${seed}: one post per building`);
      for (const p of plan.posts) {
        assert.equal(inChokepoint(p.pos, polys, PMC_POSTS.chokepoint_margin_m * 100), false, `seed ${seed} ${p.id} not in a chokepoint`);
        assert.ok(d2(p.pos, start) >= PMC_POSTS.min_from_start_m * 100, `seed ${seed} ${p.id} away from the start`);
        assert.ok(routeDistOnLayer(p.pos, segs, p.layer) >= PMC_POSTS.route_clear_m * 100, `seed ${seed} ${p.id} off the mandatory route`);
        assert.ok(routeAnchor(p.pos, segs).d <= PMC_POSTS.max_from_route_m * 100, `seed ${seed} ${p.id} near the route`);
        assert.ok(p.yaw > -180 && p.yaw <= 180 && Number.isInteger(p.yaw));
        for (const q of plan.posts) if (q !== p) assert.ok(d2(p.pos, q.pos) >= PMC_POSTS.squad_spacing_m * 100, `seed ${seed} ${p.id}/${q.id} spaced`);
      }
    }
    assert.ok(counts.size > 1, 'the count varies with the seed');
  });

  it('deterministic per seed; another seed gives another plan', () => {
    const a = planPmcPosts(layout, tactical, 42), b = planPmcPosts(layout, tactical, 42), c = planPmcPosts(layout, tactical, 43);
    assert.deepEqual(a, b);
    assert.notDeepEqual(a.posts, c.posts);
    assert.equal(a.geometry_hash, tactical.geometry_hash);
  });
});
