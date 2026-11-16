# Mantling, вторая партия правок - отчёт (2026-11-16, ветка `fix/mantle-batch2`)

Баг-репорт дизайнера 2026-11-16, видео `Codex/Videos/MantlingProblem_03.avi` ... `_07.avi` (кадры ffmpeg 2-10 к/с, смотрены вручную).
Основа: `docs/port/MANTLING_ALIGN_REPORT.md` (утренняя правка «align»). Клипы / `.uasset`, карты, `Config/*.ini`, `L_Gen_Depot_1.json`, ABP,
fire gate / Aim / FireArc, ragdoll, PMC AI не трогались. Все числа - в `Content/Data/Movement/mantle.json` (`align.*`, `descent.*`, `order.*`).

**A/B для дизайнера:** консоль `Codex.Mantle.Batch2 0` возвращает поведение до этой партии (тот же бинарник; так получены строки «до»
ниже: `smoke_parallel.ps1 -PreCmds "Codex.Mantle.Batch2 0"`). `Codex.Mantle.Align 0` по-прежнему возвращает slice 1.

## 1. Что показали видео

- **03:** подъём на блок - тело в кадрах 7-8 целиком «просвечено» (синий силуэт = тело внутри меша). Спуск - на ту же сторону.
- **04:** парапет 230 - то же погружение в стену на хвате (кадры 3-6 при 5 к/с); наверху он стоит выпрямившись ~0,6 с (метка над
  головой ещё STANDING), потом присед. Одиночные клики по боковой грани дают «Cover: wall... Click again to take it» (призрак укрытия).
- **05:** спуск с длинного блока - играет клип перелаза (hurdle), тело идёт по прямой дуге через верхнее ребро.
- **06:** небольшой ящик - сверху приказы «Run to position» принимаются, но он стоит на месте (лёжа / стоя).
- **07:** лёжа на ящике, приказ вниз - мгновенный перенос на землю, встаёт уже внизу.
- **Пункт 9 (без видео):** на сгенерированных картах `L_Gen_Depot_X4` / `L_Gen_Depot_1_X6` бойцы не лезут через низкие заборы - обегают.

## 2. Причины (с цифрами)

1. **Погружение в меш (03 / 04).** Второе окно Motion Warping (хват -> посадка, SkewWarp) переносит недостающее смещение «сдвигом» каждой
   дельты root motion. В висе (0,9-1,1 с клипа Standing_JumpClimbUpNEW) корень клипа почти не движется (x -0,1 -> -0,4 см), а капсула
   уехала на 11 см вперёд (ступни 0,1 -> 11,0 см за гранью), таз на 9 см внутрь стены, кисти соскользнули с кромки на 11 см. Замер
   торса (pelvis / spine_03 / head) ниже верха: TallLedge_ClimbUpNEW **1,4 см** до грани, Standing_JumpClimbUpNEW **-9,1 см** (внутри),
   забор 135 **-4,6 см**. Обломок 140 / ящик 85 - в норме (48 / >100 см).
2. **«Замирание» наверху (04).** Конец подъёма считался по «все кости медленнее 20 см/с»; у tall-ledge клипов последние 0,6 с - медленное
   доустаивание (таз 349,8 -> 352,5 см с 4,1 по 4,7 с, кисти по 3-6 см), т.е. поза уже финальная, а передача шла в 4,43 / 4,70 с.
   Замер: торс в пределах 8 см от позы передачи **0,40 с** до конца подъёма (Standing_JumpClimbUpNEW).
3. **Клик не работает (03 / 04).** Изометрическая камера показывает боковые грани; клик по грани (нормаль горизонтальна) не проходит
   `PlanTopClick` (только нормаль вверх) и уходит в клик укрытия - превью; второй клик = «занять укрытие». Клик по верху работал всегда.
4. **Призрака при наведении не было** - функции не было.
5. **Спуск на ту же сторону (03) и 7. «застрял наверху» (06) - одна причина.** Ссылки вниз генерируются только в паре со ссылкой вверх
   той же грани; на Depot есть верхушки с навмешем без ссылок (обломки StaticMeshActor_387 / _388 - ни одной ссылки; «тупики» отбрасываются)
   или со ссылками только с одной грани. Тогда `MoveTo` с верха возвращает **частичный** путь (Accepted): он доходит до края островка и
   стоит. `TryMantleStepDown` срабатывал только на Failed. До: «far-face links off» / «island» / «island prone» - **NOT reached, 275-297 см**
   от цели за 15 с.
