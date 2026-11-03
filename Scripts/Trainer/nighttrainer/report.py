"""Outputs: leaderboard.json / leaderboard.csv and a self-contained report.html (inline SVG + CSS, no network)."""
from __future__ import annotations

import csv
import html
import json
import os

from . import highlights
from .genome import diff_genes, write_json

LEADER_COLUMNS = ("rank", "id", "champion", "generation", "fitness", "fresh_fitness", "win_rate", "win_rate_vs_bot",
                  "survivors_frac", "hp_lost", "duration_s", "crashes", "rounds")
STAT_ROWS = (("fitness", "Fitness"), ("win_rate", "Win rate"), ("survivors_frac", "Survivors (fraction)"),
             ("hp_lost", "HP lost / round"), ("duration_s", "Duration, s"), ("kills", "Kills"),
             ("flanks_attempted", "Flanks attempted"), ("flanks_succeeded", "Flanks succeeded"),
             ("suppression_s", "Suppression, s"), ("ambushes", "Ambushes"), ("retreats", "Retreats"),
             ("retreats_survived", "Retreats survived"), ("heals", "Heals"), ("friendly_fire", "Friendly fire"),
             ("stuck_events", "Stuck events"), ("crashes", "Crashed rounds"))


def leaderboard(state: dict, limit: int = 100) -> list:
    champs = {c["id"] for c in state["champions"]}
    rows = sorted(state["entries"], key=lambda e: e["fitness"], reverse=True)[:limit]
    return [dict(e, rank=i + 1, champion=("current" if e["id"] == state["champion"]["id"] else
                                          "former" if e["id"] in champs else "")) for i, e in enumerate(rows)]


def write_all(trainer, state: dict) -> None:
    board = leaderboard(state)
    write_json(os.path.join(trainer.run_dir, "leaderboard.json"),
               {"run_id": trainer.run_id, "sorted_by": "fitness (its generation's evaluation); fresh_fitness = the fresh-seed re-evaluation of the top-k",
                "entries": board})
    with open(os.path.join(trainer.run_dir, "leaderboard.csv"), "w", encoding="utf-8", newline="") as f:
        w = csv.DictWriter(f, fieldnames=LEADER_COLUMNS, extrasaction="ignore")
        w.writeheader()
        w.writerows(board)
    with open(os.path.join(trainer.run_dir, "report.html"), "w", encoding="utf-8") as f:
        f.write(render_html(trainer, state, board))


def _fmt(v, digits: int = 3) -> str:
    if v is None:
        return "-"
    if isinstance(v, float):
        return f"{v:.{digits}f}"
    return html.escape(str(v))


def _chart(history: list) -> str:
    if not history:
        return "<p class='muted'>No finished generation yet.</p>"
    w, h, pad = 760, 260, 40
    series = {"best": [x["best"] for x in history], "mean": [x["mean"] for x in history],
              "champion (fresh seeds)": [x["champion_fresh"] for x in history]}
    vals = [v for s in series.values() for v in s]
    lo, hi = min(vals), max(vals)
    if hi - lo < 1e-9:
        lo, hi = lo - 1, hi + 1
    n = len(history)

    def xy(i, v):
        x = pad + (w - 2 * pad) * (i / max(1, n - 1))
        y = h - pad - (h - 2 * pad) * ((v - lo) / (hi - lo))
        return f"{x:.1f},{y:.1f}"

    colors = {"best": "var(--c1)", "mean": "var(--c2)", "champion (fresh seeds)": "var(--c3)"}
    parts = [f"<svg viewBox='0 0 {w} {h}' role='img' aria-label='fitness per generation'>",
             f"<line x1='{pad}' y1='{h - pad}' x2='{w - pad}' y2='{h - pad}' class='axis'/>",
             f"<line x1='{pad}' y1='{pad}' x2='{pad}' y2='{h - pad}' class='axis'/>",
             f"<text x='{pad - 6}' y='{pad + 4}' class='tick' text-anchor='end'>{hi:.2f}</text>",
             f"<text x='{pad - 6}' y='{h - pad + 4}' class='tick' text-anchor='end'>{lo:.2f}</text>",
             f"<text x='{pad}' y='{h - pad + 18}' class='tick'>gen {history[0]['generation']}</text>",
             f"<text x='{w - pad}' y='{h - pad + 18}' class='tick' text-anchor='end'>gen {history[-1]['generation']}</text>"]
    for name, s in series.items():
        pts = " ".join(xy(i, v) for i, v in enumerate(s))
        parts.append(f"<polyline points='{pts}' fill='none' stroke='{colors[name]}' stroke-width='2'/>")
        for i, v in enumerate(s):
            parts.append(f"<circle cx='{xy(i, v).split(',')[0]}' cy='{xy(i, v).split(',')[1]}' r='2.5' "
                         f"fill='{colors[name]}'><title>{name} gen {history[i]['generation']}: {v:.3f}</title></circle>")
    for i, x in enumerate(history):
        if x.get("promoted"):
            px = xy(i, lo).split(",")[0]
            parts.append(f"<line x1='{px}' y1='{pad}' x2='{px}' y2='{h - pad}' class='promo'>"
                         f"<title>new champion {html.escape(x['champion'])}</title></line>")
    parts.append("</svg>")
    legend = " ".join(f"<span class='key' style='--k:{c}'>{html.escape(n)}</span>" for n, c in colors.items())
    return "".join(parts) + f"<div class='legend'>{legend} <span class='key promo-key'>new champion</span></div>"


