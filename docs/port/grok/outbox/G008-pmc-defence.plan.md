# G008 план — роли обороны PMC (P1): часовой доступа, сектора, дистанция, Objective как зона

Код в этом коммите не менялся. Ветка `grok/G008-pmc-defence` от main. Роутер: `model_router.py` в этом шаге не вызывался (шелл без одобрения). По README это **уровень 3** (логика командира, C++). Модель: **grok-4.7**.

Спека: `docs/design/PMC_DEFENCE.md` Diagnosis, D1, D4 (в этом задании только `defender_spread_m` и `guard_near_access_share`). Заметки дизайнера 1, 1b, 2 (часть про «один боец — одно укрытие»). D2 (мины, setup) и D3 (реакция атакующих) не делаю.

## Что сломано (по коду, без прогона)

Ролей «часовой доступа / стрелок сектора» нет. `EPmcRole` — только Attacker / Defender (`PmcCommanderRules.h`).

Стягивание в круг — приказ Objective, не расстановка постов:

- Счёт защитника: `PmcCommanderRules.cpp` `ScoreOrder`, ветка `EPmcOrder::Objective` (~238–245). Пока он дальше `0.7 * радиуса` и враг на точке — Retake. Дальше overwatch-радиуса — Return. Вне точки при push или после 60 % раунда — Occupy (на точку). Push глушит укрытие (`PushDamp`, ~129).
- Исполнение: `PmcSquadSubsystem.cpp` ~1970–2000. Каждый боец идёт в свою точку кольца `Index * 120° * 0.4 * радиус` вокруг точки, иначе на саму точку.
- Второй рычаг, слабее: `ObjectivePositionFactor` (~668–693) для защитника даёт `1 + overwatch * reach` слоту в радиусе оружия от точки. На крыше это почти вся крыша, в один угол само по себе не ставит. Главный сдвиг — приказ Objective. Долю 17–23 % беру из Diagnosis (ночь 2026-11-18), не из нового прогона. После смоука посчитаю её по событиям `tactic` / `decision=Objective` стороны защитников (они уже пишутся, `PmcArenaCommand.cpp` ~1759–1785).

`PmcTacticalRules::PlanPlacement` — посты тактического сценария при спавне (снайпер на лучшей крыше, один на группу, 3 м). Roofs-сценарий арены это не вызывает: сайт — случайная точка навмеша на высоте ≥ 1 (`PmcArenaCommand.cpp` `FindSite`). На смерти посты не переназначаются.

Крыша в тактическом файле (`TacticalMapRules.h` `FTacticalOverwatchRoof`): `access[]` — только id (`stair:<id>` / `ladder:<id>` / `hatch:<id>`), `access_kind` = stairs, если есть лестничный марш, иначе ladder, иначе hatch (`tacticalMap.ts` `accessKind`). Мировых координат марша и люка в файле нет. `ladder_tops` (xyz) есть только у `ladder_only_roofs` того же building, индекс совпадает с `ladders[]`.

`L_Gen_Depot_1` тактического файла не имеет (в `Content/Data/LevelGen/tactical/` лежат X2, X4, X6). `GetTacticalMap()` на арене Depot пустой. X4 внешние лестницы имеет: семь крыш с `access_kind: ladder` (gallery), например `B_gallery` / `LAD_B_gallery`.

Укрытие: `FindCoverNear` (`PmcSquadSubsystem.cpp` ~2058) не смотрит ни дистанцию до товарища, ни «один актор стены». Идентичность слота — `StableCoverSlotId` (актор + точка). Один объект укрытия = `WallActor` (`FCoverSlot::WallActor`).

## Как чиню

Новые чистые правила: `Source/CodexTactics/Public/AI/PmcDefenceRules.h`, `Private/AI/PmcDefenceRules.cpp`. Подсистема только собирает вход и исполняет. Тесты `CodexTactics.AI.PmcDefence.*` в `Source/CodexTacticsTests/Private/AI/PmcDefenceRulesTest.cpp`.

Назначение — в `UPmcSquadSubsystem`, когда у команды роль Defender и цель жива: в момент `SetTeamObjective` и снова, только если набор живых изменился. Живые сохраняют роль. Умер часовой — часовым становится живой стрелок, ближайший к доступу (ничья — меньший индекс в скваде). Сектора живых стрелков делятся заново, потому что N изменилось. Атакующих не размечаю.

