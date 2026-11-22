# Консольные команды CodexTactics (справочник дизайнера)

Составлено 2026-11-22 по коду (`TAutoConsoleVariable`, `FAutoConsoleVariableRef`, `FAutoConsoleCommand*`) трёх веток:
**main** (~644 имени), **claude/bgen-test** (здания: +13), **ai/arena-determinism** (ИИ: +1).
Колонка «Где есть»: `main` = есть везде; иначе команда есть только в названной ветке, в обычной игре из main её нет.

## Главное сейчас (то, что вам давали в чате)
| Команда | Что делает | Где есть |
|---|---|---|
| `Codex.Doors.Openable 1` | Включить открываемые двери (1 клик — рукой, 2 клика — выбить ногой) | только CodexTactics-agents |
| `Codex.Doors.GiveKey *` | Дать отряду ключ ко всем запертым дверям (для проверки) | только CodexTactics-agents |
| `Codex.Formation.Confined 0` / `1` | Выключить / включить сжатие строя в квартирах и узких коридорах | main |
| `Codex.Formation.ConfinedQueue 0` / `1` | Выключить / включить очередь у двери (входят по одному) | main |
| `Codex.RoofCutaway.HiddenShadow 0` / `1` | Тень от скрытых этажей (срез этажей) | только CodexTactics-agents |
| `Codex.Random.Trace 1` / `2` | Лог случайных бросков / + позиции бойцов (для разработчиков) | только ветка ИИ врагов |

## Как вводить
- Консоль открывается клавишей `~` (тильда). Переменная: `Имя значение` (например `Codex.Formation.Confined 1`); `Имя` без значения печатает текущее.
- Значение **не сохраняется** после перезапуска: ни одна переменная не помечена `ECVF_Archive`, в `Config/*.ini` Codex-переменных нет. После каждого запуска вводить заново (или добавлять в `-ExecCmds=`).
- Для большинства переключателей: `1` = включено (новое поведение), `0` = выкл / откат на старое (для A/B). `-1` = «решает JSON-данные уровня или конфига» (обычно это и есть значение по умолчанию).
- Команды вида `Имя [арг]` — действия, а не переменные; квадратные скобки = необязательный аргумент.
- Описания пунктов, у которых в коде нет справки, выведены из имени переменной и помечены «(по имени)».

