// T-02 gap 4: a route link via a MANTLE EDGE id (schema.ts GenRoute.links, mantleLegIssue; evaluate.ts checkRoute / toFormationLayout):
// the leg crosses an obstacle's edge instead of walking round it. The lab map L_Lab_Bridge's V3_mantle goes over W_obstacle.
import assert from 'node:assert/strict';
import { test } from 'node:test';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { configValues } from '../movement/config.ts';
import { evaluateLayout } from './evaluate.ts';
import { ROUTE_COSTS } from './buildingGraph.ts';
import { buildLabMap } from './labMaps.ts';
import { findMantleEdge, mantleLegIssue, toFormationLayout, validateGenLayout } from './schema.ts';
import type { GenLayout, GenRoute } from './schema.ts';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../..');
const config = configValues(JSON.parse(fs.readFileSync(path.join(ROOT, 'Content/Data/FormationLab/formation_config.json'), 'utf8')));

const bridge = () => buildLabMap('bridge');
const clone = <T>(x: T): T => JSON.parse(JSON.stringify(x)) as T;
const v3 = (l: GenLayout): GenRoute => l.routes.find((r) => r.id === 'V3_mantle')!;
const mantleErrors = (l: GenLayout) => validateGenLayout(l).filter((e) => e.includes('V3_mantle'));

test('mantle link: the bridge lab ships V3_mantle, valid, with one mantle link over W_obstacle', () => {
  const { layout } = bridge();
  assert.deepEqual(validateGenLayout(layout), []);
  const r = v3(layout);
  assert.ok(r, 'V3_mantle exists');
  assert.deepEqual(r.links, [{ at: 4, via: 'me_W_obstacle_s' }]);
  assert.equal(r.mandatory, undefined, 'a variant, not the mandatory route');
  assert.ok(findMantleEdge(layout, 'me_W_obstacle_s'));
  assert.equal(layout.routes.find((x) => x.mandatory)!.links?.length, 0, 'R_main has no link');
});

test('mantle link validator: unknown edge id / a leg that misses the edge / an edge too high / a layer change are rejected', () => {
  const { layout } = bridge();

  const unknown = clone(layout);
  v3(unknown).links = [{ at: 4, via: 'me_W_obstacle_x' }];
  assert.ok(mantleErrors(unknown).some((e) => e.includes('unknown link "me_W_obstacle_x"')), mantleErrors(unknown).join('; '));

  // the same edge id on a leg that does not cross it: the walk along the bridge approach (points 5 -> 6)
  const misses = clone(layout);
  v3(misses).links = [{ at: 6, via: 'me_W_obstacle_s' }];
  assert.ok(mantleErrors(misses).some((e) => e.includes('does not cross mantle edge me_W_obstacle_s')), mantleErrors(misses).join('; '));

  const tall = clone(layout);
  tall.mantle_edges!.find((m) => m.id === 'me_W_obstacle_s')!.height_cm = 200;
  assert.ok(mantleErrors(tall).some((e) => e.includes('outside the mantle band')), mantleErrors(tall).join('; '));

  const layerChange = clone(layout);
  v3(layerChange).layers![4] = 'cut';
  assert.ok(mantleErrors(layerChange).some((e) => e.includes('stays on one layer')), mantleErrors(layerChange).join('; '));

  // the pure helper agrees: crossing legs pass, a parallel leg fails
  const edge = findMantleEdge(layout, 'me_W_obstacle_s')!;
  const mid: [number, number] = [(edge.start[0] + edge.end[0]) / 2, (edge.start[1] + edge.end[1]) / 2];
  const off = (k: number): [number, number] => [mid[0] + edge.normal[0] * k, mid[1] + edge.normal[1] * k];
  assert.equal(mantleLegIssue(off(100), off(-100), 'ground', 'ground', edge), null);
  assert.ok(mantleLegIssue(off(100), [off(100)[0] + 50, off(100)[1] + 50], 'ground', 'ground', edge), 'a leg on one side of the edge');
});

