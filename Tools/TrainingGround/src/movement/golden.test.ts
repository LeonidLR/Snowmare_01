// Golden vectors stay in step with the rules and formation_config.json (FORMATION_CONTRACT.md 3): a rule or config change
// without `npm run golden` fails here, before the C++ parity tests see stale files.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { configValues } from './config.ts';
import { GOLDEN_RULES, buildGolden, goldenFileName, goldenText, sameGoldenText } from './golden.ts';
import { CONFIG_REL, GOLDEN_DIR, readConfigFile } from './cli/paths.ts';

test('golden files are up to date (run `npm run golden` after changing the rules or the config)', () => {
  const files = buildGolden(configValues(readConfigFile()), CONFIG_REL);
  for (const rule of GOLDEN_RULES) {
    const file = path.join(GOLDEN_DIR, goldenFileName(rule));
    assert.ok(fs.existsSync(file), `${goldenFileName(rule)} missing`);
    assert.ok(sameGoldenText(fs.readFileSync(file, 'utf8'), goldenText(files[rule])), `${goldenFileName(rule)} is stale`);
  }
});

test('golden cases are rotation / translation invariant for Classify (no borderline scenes)', () => {
  const files = buildGolden(configValues(readConfigFile()), CONFIG_REL);
  const byKey = new Map<string, Set<string>>();
  type In = { scene: string; previousClass: string | null; timeInClass: number; leaderPos: number[]; destination: number[] | null };
  for (const c of files.Classify.cases as { in: In; out: { class: string } }[]) {
    const dest = c.in.destination === null ? 'none' : c.in.destination[0] === c.in.leaderPos[0] && c.in.destination[1] === c.in.leaderPos[1] ? 'here' : 'ahead';
    const key = `${c.in.scene}|${c.in.previousClass}|${c.in.timeInClass}|${dest}`;
    if (!byKey.has(key)) byKey.set(key, new Set());
    byKey.get(key)!.add(c.out.class);
  }
  for (const [key, classes] of byKey) assert.equal(classes.size, 1, `${key}: ${[...classes].join(', ')}`);
});

test('golden comparison ignores CRLF (Windows checkouts)', () => {
  assert.ok(sameGoldenText('{\r\n "a": 1\r\n}\r\n', '{\n "a": 1\n}\n'));
  assert.ok(!sameGoldenText('{\n "a": 2\n}\n', '{\n "a": 1\n}\n'));
});