## Двери и здания
| Команда | Что делает | Значения (по умолч.) | Где есть |
|---|---|---|---|
| `Codex.Doors.Openable` | Открываемые двери: закрытые/запертые створки сгенерированных зданий становятся дверями (открыть кликом, авто-открытие на пути, запертая блокирует навмеш, выбить). 0 = створки как раньше (декор) | -1 (= `doors.json` enabled, по умолч. 0); 0 / 1 | только claude/bgen-test |
| `Codex.Doors.Seed` | Seed бросков дверей (выбивание, травма, выстрел в замок) | -1 (= `-DoorSeed=` или `doors.json` roll_seed); число | только claude/bgen-test |
| `Codex.Doors.GiveKey <здание\|проём\|тег\|*>` | Тест: отряд получает ключ (все запертые двери здания / проём / значение door_key / все) | аргумент обязателен | только claude/bgen-test |
| `Codex.Props.Enabled` | Спавн пропсов генератора уровней | 1; 0 | main |
| `Codex.Props.Real` | Пропсы появляются настоящими объектами игры (бочки, лут) | 1; 0 | main |
| `Codex.Props.Greybox` | Серый вид пропсов без своего класса: 1 автогенерация полигона, 2 все файлы, 0 выкл | 1; 0/1/2 | main |
| `Codex.Props.MinesInert` | Мины-пропсы безопасны (по имени) | 0; 1 | main |
| `Codex.Props.Dump` | Лог: сколько пропсов по слоям, отброшенные ID | действие | main |
| `Codex.MissionGoals.Enabled` | Цели генератора как задания миссии (`objectives/<Карта>.objectives.json`) | 1; 0 | main |
| `Codex.MissionGoals.Dump` | Лог целей карты и какие достигнуты | действие | main |
| `Codex.TacticalMap.Draw` | Рисует тактическую карту вокруг вида (места по оценке, точки спуска, окна, крыши) | 0; 1 | main |
| `Codex.TacticalMap.Dump` | Лог тактической карты: счётчики, лучшие места, опасность у пешки | действие | main |
| `Codex.Gate.OpenMode` | Открытие ворот: 0 сразу (терминал), 1 генератор + волны | -1 (= JSON gate_open_mode, по умолч. генератор+волны); 0/1 | main |
| `Codex.Gate.ProgressPerSecond` | Скорость прогресса открытия ворот (по имени) | -1 (данные) | main |
| `Codex.Mantle.Links` | Авто-линки перелезания через подходящие препятствия (путь приказа сам перелезает) | 1; 0 | main |
| `Codex.Mantle.Batch2` | Пакет 2 исправлений перелезания (защита стены, выход клипа) | 1; 0 | main |
| `Codex.Mantle.Align`, `Codex.Mantle.Squad` | Выравнивание на перелезании; очередь перелезания отряда (по имени) | 1; 0 | main |
| `CodexTactics.MantleLinks` | Пересоздать линки перелезания сейчас | действие | main |
| `Codex.Fall.DropLinks` | Генерация линков-спусков (падение вниз) | 2 | main |
| `Codex.Fall.DropLinkReach` | Линк спуска должен приземляться там, откуда достижим старт (против залипания) | 1; 0 | main |
| `Codex.Fall.LinkCommit` | Бойца на линке-спуске не сбрасывают с пути (против залипания) | 1; 0 | main |
| `Codex.Fall.Rescue` | Страховка: боец вне навмеша / в тупике после падения возвращается | 1; 0 | main |
| `Codex.Fall.SlotClips` | Клипы падения/приземления из C++: 0 никогда, 1 всегда, 2 если у ABP нет FallStates | 2; 0/1/2 | main |
| `Codex.Fall.AirGravity` | Усиленная гравитация падения: 0 выкл, 1 линки и отбрасывание, 2 + любое падение с уступа | 2; 0/1/2 | main |
| `CodexTactics.DropLinks [clear]` | Пересоздать линки спусков / убрать | действие | main |
| `Codex.Click.Preview` | Единый клик по зданию во всех режимах (без превью по наведению) | 1; 0 | main |
| `Codex.Click.LowerHighHits` | Попадание курсором высоко на стене опускается к груди бойца | 1; 0 | main |
| `Codex.Test.ShowBreachGhost [id\|hide]` | Голограмма штурмовой тройки у проёма | действие | main |

Перелезание через окна (window vault): отдельной консольной переменной в коде ветки bgen не нашёл (не проверено, чем управляется).

