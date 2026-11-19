// «Здания» panel of the level generator page: the building traversal validator per building
// (src/levelgen/buildingTraversal.ts, docs/design/levelgen/BUILDING_GENERATOR.md section 5). Failing buildings first.
// Per building also its stories and the key route (src/levelgen/buildingRoutes.ts, building-route-model.md UI Requirements):
// the chain of leg modes, cost, D, fast / quiet entries and the F4 choice flag.
import { useState } from 'react';
import { CheckCircle2, XCircle } from 'lucide-react';
import type { StoryId } from '../levelgen/buildingStories';
import type { RouteSummary } from '../levelgen/buildingRoutes';
import type { StoryMapSummary } from '../levelgen/storyPass';
import type { TraversalCode, TraversalReport } from '../levelgen/buildingTraversal';
import type { BuildingRole } from '../levelgen/schema';

/** Russian story names (building-stories.md 6; Record<StoryId>: a new story without a name fails the typecheck). */
const STORY_RU: Record<StoryId, string> = {
  open: 'открытое',
  locked_door: 'запертая дверь',
  broken_back_windows: 'выбитые окна сзади',
  rubble_escape: 'завал + пожарная лестница',
  roof_outside: 'крыша только снаружи',
  snow_drift: 'вход по сугробу',
};

/** Short Russian label per edge mode (building-route-model.md rule 3). */
const MODE_RU: Record<string, string> = {
  walk: 'пешком',
  open: 'открыть',
  kick: 'выбить',
  shotgun: 'дробовик',
  vault: 'перелезть',
  shatter: 'разбить',
  climb: 'лестница',
  explosive: 'заряд',
  drop: 'спрыгнуть',
};

function RouteLine({ route, stories }: { route?: RouteSummary; stories?: string[] }) {
  if (!route && !stories?.length) return null;
  return (
    <span className="pl-5 text-slate-400">
      {stories?.length ? <span className="text-violet-300">{stories.map((s) => STORY_RU[s as StoryId] ?? s).join(' + ')} · </span> : null}
      {route ? (
        <span title={`быстрый вход: ${route.fast_entry ?? '—'}, тихий: ${route.quiet_entry ?? '—'}${route.stored ? '' : ' (посчитано на лету)'}`}>
          {route.modes.map((m) => MODE_RU[m] ?? m).join(' → ')} · цена {route.cost.toFixed(1)} · D {route.detour.toFixed(2)} · {route.choice ? <span className="text-emerald-300">выбор: быстрый {route.fast_entry} / тихий {route.quiet_entry}</span> : <span className="text-slate-500">без выбора</span>}
        </span>
      ) : <span className="text-rose-300">нет ключевого маршрута</span>}
    </span>
  );
}

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
  unreachable_without_explosive: 'войти можно только взрывчаткой (баррикада)',
  story_rolled_back: 'история отменена',
};

/** Short Russian label per tactical role (buildingRoles.ts, building-stories.md section 1). */
const ROLE_RU: Record<BuildingRole, string> = {
  objective: 'цель',
  garrison: 'гарнизон',
  overwatch: 'наблюдение',
  transit: 'транзит',
  background: 'фон',
};
const ROLE_CLASS: Record<BuildingRole, string> = {
  objective: 'text-amber-300',
  garrison: 'text-rose-300',
  overwatch: 'text-sky-300',
  transit: 'text-emerald-300',
  background: 'text-slate-500',
};

const pctOf = (v: number) => `${Math.round(v * 100)} %`;

/** The per-map story line (storyPass.ts storyMapSummary): role shares, storied share, rollbacks, choice share. */
function StorySummaryLine({ s }: { s: StoryMapSummary }) {
  const roles = (Object.keys(ROLE_RU) as BuildingRole[]).filter((r) => s.roles[r]).map((r) => `${ROLE_RU[r]} ${pctOf(s.roles[r]!)}`).join(', ');
  const stories = Object.entries(s.stories).filter(([k]) => k !== 'open').map(([k, n]) => `${STORY_RU[k as StoryId] ?? k} ${n}`).join(', ');
  return (
    <p className="text-slate-400" title="Доли считаются по входимым зданиям с ролью; «выбор» = быстрый вход отличается от тихого (F4)">
      Истории: зданий {s.buildings}; роли: {roles || '—'}; с историей {pctOf(s.storied)}{stories ? ` (${stories})` : ''}; откатов {s.rollbacks ?? '—'}; выбор быстрый/тихий {pctOf(s.choice)}
    </p>
  );
}

export interface BuildingsPanelProps {
  report: TraversalReport;
  /** The per-map story summary (null / absent = the map has no stories). */
  summary?: StoryMapSummary | null;
  /** The last «другая история» result. */
  message?: string;
  /** «другая история» per building (absent = no story state: a loaded map or the toggle was off). */
  onReroll?: (id: string) => void;
  isLocked?: (id: string) => boolean;
  canReroll?: (id: string) => boolean;
}

export function BuildingsPanel({ report, summary, message, onReroll, isLocked, canReroll }: BuildingsPanelProps) {
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
      {summary && <StorySummaryLine s={summary} />}
      {message && <p className="text-violet-300">{message}</p>}
      <ul className="flex flex-col gap-0.5 max-h-56 overflow-auto">
        {list.map((b) => (
          <li key={b.id} className="flex flex-col">
            <span className="flex items-center gap-1.5">
              {b.ok ? <CheckCircle2 size={11} className="text-emerald-400" /> : <XCircle size={11} className="text-rose-400" />}
              <span className="font-mono">{b.id}</span>
              {b.role && <span className={ROLE_CLASS[b.role]} title="тактическая роль здания">{ROLE_RU[b.role]}</span>}
              <span className="text-slate-500">{b.set_piece}</span>
              <span className="text-slate-500">комнаты {b.rooms.reachable}/{b.rooms.total}, этажи {b.levels.reachable.join(',') || '—'}{b.roof !== 'none' ? `, крыша ${b.roof === 'reachable' ? 'да' : 'нет'}` : ''}</span>
              {isLocked?.(b.id) && <span className="text-amber-400" title="здание в закреплённом районе: история сохраняется">закреплено</span>}
              {onReroll && b.stories?.length ? (
                <button className="ml-auto text-violet-300 hover:text-violet-200 underline disabled:opacity-40 disabled:no-underline" disabled={!!isLocked?.(b.id) || !(canReroll?.(b.id) ?? true)}
                  title={isLocked?.(b.id) ? 'здание закреплено' : 'перебросить историю здания следующим под-seed и пересчитать его маршруты'} onClick={() => onReroll(b.id)}>другая история</button>
              ) : null}
            </span>
            <RouteLine route={b.route} stories={b.stories} />
            {b.issues.slice(0, 4).map((i, k) => (
              <span key={k} className={`pl-5 ${i.severity === 'warning' ? 'text-amber-300' : 'text-rose-300'}`} title={i.message}>{i.severity === 'warning' ? 'предупреждение: ' : ''}{CODE_RU[i.code] ?? i.code}{i.ref ? ` (${i.ref})` : ''}</span>
            ))}
          </li>
        ))}
      </ul>
    </div>
  );
}