def _stats_table(base: dict | None, champ: dict | None) -> str:
    if not base or not champ:
        return "<p class='muted'>Needs at least one finished generation.</p>"
    head = "<tr><th>Metric</th><th>Baseline</th><th>Champion</th><th>Baseline vs bot</th><th>Champion vs bot</th></tr>"
    rows = []
    for key, label in STAT_ROWS:
        b, c = base.get("all", {}).get(key), champ.get("all", {}).get(key)
        bb, cb = base.get("bot", {}).get(key), champ.get("bot", {}).get(key)
        rows.append(f"<tr><td>{label}</td><td>{_fmt(b)}</td><td>{_fmt(c)}</td><td>{_fmt(bb)}</td><td>{_fmt(cb)}</td></tr>")
    return f"<table>{head}{''.join(rows)}</table>"


def _diff_table(trainer, state: dict) -> str:
    rows = diff_genes(trainer.baseline_genes, state["champion"]["genes"])
    if not rows:
        return "<p class='muted'>The champion is still the baseline (no weight changed).</p>"
    body = "".join(f"<tr><td><code>{html.escape(p)}</code></td><td>{_fmt(b)}</td><td>{_fmt(o)}</td>"
                   f"<td class='{'up' if (o - b) > 0 else 'down'}'>{'-' if pct is None else f'{pct:+.1f} %'}</td></tr>"
                   for p, b, o, pct in sorted(rows, key=lambda r: -abs(r[3] or 0)))
    return f"<table><tr><th>Weight (JSON pointer)</th><th>Baseline</th><th>Champion</th><th>Change</th></tr>{body}</table>"