## Строй и отряд
| Команда | Что делает | Значения (по умолч.) | Где есть |
|---|---|---|---|
| `Codex.Formation.Confined` | «Весь отряд входит»: в тесном кармане навмеша (зал, коридор 2,5 м) слоты строя сжимаются вокруг лидера. 0 = собственные слоты строя (колонна оставалась снаружи) | -1 (= `squad_confined.json` enabled); 0 / 1 | main |
| `Codex.Formation.ConfinedQueue` | Порядок входа: 1 = очередь по глубине (первый в дверь занимает самое дальнее место), 0 = правило стоимости раунда 3 | -1 (= JSON entry_queue); 0 / 1 | main |
| `Codex.Formation.ConfinedCrowd` | 0 = в тесном кармане у ведомых выключены разведение толпы и обход препятствий | 0; 1 | main |
| `Codex.Formation.Sectors` | Секторы взгляда строя (ромб, клин, колонна, эшелон, стена) | 1; 0 | main |
| `Codex.Formation.CoverAllocator` | Взгляды 3+ бойцов от распределителя покрытия (правила v2.2) | 1; 0 | main |
| `Codex.Formation.Menu <режим>` | Замена меню строя по удержанию B: auto, line, diamond, echelon_right/left, blizzard, column, hedgehog, all_forward | действие | main |
| `Codex.Formation.Debug` | Отладка строя в лог | 0; 1 | main |
| `Codex.Formation.{TwoCornerHold, LeaderLevelSlots, LookFilter, WallEnd, WallSectors, WallFinalApproach, Approach, ApproachHold, WallTraceBlocking, WallSnap, ShortSideObstacles, RearTurn, YieldToLeader, LeaderPeekCorner, LeaderCorner, WallRoute, WallLeadership, ReformAlongWall, PeekCarry, SavedShapes, V21, SquadWallEndOneDetector}` | Правила строя у стен/углов/подхода (по имени): 1 = новая логика, 0 = откат для A/B | 1; 0 | main |
| `Codex.Squad.WoundPace` | Строй идёт в темпе самого раненого | -1 (= `squad_wound_pace.json`); 0 / 1 | main |
| `Codex.Squad.{SprintStandUp, PostCombatFormation, CombatFormation, SlotFallback, DropPendingCover, SoloOwnStance, SoloIndependent, LeaderSwitchKeepsMove, GiveWay, RampFile, FastRegroup, WallMoveNearestLeads, GroupCornerPeek}` | Поведения отряда (по имени): соло-режим независим, смена лидера не сбрасывает ход, уступание дороги, переформирование | 1; 0 | main |
| `Codex.Squad.RampFile.Debug` | Лог: почему стоящий боец не уступает дорогу выбранному (каждый кадр) | 0; 1 | main |
| `Codex.Posture.DefensiveSquadWide` | Оборонительная стойка на весь отряд (по имени) | 0; 1 | main |
| `Codex.Posture.AggressiveExplorationFire` | Огонь в агрессивной стойке при разведке (по имени) | 1; 0 | main |
| `Codex.GroupCover.{InCombat, LeaderPriority, NearbyCovers}` | Групповое укрытие: в бою / приоритет лидера / ближние укрытия (по имени) | 1; 0 | main |
| `Codex.Cover.{RunCornerPeek, RunPeekStance, RunPeekYawLock, CornerPeekEntryTurn}` | Подход бегом к углу сразу в стойку выглядывания (видео дизайнера 2026-11-08) | 1; 0 | main |
| `Codex.RealTimeOrders` | Приказы сразу в бою в реальном времени; 0 = только в тактической паузе | 1; 0 | main |
| `Codex.Cursor.{TightPick, WallSide}` | Точный выбор курсором / сторона стены (по имени) | 1; 0 | main |
| `CodexTactics.AutonomousSquad [0\|1]` | Режим Commander (автономный бой отряда); без аргумента печатает состояние | действие | main |
| `CodexTactics.ToggleAutonomousCombat` | Переключает режим Commander (Ctrl+T) | действие | main |
| `CodexTactics.DefendObjective [idx\|here]` | Боец держит ближайшую цель (генератор/терминал/ворота/баррикада) любой ценой | действие | main |

