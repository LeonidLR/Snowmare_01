// Training Ground / level generator v3 - BAND PLAN tests (S2_ROOMS_PLAN.md S2.1, bandPlan.ts): a property test over footprints
// (800..1800 x 900..1400 in 100 steps, both orders through snapFootprint), core positions (none, at 0, at the far end, in the middle,
// both depths) and seeds - every plan validates and is deterministic - the fallback share per footprint (printed), and ASCII
// goldens of three fixed inputs. `npm test`. Pure.
import assert from 'node:assert/strict';
import { test } from 'node:test';
import type { BandPlan, BandPlanInput } from './bandPlan.ts';
import { BUILDING_PLANS, planBands, planToAscii, snapFootprint, validatePlanTiling } from './bandPlan.ts';

type Core = BandPlanInput['core'];
const COREBAY = BUILDING_PLANS.constants.core.bay_length_cm;
const MIN_ROOM = BUILDING_PLANS.constants.room_cm.min;
/** A stretch beside the core must be 0 or a room long. */
const stretchOk = (x: number): boolean => x === 0 || x >= MIN_ROOM;

/** The footprints of the property test, once each (the two orders snap to the same L x D). */
function footprints(): { L: number; D: number }[] {
  const seen = new Map<string, { L: number; D: number }>();
  for (let w = 800; w <= 1800; w += 100) {
    for (let d = 900; d <= 1400; d += 100) {
      const a = snapFootprint(w, d), b = snapFootprint(d, w);
      assert.deepEqual([a.L, a.D], [b.L, b.D]);
      seen.set(`${a.L}x${a.D}`, { L: a.L, D: a.D });
    }
  }
  return [...seen.values()];
}

/**
 * The core placements that fit a long side (both stretches 0 or >= 400): none, at 0, at the far end, and the valid u0 nearest the middle;
 * both depths. A placement leaving 100-300 beside the core is infeasible by the grammar and is covered by the Fallbacks test instead.
 */
function cores(L: number): { label: string; core: Core }[] {
  const out: { label: string; core: Core }[] = [{ label: 'none', core: null }];
  const valid: number[] = [];
  for (let u0 = 0; u0 + COREBAY <= L; u0 += 100) if (stretchOk(u0) && stretchOk(L - u0 - COREBAY)) valid.push(u0);
  if (!valid.length) return out;
  const middle = (L - COREBAY) / 2;
  const mid = valid.reduce((best, u0) => (Math.abs(u0 - middle) < Math.abs(best - middle) ? u0 : best), valid[0]);
  for (const depth of [350, 200] as const) {
    const seenU0 = new Set<number>();
    for (const [name, u0] of [['start', 0], ['end', L - COREBAY], ['mid', mid]] as const) {
      if (seenU0.has(u0) || !valid.includes(u0)) continue;
      seenU0.add(u0);
      out.push({ label: `${depth}@${name}`, core: { u0, depth } });
    }
  }
  return out;
}

/** Seeds per footprint and core placement: 1..200 (the whole run takes about a second). */
const SEEDS = 200;

test('CodexTactics.LevelGen.BandPlan.Property: every plan validates and is deterministic', () => {
  const rows: string[] = [];
  let plans = 0, bands = 0, fallbacks = 0;
  for (const { L, D } of footprints()) {
    const share: Record<string, [number, number]> = { none: [0, 0], core: [0, 0] };
    for (const { label, core } of cores(L)) {
      const key = core ? 'core' : 'none';
      for (let seed = 1; seed <= SEEDS; ++seed) {
        const input: BandPlanInput = { L, D, archetype: 'office', core, seed };
        const plan = planBands(input);
        ++plans;
        assert.deepEqual(planBands(input), plan, `determinism ${L}x${D} ${label} seed ${seed}`);
        assert.deepEqual(validatePlanTiling(plan), [], `${L}x${D} ${label} seed ${seed}\n${planToAscii(plan)}`);
        share[key][1]++;
        if (plan.kind === 'bands') {
          ++bands;
          assert.ok(plan.cells.length > 0 && plan.cells.every((c, i) => c.id === `c${i}`), 'ids c0, c1 ... in order');
        } else { ++fallbacks; share[key][0]++; }
      }
    }
    const pct = (k: string): string => (share[k][1] ? `${Math.round((100 * share[k][0]) / share[k][1])}%` : 'n/a');
    rows.push(`${String(L).padStart(4)} x ${String(D).padStart(4)}  fallback share: no core ${pct('none').padStart(4)}, with core ${pct('core').padStart(4)} (n/a = no core fits)`);
  }
  console.log(`band plan fallback share per footprint (L x D, cm):\n${rows.join('\n')}\n${plans} plans, ${bands} bands, ${fallbacks} fallback`);
  assert.ok(bands > 0);
});

