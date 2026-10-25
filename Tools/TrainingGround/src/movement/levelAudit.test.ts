// Training Ground / level design: layout audit (node --test).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { configValues } from './config.ts';
import { readConfigFile, readLayout } from './cli/paths.ts';
import { auditLayout } from './levelAudit.ts';

const config = configValues(readConfigFile());
const sample = readLayout('sample_small');

test('the sample layout has no audit errors', () => {
  const issues = auditLayout(sample, config);
  assert.deepEqual(issues.filter((i) => i.severity === 'error'), []);
});

test('audit: route point in a wall, a leg through a wall, a zone never entered, a too-short zone', () => {
  const bad = structuredClone(sample);
  bad.routes[0].points = [[-2500, 0], [-500, -300], [2550, 0]];
  bad.zones.push({ id: 'Z_nowhere', kind: 'open', poly: [[2800, 1500], [2900, 1500], [2900, 1600]] });
  bad.zones.push({ id: 'Z_tiny', kind: 'open', poly: [[-2400, -60], [-2350, -60], [-2350, 40], [-2400, 40]] });
  bad.routes[0].expect.push({ zone: 'Z_tiny', formation: 'diamond' }, { zone: 'Z_nowhere', formation: 'diamond' });
  const issues = auditLayout(bad, config);
  const text = issues.map((i) => `${i.severity} ${i.where}: ${i.message}`).join('\n');
  assert.match(text, /error R1_walk\.points\[1\]: route point inside a wall/);
  assert.match(text, /R1_walk leg \d: the leg crosses a wall/);
  assert.match(text, /error R1_walk expect Z_nowhere: the route never enters/);
  assert.match(text, /warning R1_walk expect Z_tiny: only \d+ cm/);
  assert.match(text, /warning Z_nowhere: no route crosses/);
});

test('audit: schema errors come first and stop the audit', () => {
  const issues = auditLayout({ schema: 'x' }, config);
  assert.ok(issues.length > 0 && issues.every((i) => i.where === 'schema'));
});

test('formation_lab_01 (the big lab): audit clean and every zone gets its expected formation', async () => {
  const lab = readLayout('formation_lab_01');
  assert.deepEqual(auditLayout(lab, config), []);
  const { runSeries } = await import('./bot.ts');
  const { summary } = runSeries({ layout: lab, config, members: [2, 3, 4], seeds: 1, seedBase: 1 });
  const expect = summary.checks.find((c) => c.id === 4)!;
  assert.equal(expect.fail, 0, expect.worst);
  assert.equal(summary.checks.find((c) => c.id === 5)!.fail <= 1, true, 'slots: at most the known pair-at-the-wall spacing blip');
});
