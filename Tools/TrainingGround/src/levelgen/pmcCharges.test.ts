// PMC charges (docs/design/PMC_DEFENCE.md D2, task 087): the mines / tripwires of the defend posts - the supply budget (sapper 2 mines +
// 1 tripwire), approach-line mines, doorway mine, flank tripwire, inside roof-access trap (outside ladder: none), the retreat line free,
// every charge valid for the game's props validator, the locked props layer `pmc_charges`.
import { describe, it } from 'node:test';
import * as assert from 'node:assert/strict';
import { mandatorySegments, PMC_POSTS, planPmcPosts, routeDistOnLayer, segDist, startOf } from './pmcPosts.ts';
import type { PmcPlan, PmcPostsConfig } from './pmcPosts.ts';
import { planPmc, planPmcCharges, pmcChargesLayer, retreatLine, upstreamPoint, withPmcCharges } from './pmcCharges.ts';
import { loadX4, tinyLayout, tinyTactical } from './pmcTestFixtures.ts';
import { PropsLayoutView } from './props/layoutView.ts';
import { validateStatic } from './props/validator.ts';
import { generatePropsForLayout, loadPropsBalance } from './props/autogen.ts';
import { rerollAll } from './props/placement.ts';
import { PROPS_SCHEMA, validateProps, type LevelPropsFile } from './props/schema.ts';

const d2 = (a: readonly number[], b: readonly number[]) => Math.hypot(a[0] - b[0], a[1] - b[1]);
const C = PMC_POSTS.charges;
const defendOnly = (cfg: PmcPostsConfig = PMC_POSTS): PmcPostsConfig => ({ ...cfg, max_from_route_m: 40, squad_spacing_m: 1, posture_weights: { defend: 1, hunt: 0 } });
/** The tiny map's plan with only the post on HS_ok2 ([2500, -1500], anchor [2500, 0]). */
const tinyPlanOk2 = (cfg = defendOnly()): PmcPlan => {
  const plan = planPmcPosts(tinyLayout(), tinyTactical(), 5, { config: cfg, count: 10 });
  return { ...plan, posts: plan.posts.filter((p) => p.source === 'HS_ok2') };
};

describe('PMC charges - the rules on a hand map (task 087)', () => {
  it('a ground post: 2 mines on the approach line (5 m, then +4 m) and the flank tripwire 5 m to the side', () => {
    const plan = planPmcCharges(tinyLayout(), tinyPlanOk2());
    const post = plan.posts[0];
    assert.equal(post.posture, 'defend');
    assert.deepEqual(post.anchor, [2500, 0, 0]);
    const mines = plan.charges.filter((c) => c.type === 'mine'), wires = plan.charges.filter((c) => c.type === 'tripwire');
    assert.deepEqual(mines.map((m) => [m.role, m.pos]), [['approach', [2500, -1000]], ['approach', [2500, -600]]]);
    assert.deepEqual(wires.map((w) => [w.role, w.pos, w.yaw, w.width_cm]), [['flank', [3000, -1500], 0, C.tripwire_width_cm]]);
    for (const ch of plan.charges) {
      assert.equal(ch.post, post.id);
      assert.equal(ch.walk_layer, 'ground');
      assert.ok(ch.id.startsWith(`${C.layer_id}_${post.id}_`));
    }
    assert.equal(mines[0].variant, C.mine.variant);
    assert.equal(wires[0].variant, C.tripwire.variant);
    assert.equal(wires[0].trigger, C.tripwire.trigger);
  });

  it('the budget is the sappers x per-sapper supply; a hunt post gets nothing', () => {
    const two = { ...defendOnly(), charges: { ...C, sappers_per_squad: 2, mine_spacing_m: 2, first_mine_m: 2 } };
    const plan2 = planPmcCharges(tinyLayout(), tinyPlanOk2(two), { config: two });
    assert.ok(plan2.charges.filter((c) => c.type === 'mine').length <= 4);
    assert.ok(plan2.charges.filter((c) => c.type === 'tripwire').length <= 2);
    const hunt = tinyPlanOk2();
    hunt.posts = hunt.posts.map((p) => ({ ...p, posture: 'hunt' as const }));
    assert.equal(planPmcCharges(tinyLayout(), hunt).charges.length, 0);
  });

  it('the retreat line stays free: with retreat_clear 8 m nothing lands within 8 m of it (the flank is refused)', () => {
    const cfg = { ...defendOnly(), charges: { ...C, retreat_clear_m: 8 } };
    const plan = planPmcCharges(tinyLayout(), tinyPlanOk2(cfg), { config: cfg });
    const [a, b] = retreatLine(plan.posts[0], cfg.charges.retreat_m * 100);
    assert.deepEqual(a, [2500, -1500]);
    assert.deepEqual(b, [2500, -2700], 'straight away from the anchor');
    assert.ok(plan.charges.length > 0);
    for (const ch of plan.charges) assert.ok(segDist(ch.pos, a, b).d >= 800, `${ch.id} off the retreat line`);
    assert.equal(plan.charges.filter((c) => c.type === 'tripwire').length, 0);
    const rep = (plan.report.charges as { posts: { unused_tripwires: number; notes: string[] }[] }).posts[0];
    assert.equal(rep.unused_tripwires, 1);
    assert.ok(rep.notes.some((n) => n.includes('retreat')));
  });

  it('upstreamPoint walks back along the mandatory route towards the start', () => {
    const segs = mandatorySegments(tinyLayout());
    assert.deepEqual(upstreamPoint(segs, [2500, -1500], 2000), [500, 0]);
    assert.deepEqual(upstreamPoint(segs, [-4400, 300], 2000), [-4500, 0], 'clamped at the route start');
  });
});

