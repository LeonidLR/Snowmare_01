import { useEffect, useLayoutEffect, useRef, useState } from 'react';
import { LayoutView } from './LayoutView';
import { planTransform, toScreen } from '../levelgen/planView';
import { X6 } from '../aiTraining/trainingTypes';
import type { TrainingData, TrainingMapId, TrainingSector, TrainingSelection } from '../aiTraining/trainingTypes';

const API = '/api/ai-training';
const panel = 'rounded-xl border border-slate-800 bg-slate-900/70';
const field = 'rounded-md border border-slate-700 bg-slate-950 px-3 py-2 text-sm text-slate-100 focus:ring-2 focus:ring-cyan-400';
const postures: Record<string, string> = { defend: 'Оборона', hunt: 'Поиск', patrol: 'Патруль' };
const label = (id: number) => id === 0 ? 'Вся карта' : 'Сектор ' + id;

function SectorPlan({ data, selected, choose }: { data: TrainingData; selected: number; choose: (id: number) => void }) {
  const ref = useRef<HTMLDivElement>(null);
  const [size, setSize] = useState([0, 0]);
  useLayoutEffect(() => {
    const node = ref.current;
    if (!node) return;
    const measure = () => setSize([node.clientWidth, node.clientHeight]);
    measure();
    const resize = new ResizeObserver(measure);
    resize.observe(node);
    return () => resize.disconnect();
  }, []);
  if (!data.layout) return <p className="p-5 text-sm text-slate-400">План X6 недоступен. Сектор можно выбрать в списке.</p>;
  const transform = size.every((n) => n > 20) ? planTransform(data.layout.bounds, size[0], size[1]) : null;
  return (
    <>
      <div className="relative">
        <LayoutView layout={data.layout} height="clamp(440px, 64vh, 680px)" hideObjectives />
        <div ref={ref} className="absolute inset-0 overflow-hidden rounded-xl" aria-label="План секторов Депо X6">
          {transform && data.sectors.map(({ id, box_cm }) => {
            const [x1, y1] = toScreen(transform, box_cm.min);
            const [x2, y2] = toScreen(transform, box_cm.max);
            return (
              <button key={id} type="button" onClick={() => choose(id)} aria-pressed={id === selected} aria-label={label(id)}
                style={{ left: Math.min(x1, x2), top: Math.min(y1, y2), width: Math.abs(x2 - x1), height: Math.abs(y2 - y1) }}
                className={'absolute grid place-items-center border-2 outline-none focus-visible:z-10 focus-visible:border-white ' +
                  (id === selected ? 'border-cyan-300 bg-cyan-400/20' : 'border-slate-300/50 bg-slate-950/15 hover:border-cyan-200 hover:bg-cyan-400/10')}>
                <span className="rounded-md bg-slate-950/90 px-2.5 py-0.5 font-mono text-lg font-bold text-slate-100">{id}</span>
              </button>
            );
          })}
          {transform && data.sectors.flatMap((sector) => [
            ...sector.starts.map(({ pos }, i) => ({ pos, key: sector.id + '-start-' + i, start: true })),
            ...sector.pmc_posts.map(({ pos }, i) => ({ pos, key: sector.id + '-post-' + i, start: false })),
          ]).map(({ pos, key, start }) => {
            const [left, top] = toScreen(transform, [pos[0], pos[1]]);
            return <span key={key} aria-hidden className={'pointer-events-none absolute h-2.5 w-2.5 -translate-x-1/2 -translate-y-1/2 border border-slate-950 ' +
              (start ? 'rounded-full bg-emerald-300' : 'rotate-45 rounded-sm bg-orange-300')} style={{ left, top }} />;
          })}
        </div>
      </div>
      <div className="flex flex-wrap gap-5 px-4 py-3 text-xs text-slate-300">
        <span><i className="mr-2 inline-block h-2 w-2 rounded-full bg-emerald-300" />Старт отряда</span>
        <span><i className="mr-2 inline-block h-2 w-2 rotate-45 bg-orange-300" />Пост PMC</span>
      </div>
    </>
  );
}

