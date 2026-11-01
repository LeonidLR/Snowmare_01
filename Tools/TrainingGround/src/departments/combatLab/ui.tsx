// Training Ground «Боевая лаборатория»: small shared UI pieces (cards, buttons, stat tiles) in the style of the other departments.
import React from 'react';

export const inputCls = 'bg-slate-950 border border-slate-800 rounded-lg px-2.5 py-1.5 text-slate-100 text-sm focus:outline-none focus:border-cyan-500 w-full';
export const btnCls = 'inline-flex items-center gap-1.5 px-3 py-1.5 rounded-lg text-sm font-semibold border transition-colors disabled:opacity-50 disabled:cursor-not-allowed';
export const btnPrimary = `${btnCls} bg-cyan-600 border-cyan-500 text-slate-950 hover:bg-cyan-500`;
export const btnGhost = `${btnCls} bg-slate-900 border-slate-700 text-slate-200 hover:bg-slate-800`;

export const Card: React.FC<{ title: string; hint?: string; right?: React.ReactNode; children: React.ReactNode; className?: string }> = ({ title, hint, right, children, className }) => (
  <section className={`bg-slate-900 border border-slate-800 rounded-2xl p-4 flex flex-col gap-3 min-w-0 ${className ?? ''}`}>
    <div className="flex items-start justify-between gap-2">
      <div className="min-w-0">
        <h3 className="text-xs font-bold tracking-widest uppercase text-slate-400">{title}</h3>
        {hint && <p className="text-xs text-slate-500 mt-1">{hint}</p>}
      </div>
      {right}
    </div>
    {children}
  </section>
);

export const Stat: React.FC<{ label: string; value: React.ReactNode; sub?: React.ReactNode; tone?: 'ok' | 'warn' | 'bad' }> = ({ label, value, sub, tone }) => (
  <div className="bg-slate-950 border border-slate-800 rounded-xl px-3 py-2 min-w-0">
    <div className="text-[11px] uppercase tracking-wider text-slate-500">{label}</div>
    <div className={`text-lg font-bold tabular-nums ${tone === 'ok' ? 'text-emerald-400' : tone === 'warn' ? 'text-amber-300' : tone === 'bad' ? 'text-rose-400' : 'text-slate-100'}`}>{value}</div>
    {sub && <div className="text-xs text-slate-400 mt-0.5">{sub}</div>}
  </div>
);

/** Horizontal share bars (label, value, share 0..1). */
export const Bars: React.FC<{ rows: { label: string; value: React.ReactNode; share: number; color?: string }[] }> = ({ rows }) => (
  <div className="flex flex-col gap-1.5">
    {rows.map((r) => (
      <div key={r.label} className="grid grid-cols-[minmax(7rem,auto)_1fr_auto] items-center gap-2 text-xs">
        <span className="text-slate-300 truncate">{r.label}</span>
        <div className="h-2 bg-slate-800 rounded-full overflow-hidden">
          <div className={`h-full rounded-full ${r.color ?? 'bg-cyan-500'}`} style={{ width: `${Math.max(0, Math.min(1, r.share)) * 100}%` }} />
        </div>
        <span className="text-slate-200 tabular-nums">{r.value}</span>
      </div>
    ))}
  </div>
);

export const fmtNum = (n: number, digits = 0) => (Number.isFinite(n) ? n.toLocaleString('ru-RU', { maximumFractionDigits: digits, minimumFractionDigits: digits }) : '—');
export const pct = (share: number) => `${Math.round(share * 100)} %`;
