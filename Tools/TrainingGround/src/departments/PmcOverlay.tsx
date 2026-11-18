// The «ЧВК» plan layer (task 087, PMC_DEFENCE.md D2): the generated PMC posts (icon by kind + the facing ray, hunt posts hollow) and the
// defend posts' charges (mine = red disc with a cross, tripwire = a wire across the way), each charge linked to its post by a thin line.
import React from 'react';
import type { OverlayHelpers } from './RegionOverlay';
import type { PmcPlan, PmcPostKind } from '../levelgen/pmcPosts';

export const PMC_KIND_RU: Record<PmcPostKind, string> = {
  ambush_exit: 'засада у выхода из узкого места', overwatch_roof: 'крыша (наблюдение)', window: 'окно над маршрутом', hiding_spot: 'укрытие',
};
const ROLE_RU: Record<string, string> = { approach: 'на подходе', doorway: 'в дверном проёме', flank: 'на фланге', roof_access: 'у выхода на крышу' };
const POST_COLOR = '#f97316';
const CHARGE_COLOR = '#ef4444';

export const PmcOverlay: React.FC<{ h: OverlayHelpers; plan: PmcPlan }> = ({ h, plan }) => {
  const byId = new Map(plan.posts.map((p) => [p.id, p]));
  const ray = (p: readonly number[], yaw: number, len: number): [number, number] => {
    const r = (yaw * Math.PI) / 180;
    return h.scr([p[0] + Math.cos(r) * len, p[1] + Math.sin(r) * len]);
  };
  return (
    <g data-pmc-overlay style={{ pointerEvents: 'none' }}>
      {plan.charges.map((c) => {
        const post = byId.get(c.post), q = h.scr(c.pos);
        const link = post ? h.scr([post.pos[0], post.pos[1]]) : q;
        const title = `${c.id}: ${c.type === 'mine' ? 'мина' : 'растяжка'} ${ROLE_RU[c.role] ?? c.role} (пост ${c.post}${c.door ? `, дверь ${c.door}` : ''}; ${c.walk_layer})`;
        if (c.type === 'tripwire') {
          const half = ((c.width_cm ?? 300) / 2), a = ray(c.pos, c.yaw + 90, half), b = ray(c.pos, c.yaw - 90, half);
          return (<g key={c.id} data-pmc-charge={c.id}><title>{title}</title>
            <line x1={q[0]} y1={q[1]} x2={link[0]} y2={link[1]} stroke={CHARGE_COLOR} strokeOpacity={0.35} strokeWidth={1} strokeDasharray="2 2" />
            <line x1={a[0]} y1={a[1]} x2={b[0]} y2={b[1]} stroke={CHARGE_COLOR} strokeWidth={2.4} strokeLinecap="round" />
            <circle cx={a[0]} cy={a[1]} r={2.2} fill={CHARGE_COLOR} /><circle cx={b[0]} cy={b[1]} r={2.2} fill={CHARGE_COLOR} /></g>);
        }
        return (<g key={c.id} data-pmc-charge={c.id}><title>{title}</title>
          <line x1={q[0]} y1={q[1]} x2={link[0]} y2={link[1]} stroke={CHARGE_COLOR} strokeOpacity={0.35} strokeWidth={1} strokeDasharray="2 2" />
          <circle cx={q[0]} cy={q[1]} r={5} fill={CHARGE_COLOR} stroke="#450a0a" strokeWidth={1.2} />
          <path d={`M${q[0] - 3},${q[1] - 3} L${q[0] + 3},${q[1] + 3} M${q[0] + 3},${q[1] - 3} L${q[0] - 3},${q[1] + 3}`} stroke="#fff" strokeWidth={1.3} /></g>);
      })}
      {plan.posts.map((p) => {
        const q = h.scr([p.pos[0], p.pos[1]]), t = ray(p.pos, p.yaw, 700), hollow = p.posture === 'hunt';
        const title = `${p.id}: ${PMC_KIND_RU[p.kind]}, ${p.posture === 'defend' ? 'оборона' : 'охота'}, ${p.count} бойца, задержка ${p.delay_s} с; ${p.layer}${p.place ? `, место ${p.place}` : ''}; до маршрута ${p.route_m} м`;
        const icon = p.kind === 'overwatch_roof'
          ? <path d={`M${q[0]},${q[1] - 9} L${q[0] + 9},${q[1]} L${q[0]},${q[1] + 9} L${q[0] - 9},${q[1]} Z`} />
          : p.kind === 'window' ? <rect x={q[0] - 7} y={q[1] - 7} width={14} height={14} rx={2} />
          : p.kind === 'ambush_exit' ? <path d={`M${q[0]},${q[1] - 9} L${q[0] + 8.5},${q[1] + 6} L${q[0] - 8.5},${q[1] + 6} Z`} />
          : <circle cx={q[0]} cy={q[1]} r={7.5} />;
        return (<g key={p.id} data-pmc-post={p.id} data-pmc-kind={p.kind}><title>{title}</title>
          <line x1={q[0]} y1={q[1]} x2={t[0]} y2={t[1]} stroke={POST_COLOR} strokeWidth={2} />
          <g fill={hollow ? '#0f172a' : POST_COLOR} stroke={hollow ? POST_COLOR : '#431407'} strokeWidth={1.8}>{icon}</g>
          <text x={q[0] + 11} y={q[1] - 8} fill={POST_COLOR} fontSize={10} fontWeight="bold" fontFamily="ui-monospace, monospace">{p.id.slice(4)}</text></g>);
      })}
    </g>
  );
};