## Камера и срез этажей
| Команда | Что делает | Значения (по умолч.) | Где есть |
|---|---|---|---|
| `Codex.RoofCutaway` | Срез крыш/потолков над бойцами отряда | 1; 0 | main |
| `Codex.RoofCutaway.FollowSelected` | Срезается крыша только выбранного бойца (0 = всех, старое правило) | 1; 0 | main |
| `Codex.RoofCutaway.Storeys` | В здании с тегами building/floor срезаются все этажи выше выбранного бойца, включая крышу; свой этаж никогда. 0 = только правило высоты головы | 1; 0 | только claude/bgen-test |
| `Codex.RoofCutaway.HiddenShadow` | 0 = срезанная крыша/этаж не отбрасывает тень (внутри светло); 1 = тень остаётся (старое) | 0; 1 | только claude/bgen-test |
| `Codex.Camera.FreeExploration` | Свободная разведка камерой (по имени) | 1; 0 | main |
| `Codex.Camera.FreePanBounds` | Свободное панорамирование без привязки к бойцу, только по границам уровня | 1; 0 | main |
| `Codex.Camera.OperativeClearance` | Фокус ручной панорамы берёт высоту бойца (камера не уходит под крышу) | 1; 0 | main |
| `Codex.Debug.VisionCones` | Конусы зрения на земле: 0 все выкл, 1 все вкл, -1 по настройке персонажа | -1 | main |
| `Codex.VisionCones.Enemies`, `Codex.VisionCones.Squad` | Конусы всех врагов (клавиша O) / всего отряда (Shift+O) | 0; 1 | main |
| `Codex.VisionCones.{SquadLevels, MaxRebuildsPerFrame, ElevatedFix, MaxMsInFlight, RefineGapCm, Parallel}` | Производительность и качество конусов (по имени): пересборок за кадр, параллельность | 2 / 6 / 1 / 150 / -1 / 2 | main |
| `Codex.Minimap.DefaultPanel` | Правая верхняя панель до выбора игроком: 0 сообщения, 1 миникарта | 0; 1 | main |
| `Codex.Minimap.{CaptureSize, CapturePasses, TopDown, ExposureBias}` | Снимок миникарты: размер, проходы, вид сверху, экспозиция | 2048 / 4 / 1 / 0 | main |
| `CodexTactics.HUD.ShowStatus` | Панель статуса отряда и подписи над бойцами | true; false | main |
| `Codex.HUD.WoundTags` | Теги ранений над головой: -1 = `hud_labels.json`, 0 выкл, 1 вкл | -1 | main |

## Враги / ИИ
| Команда | Что делает | Значения (по умолч.) | Где есть |
|---|---|---|---|
| `Codex.Enemy.Tactics` | Тактика ближнего боя стаей (1) / прямой бег (0) | 1; 0 | main |
| `Codex.Enemy.Morale` | Хрупкие враги отступают при ранении | 1; 0 | main |
| `Codex.Enemy.{FlankShareScale, FocusCapBonus, PreferenceScale, MoraleDeathsBonus}` | Множители/бонусы: доля обходящих, лимит атакующих на цель, предпочтения целей, порог морали | 1.0 / 0 / 1.0 / 0 | main |
| `Codex.Enemy.{FlankDetour, FlankArriveCm, FlankSideConeDeg, FlankTimeout, FlankStuckSeconds}` | Обход с фланга: сперва к точке фланга (1) / старый прямой заход (0); допуски по расстоянию, углу, времени | 1 / 150 см / 60° / 8 с / 1,5 с | main |
| `Codex.Enemy.MeleeCloseIn`, `Codex.Enemy.OperativeAttackersBeforeObjects` | Сближение ближнего боя; сколько атакующих бойцов до объектов (по имени) | 0 / 2 | main |
| `Codex.AI.{Repath, RepathInterval, RepathMinGoalDeltaCm}` | Ограничение пере-прокладки пути врагов: 1 только при смене цели/по интервалу, 0 каждый тик | 1 / 0,3 с / 75 см | main |
| `Codex.Marksman.{RetreatCooldown, RetreatMaxSeconds, ShotDamage, BaseAccuracy, AimDuration, ShotCooldown}` | Переопределение параметров стрелка-снайпера | -1 (= из ассета) | main |
| `Codex.Perception.{SightRangeScale, FovScale, ProneVisibilityScale, HearingScale, SmellScale, TimeToDetectScale}` | Множители восприятия врагов: дальность зрения, угол, видимость лёжа, слух, нюх собак, время обнаружения | 1.0 | main |
| `Codex.Perception.HearingOcclusion` | Заглушение слуха преградами (по имени) | -1 (данные) | main |
| `Codex.Patrol.{SearchSeconds, SearchRadius, SearchSpeedScale}` | Поиск патруля после ловушки: время, радиус, множитель темпа | -1 / -1 / 1.0 | main |
| `Codex.Lure.InvestigateSeconds` | Сколько секунд враг проверяет свист/камень | -1 (JSON) | main |
| `Codex.Sight`, `.Exploration`, `.Fov`, `.FovFight`, `.HeightRange`, `.Conditions`, `.LongRange`, `.Peek` | Система тактического зрения и её правила (по имени); 1 вкл | 1; 0 | main |
| `Codex.Sight.{FovStandDeg, FovCrouchDeg, FovProneDeg}` | Поле зрения бойца стоя / сидя / лёжа, градусы | 140 / 140 / 110 | main |
| `Codex.Pmc.Encounters` | Запланированные уровнем отряды ЧВК приходят; 0 = не приходят | 1; 0 | main |
| `CodexTactics.PmcSpawn [count] [weights] [posture=hunt\|defend\|patrol] [min] [max] [seed]` | Вызывает враждебный отряд ЧВК в игру | действие | main |
| `Codex.Horde.Enabled` | Орда: 1 вкл, 0 выкл, -1 данные | -1 | main |
| `Codex.Horde.{AfterFirefights, FirefightWarningSeconds}` | Орда после перестрелок; секунды от предупреждения HQ | -1 | main |
| `CodexTactics.Horde.Release` | Выпустить орду сейчас (dev) | действие | main |
| `Codex.HordeIntro.{SuddenStart, Enabled, AutoPause, LaterWaves}` | Начало орды: внезапное, пролёт камеры, авто-пауза | -1 / -1 / 0 / 0 | main |
| `Codex.HordeIntro.*` (ещё 18 чисел: Mass*, MinDelay/MaxWait, Flyover*, WideShot*, TypeShot*, *Blend*, View*, AlertSeconds) | Тайминги и камеры пролёта над ордой; < 0 = по умолчанию | -1 | main |
| `Codex.Pressure.Enabled` | Давление врагов боевой лаборатории (патрули, засады) | -1 (JSON `pressure`); 0/1 | main |
| `Codex.Pressure.IntervalSec` | Секунды между событиями спавна | 0 (= JSON, 120) | main |
| `Codex.Pressure.Spawn [patrol\|ambush\|coordinated] [n]`, `.Clear`, `.Status` | Спавн события сейчас / убрать всех / состояние | действие | main |
| `Codex.Corpse.{Persist, Cap, FadeSeconds, Freeze, OldestPool}` | Тела врагов: хранить, лимит (-1 = JSON, 150), время исчезания, заморозка | 1 / -1 / 1 / 1 / 10 | main |
| `CodexTactics.Corpse.Status` | Лог тел на уровне | действие | main |
| `Codex.EnemyRegistry` | Реестр врагов; 0 = старые сканы мира (A/B замеров) | 1; 0 | main |
| `CodexTactics.CombatStart [auto\|ambush\|button\|level]` | Как начинается бой на уровне; без аргумента показывает | действие | main |