6. **Странная анимация и погружение на спуске (05).** Спуск = «прыжок» `StartMantleHop`: `bVaulting` -> ABP играет клип перелаза
   `M_Neutral_Traversal_Hurdle_1_0_stand_F_V2_*foot_Rifle` (x1,5, подогнан под 0,6 с), капсула летит (MOVE_Flying, коллизия выкл.) по
   прямой дуге от точки на верху к земле - через верхнее ребро. Замер: на всех спусках «до» играл клип перелаза (1 шт.), fall states нет.
7. см. п. 5.
8. **Телепорт (07).** Страховка падений (`falls.json rescue.*`): после ЛЮБОГО падения 20 с следит, не стоит ли он вне навмеша, и через 2 с
   переносит на ближайший навмеш (до 15 м). Маленькие верхушки (ящики 120x120) навмеша не имеют -> подъём в эти 20 с = телепорт вниз.
   Пойман смоуком: `[Fall] rescue: Commander was off the navmesh at (1136, -5031, 87) (after a fall) - put on (1216, -5031, 10)`. Лёжа
   прыжок шёл без вставания (поза на краю - Prone).
9. **Сгенерированные карты без mantling.** Высоты в раскладках X4 / X6 в классах (заборы 135, ящики 85, обломки / котлы 140, cut_wall 140),
   теги видов тоже (`fence`, `crate`, `wreck`...), билдер тут ни при чём. Причина - **лимит ссылок slice 1: `MantleMaxLinks = 900`**
   (`MantleNavigation.cpp`): сканирование актёров останавливалось на 900 ссылках. Уже `L_Gen_Depot_1` упирался в него (910 ссылок; без
   лимита - 1184), на копии раскладки X4 (`L_Gen_MantleTest`, собрана в этой ветке) - **900 из 3129**: ссылки только у первых по порядку
   актёров (заборов 690 из 979, «высоких» верхушек 105 из 1024, низких 0 из 51), остальные заборы без ссылки -> путь обходит. Перелаз
   (vault) мешков / блоков 95 на этих картах работает (тег ставит GameMode на старте) - это не он; заборы 135 - mantle `high_fence_over`.

## 3. Что изменено

