// 2D top-down replay of codex.pmc_arena_track/1 rounds; inlined into report.html by report.py (canvas, play / pause / scrub, event ticker).
(function () {
  const data = JSON.parse(document.getElementById('rp-data').textContent);
  const cv = document.getElementById('rp-canvas'); if (!cv || !data.length) return;
  const ctx = cv.getContext('2d'), sel = document.getElementById('rp-sel'), play = document.getElementById('rp-play');
  const scrub = document.getElementById('rp-scrub'), clock = document.getElementById('rp-time');
  const speedSel = document.getElementById('rp-speed'), ticker = document.getElementById('rp-ticker');
  const css = n => getComputedStyle(document.documentElement).getPropertyValue(n).trim();
  let tr, t = 0, playing = false, last = 0, box, lastPos;
  const dur = () => tr.frames.length * tr.dt;
  const sx = x => (x - box.x0) / box.w * cv.width, sy = y => (y - box.y0) / box.h * cv.height;
  function load(i) {
    tr = data[i].track; t = 0; lastPos = {};
    let x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
    tr.frames.forEach(f => f.forEach(p => { if (p) { x0 = Math.min(x0, p[0]); x1 = Math.max(x1, p[0]); y0 = Math.min(y0, p[1]); y1 = Math.max(y1, p[1]); } }));
    const pad = Math.max(2, (x1 - x0) * 0.05);
    box = { x0: x0 - pad, y0: y0 - pad, w: (x1 - x0) + 2 * pad || 1, h: (y1 - y0) + 2 * pad || 1 };
    cv.height = Math.round(cv.width * Math.min(0.75, Math.max(0.4, box.h / box.w)));
    scrub.max = Math.round(dur() * 10); draw();
  }
  function posAt(id, k) {
    const i = tr.fighters.findIndex(f => f.id === id); const p = tr.frames[k] && tr.frames[k][i]; return p || lastPos[id];
  }
  function describe(e) {
    if (e.type === 'kill') return (e.by || '?') + ' killed ' + e.target;
    if (e.type === 'down') return e.target + ' is down' + (e.by ? ' (' + e.by + ')' : '');
    if (e.type === 'heal') return (e.by || '?') + ' heals ' + (e.target || '');
    if (e.type === 'tactic') return 'side ' + e.side + ': ' + e.decision + (e.by ? ' (' + e.by + ')' : '');
    return e.type;
  }
  function draw() {
    const k = Math.min(tr.frames.length - 1, Math.floor(t / tr.dt)), frame = tr.frames[k] || [];
    ctx.fillStyle = css('--card'); ctx.fillRect(0, 0, cv.width, cv.height);
    ctx.strokeStyle = css('--line'); ctx.lineWidth = 1;
    for (let gx = Math.ceil(box.x0 / 10) * 10; gx < box.x0 + box.w; gx += 10) { ctx.beginPath(); ctx.moveTo(sx(gx), 0); ctx.lineTo(sx(gx), cv.height); ctx.stroke(); }
    for (let gy = Math.ceil(box.y0 / 10) * 10; gy < box.y0 + box.h; gy += 10) { ctx.beginPath(); ctx.moveTo(0, sy(gy)); ctx.lineTo(cv.width, sy(gy)); ctx.stroke(); }
    tr.fighters.forEach((f, i) => { if (frame[i]) lastPos[f.id] = frame[i]; });
    tr.events.forEach(e => {
      if (e.type !== 'shot' || e.t > t || e.t < t - 0.35) return;
      const a = posAt(e.from, k), b = posAt(e.to, k); if (!a || !b) return;
      ctx.strokeStyle = e.from[0] === 'A' ? css('--c1') : css('--c3'); ctx.globalAlpha = e.hit ? 0.9 : 0.3; ctx.lineWidth = e.hit ? 2 : 1;
      ctx.beginPath(); ctx.moveTo(sx(a[0]), sy(a[1])); ctx.lineTo(sx(b[0]), sy(b[1])); ctx.stroke(); ctx.globalAlpha = 1;
    });
    tr.fighters.forEach((f, i) => {
      const p = frame[i] || lastPos[f.id]; if (!p) return;
      const col = f.side === 'A' ? css('--c1') : css('--c3'), x = sx(p[0]), y = sy(p[1]);
      if (frame[i]) { ctx.fillStyle = col; ctx.beginPath(); ctx.arc(x, y, 7, 0, 7); ctx.fill(); }
      else { ctx.strokeStyle = col; ctx.lineWidth = 2; ctx.beginPath(); ctx.moveTo(x - 5, y - 5); ctx.lineTo(x + 5, y + 5); ctx.moveTo(x + 5, y - 5); ctx.lineTo(x - 5, y + 5); ctx.stroke(); }
      ctx.fillStyle = css('--fg'); ctx.font = '11px system-ui'; ctx.fillText(f.id, x + 9, y - 8);
    });
    const shown = tr.events.filter(e => e.type !== 'shot' && e.t <= t).slice(-8).reverse();
    ticker.innerHTML = shown.map(e => '<li>' + e.t.toFixed(1) + ' s - ' + describe(e) + '</li>').join('');
    clock.textContent = t.toFixed(1) + ' / ' + dur().toFixed(1) + ' s'; scrub.value = Math.round(t * 10);
  }
  function step(ts) {
    if (!playing) return;
    t = Math.min(dur(), t + (ts - last) / 1000 * parseFloat(speedSel.value)); last = ts; draw();
    if (t >= dur()) { playing = false; play.textContent = 'Play'; return; }
    requestAnimationFrame(step);
  }
  play.onclick = () => {
    playing = !playing; play.textContent = playing ? 'Pause' : 'Play';
    if (playing) { if (t >= dur()) t = 0; last = performance.now(); requestAnimationFrame(step); }
  };
  scrub.oninput = () => { t = scrub.value / 10; draw(); };
  sel.onchange = () => { playing = false; play.textContent = 'Play'; load(sel.selectedIndex); };
  document.querySelectorAll('[data-replay]').forEach(b => b.onclick = () => {
    sel.selectedIndex = +b.dataset.replay; sel.onchange(); cv.scrollIntoView({ behavior: 'smooth' });
  });
  load(0);
})();
