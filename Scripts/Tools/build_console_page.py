"""Console commands page (designer 2026-11-22: "where is the list of all commands? make it a page, always up to date").

Scans the C++ of one or more checkouts for every console variable / command registered in code
(TAutoConsoleVariable, FAutoConsoleVariableRef, FAutoConsoleCommand*), takes the Russian designer text from
docs/design/CONSOLE_COMMANDS.md where a row names the command, else the help text from the code, and writes
  <out>/index.html      the page (search, sections, smoke checks hidden by default)
  <out>/commands.json   one record per command: name, kind, default, help_ru, help_en, section, where, file
Publish <out>/index.html as the "Консольные команды" artifact with commands.json beside it; re-run + republish
after every merge that adds / changes a command (docs/port/HANDOFF.md names the artifact URL).

  python Scripts/Tools/build_console_page.py --out <dir> \
      --root main=C:/.../CodexTactics --root claude/bgen-test=C:/.../CodexTactics-agents [--doc <CONSOLE_COMMANDS.md>]
The first --root is the reference ("main" = in every game); a name found only in a later root is marked "only <label>".
"""
from __future__ import annotations

import argparse
import datetime as _dt
import json
import os
import re
import sys

KINDS = {
    "TAutoConsoleVariable": "variable",
    "FAutoConsoleVariableRef": "variable",
    "FAutoConsoleCommand": "command",
    "FAutoConsoleCommandWithWorld": "command",
    "FAutoConsoleCommandWithWorldAndArgs": "command",
    "FAutoConsoleCommandWithArgsAndOutputDevice": "command",
    "FAutoConsoleCommandWithWorldArgsAndOutputDevice": "command",
    "FAutoConsoleCommandWithOutputDevice": "command",
}
DECL = re.compile(r"\b(TAutoConsoleVariable\s*<[^>]+>|FAutoConsoleVariableRef|FAutoConsoleCommand[A-Za-z]*)\s+[A-Za-z0-9_]+\s*\(")
STR = re.compile(r'TEXT\(\s*"((?:[^"\\]|\\.)*)"\s*\)|"((?:[^"\\]|\\.)*)"')

SECTIONS = [  # (prefix, section) - first match wins
    ("Codex.Doors", "Двери и здания"), ("Codex.RoofCutaway", "Камера и срез этажей"), ("Codex.Cursor", "Камера и срез этажей"),
    ("Codex.Camera", "Камера и срез этажей"), ("Codex.Formation", "Строй и отряд"), ("Codex.Squad", "Строй и отряд"),
    ("Codex.Mantle", "Перелезание и лестницы"), ("Codex.Vault", "Перелезание и лестницы"), ("Codex.Ladder", "Перелезание и лестницы"),
    ("Codex.Props", "Двери и здания"), ("Codex.LevelGen", "Двери и здания"), ("Codex.Debug", "Отладка и подписи"),
    ("Codex.Pmc", "Враги / ИИ"), ("Codex.Enemy", "Враги / ИИ"), ("Codex.AI", "Враги / ИИ"), ("Codex.Horde", "Враги / ИИ"),
    ("Codex.Stealth", "Скрытность"), ("Codex.Sight", "Скрытность"), ("Codex.Combat", "Бой и стрельба"), ("Codex.Fire", "Бой и стрельба"),
    ("Codex.Weapon", "Бой и стрельба"), ("Codex.Weather", "Погода и окружение"), ("Codex.Env", "Погода и окружение"),
    ("Codex.Snow", "Погода и окружение"), ("Codex.Cold", "Погода и окружение"), ("Codex.Random", "Повторяемость и логи"),
    ("Codex.Bot", "Бот"), ("Codex.Preload", "Загрузка"),
    ("Codex.Cover", "Строй и отряд"), ("Codex.GroupCover", "Строй и отряд"), ("Codex.Knockdown", "Бой и стрельба"),
    ("Codex.Marksman", "Враги / ИИ"), ("Codex.Patrol", "Враги / ИИ"), ("Codex.Pressure", "Враги / ИИ"), ("Codex.Perception", "Скрытность"),
    ("Codex.VisionCones", "Скрытность"), ("Codex.DeathCam", "Камера и срез этажей"), ("Codex.Minimap", "Камера и срез этажей"),
    ("Codex.Corpse", "Тела и анимация"), ("Codex.DeathRagdoll", "Тела и анимация"), ("Codex.Anim", "Тела и анимация"),
    ("Codex.Tutorial", "Обучение"), ("Codex.HitchProbe", "Загрузка"),
]