| Где | Что |
|---|---|
| `Source/CodexTactics/Private/Characters/OperativeCharacterMantle.cpp:435` `ApplyMantleWallGuard` (+ вызов в `UpdateMantleMeshShift` :398 - Tick и колбэк финализации костей) | п.1, только подъём НА верх: пока сустав торса ниже верха (`align.gap_above_margin_cm` 20) и рядом с настоящей коллизией препятствия (`ActorGetDistanceToCollision` < зазора; плоскость грани меряется на высоте колена - у покатого обломка капот дальше), он держится `align.guard_gap_cm` 14 перед гранью - капсула сдвигается назад на дефицит, не больше `align.guard_max_push_cm` 30 за подъём (потом клип без стража - никакого рывка на посадке); warp сам перенацеливается на посадку |
| `Source/CodexTactics/Private/Characters/MantleRules.cpp:738` `AnalyseClipTrack`, `OperativeCharacterMantle.cpp:326` | п.2: выход = первый кадр, с которого все кости клипа в пределах `align.exit_pose_cm` 8 от финальной позы (TallLedge 4,43 -> 4,10 с, Standing_Jump 4,70 -> 4,10 с); старое правило - `StillExitSeconds` для `Batch2 0` |
| `Source/CodexTactics/Private/Interactables/MantleNavigation.cpp:364` `TopHitFromFace`; `OperativeCharacterMantle.cpp:637` `PlanMantleClick`; `Private/Core/CodexTacticsPlayerControllerOrders.cpp:467` | п.3: клик по грани -> верх в `order.face_inset_cm` 15 от грани; **двойной** клик по грани лазаемого препятствия = подъём (одиночный - превью укрытия, как раньше; в укрытии двойной клик остаётся переходом между укрытиями). План клика теперь меряет подъём с точки подхода (посадка, высота над посадкой, лимиты warp) - нет плана = нет приказа и нет призрака |
| `Private/Core/CodexTacticsPlayerControllerOrders.cpp:744` `CanOrderMantleClick`, `:760` `UpdateMantleHover`; `CodexTacticsPlayerController.cpp:404`; `Private/Tactics/CoverGhostActor.cpp:150` `ShowMantleFor` (+ общий `ApplyGhostBody` :175) | п.4: тот же голограммный призрак, что у выглядывания из-за угла (M_Silhouette, цвет силуэта бойца), стоит на точке посадки в позе после подъёма (присед на верху, стойка за забором), лицом по направлению подъёма. Только где клик сам дал бы подъём (Solo, один выбран, реальное время, без Alt/Shift/Ctrl). Новых ассетов нет |
| `OperativeCharacterMantle.cpp:843` `FindMantleDescent`, `:935` `TryMantleStepDown`, `:985` `StartMantleDescent`, `:1026` `UpdateMantleDescent`; `MantleRules.cpp:824` `ChooseDescent`, `:853` `DescentExitOnFace` | пп.5-8: спуск = встать (если лёжа / в приседе; ждём клип стойки, максимум `descent.stand_up_max_s`), дойти до края выбранной грани и сойти с него - обычное падение CharacterMovement (коллизия включена -> тело снаружи; ABP играет FallStates), потом дальше к цели. Грань - кратчайшая сумма «пройти по верху + путь по навмешу от точки приземления до цели»; приземление на землю (не на соседний ящик) в `descent.min_drop_cm`..`max_drop_cm` (30..250), с навмешем и местом для капсулы. Не сошёл за `descent.walk_timeout_s` (3) - старый прыжок, с предупреждением в логе |
| `Private/Characters/OperativeCharacterMovement.cpp:398` | пп.5/7 (только бойцы игрока, `IsPlayerTeam` - бойцов ЧВК и их ИИ не трогает): стоя на верхушке, приказ с Failed **или частичным** путём -> спуск; полный путь, но спуск через другую грань короче на `descent.prefer_margin_cm` (100) - спуск. `:315` новый приказ во время спуска - идти к нему после приземления |
| `OperativeCharacterMantle.cpp:769` | ссылка вниз (`UNavArea_MantleDown`) тоже спускается через `StartMantleDescent` (не прыжок) |
| `Private/Falls/LedgeFallComponentRescue.cpp:137` | п.8: страховка падений не трогает бойца игрока, стоящего на верхушке mantle-препятствия (его следующий приказ спустит его сам); страховка при старте приказа осталась последним средством |
| `Private/Interactables/MantleNavigation.cpp:531`, `:629` | п.9: лимит ссылок из `mantle.json links.max_total` (12000) вместо 900; достигнут - предупреждение в логе и `UMantleSubsystem::WasLinkCapReached` |
| `Private/Interactables/MantleNavigation.cpp:359` | CVar `Codex.Mantle.Batch2` (1 = всё выше; 0 = поведение до партии, включая лимит 900) |
| `Content/Data/Movement/mantle.json` | `align.exit_pose_cm`, `align.guard_gap_cm`, `order.face_inset_cm`, `descent.land_out_cm / min_drop_cm / max_drop_cm / stand_up_max_s / walk_timeout_s / prefer_margin_cm`; приёмка `align.min_body_gap_cm` 10, `align.gap_above_margin_cm`, `align.settle_pelvis_cm`, `align.handoff_limit_s`, `descent.arrive_limit_s / arrive_radius_cm / max_speed_cm_s`; `align.guard_max_push_cm`, `links.max_total` |
| `Source/CodexTacticsDev/Private/Debug/MantleDescentSmokeCommand.cpp` (новый), `MantleClickSmokeCommand.cpp` (новый), `MantleFenceRouteSmokeCommand.cpp` (новый, п.9; на любой карте: `Scripts/smoke.ps1 -Command CodexTactics.MantleFenceRouteSmoke -Map <map>`), `MantleSmokeSites.h` (общие помощники), `MantleAlignSmokeCommand.cpp`, `Scripts/test_map.json` | смоуки ниже |
| `Source/CodexTacticsTests/Private/Characters/MantleRulesTest.cpp` | тест `CodexTactics.Characters.Mantle.Descent` (выбор грани, точка выхода) |