def _highlights_section(trainer, state: dict) -> str:
    items = highlights.entries(state)
    if not items:
        return ""
    stride = int(trainer.cfg.get("highlights", {}).get("replay_stride", 2))
    replays, figs = [], []
    for e in items:
        track = highlights.load_track(trainer.run_dir, e, stride)
        idx = len(replays)
        if track:
            replays.append({"id": e["id"], "caption": e["caption"], "track": track})
        label = "Champion's most decisive win" if e.get("kind") == "champion" else f"Highlight, score {e['score']:.1f}"
        video = (f"<video controls preload='metadata' playsinline src='{html.escape(e['clip'])}'></video>"
                 if e.get("clip") else "<div class='novideo muted'>no clip yet (make_highlights.ps1)</div>")
        btn = f"<button data-replay='{idx}'>2D replay</button>" if track else ""
        figs.append(f"<figure>{video}<figcaption><b>{html.escape(e['caption'])}</b><br><span class='muted'>{label} - "
                    f"gen {e['generation']}, vs {html.escape(e['opponent'])}, seed {e['seed']}</span> {btn}</figcaption>"
                    "</figure>")
    viewer = ""
    if replays:
        with open(os.path.join(os.path.dirname(__file__), "replay_viewer.js"), encoding="utf-8") as f:
            js = f.read()
        opts = "".join(f"<option>{html.escape(r['caption'])}</option>" for r in replays)
        payload = json.dumps(replays, separators=(",", ":")).replace("</", "<\\/")
        viewer = ("<h2>2D replay (top-down, from the track files)</h2><div class='controls'>"
                  f"<select id='rp-sel'>{opts}</select><button id='rp-play'>Play</button>"
                  "<select id='rp-speed' aria-label='speed'><option value='1'>1x</option><option value='2' selected>2x</option>"
                  "<option value='4'>4x</option><option value='8'>8x</option></select>"
                  "<span id='rp-time' class='muted'></span></div>"
                  "<input type='range' id='rp-scrub' min='0' max='1000' value='0' aria-label='time'>"
                  "<canvas id='rp-canvas' width='960' height='560'></canvas>"
                  "<div class='legend'><span class='key' style='--k:var(--c1)'>side A (candidate PMC)</span>"
                  "<span class='key' style='--k:var(--c3)'>side B (opponent)</span><span>lines = shots (bright = hit), "
                  "x = dead</span></div><ol id='rp-ticker'></ol>"
                  f"<script type='application/json' id='rp-data'>{payload}</script><script>{js}</script>")
    session = (state.get("highlights") or {}).get("session", "")
    return (f"<section><h2>Highlights (night of {html.escape(session)})</h2><div class='hl-grid'>{''.join(figs)}</div>"
            f"{viewer}</section>")


