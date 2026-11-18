// «Здания» panel of the level generator page: the building traversal validator per building
// (src/levelgen/buildingTraversal.ts, docs/design/levelgen/BUILDING_GENERATOR.md section 5). Failing buildings first.
import { useState } from 'react';
import { CheckCircle2, XCircle } from 'lucide-react';
import type { TraversalCode, TraversalReport } from '../levelgen/buildingTraversal';

/** Russian label per traversal code (Record<TraversalCode>: a new code without a label fails the typecheck). */
const CODE_RU: Record<TraversalCode, string> = {
  unreachable_room: 'комната недостижима',
  unreachable_roof: 'крыша недостижима',
  unreachable_level: 'этаж недостижим',
  stair_steep: 'марш круче 35°',
  stair_foot_blocked: 'подход к маршу перекрыт',
  stair_top_blocked: 'площадка после марша перекрыта',
  stair_foot_no_floor: 'нет пола у подножия марша',
  stair_top_no_floor: 'нет пола на площадке после марша',
  gap_overshoot: 'дыра в перекрытии за маршем',
  stair_in_other_building: 'марш внутри соседнего здания',
  no_exterior_door: 'нет наружной двери',
};

export function BuildingsPanel({ report }: { report: TraversalReport }) {
  const [open, setOpen] = useState(false);
  const enterable = report.buildings.filter((b) => b.rooms.total > 0);
  const failed = report.buildings.filter((b) => !b.ok);
  const list = open ? [...failed, ...report.buildings.filter((b) => b.ok && b.rooms.total > 0)] : failed;
  return (
    <div className="text-xs text-slate-300 flex flex-col gap-1 border border-slate-800 rounded-lg p-2">
      <p className={`flex items-center gap-2 ${failed.length ? 'text-rose-400' : 'text-emerald-400'}`}>
        {failed.length ? <XCircle size={14} /> : <CheckCircle2 size={14} />}
        Здания: {failed.length ? `${failed.length} не проходят проверку проходимости` : 'все проходимы'} — входимых {enterable.length}, всего {report.checked}
        <button className="ml-auto text-slate-400 hover:text-slate-200 underline" onClick={() => setOpen(!open)}>{open ? 'только ошибки' : 'все здания'}</button>
      </p>
      <ul className="flex flex-col gap-0.5 max-h-56 overflow-auto">
        {list.map((b) => (
          <li key={b.id} className="flex flex-col">
            <span className="flex items-center gap-1.5">
              {b.ok ? <CheckCircle2 size={11} className="text-emerald-400" /> : <XCircle size={11} className="text-rose-400" />}
              <span className="font-mono">{b.id}</span>
              <span className="text-slate-500">{b.set_piece}</span>
              <span className="text-slate-500">комнаты {b.rooms.reachable}/{b.rooms.total}, этажи {b.levels.reachable.join(',') || '—'}{b.roof !== 'none' ? `, крыша ${b.roof === 'reachable' ? 'да' : 'нет'}` : ''}</span>
            </span>
            {b.issues.slice(0, 4).map((i, k) => (
              <span key={k} className={`pl-5 ${i.severity === 'warning' ? 'text-amber-300' : 'text-rose-300'}`} title={i.message}>{i.severity === 'warning' ? 'предупреждение: ' : ''}{CODE_RU[i.code] ?? i.code}{i.ref ? ` (${i.ref})` : ''}</span>
            ))}
          </li>
        ))}
      </ul>
    </div>
  );
}
