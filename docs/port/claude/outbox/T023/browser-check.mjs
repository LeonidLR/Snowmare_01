// T-023 browser check: drives the Training Ground «Пропсы уровней» tab in a HEADLESS Edge over CDP (temp profile, no screen takeover).
// usage: node browser-check.mjs <base url> <out dir> <repo root>      (the dev server must run from the worktree; map L_Gen_Test_X2)
// It saves on the fixture map, reads the file from disk to compare, and the caller restores it with `git checkout`.
import { spawn } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';

const [base, outDir, repo] = process.argv.slice(2);
const MAP = 'L_Gen_Test_X2';
const PROPS = path.join(repo, 'Content/Data/LevelGen/props', `${MAP}.props.json`);
const EDGE = 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe';
const PORT = 9347;
fs.mkdirSync(outDir, { recursive: true });
const logLines = [];
const log = (...a) => { const s = a.join(' '); logLines.push(s); console.log(s); };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

const profile = fs.mkdtempSync(path.join(os.tmpdir(), 'tg-cdp-'));
const edge = spawn(EDGE, [`--remote-debugging-port=${PORT}`, `--user-data-dir=${profile}`, '--headless=new', '--window-size=1600,950', '--no-first-run', '--disable-gpu', 'about:blank'], { stdio: 'ignore' });
let ws;
try {
  let targets;
  for (let i = 0; i < 60; i++) { try { targets = await (await fetch(`http://127.0.0.1:${PORT}/json`)).json(); if (targets.length) break; } catch { /* wait */ } await sleep(250); }
  const page = targets.find((t) => t.type === 'page');
  ws = new WebSocket(page.webSocketDebuggerUrl);
  await new Promise((r) => { ws.onopen = r; });
  let id = 0;
  const pending = new Map();
  const dialogs = [];
  ws.onmessage = (m) => {
    const d = JSON.parse(m.data);
    if (d.id && pending.has(d.id)) { pending.get(d.id)(d); pending.delete(d.id); }
    if (d.method === 'Page.javascriptDialogOpening') { dialogs.push(d.params.message); send('Page.handleJavaScriptDialog', { accept: dialogs.acceptNext === true }); }
  };
  const send = (method, params = {}) => new Promise((res, rej) => { const i = ++id; pending.set(i, (d) => (d.error ? rej(new Error(`${method}: ${d.error.message}`)) : res(d.result))); ws.send(JSON.stringify({ id: i, method, params })); });
  const ev = async (expr) => { const r = await send('Runtime.evaluate', { expression: expr, returnByValue: true, awaitPromise: true }); if (r.exceptionDetails) throw new Error(r.exceptionDetails.exception?.description ?? r.exceptionDetails.text); return r.result.value; };
  const shot = async (name) => { const r = await send('Page.captureScreenshot', { format: 'jpeg', quality: 70 }); fs.writeFileSync(path.join(outDir, name), Buffer.from(r.data, 'base64')); log('screenshot', name); };
  const mouse = (type, x, y, o = {}) => send('Input.dispatchMouseEvent', { type, x, y, button: o.button ?? 'none', buttons: o.buttons ?? 0, clickCount: o.clickCount ?? 0, deltaX: 0, deltaY: o.deltaY ?? 0, modifiers: o.modifiers ?? 0 });
  const click = async (x, y) => { await mouse('mouseMoved', x, y); await mouse('mousePressed', x, y, { button: 'left', buttons: 1, clickCount: 1 }); await mouse('mouseReleased', x, y, { button: 'left', clickCount: 1 }); };
  const drag = async (pts, button = 'left') => {
    const b = button === 'left' ? 1 : 4;
    await mouse('mouseMoved', pts[0][0], pts[0][1]);
    await mouse('mousePressed', pts[0][0], pts[0][1], { button, buttons: b, clickCount: 1 });
    for (const p of pts.slice(1)) { await mouse('mouseMoved', p[0], p[1], { button, buttons: b }); await sleep(15); }
    const e = pts[pts.length - 1];
    await mouse('mouseReleased', e[0], e[1], { button, clickCount: 1 });
  };
  const VK = { F11: 122, Escape: 27, ArrowUp: 38, ArrowDown: 40, ArrowLeft: 37, ArrowRight: 39, z: 90, y: 89, Enter: 13 };
  const key = async (k, mod = 0) => {
    const o = { key: k, code: k.length === 1 ? `Key${k.toUpperCase()}` : k, windowsVirtualKeyCode: VK[k] ?? k.charCodeAt(0), modifiers: mod };
    await send('Input.dispatchKeyEvent', { type: 'rawKeyDown', ...o });
    await send('Input.dispatchKeyEvent', { type: 'keyUp', ...o });
  };
  const waitFor = async (expr, what, ms = 60000) => { const t0 = Date.now(); for (;;) { if (await ev(expr)) return; if (Date.now() - t0 > ms) throw new Error(`timeout: ${what}`); await sleep(200); } };
  const clickText = (sel, text) => ev(`(() => { const b=[...document.querySelectorAll('${sel}')].find(x=>x.textContent.includes(${JSON.stringify(text)})); if(!b) return false; b.click(); return true; })()`);
  const setSelect = (sel, value) => ev(`(() => { const s=document.querySelector(${JSON.stringify(sel)}); const set=Object.getOwnPropertyDescriptor(HTMLSelectElement.prototype,'value').set; set.call(s,${JSON.stringify(value)}); s.dispatchEvent(new Event('change',{bubbles:true})); return s.value; })()`);

  await send('Page.enable'); await send('Runtime.enable');
  await send('Emulation.setDeviceMetricsOverride', { width: 1600, height: 950, deviceScaleFactor: 1, mobile: false });
  await send('Page.navigate', { url: base });
  await sleep(1500);
  await ev(`localStorage.setItem('training_ground_department','levelgen')`);
  await send('Page.navigate', { url: base });
  await waitFor(`!!document.querySelector('[data-testid=current-map-select]')`, 'map selector');
  log('current map in the page:', await ev(`document.querySelector('[data-testid=current-map-select]').value`));
  if ((await ev(`document.querySelector('[data-testid=current-map-select]').value`)) !== MAP) { await setSelect('[data-testid=current-map-select]', MAP); await sleep(1500); }
  await clickText('button', 'Пропсы уровней');
  await waitFor(`document.querySelector('[data-testid=props-map-name]')?.textContent.trim() === '${MAP}' && !!document.querySelector('[data-props-tools]') && !!document.querySelector('canvas') && !document.body.textContent.includes('Загрузка пропсов') && document.body.textContent.includes('109 шт.')`, `props tab of ${MAP}`);
  await sleep(1500);
  await ev(`window.scrollTo(0,0)`);

  // ---- the map data to convert world <-> screen (same transform as planView.ts) ----
  const data = await ev(`fetch('/api/levelgen/props?map=${MAP}').then(r=>r.json())`);
  const orig = JSON.parse(fs.readFileSync(PROPS, 'utf8'));
  const bounds = data.layout.bounds;
  const box = async () => ev(`(() => { const c=document.querySelector('canvas'); const r=c.getBoundingClientRect(); return {l:r.left,t:r.top,w:c.clientWidth,h:c.clientHeight}; })()`);
  const toPx = async (pos, view = { zoom: 1, panX: 0, panY: 0 }) => {
    const b = await box();
    const [minX, minY] = bounds.min, [maxX, maxY] = bounds.max, pad = 10;
    const s = Math.min((b.w - 2 * pad) / (maxY - minY), (b.h - 2 * pad) / (maxX - minX));
    const ox = (b.w - (maxY - minY) * s) / 2, oy = (b.h - (maxX - minX) * s) / 2;
    return [b.l + 1 + (ox + (pos[1] - minY) * s) * view.zoom + view.panX, b.t + 1 + (oy + (maxX - pos[0]) * s) * view.zoom + view.panY, s];
  };
  const toWorld = async (px) => {
    const b = await box();
    const [minX, minY] = bounds.min, [maxX, maxY] = bounds.max, pad = 10;
    const s = Math.min((b.w - 2 * pad) / (maxY - minY), (b.h - 2 * pad) / (maxX - minX));
    const ox = (b.w - (maxY - minY) * s) / 2, oy = (b.h - (maxX - minX) * s) / 2;
    return [maxX - (px[1] - b.t - 1 - oy) / s, (px[0] - b.l - 1 - ox) / s + minY];
  };
  const zoomText = () => ev(`document.querySelector('[data-props-zoom]').textContent`);
  const note = () => ev(`(document.querySelector('[data-props-move-note]')||{}).textContent||''`);
  const sel = () => ev(`(document.querySelector('[data-props-selected]')||{}).textContent||''`);

  const b0 = await box();
  log(`props tab loaded: map ${MAP}, ${data.props.layers.map((l) => `${l.id}:${l.objects.length}`).join(' ')}; canvas ${b0.w} x ${b0.h} px (was 560 px high, 3-column grid)`);
  await shot('01-normal-plan.jpg');

  // ---- zoom + pan ----
  const centre = [b0.l + b0.w / 2, b0.t + b0.h / 2];
  log('zoom label at start:', await zoomText());
  for (let i = 0; i < 3; i++) await mouse('mouseWheel', centre[0] + 100, centre[1] - 50, { deltaY: -100 });
  await sleep(300);
  const z3 = await zoomText();
  log('after 3 wheel notches at the cursor:', z3);
  await shot('02-zoomed-wheel.jpg');
  await ev(`document.querySelector('[data-props-zoom-in]').click()`); await sleep(200);
  log('after the + button:', await zoomText());
  await ev(`document.querySelector('[data-props-zoom-out]').click()`); await ev(`document.querySelector('[data-props-zoom-out]').click()`); await sleep(200);
  log('after two - buttons:', await zoomText());
  await drag([[centre[0], centre[1]], [centre[0] + 60, centre[1] + 40], [centre[0] + 140, centre[1] + 90]], 'middle');
  await sleep(300);
  await shot('03-panned-middle-button.jpg');
  await clickText('button', 'Вписать'); await sleep(300);
  log('after «Вписать»:', await zoomText());

  // ---- F11 ----
  const fullOn = () => ev(`document.querySelector('[data-full-window]')?.getAttribute('data-full-window')`);
  log('full window before F11:', await fullOn());
  await key('F11'); await sleep(500);
  log('after F11:', await fullOn(), `plan ${JSON.stringify(await box())}`);
  await shot('04-f11-full-window.jpg');
  await key('F11'); await sleep(400);
  log('after F11 again:', await fullOn());
  await key('F11'); await sleep(300); await key('Escape'); await sleep(300);
  log('F11 then Esc:', await fullOn());
  await clickText('button', 'На весь экран'); await sleep(400);
  log('button «На весь экран»:', await fullOn());
  await clickText('button', 'Свернуть'); await sleep(400);
  log('button «Свернуть»:', await fullOn());

  // ---- drag a crate ----
  const crate = orig.layers.find((l) => l.id === 'crates').objects.find((o) => !o.attach && o.walk_layer === 'ground' && !o.locked);
  let [px, py] = await toPx(crate.pos);
  log(`crate ${crate.id} at ${crate.pos} -> px ${px.toFixed(0)},${py.toFixed(0)}`);
  await click(px, py); await sleep(300);
  log('selected card:', (await sel()).replace(/\s+/g, ' ').trim());
  await shot('05-prop-selected.jpg');
  await drag([[px, py], [px + 10, py + 5], [px + 30, py + 20], [px + 40, py + 25]]);
  await sleep(600);
  log('after the drag, note:', await note());
  log('selected card after the drag:', (await sel()).replace(/\s+/g, ' ').trim());
  await shot('06-prop-dragged-locked.jpg');
  const afterDrag = await ev(`document.querySelector('[data-props-lock]').textContent`);
  log('lock button text:', afterDrag.trim());
  for (let i = 0; i < 3; i++) { await key('ArrowUp'); await sleep(80); }
  await key('ArrowRight', 8); await sleep(300); // Shift + right = 1 m
  log('after 3 x ArrowUp + Shift+ArrowRight:', (await sel()).replace(/\s+/g, ' ').trim());
  log('undo label count:', await ev(`document.querySelector('[data-props-undo]').textContent`));
  await key('z', 2); await sleep(300);
  log('after Ctrl+Z (the nudge run is ONE step: back to the drag result):', (await sel()).replace(/\s+/g, ' ').trim());
  await key('y', 2); await sleep(300);
  log('after Ctrl+Y:', (await sel()).replace(/\s+/g, ' ').trim());
  // the validation panel
  log('check panel:', (await ev(`document.querySelector('[data-props-check]').textContent`)).replace(/\s+/g, ' ').trim());
  await waitFor(`!document.querySelector('[data-props-gate]').textContent.includes('ПРОВЕРКА')`, 'check');
  log('check panel after the debounce:', (await ev(`document.querySelector('[data-props-check]').textContent`)).replace(/\s+/g, ' ').trim());
  await shot('07-check-panel.jpg');

  // ---- the dirty guard: switching the tab asks, "stay" keeps the edits ----
  dialogs.length = 0; dialogs.acceptNext = false;
  await clickText('button', 'Управление картой'); await sleep(500);
  log('tab switch with unsaved edits -> dialog:', JSON.stringify(dialogs), '; still on props:', await ev(`!!document.querySelector('[data-props-tools]')`));

  // ---- zone re-roll: box ----
  const before = JSON.parse(JSON.stringify(data.props));
  await clickText('button', 'Рамка'); await sleep(200);
  const zx0 = b0.l + b0.w * 0.12, zx1 = b0.l + b0.w * 0.48, zy0 = b0.t + b0.h * 0.10, zy1 = b0.t + b0.h * 0.95;
  const wA = await toWorld([zx0, zy0]), wB = await toWorld([zx1, zy1]);
  const boxPoly = [[wA[0], wA[1]], [wA[0], wB[1]], [wB[0], wB[1]], [wB[0], wA[1]]];
  await drag([[zx0, zy0], [zx1, zy1 * 0.6], [zx1, zy1]]);
  await sleep(500);
  log('zone summary (box):', (await ev(`(document.querySelector('[data-props-zone-summary]')||{}).textContent`)) ?? '');
  await shot('08-zone-box.jpg');
  await clickText('button', 'Перебросить в зоне');
  await waitFor(`!!document.querySelector('[data-props-zone-reroll]') && !document.body.textContent.includes('Переброска в зоне (seed')`, 'zone re-roll', 240000);
  await sleep(1500);
  const noteText = await ev(`[...document.querySelectorAll('[data-testid=reroll-note]')].map(x=>x.textContent).join(' | ')`);
  log('zone re-roll note:', noteText);
  await shot('09-after-box-reroll.jpg');

  // ---- zone: lasso ----
  await clickText('button', 'Лассо'); await sleep(200);
  const cx = b0.l + b0.w * 0.7, cy = b0.t + b0.h * 0.5, R = Math.min(b0.w, b0.h) * 0.18;
  const lasso = []; for (let a = 0; a <= 360; a += 15) lasso.push([cx + R * Math.cos((a * Math.PI) / 180), cy + R * Math.sin((a * Math.PI) / 180) * 0.8]);
  const lassoPoly = []; for (const p of lasso) lassoPoly.push(await toWorld(p));
  await drag(lasso); await sleep(500);
  log('zone summary (lasso):', await ev(`(document.querySelector('[data-props-zone-summary]')||{}).textContent`));
  await shot('10-zone-lasso.jpg');
  await clickText('button', 'Перебросить в зоне');
  await waitFor(`!document.body.textContent.includes('Переброска в зоне (seed')`, 'lasso re-roll', 240000); await sleep(1500);
  log('lasso re-roll note:', await ev(`[...document.querySelectorAll('[data-testid=reroll-note]')].map(x=>x.textContent).join(' | ')`));
  await shot('11-after-lasso-reroll.jpg');

  // ---- zone: polygon (click the corners, double-click closes) ----
  await clickText('button', 'Многоугольник'); await sleep(200);
  const poly = [[b0.l + b0.w * 0.55, b0.t + b0.h * 0.12], [b0.l + b0.w * 0.9, b0.t + b0.h * 0.15], [b0.l + b0.w * 0.85, b0.t + b0.h * 0.35], [b0.l + b0.w * 0.6, b0.t + b0.h * 0.3]];
  for (const p of poly) { await click(p[0], p[1]); await sleep(80); }
  await send('Input.dispatchKeyEvent', { type: 'rawKeyDown', key: 'Enter', code: 'Enter', windowsVirtualKeyCode: 13 }); await send('Input.dispatchKeyEvent', { type: 'keyUp', key: 'Enter', code: 'Enter', windowsVirtualKeyCode: 13 });
  await sleep(500);
  log('zone summary (polygon):', await ev(`(document.querySelector('[data-props-zone-summary]')||{}).textContent`));
  await shot('12-zone-polygon.jpg');
  await clickText('button', 'Двигать'); await sleep(200);

  // ---- save, read the file back, compare ----
  await ev(`window.scrollTo(0,0)`);
  const saved = await ev(`(async () => { const b=[...document.querySelectorAll('button')].find(x=>x.textContent.trim().startsWith('Сохранить') && !x.disabled); if(!b) return 'no enabled save button'; b.click(); return 'clicked'; })()`);
  log('save click:', saved);
  await waitFor(`document.body.textContent.includes('Сохранено успешно')`, 'save');
  log('page says:', (await ev(`[...document.querySelectorAll('div')].map(d=>d.textContent).find(t=>t.startsWith('Сохранено успешно'))`)) ?? '');
  await shot('13-saved.jpg');
  const after = JSON.parse(fs.readFileSync(PROPS, 'utf8'));
  log('file on disk: revision', orig.revision, '->', after.revision);
  const inPoly = (p, poly2) => { let c = false; for (let i = 0, j = poly2.length - 1; i < poly2.length; j = i++) { const a = poly2[i], b = poly2[j]; if ((a[1] > p[1]) !== (b[1] > p[1]) && p[0] < ((b[0] - a[0]) * (p[1] - a[1])) / (b[1] - a[1]) + a[0]) c = !c; } return c; };
  const o1 = new Map(); for (const l of orig.layers) for (const o of l.objects) o1.set(o.id, [l.id, o]);
  const o2 = new Map(); for (const l of after.layers) for (const o of l.objects) o2.set(o.id, [l.id, o]);
  const movedCrate = o2.get(crate.id)?.[1];
  log('dragged crate on disk:', JSON.stringify(movedCrate && { pos: movedCrate.pos, locked: movedCrate.locked, walk_layer: movedCrate.walk_layer }), '(was', JSON.stringify(crate.pos), ')');
  // an object outside every zone, not the dragged crate, is identical on disk (only unlocked layers 'crates','barrels','turrets','mines','loot' were re-rolled)
  let outsideSame = 0, outsideChanged = 0, insideChanged = 0, insideSame = 0;
  for (const [oid, [lid, o]] of o1) {
    if (oid === crate.id || lid === 'power') continue;
    const inAny = inPoly(o.pos, boxPoly) || inPoly(o.pos, lassoPoly);
    const now = o2.get(oid)?.[1];
    const same = !!now && JSON.stringify(now) === JSON.stringify(o);
    if (inAny) { if (same) insideSame++; else insideChanged++; } else if (same) outsideSame++; else outsideChanged++;
  }
  log(`objects outside the zones: ${outsideSame} identical, ${outsideChanged} changed (the polygon zone was drawn but NOT rolled); inside the zones: ${insideChanged} replaced, ${insideSame} kept`);
  const freshInside = [...o2].filter(([oid]) => !o1.has(oid)).length;
  log('new objects on disk:', freshInside);
  const api = await ev(`fetch('/api/levelgen/props?map=${MAP}').then(r=>r.json()).then(d=>({rev:d.props.revision, n:d.props.layers.reduce((s,l)=>s+l.objects.length,0)}))`);
  log('GET after save:', JSON.stringify(api));
  fs.writeFileSync(path.join(outDir, 'browser-check.log'), logLines.join('\n') + '\n');
} finally {
  try { ws?.close(); } catch { /* ignore */ }
  edge.kill();
  await sleep(500);
  try { fs.rmSync(profile, { recursive: true, force: true }); } catch { /* ignore */ }
}