## Бой и стрельба
| Команда | Что делает | Значения (по умолч.) | Где есть |
|---|---|---|---|
| `Codex.Cheat.Immortal` | Бессмертие отряда (боевая лаборатория); в Shipping нет | -1 (JSON cheats.immortal); 0/1 | main |
| `Codex.Cheat.InfiniteAmmo` | Бесконечные патроны | -1 (JSON); 0/1 | main |
| `Codex.Fire.Gate` | Каждый выстрел бойца ждёт ворота огня: ствол/тело на цели (баги 058c/061) | 1; 0 | main |
| `Codex.Fire.BodyArcDeg` | Угол корпуса для огня (по имени) | из правил | main |
| `Codex.Combat.{HonestFire, Suppression, UnderFireManoeuvre, LauncherAI, ProneCrawlHoldFire}` | Честная стрельба, подавление, манёвр под огнём, ИИ гранатомёта, огонь лёжа ползком (по имени) | 1; 0 | main |
| `Codex.Cover.{HighFrontalAbsorb, LowCrouchedAbsorb, FrontalArcDeg, BlindFireAccuracy, MinBlindHitChance, LeanHitMultiplier}` | Параметры укрытий: поглощение, дуга, слепой огонь, наклон | -1 (= по умолч. FCoverCombatConfig) | main |
| `Codex.Knockdown.{Chance, DamageThreshold, ExplosionRadius, DownedSeconds, GetUpSeconds, GetUpMaxPlayRate, GetUpActionPoints, DownedDamage, ReknockImmunitySeconds, CoverReturn, CoverReturnThreatRadius}` | Нокдаун: порог урона (40), радиус взрыва (250 см), время лёжа (0,6 с), подъём (0,8 с), множитель урона лежачему (1,33), иммунитет 1,5 с | -1 (= по умолч.) | main |
| `Codex.DeathCam.{SlowMoScale, SlowMoRealSeconds, FocusDistance, HoldAfterClipSeconds, MaxFocusRealSeconds, DefeatFadeSeconds, DefeatTextSeconds}` | Камера смерти: замедление (0,3), дистанция (1100 см), удержание (0,4 с), затемнение (1,2 с), текст (2,5 с) | < 0 (по умолч.) | main |
| `Codex.DeathRagdoll.{Enabled, MaxSimultaneous}` | Регдолл при смерти (0 = клип/«лечь»); лимит одновременных | 1; -1 | main |
| `Codex.Mine.Mishap`, `Codex.Relocation.RealTimeCombat` | Осечка мин; перемещение объектов в реальном времени (по имени) | 1; 0 | main |
| `CodexTactics.Mortar{List, Man, Fire, Cease, CrewPmc}` | Миномёты: список гнёзд / расчёт гнезда / огонь в точку / прекратить / расчёт ЧВК | действие | main |
| `CodexTactics.GiveWeapon <id> [idx]` | Выдать оружие (`DA_Weapon_<id>`, например rpg7) лидеру / бойцу №N | действие | main |
| `Codex.Bot.{MarksmanAssault, AssaultClearRadius, AssaultStopDistance, WallCover, WallCoverRadius}` | Поведение тест-бота: штурм одиночных стрелков, укрытие у стен | 1 / 1500 / 900 / 1 / 700 | main |