Один часовой, остальные стрелки секторов. Первый часовой — ближайший к точке доступа (ничья — меньший индекс).

Сектор: дуга `sector_arc_deg` (180) с осью «от цели к спавну атакующих», N равных клиньев на N стрелков. Пост стрелка — спот `overwatch_roofs[].spots` этого клина (край крыши; лучший `covered_m`). Нет спота — слот укрытия в том же клине. Сдвиг по краю — существующий Relocate, но цель только следующий спот того же клина: после `shift_after_s` на посту или под подавлением. Чужой клин и чужая крыша для этого Relocate дают 0.

Часовой:

- Внутри (stairs / hatch): стоит в `guard_dist_m` от головы марша / люка, взгляд на эту точку.
- Снаружи (ladder): пост = `ladder_tops` + внутрь на `guard_inset_m` к центроиду спотов крыши. Взгляд наружу через верх лестницы (лестница и улица в одном секторе огня).
- Исполнение — тот же путь, что пост Ambush (дойти, встать, смотреть в yaw). Пока он в зоне, Objective = 0.

Зона защитника (счёт Objective = 0, пока он внутри, включая push и поздний таймер):

- Крыша выбрана: тот же уровень и он не дальше самого дальнего спота от центроида спотов.
- Крыши нет (земля): тот же уровень и дистанция до точки ≤ радиус × `area_factor`.

Вне зоны приказ ведёт на его пост (часовой — к доступу, стрелок — в свой клин), не в круг захвата. Ветка атакующего в `ScoreOrder` и `ObjectivePositionFactor` не меняется. Часы захвата не меняются: стоять в круге защитник больше не обязан. Это прямое правило D1.

Дистанция укрытия в `FindCoverNear`, множитель на уже посчитанный score: тот же `WallActor`, что у занятого слота товарища — 0. Слот ближе `min_spacing_m` к занятому — × `spacing_penalty`. Дальше — без множителя. Занятый слот = текущий пост или укрытие другого живого защитника.

Точка доступа (TacticalMapSubsystem только читаю, файлы карты не правлю):

- Файл есть. Крыша — `overwatch_roofs` того же уровня, чей спот ближе всех к цели. `access_kind == ladder`: верх из `ladder_only_roofs` того же building, id из `access[]` (`ladder:<id>` = `ladders[]`). `stairs` / `hatch`: xyz в тактическом файле нет. Беру их чтением layout, на который level JSON уже ссылается (`levelgen_layout`): `buildings[].stairs[]` (`id`, `top`, `to`) и `hatches[]`, id из `access[]`. Z = `overwatch_roofs[].z`. `PropsLayoutView` не трогаю (марши там приватные). Парсер — чистая функция в `PmcDefenceRules`, тест на короткой JSON-строке.
- Файла нет (Depot_1, смоук roofs). Та же раскладка: здание, чей footprint содержит точку раунда; марш с `to >= floors`, иначе люк, иначе верх лестницы layout. Нет ни одного — следующий сайт roofs (`FindSite` Skip), порог смоука не ослабляю.

Прошу подтвердить этот обход: тактический файл не содержит xyz марша и люка, поэтому внутри-доступ читается из layout, а не из `TacticalMapSubsystem`. Снаружи-лестница читается из `ladder_tops`.

Числа только в `Content/Data/AI/pmc_commander.json` (не гены, `pmc_commander.bounds.json` не трогаю — его нет в allowed paths):

| ключ | значение | зачем |
|---|---|---|
| `guard_dist_m` | 4 | часовой «у доступа»; метрика guard share |
| `guard_inset_m` | 1.5 | сдвиг поста внутрь от верха внешней лестницы |
| `min_spacing_m` | 5 | штраф соседнего слота; порог spread в смоуке |
| `spacing_penalty` | 0.25 | множитель score слота внутри `min_spacing_m` |
| `area_factor` | 2 | земная зона = радиус точки × это |
| `shift_after_s` | 8 | сдвиг стрелка вдоль кромки |
| `sector_arc_deg` | 180 | дуга со стороны атакующих |

`relocate_after_s` не переиспользую: это «засветился на месте», другое правило.

Метрики. В каталог `PmcArenaBehaviourRules` (схема `codex.pmc_arena_behaviour/1`, два ключа в конец, старые 11 на месте):

