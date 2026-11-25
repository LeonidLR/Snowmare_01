# G016 — огонь из укрытия через окно

Статус: PASS. Код `21e8bd03e19c79dd84a1e750cfb500ae630af538` на `grok/G016-window-cover-fire`. Не пушил.

Роутер: `python Scripts/Tools/model_router.py --classify "G016 window cover fire through an opening"` → level 3, model null, route false, policy_blocked (`no subagents while Saved/Lead/ALLOW_SUBAGENTS is absent`). Работал на grok-4.7, без субагентов.

`3 | grok-4.7 | pass`

Боец у своего слота приседает у проёма и стреляет через него. Первый патрон уходит в целое стекло, следующий может попасть в цель. Чужая стена, другой этаж и угол окна не берут. Выход с подоконника возвращает обычное укрытие. Проход через окно по-прежнему только в `WindowOrderRules`.

## Что сделано

Привязка слота к `window_sill` кэшируется на бойце (`FWindowCoverBinding`), не в `FCoverSlot`. Пересчёт при входе, смене стены или этажа, потере подоконника и пересечении интервала. Допуски — именованные константы `WindowBreakage` (`OpeningPadCm`, `OpeningBottomCm`, `OpeningTopCm`, `ThroughLandingCm`).

Выстрел целится сквозь центр проёма. Целое стекло, которое трасса видимости не видит (серое стекло карты игнорирует visibility), всё равно забирает патрон, если линия прошла дальнюю грань проёма. Рама и глухая стена до этой грани выстрел держат. Флаг `bWindowGlassShot` — результат этого выстрела, не защёлка. Прицел и shimmy стекло не бьют.

Поза: присед, `bCoverWindow`, набор crouch-over / Aim2, когда клип есть. Пустой наружный набор остаётся боковым fallback.

## Claim | Evidence

| Утверждение | Доказательство |
|---|---|
| Коммиты только по задаче, не пушил | WIP `2957864c`. Слияние локального main `57495ba8`, затем `29d5ddac` (main `faf65be5`). `694a85c0` следующий патрон бьёт цель, смоук тикает позу. `14a6c36b` патрон уходит в стекло, которое трасса не видит. `21e8bd03` смоук выключает бесконечные патроны отряда. Junction паков и `Config/DefaultScalability.ini` не в коммитах |
| Сборка после `14a6c36b` | `Scripts/build.ps1` → `Result: Succeeded`, `Total execution time: 786.49 seconds`, exit 0. Повтор после смоука патрона: `[1/4] Compile [x64] WindowCoverFireSmokeCommand.cpp`, `Result: Succeeded`, `Total execution time: 21.59 seconds`, exit 0. Unity-clash (7) и `Feet` в `CodexTacticsPlayerControllerOrders.cpp` были до этой правки |
| Юнит-тесты | `Scripts/test.ps1 -Smart -SkipSmokes` на исходниках `14a6c36b` → `318 tests, 0 failed`, `== test -Changed: unit 0/318 failed (54 s); smokes skipped ==`, exit 0. Отчёт `Saved/TestReport`. Смоук-только `21e8bd03` юниты не меняет, повтор не гонял |
| Граница агента | Перед `14a6c36b`, 5 файлов в индексе: `Boundary Breach : PASS`, риск `1.5 / 4`, `5 files changed, 148 insertions(+), 9 deletions(-)`, exit 0. Перед `21e8bd03`: `PASS`, риск `1.0 / 4`, `1 file changed, 7 insertions(+), 1 deletion(-)`, exit 0 |
| Смоуки на slot2, код `21e8bd03` | `worker_sync.ps1 -RemoteRoot C:/Work/CodexTactics-slot2 -ShipHead -ShareMachine -Jobs 1 -AllLogs -NoRetry -Commands WindowCoverFireSmoke,FormationWindowSmoke`, exit 0. Каталог `Saved/Logs/Worker/20261125-022248` |
| Наружные клипы отдельно | Тот же вызов с `-SmokeExtra "-PreCmds 'Codex.Cover.RiffleOutdoor 1'" -Commands WindowCoverFireSmoke`, exit 0. Каталог `Saved/Logs/Worker/20261125-022611` |

Таблица воркера `20261125-022248`:

```
[smoke_parallel] 2 smokes, 1 at a time, fixed step (-UseFixedTimeStep -FPS=60 -FixedSeed)
  WindowCoverFireSmoke       PASS      72.3s
  FormationWindowSmoke       PASS      48.3s
[smoke_parallel] 2 run, 0 failed, 0 passed only on the re-run (PASS*), 0 known-fail; wall 122 s, sum of checks 121 s
```

