// Opening states (opening-states.md 1-3, ADR-0002 decision 1): /1 legacy defaults vs the /2 default table, explicit state wins.
import assert from 'node:assert/strict';
import { test } from 'node:test';
import { classifyOpening, effectiveOpeningState } from './openingStates.ts';
import type { GenBuilding, GenLayout, SetPiece, WallOpening } from './schema.ts';
import { isLevelgenSchema, LEVELGEN_SCHEMA, LEVELGEN_SCHEMA_V2, validateGenLayout } from './schema.ts';

const extDoor: WallOpening = { id: 'B_door_s', kind: 'door', pos: [500, 1000], floor: 0, wall: 'B_S' };
const partDoor: WallOpening = { id: 'B_partdoor_f0_0', kind: 'door', pos: [500, 400], floor: 0 };
const upperPartDoor: WallOpening = { id: 'B_partdoor_f1_0', kind: 'door', pos: [500, 400], floor: 1 };
const groundWin: WallOpening = { id: 'B_win_s_f0_1', kind: 'window', pos: [200, 1000], floor: 0, wall: 'B_S' };
const upperWin: WallOpening = { id: 'B_win_s_f1_1', kind: 'window', pos: [200, 1000], floor: 1, wall: 'B_S' };
const loophole: WallOpening = { id: 'B_lp_s_f0_1', kind: 'loophole', pos: [800, 1000], floor: 0, wall: 'B_S' };
/** A door cut into a layout wall of kind 'interior' (not listed in a partition). */
const wallDoor: WallOpening = { id: 'B_iw_door', kind: 'door', pos: [300, 400], floor: 0, wall: 'B_iw' };

function building(set_piece: SetPiece = 'facade'): GenBuilding {
  return {
    id: 'B', footprint: [[0, 0], [1000, 1000]], height: 600, floors: 2, enterable: true, roof_walkable: false, set_piece,
    doors: [{ pos: extDoor.pos, floor: 0, wall: 'B_S' }], openings: [extDoor, groundWin, upperWin, loophole, wallDoor],
    partitions: [
      { id: 'B_part_f0_0', start: [0, 400], end: [1000, 400], floor: 0, openings: [partDoor] },
      { id: 'B_part_f1_0', start: [0, 400], end: [1000, 400], floor: 1, openings: [upperPartDoor] },
    ],
    stairs: [], roof_holes: [], floor_holes: [],
  };
}
const layout = (schema: string): Pick<GenLayout, 'schema' | 'walls'> => ({
  schema, walls: [{ id: 'B_iw', a: [300, 0], b: [300, 400], height: 300, thickness: 20, kind: 'interior', building: 'B' }],
});
const V1 = layout(LEVELGEN_SCHEMA), V2 = layout(LEVELGEN_SCHEMA_V2);

test('classifyOpening: facade = exterior, partition / interior-wall openings = interior; floor 0 = ground', () => {
  const b = building();
  assert.deepEqual(classifyOpening(V2, b, extDoor), { exterior: true, ground: true });
  assert.deepEqual(classifyOpening(V2, b, partDoor), { exterior: false, ground: true });
  assert.deepEqual(classifyOpening(V2, b, { ...partDoor }), { exterior: false, ground: true }, 'matched by id, not only identity');
  assert.deepEqual(classifyOpening(V2, b, upperPartDoor), { exterior: false, ground: false });
  assert.deepEqual(classifyOpening(V2, b, wallDoor), { exterior: false, ground: true }, 'wall of kind interior');
  assert.deepEqual(classifyOpening(V2, b, upperWin), { exterior: true, ground: false });
});

test('/1 legacy defaults: every door open, every window broken, loopholes fixed', () => {
  for (const sp of ['facade', 'ruined_two_level', 'industrial_hall', 'warehouse'] as SetPiece[]) {
    const b = building(sp);
    for (const d of [extDoor, partDoor, upperPartDoor, wallDoor]) assert.equal(effectiveOpeningState(V1, b, d), 'open', `${sp} ${d.id}`);
    for (const w of [groundWin, upperWin]) assert.equal(effectiveOpeningState(V1, b, w), 'broken', `${sp} ${w.id}`);
    assert.equal(effectiveOpeningState(V1, b, loophole), 'fixed', `${sp} loophole`);
  }
  assert.equal(effectiveOpeningState({ schema: 'something/else' }, building(), groundWin), 'broken', 'unknown schema = /1 meaning');
});

