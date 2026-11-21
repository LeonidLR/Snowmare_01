# G013 — чтение 20 домов советского набора в данные планов

Статус: скрипты готовы, юнит-тест зелёный. Редактор не запускался. JSON по настоящим картам `L_SH_Building_01..20` ещё нет: его снимет Claude на ворктри `CodexTactics-agents`, там лежит неотслеживаемый пак.

Ветка `grok/G013-kit-buildings-dump` от `origin/main` (`62ac64c3`). Ассеты пака не копировались и не коммитились.

## Команда дампа

Писать некуда: скрипт ничего не сохраняет (`save_map` / `save_asset` нет). В конце он снимает dirty-флаг, если интерпретатор его отдаёт, и пишет только `Saved/KitDump/kit_buildings.json` того проекта, чей `.uproject` открыт. Грязные пакеты в логе: `dirty_before` / `dirty_cleared`. После прогона в agents-ворктри `git status` не должен показать правок в `Content/`.

Полный дамп, PowerShell. Дизайнерский редактор может быть открыт: это отдельный commandlet.

```powershell
$editor = "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
$project = "C:\Users\Zephyrus15Duo\Documents\Unreal Projects\CodexTactics-agents\CodexTactics.uproject"
$script = "C:\Users\Zephyrus15Duo\Documents\Unreal Projects\CodexTactics-grok\Scripts\Editor\dump_kit_buildings.py"
& $editor $project -run=pythonscript "-Script=`"$script`"" -unattended -nullrhi -nosplash -log
```

Один дом. Номер 1..20. Переменная надёжнее, чем аргумент в кавычках PowerShell. `--only` тоже читается, если commandlet положил его в `sys.argv` (форма UE 5.8: путь в кавычках, аргумент снаружи `.py`).

```powershell
$env:CODEX_KIT_ONLY = "01"
& $editor $project -run=pythonscript "-Script=`"$script`"" -unattended -nullrhi -nosplash -log
Remove-Item Env:CODEX_KIT_ONLY
```

Тот же один дом аргументом:

```powershell
& $editor $project -run=pythonscript "-Script=`"$script --only 01`"" -unattended -nullrhi -nosplash -log
```

`CODEX_KIT_OUT` переопределяет путь json. Иначе файл:

`C:\Users\Zephyrus15Duo\Documents\Unreal Projects\CodexTactics-agents\Saved\KitDump\kit_buildings.json`

Дальше чистый Python, редактор не нужен. Рядом появятся `kit_plans.json` и `kit_plans.md`.

```powershell
python Scripts/Tools/kit_plan_report.py --input "C:\Users\Zephyrus15Duo\Documents\Unreal Projects\CodexTactics-agents\Saved\KitDump\kit_buildings.json"
```

Без `--input` скрипт берёт `Saved/KitDump/kit_buildings.json` текущего репозитория, а если его нет — тот же путь в соседнем `CodexTactics-agents`.

В логе commandlet одна строка на карту: `G013 kit dump: ... actors=N mesh_records=N errors=N`. Сломанный актор или карта не останавливают остальные. Незагруженная карта остаётся в json с `loaded: false` и `error`, без чужих акторов.

Схема `codex.kit_buildings/1`, сантиметры. `buildings[]`: id, map, world, actors. У каждой постановки mesh (путь пакета и имя), location / rotation / scale, bounds_min / bounds_max. Инстанс ISM/HISM — отдельная запись, `instance` с нуля. Обычный меш: `instance: null`. `meshes[]`: каждый StaticMesh под `/Game/SovietHouses/Meshes`, локальный размер коробки и min/max.

## Что считает отчёт и как

`Scripts/Tools/kit_plan_report.py`. Схема `codex.kit_plans/1`. Это эвристика по именам и коробкам, не обмер планировки архитектором.

- След здания — объединение XY у `Slab_F*`. Нет плит: стены, крыша, лестницы, парапет, цоколь. В метрах: width по X, depth по Y.
- Этажи — индексы `Slab_F0..`. Высота этажа — медиана разниц `bounds_min.z` соседних плит. `Interior_F*` этажом не считается.
- Подъезд — меш с `Ent_Door`. `StairEnt_*` это наружная лестница входа, не дверь.
- Лестничная клетка — кластер в 250 см из `SM_SH_Stair_*`, `StairGlass_*`, `IntWall_stair*`, `Steps_*`. `StairWall_*` остаётся фасадом. Число кластеров = число секций. Нет клетки, но след есть: секция одна.
- Улица / двор. Длинная сторона следа, где больше объёмов `Balc_*`, называется улицей. Противоположная длинная — двором. Короткие стороны — торцы. При равенстве балконов улицей берётся сторона `max_*`. Это догадка: набор не пишет, где улица.
- Двери квартир. Считаются `Door_Apt`. `IntWall_aptdoor` берётся только если листьев `Door_Apt` в доме нет: стена с проёмом и полотно стоят в одном проёме. Дверь относится к ближайшей клетке и к плите с ближайшим `bounds_min.z`. Квартир на лестничную площадку — медиана по парам (клетка, этаж), где есть хотя бы одна дверь.
- Комнаты на квартиру = `1 + (IntWall_part* и IntWall_partdoor*) / число дверей квартир`. Перегородка режет комнату. Общие стены квартир (`IntWall_apt`, не aptdoor) в это не входят.
- Окна — меши `Win_*`, по фасаду. Ширина — поставленный габарит вдоль фасада. Суффикс `_28` / `_30` / `_32` — номинал в дециметрах (280 / 300 / 320 см). Значения вне 20..40 не ширина модуля.
- Модули фасада `Wall_*` и `Rust_*` считаются отдельно: вид проёма (`win`, `solid`, `shop`, `shopdoor`, `balc`, ...) и номинал из имени, если он есть.
- Балконы по фасаду — только объёмы `Balc_*`, не стеновые `_balc_`.
- Магазин: в имени есть `shop`. `shopdoor` — дверь, остальное — витринный модуль (окно, стена, руст).
- Крыша: есть `Dormer` → `pitched_dormer`. Иначе есть `Roof` или `Parapet` (не лестничный) → `flat`. Иначе `unknown`.
- Буква Г: нижняя плита на сетке 300 см занимает не больше 82 % своей коробки и хотя бы один угол пустой.
- Серия по большинству семейств стен. Панель и 5 этажей → `panel 5-storey (1-464 / 1-335 guess)`. Панель и 9+ → `panel high-rise`. Кирпич и 4+ → `brick (1-447 guess)`. Штукатурка и 4+ → `Stalin (plaster)`. Доски → `barracks (planks)`. Буква Г и магазин перекрывают это: `L-block with shops`. Иначе `unknown`. Это ярлык, не паспорт серии.
- Сетка 300 см и 50 см. Ноль — минимум следа. У каждой поставки по X и Y, если сторона толще 40 см (тоньше — толщина стены, её не проверяем): min, max и ширина. Попадает, если остаток от шага не больше 2 см. В json полный список имён и до 40 подробных промахов. Каталог мешей повторяет проверку по локальным сторонам толще 40 см (`fit_300` / `fit_50` — вся коробка, высота тоже). `module_cm` — суффикс имени, а без суффикса длинная сторона; `module_fit_*` смотрит только её.

Сводка всех домов в `summary` и в таблице `kit_plans.md`: id, догадка серии, след, этажи, высота, секции, квартир на площадку, входы, крыша.

## Тест

`python -m unittest Scripts/Tools/tests/test_kit_plan_report.py -v`

Два фальшивых дома. 01: плиты на 0 / 300 / 600 см, след 9×6 м, дверь `Ent_Door` на длинной стороне без балкона, два `Door_Apt` на этаж, окно `_28` шириной 280 см, панельная стена 0..300 см, балкон на противоположной длинной стороне, плоская крыша. 02: след 5×4 м, высота 280 см, доски, люкарна, вход с торца.

```
kit plans: 2 buildings -> C:\Users\ZEPHYR~1\AppData\Local\Temp\tmpmcq4d2kq\out\kit_plans.json
kit plans markdown -> C:\Users\ZEPHYR~1\AppData\Local\Temp\tmpmcq4d2kq\out\kit_plans.md
test_entrances_stairwells_and_flats_per_landing (Scripts.Tools.tests.test_kit_plan_report.KitPlanReportTest) ... ok
test_facades_roof_and_series (Scripts.Tools.tests.test_kit_plan_report.KitPlanReportTest) ... ok
test_grid_misfits (Scripts.Tools.tests.test_kit_plan_report.KitPlanReportTest) ... ok
test_storeys_and_height (Scripts.Tools.tests.test_kit_plan_report.KitPlanReportTest) ... ok
test_summary_and_files (Scripts.Tools.tests.test_kit_plan_report.KitPlanReportTest) ... ok

