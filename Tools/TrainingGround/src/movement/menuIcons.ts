// Training Ground / movement: the radial menu's icons as SVG (64 x 64, one colour = currentColor) - schematic dots (the slots
// of the formation rules with the default config, 4 members; the leader a larger dot) and facing ticks (the sector centres).
// Written to Content/UI/FormationMenu/src/<id>.svg by `npm run menu:icons` (source files; the Unreal import happens locally).

import type { FormationConfig } from './config.ts';
import type { Vec2 } from './geometry.ts';
import { dirOf, round } from './geometry.ts';
import type { MenuChoice } from './menu.ts';
import { FormationSectorRules, FormationShapeRules } from './rules.ts';
import type { Role } from './rules.ts';

interface IconMember { role: Role; pos: Vec2; rel: number }

/** Members of an item in the leader's frame (forward x, right y; rel = look relative to the heading, deg). */
function iconMembers(id: MenuChoice, c: FormationConfig): IconMember[] {
  const look = (role: Role, formation: Parameters<typeof FormationSectorRules.LookYaw>[1], mode: 'halt' | 'forward' = 'halt') =>
    FormationSectorRules.LookYaw(role, formation, mode, 4, 0, 0, null, c);
  switch (id) {
    case 'hedgehog':
      return FormationShapeRules.HedgehogSlots([0, 0], 4, 0, c).map((s) => ({ role: s.role, pos: s.pos, rel: s.yaw }));
    case 'all_forward':
      return FormationShapeRules.Slots('diamond', 4, [0, 0], 0, c).map((s) => ({ role: s.role, pos: s.pos, rel: look(s.role, 'diamond', 'forward') }));
    case 'auto':
      return [];
    default: {
      const f = id === 'diamond' ? 'diamond' : id;
      // Diamond / blizzard: the patrol scan centres (P 45 / L -45), not the halt ones - the icon shows the walking sectors.
      return FormationShapeRules.Slots(f, 4, [0, 0], 0, c).map((s) => ({
        role: s.role, pos: s.pos,
        rel: (f === 'diamond' || f === 'blizzard') && (s.role === 'P' || s.role === 'L') ? (s.role === 'P' ? 1 : -1) * c['scan.center_side'] : look(s.role, f),
      }));
    }
  }
}

const SIZE = 64, MARGIN = 13;

/** The SVG text of one menu icon. */
export function menuIconSvg(id: MenuChoice, c: FormationConfig): string {
  const head = `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 ${SIZE} ${SIZE}" width="${SIZE}" height="${SIZE}" fill="none" stroke="currentColor" stroke-linecap="round">`;
  if (id === 'auto') {
    // Auto: a ring with four small dots in a diamond - "the terrain decides".
    return `${head}\n  <circle cx="32" cy="32" r="22" stroke-width="3" stroke-dasharray="6 5"/>\n` +
      [[32, 22], [40, 32], [24, 32], [32, 42]].map(([x, y]) => `  <circle cx="${x}" cy="${y}" r="3.5" fill="currentColor" stroke="none"/>`).join('\n') + '\n</svg>\n';
  }
  const m = iconMembers(id, c);
  // Screen: forward = up, right = right. Fit the slots into the box keeping the aspect ratio.
  const xs = m.map((p) => p.pos[1]), ys = m.map((p) => -p.pos[0]);
  const minX = Math.min(...xs), maxX = Math.max(...xs), minY = Math.min(...ys), maxY = Math.max(...ys);
  // The blizzard diamond is drawn at the plain diamond's scale, so it reads as the SAME shape pulled tight (+ snow marks).
  const spanOf = (mm: IconMember[]) => Math.max(...mm.map((p) => p.pos[1])) - Math.min(...mm.map((p) => p.pos[1]));
  const span = id === 'blizzard'
    ? Math.max(spanOf(iconMembers('diamond', c)), Math.max(...iconMembers('diamond', c).map((p) => -p.pos[0])) - Math.min(...iconMembers('diamond', c).map((p) => -p.pos[0])))
    : Math.max(maxX - minX, maxY - minY, 1);
  const k = (SIZE - 2 * MARGIN) / span;
  const ox = (SIZE - (maxX - minX) * k) / 2, oy = (SIZE - (maxY - minY) * k) / 2;
  const P = (p: Vec2): Vec2 => [ox + (p[1] - minX) * k, oy + (-p[0] - minY) * k];
  const parts: string[] = [];
  for (const mem of m) {
    const [x, y] = P(mem.pos);
    const d = dirOf(mem.rel); // forward x -> up, right y -> right
    const tx = d[1], ty = -d[0];
    const r = mem.role === 'K' ? 5 : 3.6;
    parts.push(`  <line x1="${round(x + tx * (r + 1), 2)}" y1="${round(y + ty * (r + 1), 2)}" x2="${round(x + tx * (r + 8), 2)}" y2="${round(y + ty * (r + 8), 2)}" stroke-width="2.6"/>`);
    parts.push(`  <circle cx="${round(x, 2)}" cy="${round(y, 2)}" r="${r}" fill="currentColor" stroke="none"/>`);
  }
  if (id === 'blizzard') {
    // three snowflakes (small asterisks) round the tight diamond
    for (const [sx, sy] of [[9, 12], [55, 20], [12, 52]]) {
      parts.push(`  <path d="M${sx - 3} ${sy}h6M${sx} ${sy - 3}v6M${sx - 2.1} ${sy - 2.1}l4.2 4.2M${sx - 2.1} ${sy + 2.1}l4.2 -4.2" stroke-width="1.3"/>`);
    }
  }
  if (id === 'hedgehog') parts.unshift(`  <circle cx="${round(P([0, 0])[0], 2)}" cy="${round(P([0, 0])[1], 2)}" r="3" stroke-width="2" stroke-dasharray="2 2"/>`);
  return `${head}\n${parts.join('\n')}\n</svg>\n`;
}