## 4. До / после (смоуки на L_Gen_Depot_1; «до» = `Codex.Mantle.Batch2 0`)

Строки FAIL «до»:

```
MantleAlignSmoke:
Smoke FAIL: high 230 cm parapet (1st clip): no clipping into the obstacle - the torso's smallest gap to its footprint below the top 1.4 cm (at 1.33 s, 33 cm under the top; minimum 10)
Smoke FAIL: high 230 cm parapet (2nd clip): no clipping into the obstacle - the torso's smallest gap to its footprint below the top -9.1 cm (at 1.07 s, 106 cm under the top; minimum 10)
Smoke FAIL: high 230 cm parapet (2nd clip): no held end pose before the handoff - the torso is already within 8 cm of its handoff pose 0.40 s before the climb ends (from 4.28 s; limit 0.35)
Smoke FAIL: 135 cm fence: no clipping into the obstacle - the torso's smallest gap to its footprint below the top -4.6 cm (at 0.28 s, 31 cm under the top; minimum 10)
MantleClickSmoke:
Smoke FAIL: b: double click on the 140 cm wreck's face: he goes to it and climbs it (NO climb after 10.0 s; hit StaticMeshActor_387, last refusal none)
Smoke FAIL: c: double click on the 230 cm parapet's face: he goes to it and climbs it (NO climb after 10.0 s; hit StaticMeshActor_14, last refusal none)
Smoke FAIL: e: hovering StaticMeshActor_328 (a: click on the 140 cm wreck's top) shows the mantle ghost at the landing (NO ghost)
Smoke FAIL: e: hovering StaticMeshActor_13 (c: double click on the 230 cm parapet's face) shows the mantle ghost at the landing (NO ghost)
MantleDescentSmoke:
Smoke FAIL: 85 cm crate: the descent plays the fall states (NOT seen), not the vault-over clip (1 played)
Smoke FAIL: 140 cm crate, prone on top: he stands up before he comes down (stance at the edge EOperativeStance::Prone)
Smoke FAIL: 140 cm wreck, far-face links off: the order from the top reaches its ground goal (NOT reached, 274.6 cm off at the end; limit 15 s)
Smoke FAIL: 140 cm wreck, an island top (its links off): the order from the top reaches its ground goal (NOT reached, 285.7 cm off at the end; limit 15 s)
Smoke FAIL: 140 cm wreck island, prone on top: the order from the top reaches its ground goal (NOT reached, 296.7 cm off at the end; limit 15 s)
```

(полностью: 4 FAIL в Align, 6 в Click, 16 в Descent). Пункт 9, `L_Gen_MantleTest` (копия раскладки X4), «до»:

```
[Mantle] L_Gen_MantleTest: the link cap (900) is reached - the obstacles after it get no mantle links (mantle.json links.max_total)
Smoke FAIL: the map's mantle links: 900 (690 over fences), the cap (links.max_total 12000) REACHED - links dropped
```
После: `3129 mantle links (high_mantle 1024, high_fence_over 979, low_mantle 51), 1075 down`; клик за забор 135 (обход 1084 см против
355 по прямой) - перелез `high_fence_over`, дошёл за 2,4 с. На `L_Gen_Depot_1`: 910 -> 1184 ссылок; клик за забор (обход 2256 см) - перелез,
5,3 с. В первом прогоне на исходном коде ветки (до любых правок) телепорт ещё не
проявлялся (прыжок не «падение», страховка не взводилась) - его поймал первый прогон с новым спуском (падение взводит страховку), см. п. 2.8.