----------------------------------------------------------------------
Ran 5 tests in 0.035s

OK
```

Проверено: 3 этажа и высота 3.0 м у первого, 2 этажа и 2.8 м у второго, вход со двора и с торца, 2 и 1 квартира на площадку, окно 280 см мимо сеток 300 и 50, стена 300 см на обе сетки встаёт, плита 500 см мимо 300 и на 50, серия `panel` / `barracks`, крыша `flat` / `pitched_dormer`.

## Гейт

Юнит-тест выше: OK. `git diff --check` и `git diff --cached --check` пустые.

`python Scripts/Tools/typesafe_triage.py --audit-diff --agent grok`

```
[Jev:System1] Auditing staged (the commit) changes for agent 'grok' (4 files)...
 • Boundary Breach : PASS (code check of protected paths)
 • Risk Rating     : 0.0 / 4 (Jev)
```

Четыре файла: `Scripts/Editor/dump_kit_buildings.py`, `Scripts/Tools/kit_plan_report.py`, `Scripts/Tools/tests/test_kit_plan_report.py`, этот отчёт.

Редактор, C++, карты, генератор (`Tools/TrainingGround`, `build_levelgen_map.py`) не трогались. В индексе только эти четыре файла. Неотслеживаемый `Content/` в коммит не входит.

## Не сделано

- Дамп 20 карт не снят. `kit_buildings.json` / `kit_plans.md` по живому паку нет.
- Улица и двор, серия, буква Г и комнаты на квартиру не сверены с домами. После дампа цифры надо посмотреть глазами на двух-трёх домах (панель в 5 этажей, кирпич, дом с магазином).
- Пивот набора может сидеть не в углу модуля. Сетка считает рёбра мировой коробки, не пивот. Если коробка меша шире видимого модуля, промахи будут лишними.
- Акторы внутри незагруженного level instance в список не попадут: скрипт не подгружает подуровни и не сохраняет мир.
- `set_dirty_flag` может отсутствовать в Python UE 5.8. Тогда dirty-бит останется, а запись на диск всё равно не вызывается. Имеет смысл после commandlet глянуть, что `Content/SovietHouses` не изменился.