| key | unit | range | direction | что |
|---|---|---|---|---|
| `defender_spread_m` | m | 0..40 | higher | среднее попарное расстояние живых защитников, сэмпл 1 Гц, потом среднее по сэмплам |
| `guard_near_access_share` | fraction | 0..1 | higher | доля сэмплов, где хотя бы один живой защитник в `guard_dist_m` от доступа (тот же уровень) |

Пишет сторона-защитник. У атакующего оба значения 0. Дописываю в `docs/port/grok/outbox/G002-arena-behaviour-schema.md`. Тренер (Python) не трогаю: лишние ключи он не требует, оси QD — фаза P4.

Рядом, не в каталоге, на раунде объект `defence`: `spread_m`, `guard_share`, `shared_cover_pairs` (два защитника на одном `WallActor` в одном сэмпле), `objective_share` (доля приказов Objective у защитников). Смоук и отчёт читают его. В каталог эти два поля не кладу.

Смоук `CodexTactics.PmcRoofDefenceSmoke` — новый `Source/CodexTacticsDev/Private/Debug/PmcRoofDefenceSmokeCommand.cpp`, строка в `Scripts/test_map.json`. Карта как у `PmcStalemateSmoke`: `/Game/Maps/Generated/L_Gen_Depot_1`, `-LevelJson=L_PmcArenaDepot.json`, один раунд `scenario=roofs`, сквад 3, фиксированный seed. Пороги: средний spread ≥ 5 м, guard share > 0.6, `shared_cover_pairs` = 0. Три прогона. Внешняя лестница X4: юнит-тест на числах `B_gallery` из `L_Gen_Depot_X4.tactical.json` обязателен. Живой раунд X4 — только если roofs-сайт за несколько Skip садится на крышу `access_kind: ladder`; иначе пометка в отчёте, гейт на этом не висит.

Тесты, которые сейчас требуют «защитник к концу раунда / в тишине идёт на точку», поменяю с причиной в коммите (ожидание сменилось правилом D1, порог не ослаблен): `PmcArenaObjectiveRulesTest.cpp`, `PmcStalemateRulesTest.cpp`, `PmcOnPointLevelTest.cpp`. Счётчик метрик 11 → 13: `PmcArenaBehaviourRulesTest.cpp` и проверка в `PmcArenaCommand.cpp` (~2404). Атакующие проверки не трогаю.

## Файлы

- новые: `PmcDefenceRules.h/.cpp`, `PmcDefenceRulesTest.cpp`, `PmcRoofDefenceSmokeCommand.cpp`
- правки: `PmcCommanderRules.h/.cpp` (числа + счёт Objective защитника), `PmcSquadSubsystem.h/.cpp` (роли, пост, Relocate по клину, дистанция в `FindCoverNear`), `PmcArenaBehaviourRules.h/.cpp`, `PmcArenaCommand.cpp` (сэмпл 1 Гц, JSON, проверка каталога), `Content/Data/AI/pmc_commander.json`, `Scripts/test_map.json`, `docs/port/grok/outbox/G002-arena-behaviour-schema.md`, три теста ожидания защитника выше
- отчёт потом: `docs/port/grok/outbox/G008-pmc-defence.report.md`

## Как докажу

Сборка `Scripts/build.ps1`. Тесты `CodexTactics.AI.PmcDefence`, затем затронутые `CodexTactics.AI.PmcCommander` / `CodexTactics.AI.PmcArena`. Смоук `PmcRoofDefenceSmoke` трижды. Дальше по гейту: `PmcArenaTacticalSmoke`, `PmcArenaSmoke`, `PmcArenaObjectiveSmoke`, `TacticalMapSmoke`, `Scripts/test.ps1 -Smart`, `python Scripts/Tools/typesafe_triage.py --audit-diff --agent grok`. В отчёт — spread, guard share и доля Objective до/после (до = 17–23 % из Diagnosis, после = события смоука), хвост команд.

## Не делаю

D2 и D3 (мины, setup, разминирование), гены тренера, `pmc_commander.bounds.json`, правки `TacticalMapSubsystem` и тактических JSON, карты, `.uasset`, `Config/**`, `L_Gen_Depot_1.json`, G007. Часы захвата и приказы атакующего не меняю. Порог смоука не ослабляю.