def split_args(text: str, start: int) -> tuple[list[str], int]:
    """Top-level comma-separated arguments of the call whose '(' is at text[start]."""
    depth, i, cur, out = 0, start, [], []
    while i < len(text):
        c = text[i]
        if c == '"':
            j = i + 1
            while j < len(text) and text[j] != '"':
                j += 2 if text[j] == "\\" else 1
            cur.append(text[i:j + 1])
            i = j + 1
            continue
        if c == "/" and text[i:i + 2] == "//":
            j = text.find("\n", i)
            i = len(text) if j < 0 else j
            continue
        if c in "([{":
            depth += 1
            if depth == 1 and c == "(":
                i += 1
                continue
        elif c in ")]}":
            depth -= 1
            if depth == 0:
                out.append("".join(cur).strip())
                return out, i
        elif c == "," and depth == 1:
            out.append("".join(cur).strip())
            cur = []
            i += 1
            continue
        cur.append(c)
        i += 1
    return out, i


def strings_of(arg: str) -> str:
    parts = [(m.group(1) if m.group(1) is not None else m.group(2)) for m in STR.finditer(arg)]
    return "".join(parts).replace('\\"', '"').replace("\\n", " ").strip()


def scan_root(root: str) -> dict[str, dict]:
    found: dict[str, dict] = {}
    src = os.path.join(root, "Source")
    for dirpath, _dirs, files in os.walk(src):
        for name in files:
            if not name.endswith((".cpp", ".h")):
                continue
            path = os.path.join(dirpath, name)
            try:
                text = open(path, encoding="utf-8-sig", errors="replace").read()
            except OSError:
                continue
            for m in DECL.finditer(text):
                kind_key = re.sub(r"\s*<.*", "", m.group(1)).strip()
                args, _end = split_args(text, m.end() - 1)
                if not args:
                    continue
                cmd = strings_of(args[0])
                if not cmd or " " in cmd or "." not in cmd:
                    continue
                kind = KINDS.get(kind_key, "command")
                if kind == "variable" and kind_key == "TAutoConsoleVariable":
                    default, help_en = (args[1] if len(args) > 1 else ""), (strings_of(args[2]) if len(args) > 2 else "")
                elif kind == "variable":
                    default, help_en = "", (strings_of(args[2]) if len(args) > 2 else "")
                else:
                    default, help_en = "", (strings_of(args[1]) if len(args) > 1 else "")
                default = re.sub(r"\s+", " ", default).strip()
                if default.endswith("f") and re.match(r"^-?[0-9.]+f$", default):
                    default = default[:-1]
                rel = os.path.relpath(path, root).replace("\\", "/")
                found.setdefault(cmd, {"name": cmd, "kind": kind, "default": default, "help_en": help_en, "file": rel})
    return found


def parse_doc(path: str) -> dict[str, dict]:
    """Rows of the designer doc: | `Name args` | what it does | values | where |."""
    out: dict[str, dict] = {}
    if not path or not os.path.exists(path):
        return out
    section = ""
    for line in open(path, encoding="utf-8"):
        if line.startswith("## "):
            section = line[3:].strip()
            continue
        if not line.startswith("| `"):
            continue
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        names = re.findall(r"`([A-Za-z][A-Za-z0-9_.]*)", cells[0])
        if not names or len(cells) < 2:
            continue
        values = cells[2] if len(cells) > 3 else ""
        for n in names:
            out.setdefault(n, {"help_ru": cells[1], "values_ru": values, "doc_section": section, "usage": cells[0].replace("`", "")})
    return out