test('CodexTactics.LevelGen.BandPlan.NoFallback: D >= 1000, L >= 1300 with a core never falls back', () => {
  for (const { L, D } of footprints()) {
    if (D < 1000 || L < 1300) continue;
    for (const { label, core } of cores(L)) {
      if (!core) continue;
      for (let seed = 1; seed <= 30; ++seed) {
        const plan = planBands({ L, D, archetype: 'office', core, seed });
        assert.equal(plan.kind, 'bands', `${L}x${D} ${label} seed ${seed}: ${plan.fallback}`);
      }
    }
  }
});

test('CodexTactics.LevelGen.BandPlan.Variety: different seeds give different plans and both recipes appear', () => {
  const sigs = new Set<string>(), variants = new Set<string | undefined>();
  for (let seed = 1; seed <= 40; ++seed) {
    const p = planBands({ L: 1800, D: 1200, archetype: 'office', core: { u0: 0, depth: 350 }, seed });
    sigs.add(JSON.stringify(p.cells));
    variants.add(p.variant);
  }
  assert.ok(sigs.size >= 10, `only ${sigs.size} distinct plans`);
  for (let seed = 1; seed <= 10; ++seed) variants.add(planBands({ L: 1200, D: 900, archetype: 'office', core: null, seed }).variant);
  assert.ok(variants.has('double') && variants.has('single'));
});

test('CodexTactics.LevelGen.BandPlan.Fallbacks: a footprint under the minimum, an off-grid or unfitting core', () => {
  const base = { archetype: 'office' as const, core: null, seed: 1 };
  assert.match(planBands({ ...base, L: 800, D: 800 }).fallback ?? '', /footprint_too_small/); // L < 900
  assert.match(planBands({ ...base, L: 1200, D: 700 }).fallback ?? '', /footprint_too_small/); // D < 800
  assert.match(planBands({ ...base, L: 1250, D: 900 }).fallback ?? '', /off_grid/);
  // S2.2b: a 100-300 stretch beside the core is absorbed into the core bay (1000-1200 long) instead of falling back
  const absorbed = planBands({ ...base, L: 1200, D: 900, core: { u0: 100, depth: 200 } });
  assert.equal(absorbed.kind, 'bands', absorbed.fallback);
  assert.deepEqual(absorbed.bays.filter((x) => x.kind === 'core').map((x) => [x.u0, x.u1]), [[0, 1200]], 'both short stretches (100, 200) join the core bay');
  assert.match(planBands({ ...base, L: 1200, D: 900, core: { u0: 400, depth: 200 } }).fallback ?? '', /core_does_not_fit/);
  assert.match(planBands({ ...base, L: 1200, D: 900, core: { u0: 50, depth: 200 } }).fallback ?? '', /core_does_not_fit/);
  // A two-lane core needs 350 + 200 + 400 = 950 across: D = 900 falls back, a one-lane core (200 + 200 + 400) plans.
  const two = planBands({ ...base, L: 1200, D: 900, core: { u0: 0, depth: 350 } });
  assert.equal(two.kind, 'fallback');
  assert.equal(two.cells.length, 0);
  assert.equal(planBands({ ...base, L: 1300, D: 900, core: { u0: 0, depth: 200 } }).kind, 'bands');
  assert.equal(planBands({ ...base, L: 1300, D: 900, core: { u0: 0, depth: 350 } }).kind, 'fallback');
  const whole = planBands({ ...base, L: 1200, D: 900, core: { u0: 0, depth: 200 } });
  assert.deepEqual(whole.bays.map((x) => [x.kind, x.u0, x.u1]), [['core', 0, 1200]], 'the 300 stretch joins the core bay');
});