test('/2 default table (opening-states.md 3): one assertion per row', () => {
  const b = building();
  assert.equal(effectiveOpeningState(V2, b, extDoor), 'closed', 'exterior door');
  assert.equal(effectiveOpeningState(V2, b, partDoor), 'open', 'interior door, ground floor');
  assert.equal(effectiveOpeningState(V2, b, upperPartDoor), 'open', 'interior door, upper storey');
  assert.equal(effectiveOpeningState(V2, b, wallDoor), 'open', 'door in an interior wall');
  assert.equal(effectiveOpeningState(V2, b, groundWin), 'glazed', 'ground-floor window');
  assert.equal(effectiveOpeningState(V2, b, upperWin), 'glazed', 'upper-storey window');
  assert.equal(effectiveOpeningState(V2, b, loophole), 'fixed', 'loophole');
  // ruins: doors open, windows broken (loopholes stay fixed)
  const ruin = building('ruined_two_level');
  assert.equal(effectiveOpeningState(V2, ruin, extDoor), 'open', 'ruin exterior door');
  assert.equal(effectiveOpeningState(V2, ruin, partDoor), 'open', 'ruin interior door');
  assert.equal(effectiveOpeningState(V2, ruin, groundWin), 'broken', 'ruin ground window');
  assert.equal(effectiveOpeningState(V2, ruin, upperWin), 'broken', 'ruin upper window');
  assert.equal(effectiveOpeningState(V2, ruin, loophole), 'fixed', 'ruin loophole');
  // hall / warehouse gates: closed (their interior doors stay open)
  for (const sp of ['industrial_hall', 'warehouse'] as SetPiece[]) {
    assert.equal(effectiveOpeningState(V2, building(sp), extDoor), 'closed', `${sp} gate`);
    assert.equal(effectiveOpeningState(V2, building(sp), partDoor), 'open', `${sp} interior door`);
  }
});

test('/2 role default (S2.2): a portal door is open wherever it sits, other roles keep the table', () => {
  const portal: WallOpening = { id: 'B_f0_d3', kind: 'door', pos: [500, 400], floor: 0, width: 200, role: 'portal', module: 'portal_200' };
  const b = building();
  assert.equal(effectiveOpeningState(V2, b, portal), 'open', 'a portal off every partition would classify as exterior (closed) without the role');
  assert.equal(effectiveOpeningState(V2, b, { ...portal, role: 'room' }), 'closed', 'the same door with another role: the table');
  assert.equal(effectiveOpeningState(V2, building('ruined_two_level'), portal), 'open');
  assert.equal(effectiveOpeningState(V2, b, { ...portal, state: 'sealed' }), 'sealed', 'an explicit state wins');
  assert.equal(effectiveOpeningState(V1, b, { ...extDoor, role: 'entry' }), 'open', '/1 ignores roles');
});

test('an explicit state always wins over both schemas\' defaults', () => {
  const b = building('ruined_two_level');
  for (const L of [V1, V2]) {
    assert.equal(effectiveOpeningState(L, b, { ...extDoor, state: 'locked' }), 'locked');
    assert.equal(effectiveOpeningState(L, b, { ...partDoor, state: 'sealed' }), 'sealed');
    assert.equal(effectiveOpeningState(L, b, { ...groundWin, state: 'barred' }), 'barred');
    assert.equal(effectiveOpeningState(L, b, { ...upperWin, state: 'glazed' }), 'glazed');
    assert.equal(effectiveOpeningState(L, building(), { ...groundWin, state: 'broken' }), 'broken');
  }
});

test('schema /2 is accepted next to /1; other strings are refused', () => {
  assert.ok(isLevelgenSchema(LEVELGEN_SCHEMA));
  assert.ok(isLevelgenSchema(LEVELGEN_SCHEMA_V2));
  assert.ok(!isLevelgenSchema('codex.levelgen_layout/3'));
  const schemaErr = (s: string) => validateGenLayout({ schema: s }).some((m) => m.startsWith('schema must be'));
  assert.equal(schemaErr(LEVELGEN_SCHEMA), false);
  assert.equal(schemaErr(LEVELGEN_SCHEMA_V2), false);
  assert.equal(schemaErr('codex.levelgen_layout/3'), true);
});