def section_of(name: str, kind: str, doc_section: str) -> str:
    if kind == "command" and name.lower().endswith("smoke"):
        return "Проверки (смоуки)"
    if doc_section and not doc_section.startswith("Главное") and not doc_section.startswith("Как"):
        return doc_section
    for prefix, sec in SECTIONS:
        if name.startswith(prefix):
            return sec
    if name.startswith("CodexTactics.") and re.search(r"(Shots?|Probe|Recorder|Recapture|Check)$", name):
        return "Снимки и замеры (разработчик)"
    if name.startswith("CodexTactics."):
        return "Команды разработчика"
    return "Прочее"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--root", action="append", required=True, help="label=path; the first is the reference")
    ap.add_argument("--doc", default="")
    a = ap.parse_args()
    roots = [r.split("=", 1) for r in a.root]
    scans = [(label, scan_root(path)) for label, path in roots]
    doc_path = a.doc or next((os.path.join(p, "docs/design/CONSOLE_COMMANDS.md") for _l, p in roots
                              if os.path.exists(os.path.join(p, "docs/design/CONSOLE_COMMANDS.md"))), "")
    doc = parse_doc(doc_path)
    ref_label, ref = scans[0]
    merged: dict[str, dict] = {}
    for label, found in scans:
        for name, rec in found.items():
            if name not in merged:
                merged[name] = dict(rec)
                merged[name]["where"] = ref_label if name in ref else "только " + label
    rows = []
    for name, rec in sorted(merged.items(), key=lambda kv: kv[0].lower()):
        d = doc.get(name, {})
        rec["help_ru"] = d.get("help_ru", "")
        rec["values_ru"] = d.get("values_ru", "")
        rec["usage"] = d.get("usage", "")
        rec["section"] = section_of(name, rec["kind"], d.get("doc_section", ""))
        rows.append(rec)
    os.makedirs(a.out, exist_ok=True)
    stamp = _dt.datetime.now().strftime("%Y-%m-%d %H:%M")
    meta = {"generated": stamp, "roots": [l for l, _p in roots], "count": len(rows),
            "doc": os.path.basename(doc_path) if doc_path else ""}
    with open(os.path.join(a.out, "commands.json"), "w", encoding="utf-8", newline="\n") as f:
        f.write("{\n\"meta\": " + json.dumps(meta, ensure_ascii=False) + ",\n\"rows\": [\n")
        f.write(",\n".join(json.dumps(r, ensure_ascii=False) for r in rows))
        f.write("\n]}\n")
    page = PAGE.replace("__STAMP__", stamp).replace("__COUNT__", str(len(rows)))
    with open(os.path.join(a.out, "index.html"), "w", encoding="utf-8", newline="\n") as f:
        f.write(page)
    smokes = sum(1 for r in rows if r["section"] == "Проверки (смоуки)")
    only = sum(1 for r in rows if r["where"].startswith("только"))
    print(f"{len(rows)} commands ({smokes} smoke checks, {only} only on a branch, {sum(1 for r in rows if r['help_ru'])} with Russian text) -> {a.out}")
    return 0