| Замер | До | После |
|---|---|---|
| Торс ↔ грань, TallLedge_ClimbUpNEW / Standing_JumpClimbUpNEW (мин. 10) | 1,4 / **-9,1** см | 14,0 / 14,0 см |
| Торс ↔ забор 135 (HighFenceVault, только замер) | -4,6 см | -3,8 ... -4,6 см (не исправлено, см. п. 8 «Не сделано») |
| Кисть ↔ кромка (допуск 15): TallLedge / Standing_Jump / обломок 140 / ящик 85 | 0,0 / 0,1 / 0,3 / 5,9 см | 1,8 / 0,7 / 0,3 / 5,9 см |
| Передача после подъёма: tall-ledge клипы | 4,43 / 4,70 с | 4,10 / 4,10 с |
| «Поза держится» до передачи (Standing_Jump, лимит 0,35) | **0,40 с** | 0,08 с |
| Таз садится в присед после передачи (лимит 0,6) | 0,18-0,20 с | 0,18-0,20 с |
| Подброс стопы сверх шага клипа, Standing_Jump (допуск 15) | 12,3 см | 14,6 см |
| Клик по верху обломка / двойной по грани обломка / двойной по грани парапета / одиночный по грани | лезет / нет / нет / превью укрытия | лезет / лезет за 1,4 с / лезет за 1,7 с / превью укрытия |
| Призрак над обломком (верх, грань), парапетом / над забором 330 | нет / нет | 0-1 см от посадки / нет |
| Спуск: анимация | клип перелаза (hurdle) | FallStates (падение + приземление) |
| Спуск лёжа: поза на краю | Prone | Standing |
| Верх без ссылок вниз / ссылки только со стороны подъёма: дошёл до цели | нет (275-297 см от цели за 15 с) | да, 2,3-2,8 с, через дальнюю грань |
| Самый быстрый кадр капсулы (телепорт > 1500 см/с) | 837-913 | 456-891 |

## 5. Гейты

| Гейт | Результат |
|---|---|
| `Scripts/build.ps1` | Succeeded |
| `test.ps1 -Filter CodexTactics.Characters.Mantle+CodexTactics.Characters.Vault+CodexTactics.Movement.Vault+CodexTactics.Falls` | 29 / 29 (в т.ч. новый `Mantle.Descent`, `Mantle.TuningFileMirrorsDefaults` покрывает новые ключи) |
| `smoke_parallel.ps1 -NoRetry -ExtraArgs -Multiprocess` MantleSmoke, MantleAlignSmoke, MantleDescentSmoke, MantleClickSmoke, MantleFenceRouteSmoke, VaultSmoke, DepotVaultSmoke, LadderClimbSmoke, CornerHoldSmoke | 1-й прогон 9 / 9 PASS, 2-й прогон 9 / 9 PASS, без перезапусков |
| `smoke.ps1 -Command CodexTactics.MantleFenceRouteSmoke -Map /Game/Maps/Generated/L_Gen_MantleTest` (п.9) | PASS (до, `Batch2 0`: FAIL - лимит 900) |
| `check_smoke_names.py` | OK |
| `typesafe_triage.py --audit-diff --agent claude` | PASS (защищённые пути не тронуты), риск 2,0 / 4 |

Существующие проверки не ослаблялись (новые проверки добавлены; правило выхода из клипа изменено, старое доступно по `Batch2 0`).

## 6. Что перепроверить дизайнеру (PIE, `L_Gen_Depot_1`, Solo)

1. Подъём на парапет 230 (оба клипа, кликом два раза подряд в разные моменты): тело не уходит в стену; кисти на кромке; наверху сразу
   присед, без стойки ~0,5 с. Забор 135 - перелаз без «прохода» таза сквозь забор.
2. Двойной клик по **боковой грани** ящика / обломка / парапета: бежит и лезет. Одиночный клик по грани - как раньше, призрак укрытия.
3. Наведение курсора на верх или грань лазаемого препятствия: голограмма бойца в приседе на месте посадки; над стеной 330, бочкой, мешками -
   ничего.
4. С верха ящика / обломка - приказ на землю за дальней стороной: сходит с дальней стороны (не с той, где залез), обычное падение и
   приземление, без перелаза и без прохода сквозь ребро.
5. Лёжа на ящике -> приказ вниз: встаёт, подходит к краю, спрыгивает. Никаких мгновенных переносов, в том числе если за 20 с до подъёма
   было падение.
6. Небольшой ящик / обломок без ссылок (как в 06): с верха приказ куда угодно - спускается.
7. На `L_Gen_Depot_X4` / `L_Gen_Depot_1_X6` (после мержа): в Solo клик за низкий забор, обход которого длинный - перелезает.
8. A/B: `Codex.Mantle.Batch2 0` - поведение до этой партии.

## 7. Решения для дизайнера (приняты по умолчанию)