describe('PMC charges on the accepted X4 layout (task 087)', () => {
  const { layout, tactical } = loadX4();
  const balance = loadPropsBalance();
  const view = PropsLayoutView.build(layout as unknown as Record<string, unknown>, PropsLayoutView.readConfig(balance));
  const segs = mandatorySegments(layout), start = startOf(layout);
  const plans = Array.from({ length: 25 }, (_, i) => planPmc(layout, tactical, i + 1, { balance, view }));

  it('25 seeds: budget per defend post, none for hunt posts, every rule held, every charge valid for the game', () => {
    for (const plan of plans) {
      const byPost = new Map(plan.posts.map((p) => [p.id, p]));
      for (const p of plan.posts) {
        const mine = plan.charges.filter((c) => c.post === p.id && c.type === 'mine').length;
        const wire = plan.charges.filter((c) => c.post === p.id && c.type === 'tripwire').length;
        if (p.posture === 'hunt') assert.equal(mine + wire, 0, `seed ${plan.seed} ${p.id}: a hunt post has no charges`);
        assert.ok(mine <= C.sappers_per_squad * C.per_sapper.mines && wire <= C.sappers_per_squad * C.per_sapper.tripwires, `seed ${plan.seed} ${p.id} budget`);
      }
      for (const ch of plan.charges) {
        const post = byPost.get(ch.post)!;
        assert.ok(post && post.posture === 'defend', `${ch.id} tagged with a defend post`);
        const [a, b] = retreatLine(post, C.retreat_m * 100);
        assert.ok(segDist(ch.pos, a, b).d >= C.retreat_clear_m * 100, `seed ${plan.seed} ${ch.id} off ${post.id}'s retreat line`);
        assert.ok(routeDistOnLayer(ch.pos, segs, ch.walk_layer) >= C.route_clear_m * 100, `seed ${plan.seed} ${ch.id} off the mandatory route`);
        assert.ok(d2(ch.pos, start) >= C.start_clear_m * 100, `seed ${plan.seed} ${ch.id} away from the start`);
        for (const o of plan.charges) if (o !== ch) assert.ok(d2(o.pos, ch.pos) >= C.mine_spacing_m * 100, `seed ${plan.seed} ${ch.id}/${o.id} spaced`);
        if (ch.role === 'roof_access') assert.equal(ch.walk_layer, post.layer, 'the roof trap is on the roof');
      }
      const layer = pmcChargesLayer(plan);
      const v = validateStatic(view, layer.objects.map((object) => ({ layerId: layer.id, object, bHand: false, bPinned: true })), [], balance);
      assert.deepEqual(v.issues, [], `seed ${plan.seed}: the game drops none`);
    }
  });

  it('every charge role appears; an outside-access roof gets no access trap', () => {
    const roles = new Set(plans.flatMap((p) => p.charges.map((c) => c.role)));
    for (const r of ['approach', 'doorway', 'flank', 'roof_access']) assert.ok(roles.has(r as never), r);
    const total = plans.reduce((s, p) => s + p.charges.length, 0);
    const defend = plans.reduce((s, p) => s + p.posts.filter((q) => q.posture === 'defend').length, 0);
    assert.ok(total >= defend * 2, `on average >= 2 of the 3 charges per defend post (${total} / ${defend})`);
    for (const plan of plans) {
      for (const p of plan.posts.filter((q) => q.kind === 'overwatch_roof')) {
        const b = layout.buildings.find((x) => x.id === p.building)!;
        const inside = (b.stairs ?? []).some((s) => s.to >= b.floors && s.kind !== 'external') || (b.hatches ?? []).length > 0;
        if (!inside) assert.equal(plan.charges.filter((c) => c.post === p.id && c.role === 'roof_access').length, 0, `${p.id} outside access`);
      }
    }
  });

  it('deterministic per seed', () => {
    assert.deepEqual(planPmc(layout, tactical, 3, { balance, view }), plans[2]);
  });

  it('the props layer: locked mines layer, owner pmc / hostile / post id; replaced in place; rerollAll leaves it alone', () => {
    const plan = plans.find((p) => p.charges.length > 2)!;
    const layer = pmcChargesLayer(plan);
    assert.equal(layer.kind, 'mines');
    assert.equal(layer.locked, true);
    for (const o of layer.objects) {
      assert.equal(o.owner, 'pmc');
      assert.equal(o.faction, 'hostile');
      assert.ok(plan.posts.some((p) => p.id === o.post));
    }
    const base: LevelPropsFile = { schema: PROPS_SCHEMA, map: 'L_Gen_Depot_X4', layout: layout.name, layout_hash: '0', revision: 1, frozen: false, layers: [], overrides: [] };
    const once = withPmcCharges(base, plan);
    const twice = withPmcCharges(once, plans[0]);
    assert.equal(twice.layers.length, 1, 'replaced, not added twice');
    assert.deepEqual(validateProps(once), []);
    const before = JSON.stringify(once.layers[0].objects);
    rerollAll(once, balance, view, layout);
    assert.equal(JSON.stringify(once.layers[0].objects), before, 'a locked layer is never re-rolled');
  });

});

describe('PMC charges in the props autogen (task 087)', () => {
  // the tiny map (a full X4 autogen takes minutes; the hook is the same for any layout)
  it('autogen writes the layout pmc block as the locked pmc_charges layer and the validator drops none of it', () => {
    const balance = loadPropsBalance();
    const plan = planPmcCharges(tinyLayout(), tinyPlanOk2());
    assert.equal(plan.charges.length, 3);
    const res = generatePropsForLayout({ layout: { ...tinyLayout(), pmc: plan }, balance, mapName: 'L_Gen_Tiny' });
    const layer = res.props.layers.find((l) => l.id === C.layer_id)!;
    assert.ok(layer, 'the layer is there');
    assert.equal(layer.locked, true);
    assert.deepEqual(layer.objects.map((o) => o.id), plan.charges.map((c) => c.id));
    assert.equal(res.droppedByValidator.filter((i) => i.layerId === C.layer_id).length, 0);
    assert.deepEqual(validateProps(res.props), []);
    const plain = generatePropsForLayout({ layout: tinyLayout(), balance, mapName: 'L_Gen_Tiny' });
    assert.equal(plain.props.layers.some((l) => l.id === C.layer_id), false, 'no pmc block -> no layer');
  });
});
