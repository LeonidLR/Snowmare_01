# 084 - Level generator: start weather per map, map name, props re-generation inside a frame (designer 2026-11-18) - TS + Python
Base: main (after 083c is merged; rebase on main). Branch gemini/084-levelgen-weather-name-props. Worktree CodexTactics-gemday.
Page: Training Ground, department "Генератор уровней" (Tools/TrainingGround/src/departments/LevelGenDepartment.tsx, RegionOverlay.tsx).
UI text in Russian (the designer's language), code / comments / commits in English.

Context (Claude, 2026-11-18, commit 6dbc9e38): generated maps now get the live cold + weather - Scripts/Editor/build_levelgen_map.py
`level_json()` writes `"environment": {"locked": false}` (random start weather, weather ladder) and no `cold_rate_mult`; a rebuild from the
same layout keeps an existing `environment` / `cold_rate_mult`. The level-settings environment rules already exist in TS:
Tools/TrainingGround/src/utils/levelSettings.ts (`ARCTIC_WEATHER_OPTIONS`, environment validation; weather names Clear / Overcast /
Snowfall / Blizzard = UE EWeatherState).

## Part A - start weather per map
- Each map in the generator gets a "Погода на старте" selector: «Случайная» (default) / Ясно / Пасмурно / Снегопад / Метель (reuse
  ARCTIC_WEATHER_OPTIONS labels). Stored in the layout file as `environment: { weather?: string }` (schema.ts, saved with the draft and
  the accepted file, kept by region re-generation and expansions).
- build_levelgen_map.py: `level_json()` copies the layout's `environment.weather` (when set) into the level JSON `environment.weather`,
  keeps `locked: false` (the weather starts there, then changes). On a rebuild the layout's weather WINS over the old level JSON's
  `weather` key (the rest of the old environment block is kept). «Случайная» = no `weather` key.
- Automation stays deterministic: UE forces Clear only when the level has no weather; with a weather set the smoke gets that weather -
  note it in the report, do not change C++.

## Part B - map name (the real map name, designer's choice)
- Each map gets an editable "Имя карты" field. Today the build name is derived (`build.strip_prefix`, gen_ref_01_02_depot_1_x4 ->
  L_Gen_Depot_1_X4; see the hint at LevelGenDepartment.tsx ~707 and `targetMapNameForLayout` in props/autogen.ts, builder line ~131).
- Stored as `map_name` in the layout (validated: latin letters, digits, underscore, 3-40 chars, no `L_Gen_` prefix typed by the user -
  the tool adds it; unique among accepted maps). When set, EVERY place that derives the target map name uses it: the builder's map
  path (/Game/Maps/Generated/L_Gen_<map_name>), the level JSON name (Content/Data/LevelJson/L_Gen_<map_name>.json, `level_name`),
  the props / objectives / tactical file names (`targetMapNameForLayout` and the Python side), the «Собрать в Unreal» target shown.
- A rename does NOT move or delete the old built map / files: the next «Собрать в Unreal» builds under the new name; the page shows
  "старая карта L_Gen_<old> остаётся - удалить можно вручную" once. Never delete files.
- The list of accepted maps shows the map name (with the layout name small under it).

## Part C - re-generate props inside a frame
- The region frame tool (rect / lasso, RegionOverlay + regionRegen.ts) today re-generates buildings / walls / covers inside the frame.
  Add a second action on the same frame: «Перераспределить ресурсы» = re-roll ONLY the props inside the frame (mines, tripwires,
  barrels, loot crates / piles, turrets, generators - everything props autogen places, Tools/TrainingGround/src/levelgen/props/
  autogen.ts `generatePropsForLayout`) with a new seed; props outside the frame and in lock districts stay exactly as they are;
  buildings / geometry unchanged. Hand-placed props (non-autogen layers / ids) inside the frame are kept unless the designer ticks
  «включая ручные».
- The same validation as the full autogen (placement.ts / layoutView.ts: no props in walls / closed buildings, rotated footprints,
  turret-generator power links within cable range - a turret whose generator is outside the frame keeps its link or is re-linked).
- «Отменить» restores the previous props (same undo as the geometry re-generation).
- Writes the same props file the full autogen writes (Content/Data/LevelGen/props/<map>.props.json) only on the designer's save /
  accept, as today.

## Checks
- TS unit tests: weather stored + survives regen / expansion; map_name validation + all name derivations use it; props regen in a
  frame changes only props inside (count outside identical, ids outside identical), respects locks, keeps power links valid; 0 props in
  walls. `npm run typecheck`, full Training Ground test suite green.
- Python: a unit test (or a --dry-run) of build_levelgen_map.level_json with weather + map_name; a rebuild keeps the old environment
  except the layout's weather.
- Screenshots of the three UI pieces in the report (the designer checks by eye).
Allowed paths: Tools/TrainingGround/src/**, Scripts/Editor/build_levelgen_map.py, Scripts/Editor/tests/**, docs/port/gemini/outbox/084-*

Forbidden: C++, Content (no maps, no level / props JSON edits by hand), Config, worker / ssh actions.
PLAN first (docs/port/gemini/outbox/084-*.plan.md, post in topic gemini-084, wait for Claude's OK), then implement A -> B -> C with a
commit per part. Report docs/port/gemini/outbox/084-*.report.md. FINISH line in chat topic gemini-084.
</content>
</invoke>