## Погода и окружение
| Команда | Что делает | Значения (по умолч.) | Где есть |
|---|---|---|---|
| `Codex.Env.SetWeather <Clear\|Overcast\|Snowfall\|Blizzard> [instant]` | Сменить погоду (instant сразу). На умеренных уровнях ещё Sunny/Rain/Storm | действие | main |
| `Codex.Env.SetHour <ч>` | Часы игры 0-24 | действие | main |
| `Codex.Env.ClockScale <x>` | Ускорение часов и погоды (1 = штатно) | действие | main |
| `Codex.Env.Seed <n>` | Перезапуск последовательности погоды с seed (0 = случайно) | действие | main |
| `Codex.Env.Climate <arctic\|temperate>` | Сменить климат на лету | действие | main |
| `Codex.Env.SnowCover <0..1>` | Задать постоянный снежный покров | действие | main |
| `Codex.Env.WindFrom <+X\|-X\|+Y\|-Y\|yaw\|off>` | Откуда дует ветер (переопределение) | действие | main |
| `Codex.Env.Wetness`, `Codex.Env.ObjectSnow <top px nx py ny>` | Мокрота; снег на объектах по сторонам (0..1) | действие | main |
| `Codex.Env.Dump` | Лог: час, фаза, погода, смесь, seed, блокировка, масштаб часов | действие | main |
| `Codex.Env.Visuals` | Презентер погоды управляет солнцем/небом/туманом; 0 = освещение карты как нарисовано | 1; 0 | main |
| `Codex.Env.{CharacterSnow, CharacterSnowMax}` | Снег на персонажах | 1 / 40 | main |
| `Codex.Env.{SnowTracks, SnowTracksRes, SnowTracksAnyGround, SnowTracksFadeScale}` | Следы в снегу: вкл, разрешение (0 авто / 1024 / 2048 / 4096), любой ровный пол, скорость затухания | 1 / 0 / 1 / 1.0 | main |
| `Codex.Env.{SnowOverlay, SnowOverlayMax, SnowOverlayNanite, SnowGround, SnowGroundMax}` | Накладка снега на меши/землю (замер производительности); Max = лимит мешей | вкл | main |
| `Codex.Env.{RainIndoorKill, SnowIndoorKill}` | Нет осадков внутри зданий и под крышами | 1; 0 | main |
| `Codex.Env.Lightning` | Вспышки молний (умеренный климат): -1 авто (выкл на автопрогонах), 0, 1 | -1 | main |
| `Codex.Env.{StartExposureSpeed, StartExposureSeconds, StartExposureNoCap, StartCameraCutFrames}` | Адаптация экспозиции на старте уровня (против мерцания) | из правил | main |