function SectorDetails({ sector }: { sector: TrainingSector }) {
  const { contents, pmc_squad: squad } = sector;
  const facts = [
    ['Здания со входом', contents.enterable_buildings], ['Заборы', contents.fences_m + ' м'],
    ['Лестницы', contents.ladders], ['Стартовые точки', sector.starts.length], ['Посты PMC', sector.pmc_posts.length],
    ['Отряд PMC', squad.count + ' · ' + (postures[squad.posture] ?? squad.posture)],
  ] as const;
  return (
    <section className={panel + ' p-4'}>
      <h2 className="text-base font-bold text-slate-50">{label(sector.id)}</h2>
      <p className="mt-1 text-sm text-slate-400">{sector.size_m[0]} × {sector.size_m[1]} м</p>
      <dl className="mt-4 space-y-2 text-sm">
        {facts.map(([key, value]) => <div key={key} className="flex justify-between gap-3">
          <dt className="text-slate-400">{key}</dt><dd className="text-right tabular-nums">{value}</dd>
        </div>)}
      </dl>
      {Object.keys(contents.covers).length > 0 && <>
        <h3 className="mt-4 border-t border-slate-800 pt-3 text-sm font-semibold">Укрытия</h3>
        <dl className="mt-2 space-y-1 text-xs text-slate-400">
          {Object.entries(contents.covers).map(([kind, count]) => <div key={kind} className="flex justify-between">
            <dt>{kind.replace(/_/g, ' ')}</dt><dd className="tabular-nums">{count}</dd>
          </div>)}
        </dl>
      </>}
    </section>
  );
}

