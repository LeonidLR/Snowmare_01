// Training Ground department «Левел-дизайн»: audits every layout in Content/Data/FormationLab (schema, route points in walls,
// unreachable legs, zones no route crosses, expectations too short to settle) - the level-design bot's first checks, 0 tokens.
import React, { useEffect, useMemo, useState } from 'react';
import { CheckCircle2, AlertTriangle, XCircle } from 'lucide-react';
import { SquadView } from './SquadView';
import type { Layout } from '../movement/layout';
import type { FormationConfigFile } from '../movement/config';
import { configValues } from '../movement/config';
import type { AuditIssue } from '../movement/levelAudit';
import { auditLayout } from '../movement/levelAudit';

export const LevelDesignDepartment: React.FC = () => {
  const [names, setNames] = useState<string[]>([]);
  const [layouts, setLayouts] = useState<Record<string, unknown>>({});
  const [configFile, setConfigFile] = useState<FormationConfigFile | null>(null);
  const [selected, setSelected] = useState('');
  const [error, setError] = useState('');

  useEffect(() => {
    fetch('/api/movement/config').then((r) => r.json()).then(setConfigFile).catch(() => {});
    fetch('/api/movement/layouts').then((r) => r.json()).then(async (d) => {
      const list: string[] = d.layouts ?? [];
      setNames(list);
      setSelected((s) => s || list[0] || '');
      const loaded: Record<string, unknown> = {};
      for (const n of list) loaded[n] = await fetch(`/api/movement/layout?name=${encodeURIComponent(n)}`).then((r) => r.json());
      setLayouts(loaded);
    }).catch(() => setError('Нет связи с сервером Training Ground'));
  }, []);

  const audits = useMemo(() => {
    if (!configFile) return {} as Record<string, AuditIssue[]>;
    const config = configValues(configFile);
    return Object.fromEntries(Object.entries(layouts).map(([n, l]) => [n, auditLayout(l, config)]));
  }, [layouts, configFile]);

  const issues = audits[selected] ?? [];
  const layout = layouts[selected] && !issues.some((i) => i.where === 'schema') ? (layouts[selected] as Layout) : null;
  const marks = useMemo(() => {
    if (!layout) return [];
    return issues.flatMap((i) => {
      const m = /^(.+)\.points\[(\d+)\]$/.exec(i.where);
      const route = m && layout.routes.find((r) => r.id === m[1]);
      return route ? [route.points[Number(m![2])]] : [];
    });
  }, [issues, layout]);

  return (
    <div className="p-6 grid gap-6 xl:grid-cols-[320px_minmax(0,1fr)]">
      <section className="bg-slate-900 border border-slate-800 rounded-2xl p-4 flex flex-col gap-2">
        <h3 className="text-xs font-bold tracking-widest uppercase text-slate-400">Раскладки (FormationLab)</h3>
        {error && <p className="text-sm text-amber-300">{error}</p>}
        {names.map((n) => {
          const list = audits[n] ?? [];
          const errors = list.filter((i) => i.severity === 'error').length, warnings = list.length - errors;
          return (
            <button key={n} onClick={() => setSelected(n)}
              className={`text-left px-3 py-2 rounded-lg border text-sm flex items-center justify-between gap-2 ${selected === n ? 'border-cyan-600 bg-slate-800' : 'border-slate-800 hover:bg-slate-800/60'}`}>
              <span className="font-mono text-slate-200">{n}</span>
              <span className="flex items-center gap-2 text-xs">
                {errors ? <span className="text-rose-400 flex items-center gap-1"><XCircle size={13} />{errors}</span> : null}
                {warnings ? <span className="text-amber-300 flex items-center gap-1"><AlertTriangle size={13} />{warnings}</span> : null}
                {!errors && !warnings && <CheckCircle2 size={14} className="text-emerald-400" />}
              </span>
            </button>
          );
        })}
        <p className="text-xs text-slate-500 mt-2">Дальше в этом отделе: бот левел-дизайна (проходимость, узкие места, сектора обзора по всей карте) на тех же правилах.</p>
      </section>
      <section className="bg-slate-900 border border-slate-800 rounded-2xl p-4 flex flex-col gap-3 min-w-0">
        <h3 className="text-xs font-bold tracking-widest uppercase text-slate-400">{selected || '—'}</h3>
        <SquadView layout={layout} height={460} marks={marks} />
        {issues.length === 0 ? (
          <p className="text-sm text-emerald-400 flex items-center gap-2"><CheckCircle2 size={16} />Замечаний нет.</p>
        ) : (
          <ul className="text-sm flex flex-col gap-1.5">
            {issues.map((i, k) => (
              <li key={k} className="flex gap-2 items-start">
                {i.severity === 'error' ? <XCircle size={15} className="text-rose-400 mt-0.5 shrink-0" /> : <AlertTriangle size={15} className="text-amber-300 mt-0.5 shrink-0" />}
                <span><span className="font-mono text-slate-400">{i.where}</span> — <span className="text-slate-200">{i.message}</span></span>
              </li>
            ))}
          </ul>
        )}
      </section>
    </div>
  );
};
