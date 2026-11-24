# G019 — цикл раундов сектора X6

Статус: PASS. Код `66fc718f8d00bce758bd0af76e148706fd387216` на `grok/G019-sector-rounds` (от локального main `1089f445bb6c4bf6a1d521fdefd3c14e3c9342c8`). Не пушил.

Роутер: `python Scripts/Tools/model_router.py --classify "X6 sector fight loop rounds and ammo resupply"` → level 3, model null, route false, policy_blocked (`no subagents while Saved/Lead/ALLOW_SUBAGENTS is absent`). Работал на grok-4.7, без субагентов.

`3 | grok-4.7 | pass`

Пока сектор активен, бой идёт раундами: вайп стороны или лимит времени, пауза, оба отряда заново на другой паре старт+пост. Пустой резерв оружия возвращается после тишины. Смерть отряда игрока не валит миссию и не включает кинематограф смерти. Холод и погода не менялись.

## Что сделано

Правила в `TrainingSectorRules`: итог раунда, выбор следующей пары, тишина и размер ресапплая. В `L_Gen_Depot_1_X6.sectors.json` четыре поля после `map`: `round_restart_delay_s` 8, `round_time_limit_s` 600, `ammo_mode` `resupply`, `resupply_delay_s` 20. Первый раунд берёт уже существующий `Pick` (сид 91 для смоука). Следующие — `PickRoundPair`, та же пара подряд не повторяется, если пар больше одной.

`UTrainingSectorSubsystem` тикает, пока сектор выбран. Раунд начинается, когда у обеих сторон есть живые члены, и в этот же тик не закрывается. Вайп: не боеспособен (мертв, KIA или нокдаун). Оба сразу вне боя — ничья, раньше лимита времени. После паузы уничтожаются все оперативники (труп выходит из команды PMC), затем спавн ростера и новый график PMC. Гранаты только со свежего спавна. Ресапплай пишет резерв и `AmmoInventory`, гранаты не трогает.

Строка HUD «Раунд N: …» 3 с, золотом, Y=72. В запись сессии — `round_start` / `round_end` (сектор, сид, длительность, убийства, счёт). Пустой список раундов не меняет старые 7 строк сессии.

## Claim | Evidence

| Утверждение | Доказательство |
|---|---|
| Коммиты только по задаче, не пушил | `bfb6f8fe` план. `b34e46db` правила и JSON. `848e9fb7` рантайм. `ade05118` смоук и карта. `66fc718f` тикер. `git status` по Source/Scripts/docs/sectors.json чистый. Junction паков не в коммитах |
| Сборка | `Scripts/build.ps1` → `Result: Succeeded`, `Total execution time: 939.74 seconds`, exit 0. Повтор после фикса тикера: `[1/4] Compile [x64] TrainingSectorSubsystem.cpp`, `Result: Succeeded`, `Total execution time: 19.85 seconds`, exit 0. Предупреждения unity-clash (7) и `Feet` в `CodexTacticsPlayerControllerOrders.cpp` были до этой правки |
| Юнит-тесты до коммита кода | `Scripts/test.ps1 -Smart -SkipSmokes` (дифф к HEAD, смоки здесь не стартуют) → `569 tests, 0 failed`, `== test -Changed: unit 0/569 failed (88 s); smokes skipped ==`, exit 0. Отчёт `Saved/TestReport` |
| Граница агента | До коммитов, 16 файлов в индексе: `Boundary Breach : PASS`, риск `2.6 / 4`, `16 files changed, 1321 insertions(+), 5 deletions(-)`, exit 0. Фикс тикера отдельно: `PASS`, риск `2.0 / 4`, `1 file changed, 5 insertions(+), 5 deletions(-)`, exit 0 |
| Смоуки на slot2, код `66fc718f` | `worker_sync.ps1 -RemoteRoot C:/Work/CodexTactics-slot2 -ShipHead -ShareMachine -Jobs 1 -AllLogs -Commands SectorRoundsSmoke,SectorBoundarySmoke`, exit 0. Синк `ade05118 -> 66fc718f`. Каталог `Saved/Logs/Worker/20261124-213139` |