function renderInlineMarkdown(text: string) {
  const parts = text.split(/(`[^`]+`|\*\*[^*]+\*\*)/g);
  return parts.map((part, idx) => {
    if (part.startsWith('`') && part.endsWith('`') && part.length > 2) {
      return <code key={idx} className="rounded bg-slate-950 px-1.5 py-0.5 font-mono text-xs text-cyan-200">{part.slice(1, -1)}</code>;
    }
    if (part.startsWith('**') && part.endsWith('**') && part.length > 4) {
      return <strong key={idx} className="font-semibold text-slate-100">{part.slice(2, -2)}</strong>;
    }
    return <span key={idx}>{part}</span>;
  });
}

function HowtoMemo({ markdown }: { markdown: string }) {
  const lines = markdown.split(/\r?\n/).map((line) => line.trim()).filter(Boolean);
  return (
    <section className={panel + ' mb-4 p-4 text-sm text-slate-300'} aria-label="Как запустить запись боёв">
      <div className="space-y-2">
        {lines.map((line, index) => {
          if (line.startsWith('# ')) {
            return <h2 key={index} className="text-base font-bold text-slate-50">{line.slice(2)}</h2>;
          }
          if (/^\d+\.\s+/.test(line)) {
            return <p key={index} className="pl-2 leading-relaxed">{renderInlineMarkdown(line)}</p>;
          }
          return <p key={index} className="leading-relaxed text-slate-300">{renderInlineMarkdown(line)}</p>;
        })}
      </div>
    </section>
  );
}

export function AiTrainingDepartment() {
  const [data, setData] = useState<TrainingData | null>(null);
  const [draft, setDraft] = useState<TrainingSelection>({ map: X6, sector: 0 });
  const [error, setError] = useState('');
  const [saving, setSaving] = useState(false);
  const [memoOpen, setMemoOpen] = useState(false);
  const [launching, setLaunching] = useState<'editor' | 'play' | null>(null);
  const [launchStatus, setLaunchStatus] = useState('');
  const [copiedCmd, setCopiedCmd] = useState(false);
  useEffect(() => {
    const controller = new AbortController();
    fetch(API, { signal: controller.signal }).then(async (response) => {
      if (!response.ok) throw new Error('Не удалось загрузить настройки тренировки');
      return await response.json() as TrainingData;
    }).then((result) => { setData(result); setDraft(result.active); }).catch((failure: unknown) => {
      if (!controller.signal.aborted) setError(failure instanceof Error ? failure.message : String(failure));
    });
    return () => controller.abort();
  }, []);
  async function apply() {
    setSaving(true);
    setError('');
    try {
      const response = await fetch(API, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(draft) });
      const result = await response.json() as { active?: TrainingSelection; error?: string };
      if (!response.ok || !result.active) throw new Error(result.error ?? 'Не удалось сохранить сектор');
      const active = result.active;
      setData((previous) => previous ? { ...previous, active } : previous);
    } catch (failure) {
      setError(failure instanceof Error ? failure.message : String(failure));
    } finally {
      setSaving(false);
    }
  }
  async function openEditor() {
    setLaunching('editor');
    setError('');
    setLaunchStatus('');
    try {
      const response = await fetch('/api/training/open-editor', { method: 'POST' });
      const result = await response.json() as { pid?: number | null; error?: string };
      if (!response.ok) throw new Error(result.error ?? 'Не удалось открыть редактор');
      setLaunchStatus(result.pid ? `Редактор запущен, pid ${result.pid}` : 'Редактор запущен');
    } catch (failure) {
      setError(failure instanceof Error ? failure.message : String(failure));
    } finally {
      setLaunching(null);
    }
  }
  async function playSector() {
    setLaunching('play');
    setError('');
    setLaunchStatus('');
    try {
      const response = await fetch('/api/training/play', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ sector: draft.sector }),
      });
      const result = await response.json() as { pid?: number | null; active?: TrainingSelection; error?: string };
      if (!response.ok) throw new Error(result.error ?? 'Не удалось запустить игру');
      if (result.active) {
        const active = result.active;
        setData((previous) => previous ? { ...previous, active } : previous);
      }
      setLaunchStatus(result.pid ? `запущено, pid ${result.pid}` : 'запущено');
    } catch (failure) {
      setError(failure instanceof Error ? failure.message : String(failure));
    } finally {
      setLaunching(null);
    }
  }
  async function copyCmdPath(cmdPath: string) {
    try {
      await navigator.clipboard.writeText(cmdPath);
      setCopiedCmd(true);
      setTimeout(() => setCopiedCmd(false), 1800);
    } catch { /* clipboard optional in non-secure contexts */ }
  }
  if (!data) return <main className="p-6 text-sm text-slate-300" role={error ? 'alert' : 'status'}>{error || 'Загрузка тренировки…'}</main>;
  const available = draft.map === X6 ? data.sectors : [];
  const selected = available.find((sector) => sector.id === draft.sector);
  const choose = (sector: number) => { if (!saving && !launching) setDraft((value) => ({ ...value, sector })); };
  const changed = draft.map !== data.active.map || draft.sector !== data.active.sector;
  const cmdPath = data.editorCmdPath || 'Scripts/Tools/open_editor_nozen.cmd';
  return (
    <main className="mx-auto w-full max-w-[1550px] px-4 py-5 text-slate-100 md:px-6">
      <header className="mb-4 flex flex-wrap items-end justify-between gap-4">
        <div>
          <div className="flex flex-wrap items-center gap-3">
            <h1 className="text-2xl font-bold text-slate-50">Тренировка ИИ</h1>
            <button type="button" onClick={() => setMemoOpen((open) => !open)} aria-expanded={memoOpen}
              className="rounded-md border border-slate-700 bg-slate-900 px-3 py-1 text-xs font-semibold text-slate-200 hover:border-cyan-400 hover:text-cyan-200">
              {memoOpen ? '▾ Как запустить' : '▸ Как запустить'}
            </button>
          </div>
          <p className="mt-1 text-sm text-slate-400" role="status">
            Активно: {data.maps.find(({ id }) => id === data.active.map)?.name} · {label(data.active.sector)}
          </p>
        </div>
        <div className="flex flex-wrap items-end gap-3">
          <label className="grid gap-1 text-xs text-slate-400">Карта
            <select className={field} value={draft.map} disabled={saving || Boolean(launching)}
              onChange={(event) => setDraft({ map: event.target.value as TrainingMapId, sector: 0 })}>
              {data.maps.map(({ id, name }) => <option key={id} value={id}>{name}</option>)}
            </select>
          </label>
          <label className="grid gap-1 text-xs text-slate-400">Сектор
            <select className={field} value={draft.sector} disabled={saving || Boolean(launching) || !available.length} onChange={(event) => choose(Number(event.target.value))}>
              <option value={0}>{label(0)}</option>
              {available.map(({ id }) => <option key={id} value={id}>{label(id)}</option>)}
            </select>
          </label>
          <button type="button" onClick={apply} disabled={saving || Boolean(launching) || !changed}
            className="rounded-md bg-cyan-400 px-4 py-2 text-sm font-bold text-slate-950 hover:bg-cyan-300 disabled:bg-slate-800 disabled:text-slate-500">
            {saving ? 'Сохранение…' : 'Сделать активным'}
          </button>
          <button type="button" onClick={playSector} disabled={saving || Boolean(launching) || draft.map !== X6}
            className="rounded-md bg-emerald-400 px-4 py-2 text-sm font-bold text-slate-950 hover:bg-emerald-300 disabled:bg-slate-800 disabled:text-slate-500">
            {launching === 'play' ? 'Запуск…' : `Играть сектор ${draft.sector}`}
          </button>
          <button type="button" onClick={openEditor} disabled={saving || Boolean(launching)}
            className="rounded-md border border-slate-600 bg-slate-800 px-4 py-2 text-sm font-semibold text-slate-100 hover:border-cyan-400 hover:bg-slate-700 disabled:opacity-50">
            {launching === 'editor' ? 'Открытие…' : 'Открыть редактор'}
          </button>
        </div>
      </header>
      <div className="mb-4 flex flex-wrap items-center justify-between gap-2 rounded-lg border border-slate-800/80 bg-slate-950/60 px-3 py-2 text-xs text-slate-400">
        <div className="flex flex-wrap items-center gap-2">
          <span>Ярлык редактора (без Zen-зависания):</span>
          <code className="select-all rounded bg-slate-900 px-2 py-0.5 font-mono text-slate-200">{cmdPath}</code>
          <button type="button" onClick={() => copyCmdPath(cmdPath)}
            className="rounded border border-slate-700 px-2 py-0.5 text-[11px] text-slate-300 hover:border-cyan-400 hover:text-cyan-200">
            {copiedCmd ? 'Скопировано' : 'Копировать'}
          </button>
        </div>
        <span>Запись .jsonl (DemoRecord) работает и при запуске через <code className="font-mono text-slate-300">-game</code>.</span>
      </div>
      {launchStatus && <p role="status" className="mb-4 rounded-md border border-emerald-800 bg-emerald-950/50 px-3 py-2 text-sm text-emerald-200">{launchStatus}</p>}
      {memoOpen && (data.howto ? <HowtoMemo markdown={data.howto} /> :
        <p className={panel + ' mb-4 p-4 text-sm text-slate-400'}>Памятка docs/port/FIGHTS_HOWTO_RU.md не найдена.</p>)}
      {error && <p role="alert" className="mb-4 rounded-md border border-red-900 bg-red-950/50 px-3 py-2 text-sm text-red-200">{error}</p>}
      <div className="grid gap-4 lg:grid-cols-[minmax(0,1fr)_310px]">
        <section className={panel + ' min-w-0'} aria-label="План тренировки">
          {available.length > 0 ? <SectorPlan data={data} selected={draft.sector} choose={choose} /> :
            <p className="p-5 text-sm text-slate-300">{draft.map === X6 ? 'Файл секторов X6 пока недоступен.' : 'Для этой карты доступна тренировка на всей карте.'}</p>}
        </section>
        <aside className="grid content-start gap-4">
          {selected && <SectorDetails sector={selected} />}
          <section className={panel + ' p-4'}>
            <h2 className="text-base font-bold text-slate-50">Последние прогоны</h2>
            {data.runs.length === 0 ? <p className="mt-2 text-sm text-slate-400">Записей в Saved/Demos и Saved/Arena пока нет.</p> :
              <ul className="mt-2 max-h-80 divide-y divide-slate-800 overflow-auto">
                {data.runs.map((run) => <li key={run.source + run.file} className="py-2 text-xs" title={run.file}>
                  <div className="flex justify-between gap-2"><span>{run.source === 'demo' ? 'Сессия' : 'Арена'}</span>
                    <span className="text-slate-400">{new Date(run.time).toLocaleString('ru-RU')}</span></div>
                  <div className="mt-1 truncate text-slate-400">{run.map} · {run.sector === null ? 'Сектор не записан' : label(run.sector)}</div>
                </li>)}
              </ul>}
          </section>
        </aside>
      </div>
    </main>
  );
}
