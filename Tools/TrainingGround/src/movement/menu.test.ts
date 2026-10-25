// Training Ground / movement: the hold-G radial formation menu rules (node --test; FORMATION_CONTRACT.md 9).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { FormationMenuRules as M, MENU_ITEMS, MENU_START, menuLabel } from './menu.ts';
import type { MenuState } from './menu.ts';
import { configValues } from './config.ts';
import { menuIconSvg } from './menuIcons.ts';
import { ROOT, readConfigFile } from './cli/paths.ts';

const DZ = 40;
const at = (clock: number, r = 100): [number, number] => {
  const a = (clock * 30 * Math.PI) / 180;
  return [Math.sin(a) * r, -Math.cos(a) * r]; // screen: x right, y down, 12 = up
};

test('layout: 12 line, 1:30 diamond, 3 echelon right, 4:30 blizzard, 6 column, 7:30 hedgehog, 9 echelon left, 10:30 all forward', () => {
  assert.deepEqual(MENU_ITEMS.map((i) => [i.clock, i.id]), [[12, 'line'], [1.5, 'diamond'], [3, 'echelon_right'], [4.5, 'blizzard'],
    [6, 'column'], [7.5, 'hedgehog'], [9, 'echelon_left'], [10.5, 'all_forward']]);
  for (const item of MENU_ITEMS) assert.equal(M.ItemAt(...at(item.clock), DZ), item.id);
});

test('ItemAt: the dead zone is Auto; each item owns +-22.5 deg; a border goes clockwise', () => {
  assert.equal(M.ItemAt(0, 0, DZ), null);
  assert.equal(M.ItemAt(...at(3, DZ), DZ), null, 'on the dead-zone circle = still Auto');
  assert.equal(M.ItemAt(...at(3, DZ + 1), DZ), 'echelon_right');
  assert.equal(M.ItemAt(...at(12 + 22.4 / 30), DZ), 'line');
  assert.equal(M.ItemAt(...at(22.6 / 30), DZ), 'diamond');
  assert.equal(M.ItemAt(...at(12 - 22.4 / 30), DZ), 'line');
  assert.equal(M.ItemAt(...at(12 - 22.6 / 30), DZ), 'all_forward');
  assert.equal(M.ItemAt(0, 1, DZ), null);
  assert.equal(M.ItemAt(0, 100, DZ), 'column', 'screen y down = 6 o\'clock');
});

test('hold / release: an item picks it and it holds; the centre picks Auto; Esc / RMB keeps the previous choice', () => {
  let s: MenuState = MENU_START;
  let r = M.Open(s); assert.equal(r.effect, 'opened'); s = r.state;
  s = M.Move(s, ...at(3), DZ);
  assert.equal(s.highlighted, 'echelon_right'); assert.equal(s.choice, 'auto', 'nothing chosen before the release');
  r = M.Release(s, 4); assert.equal(r.effect, 'picked'); s = r.state;
  assert.deepEqual(s, { open: false, choice: 'echelon_right', highlighted: null });
  // the manual choice holds through a cancelled menu
  s = M.Open(s).state; s = M.Move(s, ...at(6), DZ);
  r = M.Cancel(s); assert.equal(r.effect, 'cancelled'); s = r.state;
  assert.equal(s.choice, 'echelon_right');
  // release in the centre = Auto
  s = M.Open(s).state; s = M.Move(s, 3, -2, DZ);
  r = M.Release(s, 4); assert.equal(r.effect, 'auto'); assert.equal(r.state.choice, 'auto');
  // release / cancel / move while closed: nothing
  assert.equal(M.Release(MENU_START, 4).effect, 'none');
  assert.equal(M.Cancel(MENU_START).effect, 'none');
  assert.deepEqual(M.Move(MENU_START, 100, 0, DZ), MENU_START);
});

test('disabled: one operative has no formation - a pick is refused and the previous choice stays; Auto always works', () => {
  for (const i of MENU_ITEMS) { assert.equal(M.IsEnabled(i.id, 1), false); assert.equal(M.IsEnabled(i.id, 2), true); }
  let s = M.Move(M.Open({ ...MENU_START, choice: 'column' }).state, ...at(3), DZ);
  const r = M.Release(s, 1);
  assert.equal(r.effect, 'disabled'); assert.equal(r.state.choice, 'column'); assert.equal(r.state.open, false);
  s = M.Move(M.Open(MENU_START).state, 0, 0, DZ);
  assert.equal(M.Release(s, 1).state.choice, 'auto');
});

test('Resolve: auto -> terrain rules; all forward -> terrain + look forward; diamond = wedge / pair by count; too few -> Auto', () => {
  assert.deepEqual(M.Resolve('auto', 4), { manualFormation: null, allForward: false });
  assert.deepEqual(M.Resolve('all_forward', 3), { manualFormation: null, allForward: true });
  assert.deepEqual(M.Resolve('diamond', 4), { manualFormation: 'diamond', allForward: false });
  assert.deepEqual(M.Resolve('diamond', 3), { manualFormation: 'wedge', allForward: false });
  assert.deepEqual(M.Resolve('column', 2), { manualFormation: 'column', allForward: false });
  assert.deepEqual(M.Resolve('column', 1), { manualFormation: null, allForward: false }, 'members died: back to Auto');
  assert.equal(menuLabel('diamond', 4), 'Ромб'); assert.equal(menuLabel('diamond', 3), 'Клин'); assert.equal(menuLabel('diamond', 2), 'Пара');
});

test('icons: one 64x64 currentColor SVG per item + Auto, in step with the rules (npm run menu:icons)', () => {
  const config = configValues(readConfigFile());
  for (const id of ['auto', ...MENU_ITEMS.map((i) => i.id)] as const) {
    const file = path.join(ROOT, 'Content/UI/FormationMenu/src', `${id}.svg`);
    assert.ok(fs.existsSync(file), `${id}.svg missing`);
    const text = fs.readFileSync(file, 'utf8').replace(/\r\n/g, '\n');
    assert.equal(text, menuIconSvg(id, config), `${id}.svg stale`);
    assert.match(text, /viewBox="0 0 64 64"/);
    assert.ok(!/#[0-9a-fA-F]{3,6}|rgb\(/.test(text), `${id}.svg: single colour (currentColor) only`);
  }
});