test('mantle link evaluation: V3_mantle is completable through the wall, one mantle leg, time includes the vault cost', () => {
  const lab = bridge();
  const ev = evaluateLayout({ layout: lab.layout, profile: lab.profile, config, noSim: true });
  const v = ev.variants.find((x) => x.id === 'V3_mantle')!;
  assert.equal(v.completable, true, v.fail);
  assert.equal(v.mantles, 1);
  const leg = v.legs.find((x) => x.mantle)!;
  assert.equal(leg.index, 4);
  assert.equal(leg.link, 'me_W_obstacle_s');
  assert.equal(leg.detour_cm, 0, 'the leg goes straight over, no detour');
  const pts = v3(lab.layout).points;
  const walk = pts.slice(1).reduce((n, p, i) => n + Math.hypot(p[0] - pts[i][0], p[1] - pts[i][1]), 0) / 100 / ROUTE_COSTS.v_ref;
  assert.ok(Math.abs(v.time_s! - (walk + ROUTE_COSTS.actions.vault.t)) < 0.06, `time ${v.time_s} = walk ${walk.toFixed(2)} + vault ${ROUTE_COSTS.actions.vault.t}`);
  assert.ok(ROUTE_COSTS.actions.vault.t > 0);
  // routes without a mantle carry neither field (existing reports unchanged)
  assert.equal(ev.route.mantles, undefined);
  assert.equal(ev.route.time_s, undefined);

  // without the link the same leg is walled in (the straight leg runs through W_obstacle): the walls-only check refuses it
  const nolink = clone(lab.layout);
  v3(nolink).links = [];
  const bad = evaluateLayout({ layout: nolink, profile: lab.profile, config, noSim: true }).variants.find((x) => x.id === 'V3_mantle')!;
  assert.equal(bad.mantles, undefined);
  assert.ok(bad.legs.every((x) => !x.mantle), 'no link: no mantle leg');
  assert.ok(bad.legs[3].ok === false || (bad.legs[3].detour_cm ?? 0) > 0, 'no link: the leg walks round the obstacle (a detour) or fails - the link is what makes it straight');

  // an unknown link id on a same-layer leg fails the evaluation (not silently ignored)
  const unknown = clone(lab.layout);
  v3(unknown).links = [{ at: 4, via: 'me_nope' }];
  const un = evaluateLayout({ layout: unknown, profile: lab.profile, config, noSim: true }).variants.find((x) => x.id === 'V3_mantle')!;
  assert.equal(un.completable, false);
  assert.match(un.fail!, /missing link me_nope/);
});

test('mantle link on the MANDATORY route: the simulator walk is split at the crossing and its time is added', () => {
  const lab = bridge();
  const l = clone(lab.layout);
  // V3 becomes the mandatory route (R_main the variant)
  for (const r of l.routes) r.mandatory = r.id === 'V3_mantle' ? true : undefined;
  assert.deepEqual(validateGenLayout(l), []);
  const legs = toFormationLayout(l, 'ground').routes.filter((r) => r.id.startsWith('V3_mantle'));
  assert.deepEqual(legs.map((r) => r.id), ['V3_mantle_L0', 'V3_mantle_L1'], 'cut at the mantle leg');
  assert.equal(legs[0].points.length, 4);
  assert.equal(legs[1].points.length, 6);

  const ev = evaluateLayout({ layout: l, profile: lab.profile, config });
  assert.equal(ev.route.id, 'V3_mantle');
  assert.equal(ev.route.completable, true, ev.route.fail);
  assert.equal(ev.mechanics.mantleUses, 1);
  assert.ok(ev.mechanics.simSeconds > ROUTE_COSTS.actions.vault.t, 'the walk + the vault');

  // the generator's own layouts have no mantle link: their ground route stays ONE simulator route (id unchanged)
  const plain = toFormationLayout(lab.layout, 'ground').routes.filter((r) => r.id === 'R_main');
  assert.equal(plain.length, 1);
});
