// Training Ground department «Перемещение»: replay the squad simulator on a layout route (in the browser, 0 tokens), the run's
// checks, the movement bot (Node, report.md) and the tuner (npm run train; real model calls need the key on this PC).
// Red-team layouts (Content/Data/FormationLab/redteam, FORMATION_CONTRACT.md 11) are a group of their own in the picker: the
// failure comment is shown next to the view and the first failing run (route, members, formation, seed) is preselected.
import React, { useEffect, useMemo, useRef, useState } from 'react';
import { Bot, Pause, Play, RotateCcw, Sparkles } from 'lucide-react';
import { SquadView } from './SquadView';
import type { Layout } from '../movement/layout';
import { FormationMenuRules, MENU_ITEMS, menuLabel } from '../movement/menu';
import type { MenuChoice } from '../movement/menu';
import type { FormationConfigFile } from '../movement/config';
import { configValues } from '../movement/config';
import type { Frame } from '../movement/sim';
import { buildNav, simulate } from '../movement/sim';
import { RunCollector } from '../movement/checks';
import { guardHudText } from '../movement/labels';
import type { RunMetrics } from '../movement/checks';
import type { RedteamFailureView, RedteamView } from '../movement/redteamView';
import { groupLayoutNames, redteamView, seekFor } from '../movement/redteamView';

const BYPASS_RU: Record<string, string> = { shortest: 'кратчайший путь', shielded: 'укрыт от угрозы', too_long: 'укрытый путь слишком длинный' };
const ROLE_RU: Record<string, string> = { K: 'командир', P: 'правый', L: 'левый', Z: 'замыкающий' };
const FORMATION_RU: Record<string, string> = {
  diamond: 'Ромб', wedge: 'Клин', column: 'Колонна-ёлочка', line: 'Цепь', echelon_left: 'Эшелон влево',
  echelon_right: 'Эшелон вправо', wall: 'У стены', hedgehog: 'Ёж', blizzard: 'Метельная сцепка',
};
const CHECK_RU: Record<number, string> = {
  1: 'Взгляд в стену', 2: 'Тыл 5–7 ч.', 3: 'Мерцание', 4: 'Построение по зонам', 5: 'Слоты', 6: 'Темп замыкающего',
  7: 'Отряд разрезан стеной', 8: 'Обход с укрытой стороны', 9: 'Проверка угла',
};
const MODE_RU: Record<string, string> = { walk: 'шаг (патруль)', run: 'бег', halt: 'остановка', wall: 'у стены', forward: 'шаг, все вперёд' };

const Panel: React.FC<{ title: string; children: React.ReactNode; right?: React.ReactNode }> = ({ title, children, right }) => (
  <section className="bg-slate-900 border border-slate-800 rounded-2xl p-4 flex flex-col gap-3 min-w-0">
    <div className="flex items-center justify-between gap-2">
      <h3 className="text-xs font-bold tracking-widest uppercase text-slate-400">{title}</h3>
      {right}
    </div>
    {children}
  </section>
);

const Field: React.FC<{ label: string; children: React.ReactNode }> = ({ label, children }) => (
  <label className="flex flex-col gap-1 text-xs text-slate-400">
    <span>{label}</span>
    {children}
  </label>
);

const inputCls = 'bg-slate-950 border border-slate-800 rounded-lg px-2.5 py-1.5 text-slate-100 text-sm focus:outline-none focus:border-cyan-500';
const btnCls = 'inline-flex items-center gap-1.5 px-3 py-1.5 rounded-lg text-sm font-semibold border transition-colors disabled:opacity-50';