PAGE = r"""<title>Консольные команды</title>
<meta name="claude:viewport-fit-sides" content="cover">
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=IBM+Plex+Sans:wght@400;500;600&family=IBM+Plex+Mono:wght@400;500&display=swap">
<style>
/* Layout: a console reference - a sticky search bar over section-grouped rows; the command in mono is the anchor of each row. */
:root {
  --bg: #f4f6f5; --panel: #ffffff; --fg: #1b2420; --muted: #5d6b65; --line: #d9e0dc;
  --accent: #1f6f5c; --accent-soft: #e2f1ec; --warn: #8a5a00; --warn-soft: #fbf0d9;
  --mono: "IBM Plex Mono", ui-monospace, Consolas, monospace; --sans: "IBM Plex Sans", system-ui, "Segoe UI", sans-serif;
}
@media (prefers-color-scheme: dark) { :root:not([data-theme="light"]) {
  --bg: #121715; --panel: #1a211e; --fg: #e3ebe7; --muted: #94a39c; --line: #2b3531;
  --accent: #5cc3a6; --accent-soft: #1d3a32; --warn: #e8b85a; --warn-soft: #3a2f17; color-scheme: dark } }
:root[data-theme="dark"] {
  --bg: #121715; --panel: #1a211e; --fg: #e3ebe7; --muted: #94a39c; --line: #2b3531;
  --accent: #5cc3a6; --accent-soft: #1d3a32; --warn: #e8b85a; --warn-soft: #3a2f17; color-scheme: dark }
body { background: var(--bg); color: var(--fg); font-family: var(--sans); font-size: 14px; line-height: 1.45; }
.wrap { max-width: 1180px; margin: 0 auto; padding-block: 20px 48px; padding-left: max(16px, env(safe-area-inset-left, 0px)); padding-right: max(16px, env(safe-area-inset-right, 0px)); }
h1 { font-size: 22px; font-weight: 600; margin: 0 0 4px; text-wrap: balance; }
.sub { color: var(--muted); margin: 0 0 14px; }
.howto { display: grid; gap: 4px; background: var(--panel); border: 1px solid var(--line); border-radius: 8px; padding: 12px 14px; margin-bottom: 14px; color: var(--muted); }
.howto b { color: var(--fg); font-weight: 500; }
kbd, code { font-family: var(--mono); font-size: 12.5px; background: var(--accent-soft); color: var(--fg); padding: 1px 5px; border-radius: 4px; }
body::before { content: ""; position: fixed; inset: 0 0 auto; height: env(safe-area-inset-top, 0px); z-index: 10; background: var(--bg); }
.bar { position: sticky; top: env(safe-area-inset-top, 0px); z-index: 5; background: var(--bg); padding-block: 10px; display: flex; flex-wrap: wrap; gap: 8px; align-items: center; border-bottom: 1px solid var(--line); }
#q { flex: 1 1 260px; min-width: 0; font: inherit; padding: 9px 12px; border: 1px solid var(--line); border-radius: 6px; background: var(--panel); color: var(--fg); }
#q:focus-visible, button:focus-visible, select:focus-visible { outline: 2px solid var(--accent); outline-offset: 1px; }
select, .toggle { font: inherit; padding: 8px 10px; border: 1px solid var(--line); border-radius: 6px; background: var(--panel); color: var(--fg); }
.toggle { display: inline-flex; gap: 6px; align-items: center; cursor: pointer; }
.count { color: var(--muted); font-variant-numeric: tabular-nums; }
section { margin-top: 22px; }
h2 { font-size: 13px; text-transform: uppercase; letter-spacing: .06em; color: var(--muted); font-weight: 600; margin: 0 0 8px; }
.rows { display: grid; gap: 6px; }
.row { display: grid; grid-template-columns: minmax(0, 330px) minmax(0, 1fr) minmax(0, 170px); gap: 6px 14px; background: var(--panel); border: 1px solid var(--line); border-radius: 6px; padding: 9px 12px; }
.cmd { display: flex; gap: 6px; align-items: flex-start; min-width: 0; }
.cmd code { background: none; padding: 0; font-weight: 500; font-size: 13px; word-break: break-all; }
.copy { flex: none; font: 11px var(--sans); border: 1px solid var(--line); background: var(--bg); color: var(--muted); border-radius: 4px; padding: 1px 6px; cursor: pointer; }
.copy:hover { color: var(--accent); border-color: var(--accent); }
.what { min-width: 0; }
.what .en { color: var(--muted); font-size: 12.5px; }
.meta { min-width: 0; display: grid; gap: 3px; justify-items: start; font-size: 12.5px; color: var(--muted); }
.pill { font-size: 11.5px; padding: 1px 7px; border-radius: 99px; background: var(--accent-soft); color: var(--accent); white-space: nowrap; }
.pill.branch { background: var(--warn-soft); color: var(--warn); }
.def { font-family: var(--mono); font-size: 12px; overflow-wrap: anywhere; }
.file { font-family: var(--mono); font-size: 11px; color: var(--muted); grid-column: 1 / -1; overflow-wrap: anywhere; }
.empty { color: var(--muted); padding: 24px 0; }
@media (max-width: 760px) { .row { grid-template-columns: minmax(0, 1fr); } }
@media (prefers-reduced-motion: reduce) { * { transition: none !important; } }
</style>
<div class="wrap">
  <h1>Консольные команды CodexTactics</h1>
  <p class="sub">Собрано из кода __STAMP__ · <span id="total">__COUNT__</span> команд и переменных · обновляется после каждого влития</p>
  <div class="howto">
    <div><b>Как вводить:</b> консоль — клавиша <kbd>~</kbd>. Переменная: <code>Имя значение</code> (например <code>Codex.Doors.Openable 1</code>); <code>Имя</code> без значения печатает текущее.</div>
    <div><b>Значения не сохраняются</b> после перезапуска. Обычно <code>1</code> — новое поведение, <code>0</code> — старое (для сравнения), <code>-1</code> — «как в JSON-данных».</div>
    <div><b>«Где есть»:</b> <span class="pill">main</span> — в обычной игре; <span class="pill branch">только …</span> — пока только в названной ветке (например в CodexTactics-agents).</div>
  </div>
  <div class="bar">
    <input id="q" type="search" placeholder="Поиск: имя или слово из описания (двери, срез, строй…)" autocomplete="off">
    <select id="sec" aria-label="Раздел"><option value="">Все разделы</option></select>
    <label class="toggle"><input id="smk" type="checkbox"> проверки (смоуки)</label>
    <span class="count" id="shown"></span>
  </div>
  <div id="list"><p class="empty">Загрузка списка…</p></div>
</div>
<script>
(function () {
  const list = document.getElementById('list'), q = document.getElementById('q'), sec = document.getElementById('sec'),
        smk = document.getElementById('smk'), shown = document.getElementById('shown');
  const SMOKE = 'Проверки (смоуки)';
  const ORDER = ['Двери и здания', 'Строй и отряд', 'Камера и срез этажей', 'Перелезание и лестницы', 'Враги / ИИ', 'Скрытность',
    'Бой и стрельба', 'Погода и окружение', 'Повторяемость и логи', 'Отладка и подписи', 'Тела и анимация', 'Обучение', 'Бот', 'Загрузка', 'Для проверок',
    'Команды разработчика', 'Снимки и замеры (разработчик)', 'Прочее', SMOKE];
  let rows = [];
  const esc = s => String(s || '').replace(/[&<>"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
  function render() {
    const t = q.value.trim().toLowerCase(), s = sec.value, withSmoke = smk.checked || s === SMOKE;
    const hit = rows.filter(r => (withSmoke || r.section !== SMOKE) && (!s || r.section === s)
      && (!t || (r.name + ' ' + r.help_ru + ' ' + r.help_en + ' ' + r.usage).toLowerCase().includes(t)));
    shown.textContent = hit.length + ' из ' + rows.length;
    if (!hit.length) { list.innerHTML = '<p class="empty">Ничего не найдено. Попробуйте другое слово или включите «проверки».</p>'; return; }
    const groups = {};
    hit.forEach(r => (groups[r.section] = groups[r.section] || []).push(r));
    const names = Object.keys(groups).sort((a, b) => (ORDER.indexOf(a) + 99 * (ORDER.indexOf(a) < 0)) - (ORDER.indexOf(b) + 99 * (ORDER.indexOf(b) < 0)));
    list.innerHTML = names.map(g => '<section><h2>' + esc(g) + ' · ' + groups[g].length + '</h2><div class="rows">' + groups[g].map(r => {
      const what = r.help_ru ? '<div>' + esc(r.help_ru) + '</div>' + (r.help_en ? '<div class="en">' + esc(r.help_en) + '</div>' : '')
        : '<div>' + (esc(r.help_en) || '<span class="en">(нет описания в коде)</span>') + '</div>';
      const def = r.values_ru ? esc(r.values_ru) : (r.default !== '' ? 'по умолч. <span class="def">' + esc(r.default) + '</span>' : '');
      return '<div class="row"><div class="cmd"><code>' + esc(r.usage || r.name) + '</code><button class="copy" data-c="' + esc(r.name) + '" type="button">копировать</button></div>'
        + '<div class="what">' + what + '</div><div class="meta"><span class="pill' + (r.where.startsWith('только') ? ' branch' : '') + '">' + esc(r.where) + '</span>'
        + (def ? '<span>' + def + '</span>' : '') + '<span>' + (r.kind === 'variable' ? 'переменная' : 'команда') + '</span></div>'
        + '<div class="file">' + esc(r.file) + '</div></div>';
    }).join('') + '</div></section>').join('');
  }
  list.addEventListener('click', e => {
    const b = e.target.closest('.copy'); if (!b) return;
    const done = () => { b.textContent = 'скопировано'; setTimeout(() => (b.textContent = 'копировать'), 1200); };
    try { navigator.clipboard.writeText(b.dataset.c).then(done, () => { b.textContent = b.dataset.c; }); } catch (_) { b.textContent = b.dataset.c; }
  });
  try { const saved = JSON.parse(localStorage.getItem('cmdsFilter') || '{}'); q.value = saved.q || ''; smk.checked = !!saved.smk; } catch (_) {}
  const save = () => { try { localStorage.setItem('cmdsFilter', JSON.stringify({ q: q.value, smk: smk.checked })); } catch (_) {} };
  [q, sec, smk].forEach(el => el.addEventListener('input', () => { save(); render(); }));
  fetch('commands.json').then(r => r.json()).then(d => {
    rows = d.rows;
    const secs = [...new Set(rows.map(r => r.section))].sort((a, b) => ORDER.indexOf(a) - ORDER.indexOf(b));
    sec.insertAdjacentHTML('beforeend', secs.map(s => '<option value="' + esc(s) + '">' + esc(s) + '</option>').join(''));
    render();
  }).catch(() => { list.innerHTML = '<p class="empty">Не удалось загрузить commands.json.</p>'; });
})();
</script>
"""

if __name__ == "__main__":
    sys.exit(main())
