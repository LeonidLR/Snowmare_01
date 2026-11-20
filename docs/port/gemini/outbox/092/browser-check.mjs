// 092 browser check: drives Training Ground «Управление картой» in headless Edge over CDP.
// usage: node browser-check.mjs <base url> <out dir> <repo root>
import { spawn } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';

const [base, outDir, repo] = process.argv.slice(2);
const MAP = 'L_Gen_Test_X2';
const EDGE = 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe';
const PORT = 9348;
fs.mkdirSync(outDir, { recursive: true });
const logLines = [];
const log = (...a) => { const s = a.join(' '); logLines.push(s); console.log(s); };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

const profile = fs.mkdtempSync(path.join(os.tmpdir(), 'tg-cdp-092-'));
const edge = spawn(EDGE, [`--remote-debugging-port=${PORT}`, `--user-data-dir=${profile}`, '--headless=new', '--window-size=1600,950', '--no-first-run', '--disable-gpu', 'about:blank'], { stdio: 'ignore' });
let ws;
try {
  let targets;
  for (let i = 0; i < 60; i++) {
    try {
      targets = await (await fetch(`http://127.0.0.1:${PORT}/json`)).json();
      if (targets && targets.length) break;
    } catch { /* wait */ }
    await sleep(250);
  }
  const page = targets.find((t) => t.type === 'page');
  ws = new WebSocket(page.webSocketDebuggerUrl);
  await new Promise((r) => { ws.onopen = r; });
  let id = 0;
  const pending = new Map();
  const dialogs = [];
  ws.onmessage = (m) => {
    const d = JSON.parse(m.data);
    if (d.id && pending.has(d.id)) { pending.get(d.id)(d); pending.delete(d.id); }
    if (d.method === 'Page.javascriptDialogOpening') {
      log('dialog opening:', d.params.message);
      dialogs.push(d.params.message);
      send('Page.handleJavaScriptDialog', { accept: true });
    }
  };
  const send = (method, params = {}) => new Promise((res, rej) => {
    const i = ++id;
    pending.set(i, (d) => (d.error ? rej(new Error(`${method}: ${d.error.message}`)) : res(d.result)));
    ws.send(JSON.stringify({ id: i, method, params }));
  });
  const ev = async (expr) => {
    const r = await send('Runtime.evaluate', { expression: expr, returnByValue: true, awaitPromise: true });
    if (r.exceptionDetails) throw new Error(r.exceptionDetails.exception?.description ?? r.exceptionDetails.text);
    return r.result.value;
  };
  const shot = async (name) => {
    const r = await send('Page.captureScreenshot', { format: 'jpeg', quality: 70 });
    fs.writeFileSync(path.join(outDir, name), Buffer.from(r.data, 'base64'));
    log('screenshot', name);
  };
  const mouse = (type, x, y, o = {}) => send('Input.dispatchMouseEvent', { type, x, y, button: o.button ?? 'none', buttons: o.buttons ?? 0, clickCount: o.clickCount ?? 0, deltaX: 0, deltaY: o.deltaY ?? 0, modifiers: o.modifiers ?? 0 });
  const click = async (x, y) => { await mouse('mouseMoved', x, y); await mouse('mousePressed', x, y, { button: 'left', buttons: 1, clickCount: 1 }); await mouse('mouseReleased', x, y, { button: 'left', clickCount: 1 }); };
  const VK = { F11: 122, Escape: 27, ArrowUp: 38, ArrowDown: 40, ArrowLeft: 37, ArrowRight: 39, z: 90, y: 89, Enter: 13 };
  const key = async (k, mod = 0) => {
    const o = { key: k, code: k.length === 1 ? `Key${k.toUpperCase()}` : k, windowsVirtualKeyCode: VK[k] ?? k.charCodeAt(0), modifiers: mod };
    await send('Input.dispatchKeyEvent', { type: 'rawKeyDown', ...o });
    await send('Input.dispatchKeyEvent', { type: 'keyUp', ...o });
  };
  const waitFor = async (expr, what, ms = 60000) => {
    const t0 = Date.now();
    for (;;) {
      if (await ev(expr)) return;
      if (Date.now() - t0 > ms) throw new Error(`timeout: ${what}`);
      await sleep(200);
    }
  };
  const clickText = (sel, text) => ev(`(() => { const b=[...document.querySelectorAll('${sel}')].find(x=>x.textContent.includes(${JSON.stringify(text)})); if(!b) return false; b.click(); return true; })()`);
  const setSelect = (sel, value) => ev(`(() => { const s=document.querySelector(${JSON.stringify(sel)}); const set=Object.getOwnPropertyDescriptor(HTMLSelectElement.prototype,'value').set; set.call(s,${JSON.stringify(value)}); s.dispatchEvent(new Event('change',{bubbles:true})); return s.value; })()`);

  await send('Page.enable');
  await send('Runtime.enable');
  await send('Emulation.setDeviceMetricsOverride', { width: 1600, height: 950, deviceScaleFactor: 1, mobile: false });
  await send('Page.navigate', { url: base });
  await sleep(1500);
  await ev(`localStorage.setItem('training_ground_department','levelgen')`);
  await send('Page.navigate', { url: base });
  await waitFor(`!!document.querySelector('[data-testid=current-map-select]')`, 'map selector');

  const currentMap = await ev(`document.querySelector('[data-testid=current-map-select]').value`);
  log('current map in page:', currentMap);
  if (currentMap !== MAP) {
    await setSelect('[data-testid=current-map-select]', MAP);
    await sleep(2000);
  }

  // Switch to «Управление картой» tab
  await clickText('button', 'Управление картой');
  await sleep(1500);
  await waitFor(`!!document.querySelector('[data-accept]')`, 'accept button on map management tab');
  log('levelgen tab loaded on map', MAP);

  // Toggle full window so the plan is sized prominently
  await clickText('button', 'На весь экран');
  await sleep(600);
  await shot('01-before-move.jpg');

  // Switch active layer to 'buildings'
  log('activating buildings layer...');
  await ev(`(() => {
    const btn = document.querySelector('[data-layer-edit="buildings"]');
    if (btn) btn.click();
  })()`);
  await sleep(600);

  // Click on building B_hall_n (center is at px 500, 300)
  log('clicking B_hall_n at px 500, 300...');
  await click(500, 300);
  await sleep(400);

  const selEntity = await ev(`(document.querySelector('[data-selected]')||{}).getAttribute?.('data-selected') || ''`);
  log('selected entity:', selEntity);

  // Nudge building using Shift+ArrowUp (+100 cm in X)
  log('nudging building with Shift+ArrowUp (+100 cm)...');
  await key('ArrowUp', 8);
  await sleep(600);

  // Wait for eval to complete (data-eval-pending clears and data-accept becomes enabled)
  log('waiting for route evaluation to finish...');
  await waitFor(`!document.querySelector('[data-eval-pending]') && !document.querySelector('[data-accept]')?.disabled`, 'eval to complete');

  const bannerText = await ev(`(document.querySelector('[data-map-banner]')||{}).textContent||''`);
  log('banner after move:', bannerText);
  await shot('02-building-moved.jpg');

  // Press «Принять»
  log('clicking Accept...');
  await ev(`(() => {
    const btn = document.querySelector('[data-accept]');
    if (btn) btn.click();
  })()`);
  await sleep(2500);

  // Wait for stale note with carry button
  await waitFor(`!!document.querySelector('[data-stale-note]')`, 'stale note banner');
  const staleText = await ev(`document.querySelector('[data-stale-note]').textContent`);
  log('stale note text:', staleText);
  await shot('03-stale-banner-with-carry-button.jpg');

  // Check if carry button is present
  const hasCarryBtn = await ev(`!!document.querySelector('[data-carry-derived]')`);
  log('has carry button:', hasCarryBtn);

  if (hasCarryBtn) {
    // Click carry button
    log('clicking carry button...');
    await ev(`document.querySelector('[data-carry-derived]').click()`);
    await sleep(2000);
    const afterCarryText = await ev(`document.querySelector('[data-stale-note]').textContent`);
    log('after carry text:', afterCarryText);
    await shot('04-props-carried.jpg');

    // Click undo button
    const hasUndoBtn = await ev(`!!document.querySelector('[data-undo-carry]')`);
    log('has undo carry button:', hasUndoBtn);
    if (hasUndoBtn) {
      log('clicking undo carry button...');
      await ev(`document.querySelector('[data-undo-carry]').click()`);
      await sleep(2000);
      const afterUndoText = await ev(`document.querySelector('[data-stale-note]').textContent`);
      log('after undo text:', afterUndoText);
      await shot('05-carry-undone.jpg');
    }
  }

  log('browser check finished successfully');
} catch (e) {
  log('browser check error:', e.message);
  throw e;
} finally {
  fs.writeFileSync(path.join(outDir, 'browser-check.log'), logLines.join('\n') + '\n');
  if (ws) ws.close();
  edge.kill();
  await sleep(500);
  try { fs.rmSync(profile, { recursive: true, force: true }); } catch { /* ignore */ }
}
