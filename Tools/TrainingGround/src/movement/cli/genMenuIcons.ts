// `npm run menu:icons` - writes the radial menu's icons to Content/UI/FormationMenu/src/<id>.svg (64 x 64, currentColor) from
// the formation rules and formation_config.json (menuIcons.ts). `--check` exits 1 when a file is missing or stale.
import fs from 'node:fs';
import path from 'node:path';
import { configValues } from '../config.ts';
import { MENU_ITEMS } from '../menu.ts';
import type { MenuChoice } from '../menu.ts';
import { menuIconSvg } from '../menuIcons.ts';
import { ROOT, parseArgs, readConfigFile } from './paths.ts';

export const ICON_DIR = path.join(ROOT, 'Content/UI/FormationMenu/src');
const args = parseArgs(process.argv.slice(2));
const config = configValues(readConfigFile());
const ids: MenuChoice[] = ['auto', ...MENU_ITEMS.map((i) => i.id)];
fs.mkdirSync(ICON_DIR, { recursive: true });
let stale = 0;
for (const id of ids) {
  const file = path.join(ICON_DIR, `${id}.svg`);
  const text = menuIconSvg(id, config);
  const same = fs.existsSync(file) && fs.readFileSync(file, 'utf8').replace(/\r\n/g, '\n') === text;
  if (args.check) { if (!same) { stale++; console.log(`STALE ${id}.svg`); } }
  else if (!same) { fs.writeFileSync(file, text); console.log(`wrote ${id}.svg`); }
}
if (args.check) { console.log(stale ? `${stale} icon(s) stale - run npm run menu:icons` : 'menu icons up to date'); process.exit(stale ? 1 : 0); }
