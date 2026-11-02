// Training Ground «Боевая лаборатория», part C: the report of a bot batch - per run and aggregated (src/combatLab/report.ts), the map with
// where the bot got stuck and where it met the enemy, and the 3-line Jev digest.
import React, { useMemo } from 'react';
import type { RunRecord } from '../../combatLab/report';
import { KIND_NAMES, aggregate, formationName, reasonText, runDrops, runRow, worstStuck } from '../../combatLab/report';
import type { LabLayout, LabMark, LabRoutePoint } from './CombatLabMap';
import { CombatLabMap, MapLegend } from './CombatLabMap';
import { Bars, Card, Stat, fmtNum } from './ui';

export interface BatchReport { id: string; dir: string; batch: Record<string, unknown> | null; runs: RunRecord[]; skipped: number; jev: string; markdown: string; log: string }

interface Props {
  report: BatchReport;
  mapId: string;
  layout: LabLayout | null;
  route: LabRoutePoint[];
}

export const ReportView: React.FC<Props> = ({ report, mapId, layout, route }) => {
  const a = useMemo(() => aggregate(report.runs), [report.runs]);
  const rows = useMemo(() => report.runs.map(runRow), [report.runs]);
  const marks = useMemo<LabMark[]>(() => report.runs.flatMap((r) => [
    ...r.route.stuck.map((s) => ({ at: s.position, kind: (s.skipped ? 'skipped' : 'stuck') as LabMark['kind'], title: `${s.label}: ${reasonText(s.reason)}` })),
    ...r.contacts.list.map((c) => ({ at: c.position, kind: c.kind as LabMark['kind'] })),
    { at: r.route.end_position, kind: 'end' as const },
  ]), [report.runs]);

  if (report.runs.length === 0) {
    return (
      <Card title={`Отчёт ${report.id} · ${mapId}`} hint="Записей забегов пока нет (серия идёт или не удалась).">
        {report.log && <pre className="text-[11px] bg-slate-950 border border-slate-800 rounded-lg p-3 overflow-x-auto text-slate-400 max-h-64">{report.log}</pre>}
      </Card>
    );
  }
  const totalFormations = Object.values(a.formationsEntered).reduce((x, y) => x + y, 0);
  const totalSeconds = Object.values(a.formationSeconds).reduce((x, y) => x + y, 0);
  return (
    <div className="flex flex-col gap-4">
      <Card title={`Итог серии ${report.id} · ${mapId}`} hint={`${a.runs} забег(ов), ${fmtNum(a.minutes, 1)} мин игры${report.batch?.preset ? `, пресет ${report.batch.preset}` : ''}; бессмертие ${a.immortal ? 'вкл' : 'выкл'}, бесконечные патроны ${a.infiniteAmmo ? 'вкл' : 'выкл'}. Папка: ${report.dir}`}>
        <div className="grid grid-cols-2 md:grid-cols-4 xl:grid-cols-8 gap-2">
          <Stat label="Маршрут пройден" value={`${a.routeCompleted} / ${a.runs}`} sub={`в среднем ${a.progressMeanPct} % · кругов ${a.lapsTotal}`} tone={a.routeCompleted === a.runs ? 'ok' : a.progressMeanPct >= 50 ? 'warn' : 'bad'} />
          <Stat label="Застревания" value={a.stuck} sub={`пропущено точек ${a.skipped}`} tone={a.skipped ? 'bad' : a.stuck ? 'warn' : 'ok'} />
          <Stat label="Контакты" value={a.contacts} sub={`п ${a.byKind.patrol} · з ${a.byKind.ambush} · ск ${a.byKind.coordinated}`} />
          <Stat label="Удар с неприкрытой стороны" value={a.unwatchedStrikes} sub={`не взглянули ни разу: ${a.neverLooked}`} tone={a.unwatchedStrikes ? 'warn' : 'ok'} />
          <Stat label="До первого взгляда" value={`${fmtNum(a.reactionMean, 1)} с`} sub={`медиана ${fmtNum(a.reactionMedian, 1)} · макс ${fmtNum(a.reactionMax, 1)}`} />
          <Stat label="Смены построения" value={a.formationChanges} sub={`прижались к стене: ${a.wallHugs} · в бою укрылись у стены ${a.coverTaken} / ${a.coverOrders} (${fmtNum(a.coverSeconds, 0)} с)`} />
          <Stat label="Смены стойки" value={a.stanceChanges} sub={`${fmtNum(a.stancePerMin, 1)} в минуту`} />
          <Stat label={a.immortal ? 'Урон (поглощён)' : 'Урон'} value={fmtNum(a.immortal ? a.absorbed : a.damage)} sub={`${a.infiniteAmmo ? 'патронов было бы потрачено' : 'патронов'} ${a.rounds} · лестниц ${a.climbsUp} · нокдаунов ${a.knockdowns}`} />
        </div>
      </Card>

      <div className="grid gap-4 xl:grid-cols-[minmax(0,1.2fr)_minmax(0,1fr)]">
        <Card title="Карта: где застревал и где встречал врага">
          <CombatLabMap layout={layout} route={route} marks={marks} height={520} />
          <MapLegend report />
        </Card>
        <div className="flex flex-col gap-4 min-w-0">
          <Card title="Маршрут: где и почему застревал">
            {a.stuckByWaypoint.length === 0 ? <p className="text-sm text-emerald-400">Ни одного застревания.</p> : (
              <ul className="text-sm flex flex-col gap-1">
                {a.stuckByWaypoint.map((s) => <li key={s.label}><span className="text-amber-300 font-semibold">{s.count}×</span> {s.label} — <span className="text-slate-400">{s.reasons.join('; ')}</span></li>)}
              </ul>
            )}
          </Card>
          {a.drops > 0 && (
            <Card title="Спуски через проломы крыши / пола" hint="fell — лидер упал через ссылку спуска; скрипт — спуск не удался, бот перенёс отряд.">
              <p className="text-sm">Всего <span className="font-semibold text-slate-100">{a.drops}</span> · упали сами <span className="text-emerald-400 font-semibold">{a.dropsFell}</span> · перенесены скриптом <span className={a.dropsScripted ? 'text-rose-300 font-semibold' : 'text-slate-400'}>{a.dropsScripted}</span></p>
              <ul className="text-xs flex flex-col gap-0.5 text-slate-300">
                {report.runs.flatMap((r) => runDrops(r).map((d, i) => (
                  <li key={`${r.run}-${i}`}>забег {r.run}: #{d.waypoint} {d.label} через {d.via || '—'} — {d.scripted ? <span className="text-rose-300">скрипт (спуск не удался)</span> : d.fell ? <span className="text-emerald-400">упал</span> : 'не упал'}, {fmtNum(d.seconds, 1)} с</li>
                )))}
              </ul>
            </Card>
          )}
          <Card title="Построение в момент контакта">
            <Bars rows={Object.entries(a.contactFormations).sort((x, y) => y[1] - x[1]).map(([k, v]) => ({ label: formationName(k), value: v, share: a.contacts ? v / a.contacts : 0, color: 'bg-orange-500' }))} />
          </Card>
          <Card title="Построения: сколько раз и сколько времени">
            <Bars rows={Object.entries(a.formationsEntered).sort((x, y) => y[1] - x[1]).map(([k, v]) => ({ label: formationName(k), value: `${v} раз · ${fmtNum((a.formationSeconds[k] ?? 0) / 60, 1)} мин`, share: totalFormations ? (a.formationSeconds[k] ?? 0) / (totalSeconds || 1) : 0 }))} />
          </Card>
          <Card title="Стойки по бойцам">
            <table className="text-xs w-full">
              <thead className="text-slate-500"><tr><th className="text-left font-normal">Боец</th><th className="font-normal">смен</th><th className="font-normal">встал</th><th className="font-normal">присел</th><th className="font-normal">лёг</th></tr></thead>
              <tbody className="text-slate-200 tabular-nums">
                {Object.entries(a.stanceByMember).map(([name, s]) => <tr key={name}><td>{name}</td><td className="text-center">{s.changes}</td><td className="text-center">{s.standing}</td><td className="text-center">{s.crouching}</td><td className="text-center">{s.prone}</td></tr>)}
              </tbody>
            </table>
          </Card>
        </div>
      </div>

      <Card title="Забеги">
        <div className="overflow-x-auto">
          <table className="text-xs w-full min-w-[900px]">
            <thead className="text-slate-500 text-left">
              <tr>{['№', 'Итог', 'Мин', 'Маршрут', 'Кругов', 'Застрял', 'Контакты п/з/ск', 'Неприкр.', 'Взгляд, с', 'Построений', 'К стене', 'Укрытие в бою', 'Стойки (/мин)', 'Урон / погл.', 'Патроны', 'Лестницы', 'Спуски (сам/скрипт)', 'Нокдауны'].map((h) => <th key={h} className="font-normal pr-3 pb-1">{h}</th>)}</tr>
            </thead>
            <tbody className="text-slate-200 tabular-nums">
              {rows.map((r) => (
                <tr key={r.run} className="border-t border-slate-800 align-top">
                  <td className="pr-3 py-1">{r.run}</td>
                  <td className="pr-3">{r.result}</td>
                  <td className="pr-3">{fmtNum(r.minutes, 1)}</td>
                  <td className={`pr-3 ${r.completed ? 'text-emerald-400' : 'text-amber-300'}`}>{r.completed ? 'пройден' : `${r.progressPct} %`}</td>
                  <td className="pr-3">{r.laps}</td>
                  <td className="pr-3 max-w-[22rem]">{r.stuck ? <span title={r.stuckWhere}>{r.stuck} · {r.stuckWhere}</span> : '—'}</td>
                  <td className="pr-3">{r.patrol}/{r.ambush}/{r.coordinated}</td>
                  <td className="pr-3">{r.unwatched}</td>
                  <td className="pr-3">{fmtNum(r.reactionMean, 1)}</td>
                  <td className="pr-3">{r.formationChanges}</td>
                  <td className="pr-3">{r.wallHugs}</td>
                  <td className="pr-3">{r.coverTaken}/{r.coverOrders} ({fmtNum(r.coverSeconds, 0)} с)</td>
                  <td className="pr-3">{r.stanceChanges} ({fmtNum(r.stancePerMin, 1)})</td>
                  <td className="pr-3">{fmtNum(r.damage)} / {fmtNum(r.absorbed)}</td>
                  <td className="pr-3">{r.rounds}</td>
                  <td className="pr-3">{r.climbs}</td>
                  <td className="pr-3">{r.drops ? `${r.dropsFell}/${r.dropsScripted} из ${r.drops}` : '—'}</td>
                  <td className="pr-3">{r.knockdowns}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
        <details className="text-xs text-slate-400">
          <summary className="cursor-pointer">Контакты по отдельности</summary>
          <ul className="mt-2 flex flex-col gap-0.5 font-mono">
            {report.runs.flatMap((r) => r.contacts.list.map((c, i) => (
              <li key={`${r.run}-${i}`}>забег {r.run}, {fmtNum(c.time, 0)} с: {KIND_NAMES[c.kind]} ({c.trigger}), построение {formationName(c.formation || '—')}, взгляд {c.reaction_sec >= 0 ? `${fmtNum(c.reaction_sec, 1)} с` : 'никогда'}{c.unwatched_strike ? ', удар с неприкрытой стороны' : ''}</li>
            )))}
          </ul>
        </details>
        <details className="text-xs text-slate-400">
          <summary className="cursor-pointer">Где закончил каждый забег</summary>
          <ul className="mt-2 flex flex-col gap-0.5">
            {report.runs.map((r) => {
              const w = worstStuck(r);
              return <li key={r.run}>забег {r.run}: шёл к #{r.route.current_index} {r.route.current_label}; последний приказ «{r.route.last_order || '—'}»{w ? `; хуже всего: #${w.waypoint} ${w.label} — ${reasonText(w.reason)}` : ''}</li>;
            })}
          </ul>
        </details>
      </Card>

      <Card title="Сводка Jev (typesafe_triage.py --telemetry)">
        <pre className="text-[11px] leading-snug bg-slate-950 border border-slate-800 rounded-lg p-3 overflow-x-auto whitespace-pre-wrap text-slate-300">{report.jev.trim() || 'Сводка ещё не готова (её пишет bot_run.ps1 в конце серии).'}</pre>
      </Card>
    </div>
  );
};