test('CodexTactics.LevelGen.BandPlan.SnapFootprint: floors to 100 and puts the long side first', () => {
  assert.deepEqual(snapFootprint(1189, 801), { L: 1100, D: 800, swapped: false });
  assert.deepEqual(snapFootprint(850, 1650), { L: 1600, D: 800, swapped: true });
  assert.deepEqual(snapFootprint(1000, 1099), { L: 1000, D: 1000, swapped: false });
});

test('CodexTactics.LevelGen.BandPlan.Validator: reports an overlap, a gap, an off-grid edge and a small room', () => {
  const plan = planBands({ L: 1200, D: 1000, archetype: 'office', core: null, seed: 5 });
  assert.equal(plan.kind, 'bands');
  const clone = (): BandPlan => JSON.parse(JSON.stringify(plan)) as BandPlan;
  const room = (p: BandPlan) => p.cells.find((c) => c.kind === 'room')!;
  const grown = clone(); room(grown).u1 += 100;
  assert.ok(validatePlanTiling(grown).some((m) => m.includes('overlaps')));
  const gap = clone(); gap.cells.splice(gap.cells.findIndex((c) => c.kind === 'room'), 1);
  assert.ok(validatePlanTiling(gap).some((m) => m.includes('gap')));
  const off = clone(); room(off).v1 += 30;
  assert.ok(validatePlanTiling(off).some((m) => m.includes('grid')));
  const tiny = clone(); room(tiny).u1 = room(tiny).u0 + 300;
  assert.ok(validatePlanTiling(tiny).some((m) => m.includes('outside 400-800')));
});

const golden = (lines: string[]): string => lines.join('\n');

test('CodexTactics.LevelGen.BandPlan.Ascii: 1800 x 1200, core at 0 (two lanes), seed 1', () => {
  const plan = planBands({ L: 1800, D: 1200, archetype: 'office', core: { u0: 0, depth: 350 }, seed: 1 });
  assert.equal(planToAscii(plan), golden([
    '|RRR|RRRR|RRRR|RRR',
    '|RRR|RRRR|RRRR|RRR',
    '|RRR|RRRR|RRRR|RRR',
    '|RRR|RRRR|RRRR|RRR',
    '+---+----+----+---',
    '|CCCCCCCC|CCCCCCCC',
    '|CCCCCCCC+--------',
    '+--------|RRRR|RRR',
    '|KKKKKKKK|RRRR|RRR',
    '|KKKKKKKK|RRRR|RRR',
    '|KKKKKKKK|RRRR|RRR',
    '+--------+----+---',
  ]));
});

test('CodexTactics.LevelGen.BandPlan.Ascii: 1700 x 1000, core in the middle (two lanes), seed 1', () => {
  const plan = planBands({ L: 1700, D: 1000, archetype: 'office', core: { u0: 400, depth: 350 }, seed: 1 });
  assert.equal(planToAscii(plan), golden([
    '|EEE|RRRR|RRR|RRR',
    '|EEE|RRRR|RRR|RRR',
    '|EEE|RRRR|RRR|RRR',
    '|EEE+----+---+---',
    '|EEE|CCCCCCCC|CCC',
    '|EEE+--------+---',
    '|EEE|KKKKKKKK|RRR',
    '|EEE|KKKKKKKK|RRR',
    '|EEE|KKKKKKKK|RRR',
    '+---+--------+---',
  ]));
});

test('CodexTactics.LevelGen.BandPlan.Ascii: 1200 x 900, single-loaded, no core, seed 1', () => {
  const plan = planBands({ L: 1200, D: 900, archetype: 'office', core: null, seed: 1 });
  assert.equal(plan.variant, 'single');
  assert.equal(planToAscii(plan), golden([
    '|EEEE|RRRRRR',
    '|EEEE|RRRRRR',
    '|EEEE|RRRRRR',
    '|EEEE+------',
    '|EEEE|CCCCCC',
    '|EEEE+------',
    '|EEEE|SS|SSS',
    '|EEEE|SS|SSS',
    '+----+--+---',
  ]));
});
