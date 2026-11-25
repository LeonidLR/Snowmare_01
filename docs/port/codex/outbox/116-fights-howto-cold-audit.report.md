# 116 · Запись секторных боёв и аудит холода

Основа `688c8038`. Инструкция `docs/port/FIGHTS_HOWTO_RU.md` занимает 14 строк.

- Training Ground сохраняет выбор в `Saved/Training/active_sector.json` (`Tools/TrainingGround/server/trainingApi.ts:52`, кнопка в `AiTrainingDepartment.tsx:149`). `TrainingSectorSubsystem.cpp:170` регистрирует `Codex.Sector`. Строки 237–250 читают `-Sector=` перед сохранённым выбором, строки 275–279 снимают границу и заканчивают упражнение для сектора 0. Смена сектора в игре действует на текущую сессию.
- `SessionRecordingSubsystem.cpp:29` регистрирует `Codex.Demo.Record`, строки 72–85 проверяют его при входе мира и выбирают `Saved/Demos`. Строки 146–177 записывают при выгрузке и отбрасывают сессию короче 60 с, без принятых приказов или с подавленной записью. Режим 1 рассчитан на обычную игру (`SessionRecordingRules.cpp:7–15`).
- Размер отряда игрока задан в `CodexTacticsGameMode.cpp:107`, ЧВК имеют `count: 3` во всех шести секторах. `TrainingSectorSubsystem.cpp:584–603` обрабатывает исход и выбирает следующую пару старта и поста. `TrainingSectorRules.cpp:316–339` задаёт условия пополнения и надпись «Раунд N». В `L_Gen_Depot_1_X6.sectors.json:3–6` заданы 8 с до нового раунда, предел 600 с и 20 с для пополнения.

## Холод на пяти картах

| Карта | Штатный запуск | Другие пути |
| --- | --- | --- |
| `L_Gen_Depot_1_X6` | `L_Gen_Depot_1_X6.json:6` — 0, в том числе при выборе сектора. | `ColdIndoorsSmoke` считает отдельную пробу с `LevelMultiplier=1`. Явный запуск X6 с `-LevelJson=L_PmcArenaX6.json` до этой правки получал 1. В арене добавлены две строки, теперь 0. |
| `L_Demo01_A_FrozenLake` | `L_Demo01_A_FrozenLake.json:14` — 0. | Учебный порыв `A1_gust` срабатывает через 20 с после мостика при максимуме холода отряда ниже 40. Добавляет 20, минимум 42 (`demo_01_steps.json:193–196`). Явный override `SmokeFixtures/lake_rules_fixture.json:13` задаёт 0,4 для теста правил. |
| `L_Gen_StoryShowcase_Kit` | `L_Gen_StoryShowcase_Kit.json:6` — 0. | Свежая временная карта с иным именем или LevelJson может попасть на значение 1. Builder сохраняет прежний `cold_rate_mult` при наличии (`build_levelgen_map.py:1606–1610`). В доступном прогоне дверей `Saved/Logs/Worker/merge-gate-20261125-033429-252/Smoke-DoorBreachSmoke.log:459` явно выбран Kit LevelJson со значением 0 в отслеживаемом файле. Результат PASS на строке 2052. Для первого отказа выстрела, о котором сообщил G018, исходный effective LevelJson и cold state не установлены. |
| `L_VehicleTest` | Своего файла не было. Обычная логика выбирала `level_01_outpost.json`, где поле отсутствовало. Новая копия `L_VehicleTest.json:9–10` оставляет структуру fallback и устанавливает 0. | В логе VehicleCombat `Saved/Logs/Worker/merge-gate-20261125-004517-724/Smoke-VehicleCombatSmoke.log:459` нет override, а строка 1427 показывает три волны, 60 с подготовки и 20 с отдыха из fallback. Это касается и бойцов вне машины. |
| `L_MainMenu` | `CodexFrontendPlayerController.h:49` указывает режим меню без игрового pawn. Меню использует визуальные акторы. | `MainMenuSmokeCommand.cpp:239` выставляет значение бойцу уже после перехода в игру для сценария сохранения. |

## Что обходит множитель

- `LevelJsonRules.cpp:486` и `WaveConfigTypes.h:326` дают 1 при пропущенном поле. `GatherEnvironment` читает активный LevelJson (`ColdSurvivalComponent.cpp:178–182`). `ColdRules.cpp:50` умножает только пассивное накопление. Коэффициенты погоды, волны и укрытия остаются множителями. Базовая скорость 1 в `ColdRules.h:36`, баланс может её переопределить (`OperativeBalance.cpp:71–78`). Глобальная команда `Codex.Cold` не зарегистрирована.
- `TutorialSubsystem.cpp:1007` вызывает `AddColdShock`, который пишет прямо в `ColdLevel` и проверяет замерзание оружия (`ColdSurvivalComponent.cpp:133–142`). Сохранения восстанавливают `cold_level`, `frostbitten` и `weapon_frozen` (`SaveGameSubsystem.cpp:446,472`). Сценарий рекрута начинает с 85 (`RecruitSubsystem.cpp:160`). Оружие может добавить `SelfColdPerShot` (`OperativeCharacterCombat.cpp:1332`), значение оружия по умолчанию 0 (`WeaponDataAsset.h:211`), положительных JSON-полей `self_cold_generation` в отслеживаемых данных нет.
- При уже высоком `ColdLevel` обморожение, принудительный prone и урон продолжают считаться по состоянию (`ColdSurvivalComponent.cpp:252–283`). По умолчанию осечки начинаются от 60, оружие замерзает от 90 вне тепла, а порог оттаивания ниже на 5 (`ColdRules.h:103–118`). Для полного отключения этих путей нужен отдельный запрет прямого начисления и обработки сохранённого состояния. Урок `A1_gust` потребует отдельного решения.
- Из активных проверок X6 `ColdIndoorsSmokeCommand.cpp:57–62` фиксирует только формулу пробы укрытия, без включения холода всему миру. `GateExitLakeSmokeCommand.cpp:862`, `SaveCoverageSmokeCommand.cpp:543,674`, `MainMenuSmokeCommand.cpp:239` и `TutorialSmokeCommand.cpp:416,1169,1196` задают холод в своих сценариях. Архивные или ручные сценарии с прямой установкой: Banners, Barrel, Cold, Deployable, DepotCampaignStart, Dialogue, InjuredLocomotion, Inventory, Mission, PMCTripwire, RegroupAfterLosses, Relocation, SaveLoad, SaveRoundTrip, TacticalHeat, Transfer, Vehicle, WarmIdle. Отдельный запуск/override способен обойти штатный LevelJson.

## Проверка и границы

Python проверил JSON Schema для обоих изменённых файлов, а также точное равенство старых параметров после исключения двух новых ключей. X6 и Kit также прошли схему. На неизменённом FrozenLake схема уже отклоняла авторские имена полос, например `NORTH_ICE` и `EAST_GULLY`. Проверены пять игровых файлов с нулевым множителем, включая альтернативную арену X6, 14 строк инструкции и имена команд.

C++, карты, общий fallback, tutorial и параметры смоуков без правок. Unreal и worker не запускались, поэтому в отчёте проверка по исходникам и имеющимся логам.