## Повторяемость и логи
| Команда | Что делает | Значения (по умолч.) | Где есть |
|---|---|---|---|
| `Codex.Random.Trace` | Лог каждого сидированного случайного броска игры (`[Rng] кадр поток вызывающий значение`) для сравнения двух прогонов одного seed. **2** = ещё и точное состояние каждого бойца каждый тик | 0; 1; 2 | только ai/arena-determinism |
| `Codex.Debug.{FireTrace, MoveTrace, CornerAim, CoverSwitch, LeftHandIK}` | Диагностика в лог: решения огня, остановки приказов движения со стеком, угловой прицел, проба смены укрытия, сферы левой руки | 0; 1 | main |
| `CodexTactics.AnimTrace` | Лог входов анимации каждый кадр | 0; 1 | main |
| `Codex.TurnArena.Debug` | Лог поиска арены пошагового боя по клеткам | 0; 1 | main |
| `CodexTactics.Dump{Perception, Environment, SquadROE, TacticalHeat}` | Лог действующих значений конфигов (enemy_perception, environment, squad_roe, tactical_heat) | действие | main |
| `CodexTactics.DumpWeaponTuning` | Записывает `Content/Data/Weapons/weapons_tuning.json` из ассетов оружия | действие | main |
| `CodexTactics.Bot [CASUAL\|NORMAL\|VETERAN] [nocollect] [seed=n]`, `Codex.Bot.Route [seconds=] [seed=] [quit]` | Запуск тест-бота / бот идёт по bot_route | действие | main |
| `Codex.Preload.{Enabled, WorldPause, LevelStartWork}` | Асинхронная подгрузка мира перед стартом | 1 | main |
| `Codex.Tutorial.{Enabled, Telemetry}` | Подсказки обучения; телеметрия | 1 | main |
| `Codex.Tutorial.{Skip, SkipTutorial, Reset, Retry, Step <Id>}` | Пропустить карточку / обучение целиком / сбросить / с контрольной точки / запустить шаг | действие | main |
| `Codex.Save.HeadlessCheckpoints` | Писать контрольные точки обучения в безголовых проверках | 0; 1 | main |
| `Codex.HitchProbe.*` (6 порогов) | Пороги проверки подвисаний (HitchProbeSmoke) | числа | main |

## Для проверок
Инструменты разработчиков и автоматизации; дизайнеру запускать не нужно.
- **`CodexTactics.*Smoke`** (около 300 команд в `Source/CodexTacticsDev/Private/Debug/`) — автопроверки, прогоняются скриптами (`smoke_parallel`, имена только полные, например `CodexTactics.MovementSmoke`). Среди них: PmcArena*/Pmc*Smoke (бои ЧВК), Wave/Horde, Formation*, Cover*, Weather/Climate/Environment, Minimap и т.д. Только в ветке зданий: `DoorsSmoke`, `RoofCutawayStoreySmoke`, `LevelGenBuildingsWalkSmoke`, `TacticalMapLab{Alley,Bridge,Crossroads}Smoke`, `LabBridgeMantleRouteSmoke`, `LevelGenIslandsProbe`.
- **`CodexTactics.*Probe`** — замеры (EnemyPerfProbe, RepathProbe, EnemyStuckProbe, RecoilProbe, EncounterProbe, LureProbe и др.): пишут числа в лог.
- **`CodexTactics.*Shots` / `*Shot`** (BodyLineupShots, HudShot, CoverLRShot, FallDropShots, ShotgunD1Shots и др.) — делают скриншоты в `Saved/Screenshots/`, затем выходят; нужен рендер.
- **`CodexTactics.ClipRecorder`**, `PackagedCheck`, `ComplexityShot` — запись клипов / проверка упакованного билда / сложность шейдеров.