Из `Smoke-WindowCoverFireSmoke.log` этого прогона (`Codex.Cover.RiffleOutdoor 0`, набор пустой, боковой fallback):

```
WindowCoverFireSmoke: Codex.Cover.RiffleOutdoor 0
Smoke ok  : outdoor set empty: side fallback (Codex.Cover.RiffleOutdoor 0)
WindowCoverFireSmoke: pose set '' over 0 window 1 corner 0 synthetic 0
Smoke ok  : the glass cartridge leaves the clip (30 -> 29)
Smoke ok  : the second round hits the hound
Smoke RESULT: PASS
```

`FormationWindowSmoke`: `Smoke RESULT: PASS`.

Прогон Aim2, `ExecCmds="Codex.Cover.RiffleOutdoor 1,CodexTactics.WindowCoverFireSmoke"`:

```
WindowCoverFireSmoke: Codex.Cover.RiffleOutdoor 1
[CoverSet] Commander: fire set -> over the top
Smoke ok  : riffle: crouch over / Aim2
WindowCoverFireSmoke: pose set 'riffle' over 1 window 1 corner 0 synthetic 0
WindowCoverFireSmoke: glass actors 1, already broken 0, through 1, intact 1, block 0, hit None, glass 0
Smoke ok  : the glass cartridge leaves the clip (30 -> 29)
Smoke ok  : the second round hits the hound
Smoke RESULT: PASS
```

`glass 0` — это `EWindowGlassState::Intact`. Линия прошла проём, стекло целое, трасса никого не задела (серое стекло не блокирует visibility), патрон всё равно ушёл в подоконник.

`-Smart` насчитал 4250 «изменённых» файлов из-за неотслеживаемых junction. На фильтр это не повлияло. В фильтр ещё попали `FormationWallHugSmoke`, `FormationStairSmoke`. Их не гонял.

## Два красных прогона

`29d5ddac`, каталог `Saved/Logs/Worker/20261125-013425`: `FormationWindowSmoke` PASS 49.8 с, `WindowCoverFireSmoke` FAIL 58.4 с.

```
WindowCoverFireSmoke: glass actors 1, already broken 0
Smoke FAIL: no pane was there, so the first round hits the hound
Smoke RESULT: FAIL
```

Присед и обе стороны уже проходили. Гончая стояла на высоте капсулы (около 58 см), дуло приседа — 85 см, проём начинается над подоконником (`OpeningBottomCm`). Линия в тело проём не пересекала, `bForceHitForTesting` всё равно снимал здоровье. Порог не ослаблял. `14a6c36b` ставит цель на луч через центр проёма (`ThroughLandingCm`) и считает целым стекло, которое трасса не видит, если линия прошла дальнюю грань.

`14a6c36b`, каталог `Saved/Logs/Worker/20261125-021135`: `FormationWindowSmoke` PASS 54.2 с, `WindowCoverFireSmoke` FAIL 49.1 с.

```
WindowCoverFireSmoke: glass actors 1, already broken 0, through 1, intact 1, block 0, hit None, glass 0
[WindowCover] Commander: the round is spent on StaticMeshActor_1231, the target is not hit
Smoke ok  : the first cartridge is spent on the glass
Smoke FAIL: the glass cartridge leaves the clip
Smoke ok  : the glass round does not hit the hound
Smoke ok  : the second round hits the hound
Smoke RESULT: FAIL
```

Стекло разбилось, гончая первым патроном не задета. Магазин не уменьшился: в `L_Gen_StoryShowcase_Kit.json` у отряда `infinite_ammo_squad` true. `21e8bd03` на время выстрела ставит `Codex.Cheat.InfiniteAmmo 0`. Порог `30 -> 29` не ослаблял.

## Не сделано

- Не пушил. `main`, карты, `.uasset`, `Config`, `Content/VFX`, `L_Gen_Depot_1.json` и props json не трогал.
- Локально смоуки не стартовали: `-Smart -SkipSmokes`. Побочные смоуки фильтра (`FormationWallHugSmoke`, `FormationStairSmoke`) не гонял.
- Повторный `-Smart` после `21e8bd03` не запускал: дифф к юнитам пустой.
- При `Codex.Cover.RiffleOutdoor 0` наружный набор пустой, Aim2 не требуется. Клип подтверждён отдельным прогоном с `1`.
- PIE не открывал. Проверка — смоуки slot2.
- `GlassOver` обходит акторов один раз при привязке подоконника, не каждый кадр.
- Сканирование стекла совпадает с кусками `HideBrokenWindow`, но это отдельный проход, общий файл `WindowBreakage.cpp` не менялся.
