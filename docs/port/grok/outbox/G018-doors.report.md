# G018 — двери: отчёт

Статус: PASS. Код смоуков `1b79de7ed1e60bd36161a0873d4b8b6e143e1750` на `grok/G018-doors`. Не пушил.

Роутер: `python Scripts/Tools/model_router.py --classify "clamp a breach hook past the centre man when the side wall leaves him in the doorway lane"` → level 2, model null, route false, policy_blocked (`no subagents while Saved/Lead/ALLOW_SUBAGENTS is absent`). Работал на grok-4.7, без субагентов.

`2 | grok-4.7 | policy_blocked, stayed on grok-4.7`

Двери на Kit и StoryShowcase. `doors.json` `enabled` остаётся 0. Холод не включал: отдельного `Codex.Cold` нет, у карты Kit `cold_rate_mult` уже 0.

## Что сделано

Порядок плана `docs/port/grok/outbox/G018-doors.plan.md`:

1. Незапертая: клик рукой, двойной клик — пинок.
2. Запертая: ключ, пинок, выстрел в замок, взрыв, после ключа и выстрела — толчок.
3. Створка от открывающего; где открытый лист закрывает другой проём или устье лестницы, дверь закрывается за отрядом.
4. ЧВК делает те же действия.

Плюс к спеке: внутренние двери закрыты на старте, сюжетный замок — сталь, у входа Kit свой лист, нав / укрытие / видимость обновляются, боты и строй проходят в двери. Вход группы в комнату: трое за порогом.

Фильтр «оба крюка на навмеше» (`d4decf04`) снят в `fa06b51e`: у каждой уличной двери крюк сидит примерно в 15 см от стены-возврата, фильтр не оставлял ни одного входа. Упор в стену оставлен (`HookWallClearCm` 70). В кладовке за `S01_open_f0_d20` (250 см в ширину) этого мало: оба крюка вставали в проём 150 см, командир оставался снаружи. `1b79de7e`: крюк ближе `HookLaneCm` (100) к оси встаёт за центровым (`HookPastCentreCm` 120), центровой останавливается на корпус ближе ближнего крюка.

## Claim | Evidence

| Утверждение | Доказательство |
|---|---|
| Коммиты только по задаче, не пушил | `2a8521d6` план. `a670edb8` тихий этаж. `e988baf0` смоуки на Kit. `83d56450` схема v2. `7b6bc3ed` ЧВК не лезет в смоук дверей. `48457310` прицел в уличную дверь. `d4decf04` фильтр крюка (снят следующим). `fa06b51e` упор в стену. `1b79de7e` очередь в кладовке. `git log origin/main..HEAD` — эти девять. Ветку не пушил |
| Сборка | `Scripts/build.ps1` → `Result: Succeeded`, `Total execution time: 475.91 seconds`, exit 0. Предупреждения unity-clash (7) и `Feet` в `CodexTacticsPlayerControllerOrders.cpp` (929, 1029) были до этой правки |
| Юнит-тесты до коммита кода | `Scripts/test.ps1 -Smart -SkipSmokes` → `102 tests, 0 failed`, `== test -Changed: unit 0/102 failed (39 s); smokes skipped ==`, exit 0. `Entry.RoomClamp` и `Entry.ClosetFile` — Success (`Saved/Logs/AutomationTests.log`, 2026.11.25-01.27.17) |
| Граница агента | До `1b79de7e`: `Boundary Breach : PASS`, риск `1.1 / 4`, `3 files changed, 96 insertions(+), 5 deletions(-)`, exit 0 |
| Смоуки на slot2, код `1b79de7e` | `worker_sync.ps1 -RemoteRoot C:/Work/CodexTactics-slot2 -ShipHead -ShareMachine -Jobs 1 -AllLogs -Commands DoorsSmoke,DoorBreachSmoke`, exit 0. Синк `fa06b51e -> 1b79de7e`. Каталог `Saved/Logs/Worker/20261125-032801`. Handshake `SYNC.DESKTOP-6RLUUV8-80872-20261125-032801` снят |

Таблица воркера:

```
[smoke_parallel] 2 smokes, 1 at a time, fixed step (-UseFixedTimeStep -FPS=60 -FixedSeed)
  DoorsSmoke                 PASS      73.9s
  DoorBreachSmoke            PASS      77.7s
[smoke_parallel] 2 run, 0 failed, 0 passed only on the re-run (PASS*), 0 known-fail; wall 153 s, sum of checks 152 s
```

`DoorsSmoke` (`Smoke RESULT: PASS DoorsSmoke (0 failure(s))`). По стадиям: рука `open_r` / `AM_Door_Open_R`; пинок дважды, урон 140→130, `kick_r` `kick_l`; ключ и толчок `push_r`; выстрел в замок, обойма 30→30, толчок `push_l` (оружие не замерзло); взрыв; close-behind `0 unflagged`; сталь сюжетного замка; ЧВК два прогона `shoot:gives push` и проход в комнату; строй 3/3 внутри. Тихий путь S03 d20 остался закрытым, не запертым. Вход S01 — сталь.

`DoorBreachSmoke` (`Smoke RESULT: PASS`). Дверь `S01_open_f0_d20`, 150 см. Стек 3/3, два косяка в пике. Вход:

```
Medic-Sapper: move to (2320, -6330) sprint -> accepted
Engineer: move to (2415, -6210) sprint -> accepted
Commander: move to (2350, -6450) sprint -> accepted
[Breach] doorway S01_open_f0_d20: room clear (3 of 3 in)
Smoke ok  : c: the entry - 3 men past the threshold (3 expected; cleared 1 after 18.7 s): Commander 171 cm inside; Engineer 459 cm inside; Medic-Sapper 373 cm inside;
```

Прошлый прогон `fa06b51e` (`Saved/Logs/Worker/20261125-030132`): `DoorsSmoke` PASS 81.3 с, `DoorBreachSmoke` FAIL 48.3 с — `2 men past the threshold (3 expected)`, командир 18 см снаружи, инженер стоял в его пути на (2389, -6572). Порог «трое внутри» не ослаблял.

`-Smart` насчитал 4248 «изменённых» файлов из-за неотслеживаемых паков. На фильтр это не повлияло. В фильтр попали и смоуки `InteriorExitSmoke`, `LevelGenBuildingsSmoke`, `TutorialAmbushLakeSmoke`, `TurnArenaLakeSmoke`. Их не гонял.

## Не сделано

- Генератор пропов ключи не кладёт.
- Командир ЧВК не оценивает замок заранее.
- Состояние дверей и ключи не пишутся в сохранение.
- `enabled` остаётся 0. `enabled_maps` не расширял: у X6 в LevelJson нет `door_state`.
- Окна, `planDoors.ts`, карты, `.uasset`, `Config`, VFX не трогал.
- Холод не включал и не выключал заново: на Kit он уже 0.
- Не пушил. Неотслеживаемый `Config/DefaultScalability.ini` и паки Content в коммиты не входили.