1. **Клик по грани:** одиночный - превью укрытия (как было), **двойной - подъём**; в укрытии двойной клик остаётся переходом между
   укрытиями. Альтернатива (одиночный клик по грани = подъём) убрала бы укрытие за ящиками / обломками одним кликом - не делал.
2. **Призрак:** поза конца подъёма (присед на верху / стойка за забором), не поза хвата; показывается и над гранью (там подъём - двойным
   кликом), только для выбранного бойца в Solo.
3. **Сгенерированные карты:** пересобирать не нужно - ссылки строятся при запуске уровня, лимит был в коде. `L_Gen_Depot_X4`,
   `L_Gen_Depot_1_X6`, `L_Gen_Depot_1` получат все ссылки после мержа ветки. Исключение - `L_Gen_Test_X2` (раскладка `..._x2` со старыми
   высотами 110 / 150 / 160 / 250 / 220 вне классов): там нужны `Scripts/Tools/apply_depot_height_classes.py` по её раскладке и пересборка.
   Цена ссылки для пути не менял: `links.entering_cost_cm` 600 - перелаз берётся, когда обход длиннее прямого пути больше чем на 6 м
   (меньше - обходит, это и задумано в slice 1). В режиме отряда (формация) ссылки закрыты по-прежнему - лазит только боец, которым
   командуют в Solo.
4. **Спуск:** встаёт и из лёжа, и из приседа (в режиме скрытности - в присед), идёт к краю и падает обычным падением (FallStates). Клип
   спрыгивания `/Game/Animations/Humanoids/Jumping_1__UE` (доска 42) сюда подходит: заменить падение+приземление на этот клип для верхушек
   85-140 - отдельная задача, не подключал.
5. Обход навмешем заменяется сходом через другую грань, если тот короче хотя бы на 1 м (`descent.prefer_margin_cm`).
6. Страховка падений не переносит бойца с верхушки mantle-препятствия: он стоит там, пока не прикажут.
7. Выход из клипа на «уже финальной» позе с допуском 8 см (`align.exit_pose_cm`); если доустаивание где-то покажется обрезанным -
   уменьшить до 4-5.

## 8. Не сделано / открыто

- **Забор 135 (HighFenceVault): торс проходит ~28-31 см ниже верха забора** (и до, и после; страж на перелаз «через» не ставится - он бы
  только держал бойца перед забором до своего лимита). Это клип / его вертикальный warp x0,89 под забор 133 см: таз клипа идёт ниже
  кромки. Решение - другой клип перелаза или забор ниже; в репорте дизайнера (03 / 04) речь о верхушках, они исправлены.
- Тестовая карта `Content/Maps/Generated/L_Gen_MantleTest.umap` и `Content/Data/LevelJson/L_Gen_MantleTest.json` собраны в этой ветке
  (копия раскладки X4 в `Saved/LevelGenTest/`) и **не закоммичены** (только для проверки п.9).

- **Корень п.1 в warp не исправлен, он ограничен:** второе окно SkewWarp по-прежнему «сдвигает» тело вперёд в висе, страж держит торс
  снаружи (капсула назад до 14 см). Правильнее - третье окно (хват -> перелаз через кромку) или RootMotionModifier с фиксацией X до
  подъёма торса над верхом; оставлено, т.к. текущее решение проходит все замеры.
- Подброс стопы у Standing_JumpClimbUpNEW вырос 12,3 -> 14,6 см (допуск 15) - близко к пределу (стопа клипа ~15 см над крышей при
  вставании, см. утренний отчёт).
- **Клипы не менялись.** Сдвиг корня в кадре 0 (HighClimb 163,5 см вверх, tall-ledge 36 см назад) по-прежнему компенсируется в рантайме.
- Наблюдение (не от этой партии): генерация ссылок недетерминирована под нагрузкой - в части прогонов 865 вместо 910 ссылок (45 tall_ledge
  уходят в «тупики», когда ссылки падений не готовы за 60 с). Подъём кликом это не затрагивает, путь «по ссылкам» на парапет - да.
- Верхушки с ссылками только с одной стороны остаются в генераторе (п.5 решён в рантайме); генерировать ссылку вниз для каждой грани со
  свободной землёй - возможное улучшение генератора.
- Женские клипы и беговые клипы этим набором смоуков не меряются (на Depot нет женщины в отряде; бег - MantleSmoke).