def render_html(trainer, state: dict, board: list) -> str:
    champ = state["champion"]
    st = trainer.status or {}
    hist = state["history"]
    champs = "".join(f"<tr><td>{_fmt(c['generation'])}</td><td>{html.escape(c['id'])}</td><td>{_fmt(c['fresh_fitness'])}</td>"
                     f"<td>{html.escape(c['previous'])}</td><td>{_fmt(c['previous_fresh_fitness'])}</td></tr>"
                     for c in state["champions"]) or "<tr><td colspan='5' class='muted'>No promotion yet.</td></tr>"
    lead = "".join("<tr>" + "".join(f"<td>{_fmt(e.get(k))}</td>" for k in LEADER_COLUMNS) + "</tr>" for e in board[:15])
    mode = "MOCK (synthetic arena - the numbers mean nothing for the game)" if trainer.mock else "arena"
    return f"""<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>Night Trainer Report</title>
<style>
:root {{ --bg:#f7f7f5; --fg:#1d1d1b; --muted:#6b6b66; --card:#fff; --line:#dcdcd6; --c1:#2f6fdf; --c2:#9a9a92; --c3:#d9822b; --up:#1f8a4c; --down:#c0392b; }}
@media (prefers-color-scheme: dark) {{ :root:not([data-theme="light"]) {{ --bg:#161614; --fg:#ecece8; --muted:#9a9a92; --card:#1f1f1c; --line:#34342f; --c1:#6b9cf0; --c2:#77776f; --c3:#e9a35a; --up:#4cc27f; --down:#ef6b5b; }} }}
:root[data-theme="dark"] {{ --bg:#161614; --fg:#ecece8; --muted:#9a9a92; --card:#1f1f1c; --line:#34342f; --c1:#6b9cf0; --c2:#77776f; --c3:#e9a35a; --up:#4cc27f; --down:#ef6b5b; }}
body {{ margin:0; background:var(--bg); color:var(--fg); font:14px/1.5 system-ui,-apple-system,Segoe UI,sans-serif; }}
main {{ max-width:980px; margin:0 auto; padding:16px; }}
section {{ background:var(--card); border:1px solid var(--line); border-radius:8px; padding:12px 16px; margin:12px 0; overflow-x:auto; }}
h1 {{ font-size:22px; margin:8px 0; }} h2 {{ font-size:16px; margin:4px 0 10px; }}
table {{ border-collapse:collapse; width:100%; font-variant-numeric:tabular-nums; }}
th, td {{ text-align:left; padding:4px 8px; border-bottom:1px solid var(--line); white-space:nowrap; }}
.muted {{ color:var(--muted); }} .up {{ color:var(--up); }} .down {{ color:var(--down); }}
svg {{ width:100%; height:auto; }} .axis {{ stroke:var(--line); }} .tick {{ fill:var(--muted); font-size:11px; }}
.promo {{ stroke:var(--c3); stroke-dasharray:3 4; opacity:.6; }}
.legend {{ display:flex; flex-wrap:wrap; gap:14px; color:var(--muted); font-size:12px; }}
.key::before {{ content:""; display:inline-block; width:14px; height:3px; margin-right:6px; vertical-align:middle; background:var(--k); }}
.promo-key::before {{ background:none; border-top:2px dashed var(--c3); }}
.kv {{ display:grid; grid-template-columns:repeat(auto-fit,minmax(170px,1fr)); gap:8px; }}
.kv div {{ border:1px solid var(--line); border-radius:6px; padding:6px 10px; }} .kv b {{ display:block; font-size:18px; }}
.hl-grid {{ display:grid; grid-template-columns:repeat(auto-fit,minmax(260px,1fr)); gap:12px; margin-bottom:12px; }}
figure {{ margin:0; }} video, .novideo {{ width:100%; aspect-ratio:16/9; background:#000; border-radius:6px; }}
.novideo {{ display:flex; align-items:center; justify-content:center; background:var(--bg); }}
figcaption {{ margin-top:6px; font-size:13px; }}
button, select {{ font:inherit; padding:3px 10px; border:1px solid var(--line); border-radius:6px; background:var(--bg); color:var(--fg); cursor:pointer; }}
.controls {{ display:flex; flex-wrap:wrap; gap:6px; align-items:center; margin-bottom:6px; }}
#rp-scrub {{ width:100%; }} canvas {{ width:100%; height:auto; border:1px solid var(--line); border-radius:6px; }}
#rp-ticker {{ font-size:12px; color:var(--muted); min-height:9em; padding-left:20px; }}
</style></head><body><main>
<h1>Night Trainer - {html.escape(trainer.run_id)}</h1>
<p class="muted">Mode: {html.escape(mode)}. Baseline: <code>{html.escape(trainer.baseline_source)}</code>. Updated {_fmt(st.get('updated'))}.
The trainer never edits the game file: to adopt the champion run <code>powershell -ExecutionPolicy Bypass -File Scripts/Trainer/apply_champion.ps1 -RunId {html.escape(trainer.run_id)}</code>.</p>
<section class="kv">
<div>Generations done<b>{state['generation_done'] + 1}</b></div>
<div>Champion<b>{html.escape(champ['id'])}</b></div>
<div>Champion fresh fitness<b>{_fmt((champ.get('stats') or {}).get('all', {}).get('fitness'))}</b></div>
<div>Promotions<b>{len(state['champions'])}</b></div>
<div>State<b>{html.escape(str(st.get('state', '-')))}</b></div>
</section>
<section><h2>Fitness per generation</h2>{_chart(hist)}
<p class="muted">Best / mean = this generation's evaluation (vs the champion of that generation and the bot squad), so the curve can dip when a stronger champion is promoted. The champion line is its re-evaluation on fresh seeds.</p></section>
{_highlights_section(trainer, state)}
<section><h2>Champion vs baseline (fresh-seed evaluations)</h2>{_stats_table(state.get('baseline_stats'), champ.get('stats'))}
<p class="muted">"vs bot" = against the player squad driven by the playtest bot: the absolute yardstick (the self-play opponent changes over the run).</p></section>
<section><h2>Champion weights vs baseline</h2>{_diff_table(trainer, state)}</section>
<section><h2>Champion history</h2><table><tr><th>Generation</th><th>New champion</th><th>Fresh fitness</th><th>Replaced</th><th>Its fresh fitness</th></tr>{champs}</table></section>
<section><h2>Leaderboard (top 15)</h2><table><tr>{''.join(f'<th>{k}</th>' for k in LEADER_COLUMNS)}</tr>{lead}</table>
<p class="muted">Full list: leaderboard.csv / leaderboard.json in the run folder.</p></section>
</main></body></html>
"""