export const MovementDepartment: React.FC = () => {
  const [layouts, setLayouts] = useState<string[]>([]);
  const [layoutName, setLayoutName] = useState('sample_small');
  const [layout, setLayout] = useState<Layout | null>(null);
  const [configFile, setConfigFile] = useState<FormationConfigFile | null>(null);
  const [routeId, setRouteId] = useState('');
  const [members, setMembers] = useState(4);
  const [seed, setSeed] = useState(1);
  // '' = Auto; 'all_forward' = the menu's "All forward" (v1.2: walking, everyone looks at 12, formation stays automatic).
  // The hold-G menu's choice (same item list and rules as the game's radial menu, menu.ts).
  const [manual, setManual] = useState<MenuChoice>('auto');
  const [speed, setSpeed] = useState(1);
  const [follow, setFollow] = useState(true);
  const [blizzard, setBlizzard] = useState(false);
  const [playing, setPlaying] = useState(true);
  const [t, setT] = useState(0);
  const [error, setError] = useState('');
  const [botMembers, setBotMembers] = useState('2,3,4');
  const [botSeeds, setBotSeeds] = useState(4);
  const [botReport, setBotReport] = useState('');
  const [trainReport, setTrainReport] = useState('');
  const [busy, setBusy] = useState('');
  const [trainClient, setTrainClient] = useState('mock');
  const [budget, setBudget] = useState(5);
  const [rounds, setRounds] = useState(10);
  const [dryRun, setDryRun] = useState(true);
  const [rtView, setRtView] = useState<RedteamView | null>(null);
  // A red-team failure picked: the replay starts 2 s before it once the run is simulated.
  const pendingSeek = useRef<number | null>(null);

  const showFailure = (f: RedteamFailureView) => {
    setRouteId(f.route);
    setMembers(f.members);
    setManual(f.choice);
    setSeed(f.seed);
    pendingSeek.current = seekFor(f);
    setT(seekFor(f));
    setPlaying(true);
  };

  useEffect(() => {
    fetch('/api/movement/layouts').then((r) => r.json()).then((d) => setLayouts(d.layouts ?? [])).catch(() => setError('Нет связи с сервером Training Ground'));
    fetch('/api/movement/config').then((r) => r.json()).then(setConfigFile).catch(() => {});
    fetch('/api/movement/report?kind=bot').then((r) => r.json()).then((d) => setBotReport(d.text ?? '')).catch(() => {});
    fetch('/api/movement/report?kind=train').then((r) => r.json()).then((d) => setTrainReport(d.text ?? '')).catch(() => {});
  }, []);

  useEffect(() => {
    fetch(`/api/movement/layout?name=${encodeURIComponent(layoutName)}`).then((r) => r.json()).then((d: Layout) => {
      if ((d as unknown as { error?: string }).error) { setError((d as unknown as { error: string }).error); return; }
      setLayout(d);
      const view = redteamView(d);
      setRtView(view);
      if (view?.failures.length) showFailure(view.failures[0]);
      else setRouteId(d.routes[0]?.id ?? '');
    }).catch(() => setError(`Не удалось загрузить ${layoutName}`));
  }, [layoutName]);

  const nav = useMemo(() => (layout ? buildNav(layout) : null), [layout]);
  const config = useMemo(() => (configFile ? configValues(configFile) : null), [configFile]);

  // The whole run is simulated once per selection, then replayed.
  const { frames, metrics } = useMemo(() => {
    const route = layout?.routes.find((r) => r.id === routeId);
    if (!layout || !route || !config || !nav) return { frames: [] as Frame[], metrics: null as RunMetrics | null };
    const out: Frame[] = [];
    const collector = new RunCollector(layout, route, members, seed, config);
    simulate({ layout, route, memberCount: members, config, seed, nav,
      manualFormation: FormationMenuRules.Resolve(manual, members).manualFormation ?? undefined,
      allForward: FormationMenuRules.Resolve(manual, members).allForward, blizzard,
      onFrame: (f) => { out.push(f); collector.onFrame(f); } });
    return { frames: out, metrics: collector.finish() };
  }, [layout, routeId, config, nav, members, seed, manual, blizzard]);

  useEffect(() => { setT(pendingSeek.current ?? 0); pendingSeek.current = null; }, [frames]);
  const last = useRef(0);
  useEffect(() => {
    if (!playing || !frames.length) return;
    let raf = 0;
    last.current = performance.now();
    const tick = (now: number) => {
      const dt = Math.min(0.1, (now - last.current) / 1000);
      last.current = now;
      setT((v) => {
        const end = frames[frames.length - 1].t;
        return v + dt * speed > end ? end : v + dt * speed;
      });
      raf = requestAnimationFrame(tick);
    };
    raf = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(raf);
  }, [playing, frames, speed]);

  const frame = frames.length ? frames[Math.min(frames.length - 1, Math.round(t * 30))] : null;

  const runBot = async () => {
    setBusy('bot');
    try {
      const r = await fetch('/api/movement/run-bot', { method: 'POST', headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ layout: layoutName, members: botMembers, seeds: botSeeds }) });
      const d = await r.json();
      setBotReport(d.text || d.error || d.log || 'нет ответа');
    } catch { setBotReport('Сервер Training Ground недоступен'); }
    setBusy('');
  };

  const runTrain = async () => {
    setBusy('train');
    try {
      const r = await fetch('/api/movement/train', { method: 'POST', headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ layout: layoutName, client: trainClient, budgetUsd: budget, rounds, dryRun }) });
      const d = await r.json();
      setTrainReport(d.text || d.error || d.log || 'нет ответа');
    } catch { setTrainReport('Сервер Training Ground недоступен'); }
    setBusy('');
  };

  const fov = config?.['sight.fov'] ?? 140;
  const checks: [string, string, boolean | null][] = metrics ? [
    ['Взгляд в стену', `${metrics.wallStare.events} эпизодов`, metrics.wallStare.events === 0],
    ['Тыл 5–7 ч.', metrics.rearCover ? `${metrics.rearCover.pct} %` : '—', metrics.members >= 3 && metrics.rearCover ? metrics.rearCover.pct >= 90 : null],
    ['Мерцание', `${metrics.flicker.unexpected} / ${metrics.flicker.changes} смен`, metrics.flicker.per100m <= 1],
    ['Построение по зонам', metrics.expect.length ? metrics.expect.map((e) => `${e.zone} ${e.pct}%`).join(', ') : '—', metrics.expect.length ? metrics.expect.every((e) => e.pct >= 80) : null],
    ['Слоты', `${metrics.slots.invalidTicks} недоступных, ${metrics.slots.spacingEvents} теснота`, metrics.slots.invalidTicks === 0 && metrics.slots.spacingEvents === 0],
    ['Темп замыкающего', metrics.pace ? `p95 ${metrics.pace.p95} см` : '—', metrics.pace ? metrics.pace.p95 <= 150 : null],
    ['Отряд не разрезан стеной', `${metrics.split.events} эпизодов`, metrics.split.events === 0],
    ['Обход с укрытой стороны', metrics.bypass.length ? metrics.bypass.map((b) => `${b.wall}: стена ${b.got === 'left' ? 'слева' : 'справа'}`).join(', ') : '—',
      metrics.bypass.length ? metrics.bypass.every((b) => b.got === b.want) : null],
    ['Обзор (самая большая открытая дуга)', metrics.cover ? `${metrics.cover.pct} % патруля в норме` : '—', metrics.cover ? metrics.cover.pct >= 90 : null],
    ['Проверка угла', metrics.corner.ends ? `${metrics.corner.ends - metrics.corner.missed} / ${metrics.corner.ends} концов стен` : '—', metrics.corner.ends ? metrics.corner.missed === 0 : null],
  ] : [];

  return (
    <div className="p-6 grid gap-6 xl:grid-cols-[minmax(0,1fr)_420px]">
      <div className="flex flex-col gap-4 min-w-0">
        {error && <div className="text-sm text-amber-300 bg-amber-950/40 border border-amber-800 rounded-lg px-3 py-2">{error}</div>}
        <Panel title="Прогон в симуляторе" right={
          <div className="flex items-center gap-2">
            <button className={`${btnCls} border-slate-700 bg-slate-800 text-slate-100 hover:bg-slate-700`} onClick={() => setPlaying((p) => !p)}>
              {playing ? <Pause size={14} /> : <Play size={14} />}{playing ? 'Пауза' : 'Играть'}
            </button>
            <button className={`${btnCls} border-slate-700 bg-slate-800 text-slate-100 hover:bg-slate-700`} onClick={() => { setT(0); setPlaying(true); }}>
              <RotateCcw size={14} />Сначала
            </button>
          </div>
        }>
          <div className="flex flex-wrap gap-3">
            <Field label="Раскладка">
              <select className={inputCls} value={layoutName} onChange={(e) => setLayoutName(e.target.value)}>
                {groupLayoutNames(layouts.length ? layouts : [layoutName]).map((g) => (
                  <optgroup key={g.label} label={g.label}>
                    {g.names.map((n) => <option key={n} value={n}>{n}</option>)}
                  </optgroup>
                ))}
              </select>
            </Field>
            <Field label="Маршрут">
              <select className={inputCls} value={routeId} onChange={(e) => setRouteId(e.target.value)}>
                {layout?.routes.map((r) => <option key={r.id} value={r.id}>{r.id} ({r.pace === 'run' ? 'бег' : 'шаг'})</option>)}
              </select>
            </Field>
            <Field label="Бойцов">
              <select className={inputCls} value={members} onChange={(e) => setMembers(Number(e.target.value))}>
                {[2, 3, 4].map((n) => <option key={n} value={n}>{n}</option>)}
              </select>
            </Field>
            <Field label="Построение">
              <select className={inputCls} value={manual} onChange={(e) => setManual(e.target.value as MenuChoice)}>
                {(['auto', ...MENU_ITEMS.map((it) => it.id)] as MenuChoice[]).map((id) => (
                  <option key={id} value={id} disabled={id !== 'auto' && !FormationMenuRules.IsEnabled(id, members)}>
                    {id === 'auto' ? 'Авто (по местности)' : `${menuLabel(id, members)} (вручную)`}
                  </option>
                ))}
              </select>
            </Field>
            <Field label="Seed">
              <input className={`${inputCls} w-20`} type="number" min={1} value={seed} onChange={(e) => setSeed(Math.max(1, Number(e.target.value) || 1))} />
            </Field>
            <Field label="Погода">
              <select className={inputCls} value={blizzard ? 'blizzard' : 'clear'} onChange={(e) => setBlizzard(e.target.value === 'blizzard')}>
                <option value="clear">Ясно</option>
                <option value="blizzard">Метель</option>
              </select>
            </Field>
            <Field label="Камера">
              <select className={inputCls} value={follow ? 'follow' : 'all'} onChange={(e) => setFollow(e.target.value === 'follow')}>
                <option value="follow">За отрядом</option>
                <option value="all">Весь уровень</option>
              </select>
            </Field>
            <Field label="Скорость">
              <select className={inputCls} value={speed} onChange={(e) => setSpeed(Number(e.target.value))}>
                {[0.5, 1, 2, 4].map((v) => <option key={v} value={v}>×{v}</option>)}
              </select>
            </Field>
          </div>
          {rtView && (
            <div className="text-xs bg-rose-950/30 border border-rose-900 rounded-lg px-3 py-2 flex flex-col gap-1.5">
              <span className="text-rose-200"><b>Red team{rtView.family ? ` · ${rtView.family}` : ''}:</b> {rtView.comment || 'без комментария'}</span>
              <span className="text-slate-400">Известные падения (expected_fail) — клик показывает прогон с 2 с до падения:</span>
              <div className="flex flex-wrap gap-1.5">
                {rtView.failures.map((f, i) => {
                  const on = f.route === routeId && f.members === members && f.choice === manual && f.seed === seed;
                  return (
                    <button key={i} onClick={() => showFailure(f)} title={f.what}
                      className={`px-2 py-1 rounded border font-mono ${on ? 'border-rose-500 bg-rose-900/50 text-rose-100' : 'border-slate-700 bg-slate-900 text-slate-300 hover:bg-slate-800'}`}>
                      #{f.check} {CHECK_RU[f.check] ?? ''} · {f.choice === 'auto' ? 'авто' : menuLabel(f.choice, f.members)} · {f.members} · {f.route} s{f.seed}{f.t !== null ? ` · ${f.t} с` : ''}
                    </button>
                  );
                })}
              </div>
            </div>
          )}
          <div className="relative">
            <SquadView layout={layout} frame={frame} routeId={routeId} fov={fov} height={540}
              focus={follow && frame ? frame.members[0].pos : null} span={2600} />
            {frame && (
              <div className="absolute top-3 left-3 bg-slate-950/85 border border-slate-800 rounded-lg px-3 py-2 text-xs font-mono text-slate-200 flex flex-col gap-0.5">
                <span>t = {frame.t.toFixed(1)} с · {MODE_RU[frame.mode]}</span>
                {/* v2.1: the requested formation and, while a wall cuts it, the one in effect ("Эшелон влево -> колонна (стена)"). */}
                <span>построение: <b className="text-cyan-300">{FORMATION_RU[frame.requested]}</b>
                  {frame.formation !== frame.requested && <> → <b className="text-amber-300">{FORMATION_RU[frame.formation]}</b> ({frame.guardCause === 'turn' ? 'поворот' : 'стена'}{frame.guardLevel === 2 ? ', по следу' : ''})</>}
                  {manual === 'all_forward' ? ' · все вперёд' : manual !== 'auto' ? ' (вручную)' : ''}</span>
                <span className="text-slate-400">в игре: {guardHudText(frame.requested, frame.formation, frame.guardLevel, frame.members.length, frame.guardCause)}</span>
                <span>местность: {frame.terrain}</span>
                <span>пройдено: {(frame.leaderDistance / 100).toFixed(1)} м</span>
                {frame.bypasses.map((b, i) => (
                  <span key={i}>обход {b.wall}: стена {b.wallOn === 'left' ? 'слева' : 'справа'} · {BYPASS_RU[b.reason]}{b.detour > 0 ? ` (+${Math.round(b.detour * 100)} %)` : ''}</span>
                ))}
                {frame.members.some((m) => m.cornerWatch) && <span>угол проверяет: {frame.members.filter((m) => m.cornerWatch).map((m) => ROLE_RU[m.role]).join(', ')}</span>}
              </div>
            )}
          </div>
          <input type="range" min={0} max={frames.length ? frames[frames.length - 1].t : 0} step={1 / 30} value={t}
            onChange={(e) => { setPlaying(false); setT(Number(e.target.value)); }} aria-label="Время прогона" />
          <p className="text-xs text-slate-500">К — командир, П — правый, Л — левый, З — замыкающий. Белое кольцо: боец идёт задом. Жёлтое кольцо: проверяет угол. Пунктирное фиолетовое: замыкающий (держит тыл). Крестик: его слот (оранжевый — запасной, слот был в стене). Красный ромб: известная угроза.</p>
        </Panel>
      </div>

      <div className="flex flex-col gap-4 min-w-0">
        <Panel title="Проверки этого прогона">
          <table className="text-xs w-full">
            <tbody>
              {checks.map(([name, value, ok]) => (
                <tr key={name} className="border-b border-slate-800 last:border-0">
                  <td className="py-1.5 pr-2 text-slate-300">{name}</td>
                  <td className="py-1.5 pr-2 font-mono text-slate-400">{value}</td>
                  <td className={`py-1.5 font-bold ${ok === null ? 'text-slate-500' : ok ? 'text-emerald-400' : 'text-rose-400'}`}>{ok === null ? '—' : ok ? 'OK' : 'FAIL'}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </Panel>

        <Panel title="Бот перемещения" right={
          <button className={`${btnCls} border-cyan-700 bg-cyan-600 text-slate-950 hover:bg-cyan-500`} disabled={!!busy} onClick={runBot}>
            <Bot size={14} />{busy === 'bot' ? 'Идёт…' : 'Запустить'}
          </button>
        }>
          <div className="flex gap-3">
            <Field label="Бойцов (через запятую)"><input className={`${inputCls} w-28`} value={botMembers} onChange={(e) => setBotMembers(e.target.value)} /></Field>
            <Field label="Seeds"><input className={`${inputCls} w-20`} type="number" min={1} max={50} value={botSeeds} onChange={(e) => setBotSeeds(Number(e.target.value) || 1)} /></Field>
          </div>
          <pre className="text-[11px] leading-snug bg-slate-950 border border-slate-800 rounded-lg p-3 overflow-x-auto whitespace-pre text-slate-300 max-h-80">{botReport || 'Отчёта ещё нет.'}</pre>
        </Panel>

        <Panel title="Обучение (тюнер)" right={
          <button className={`${btnCls} border-violet-700 bg-violet-600 text-white hover:bg-violet-500`} disabled={!!busy} onClick={runTrain}>
            <Sparkles size={14} />{busy === 'train' ? 'Идёт…' : dryRun ? 'Пробный запуск' : 'Обучать'}
          </button>
        }>
          <div className="flex flex-wrap gap-3 items-end">
            <Field label="Модель">
              <select className={inputCls} value={trainClient} onChange={(e) => setTrainClient(e.target.value)}>
                <option value="mock">mock (без сети)</option>
                <option value="claude">Claude API</option>
                <option value="jev">Jev</option>
              </select>
            </Field>
            <Field label="Бюджет, $"><input className={`${inputCls} w-20`} type="number" min={0} max={50} step={0.5} value={budget} onChange={(e) => setBudget(Number(e.target.value) || 0)} /></Field>
            <Field label="Раундов"><input className={`${inputCls} w-20`} type="number" min={1} max={50} value={rounds} onChange={(e) => setRounds(Number(e.target.value) || 1)} /></Field>
            <label className="flex items-center gap-2 text-xs text-slate-300 pb-2"><input type="checkbox" checked={dryRun} onChange={(e) => setDryRun(e.target.checked)} />без вызова API</label>
          </div>
          <p className="text-xs text-slate-500">Прогоны бесплатные (симулятор). Платные только предложения модели: ключ берётся из окружения этого компьютера, расход не превышает бюджет.</p>
          <pre className="text-[11px] leading-snug bg-slate-950 border border-slate-800 rounded-lg p-3 overflow-x-auto whitespace-pre text-slate-300 max-h-80">{trainReport || 'Обучение ещё не запускалось.'}</pre>
        </Panel>
      </div>
    </div>
  );
};