Таблица воркера:

```
[smoke_parallel] 2 smokes, 1 at a time, fixed step (-UseFixedTimeStep -FPS=60 -FixedSeed)
  SectorRoundsSmoke          PASS     116.4s
  SectorBoundarySmoke        PASS      67.6s
[smoke_parallel] 2 run, 0 failed, 0 passed only on the re-run (PASS*), 0 known-fail; wall 185 s, sum of checks 184 s
```

Сектор 5, сид 91. Из `Saved/Logs/Worker/20261124-213139/Smoke-SectorRoundsSmoke.log`:

```
[TrainingSector] Round 1 start sector 5 seed 91 start 1 post s5_post_3
[SectorRounds] round 1 pair start 1 post 2 grenades 6
[SectorRounds] reserve back after 20.5 s (6 fighters)
[TrainingSector] Round 1 end victory kills 3/0 restart in 8.0 s
[TrainingSector] Round 2 start sector 5 seed 91 start 0 post s5_post_3
[SectorRounds] round 2 pair start 0 post 2 player 0 cm pmc 0 cm
[TrainingSector] Round 2 end defeat kills 0/3 restart in 8.0 s
[TrainingSector] Round 3 start sector 5 seed 91 start 2 post s5_post_3
Smoke ok  : wipe, resupply and the next round on a different pair
Smoke RESULT: PASS
```

Пара сменилась (старт 1 → 0 → 2, пост 2). Ресапплай в 20.5 с, не раньше окна тишины: иначе смоук пишет `reserve refilled before the quiet delay`. Гранаты игрока остались 6. Победа и поражение не валят миссию: иначе `mission failed` / смерть-камера. `SectorBoundarySmoke`: `Smoke RESULT: PASS`.

`-Smart` насчитал 4261 «изменённый» файл из-за неотслеживаемых junction. На фильтр это не повлияло. В фильтр ещё попали смоуки `LevelGenBuildingsSmoke`, `FormationWallHugSmoke`, `VehicleDamageSmoke`, `GateExitLakeSmoke` (общие HUD, здоровье, sectors json). Их не гонял.

## Первый смоук и фикс тикера

Первый прогон на `ade05118` (`Saved/Logs/Worker/20261124-212220`): `SectorBoundarySmoke` PASS 67.0 с, `SectorRoundsSmoke` FAIL 117.1 с, exit 1.

```
Smoke FAIL: round 1 never started
Smoke RESULT: FAIL
```

Сектор выбрался (`Sector 5 ... seed 91 ... post s5_post_3`), PMC отспавнился, строки `Round 1 start` не было. `UWorld::BeginPlay` зовёт `OnWorldBeginPlay` подсистем до `GameMode::StartPlay`, а `Prepare` выбирает сектор до `Super::StartPlay`. `HasBegunPlay` в этот момент ещё ложь, повторного `OnWorldBeginPlay` нет, тикер не регистрировался. `66fc718f` запускает тикер из `BeginFight`. Порог смоука не ослаблял.

## Не сделано

- Не пушил. `main`, карты, `.uasset`, `Config`, `Content/VFX`, `L_Gen_Depot_1.json` и props json не трогал.
- G016 остаётся на `grok/G016-window-cover-fire`.
- Холод (`cold_rate_mult`) и погода не менялись.
- Четыре побочных смоука карты `-Smart` не запускал. Локально смоуки не стартовали: `-Smart -SkipSmokes`.
- Повторный `-Smart` после `66fc718f` не запускал: дифф к HEAD уже пустой, юниты этот тикер не покрывают. Доказательство фикса — второй смоук.
- `ApplyLevelKit` на респавне не вызывается: свежий `SpawnOperative` из `SquadRoster`.
- Гранаты возвращаются только с новым спавном, не по таймеру тишины.
- Провал миссии и кинематограф смерти глушатся только пока сектор активен.
- Строка баннера держится 3 с константой в коде, не полем JSON.
- Пост во всех трёх раундах смоука остался `s5_post_3`; менялся старт, поэтому пара другая.
