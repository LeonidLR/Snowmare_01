# 074 - RetreatFacingSmoke: приказ на разворот держится (Claude, 2026-11-16)

Ветка `fix/074-retreat-facing` от main `2b99cd57`, worktree `CodexTactics-gemcli`. Исполнитель: Claude (вместо Gemini CLI, у которого кончился бюджет API).

## Причина (доказана логами)

Воспроизведение до правки: `smoke_parallel.ps1 -Commands RetreatFacingSmoke -NoRetry -Jobs 1 -ExtraArgs -Multiprocess`, FAIL
`a facing order of the player is kept: no turn to the pack (65 deg off the ordered facing, 115 deg off the pack, turning to the threat 0)` -
тот же, что у Codex 022 (5/5).

Временный пробник (смоук + покадровый лог yaw в `AOperativeCharacter::Tick` вокруг `UpdateCombatFacing` / `ProcessCombatShooting`,
`-PreCmds "Codex.Debug.FireTrace 1"`; после диагностики удалён):

```text
[074] before SetFacingPoint yaw=7.2 F.yaw=-10.0 reach=15000
[074] after SetFacingPoint yaw=170.0
[074probe] Commander f1730 yaw 170.0 -facing-> -154.0 -shooting-> -154.0 target=BP_Enemy_Frostbitten_C_2 fireReason=reloading facingTarget=1
[074probe] Commander f1731 yaw -154.0 -facing-> -125.2 -shooting-> -125.2 target=BP_Enemy_Frostbitten_C_2 fireReason=reloading facingTarget=1
[074] t=0.10 .. t=2.00 yaw=-125.2 away=64.8 pack=115.2 v=0.0 ... facingTarget=0 threat=0 own=1
[FireTrace] Commander: ... BP_Enemy_Frostbitten_C_2 16500 cm: not visible to the squad (hidden), residual 115 deg
```

Цепочка:
1. Смоук ставит приказ `SetFacingPoint` (разворот на 180 градусов, мгновенный `SetActorRotation`) и в тот же тик уводит стаю за досягаемость
   оружия: M16 `MaxRangeCm` = 15000 (150 м), стая в 165 м.
2. Командир в этот момент перезаряжается (`fireReason=reloading`): путь стрельбы не доходит до `FindShootTarget`, поэтому старая цель
   `CurrentCombatTarget` не сбрасывается; отряд ещё «видит» стаю два кадра (до обновления видимости).
3. Ветка боевой цели в `UpdateCombatFacing` (`OperativeCharacterAim.cpp`, бывшая строка 136) не смотрела ни на приказ, ни на дальность:
   за кадры 1730-1731 `RInterpTo(…, 12)` повернул корпус на 36 + 29 = 65 градусов к цели в 165 м.
4. На кадре 1732 цель скрыта - ветка выключилась. Приказ `SetFacingPoint` - разовый поворот, сохранённого yaw нет; `bExplicitFacing`
   (правильно) блокирует разворот к угрозе (`ThreatFacingRules`), так что корпус навсегда остался на 65 градусов от приказа.

Не причина: удержание взгляда 059b (`look=0`, `bLookHold` требует `!bExplicitFacing`), огневой гейт (`gate=0`, разворот происходит в
`UpdateCombatFacing` до `ProcessCombatShooting`), сброс состояния отступления (`sprint=0`, `v=0`), сектор строя (`sector=0`).

## Правило дизайна (из кода и документов)

- `ThreatFacingRules.h` / тест `CodexTactics.Squad.ThreatFacing.TurnsBackAfterTheRetreat`: «a facing the player gave wins» - приказ на
  разворот сильнее разворота к последней угрозе.
- Баг #39 (2026-11-07, комментарий в смоуке): «a target in reach wins over a facing order, as before» - цель в досягаемости оружия
  сильнее приказа.
- `docs/design/LOOK_HOLD_SOLO.md` (059/059b): боевой приоритет как в `UpdateCombatFacing`; точку взгляда снимает спринт / укрытие / и т. д.;
  к приказу `SetFacingPoint` look hold не применяется (`!bExplicitFacing`).

Итог: угроза за пределами досягаемости приказ не перебивает; цель в досягаемости - перебивает. Код нарушал это только в переходном
случае (устаревшая цель за досягаемостью). Код и дизайн не противоречат друг другу - правка приводит код к правилу.

## Правка

- `Source/CodexTactics/Public/Characters/ThreatFacingRules.h:49` + `Private/Characters/ThreatFacingRules.cpp:11` - чистое правило
  `MayCombatTargetTurnHim(bHasOwnFacing, TargetDistanceCm, WeaponReachCm)`: без приказа - любая живая цель (как раньше); с приказом - только
  цель в пределах `Stats.Range.MaxRangeCm` текущего оружия.
- `Source/CodexTactics/Private/Characters/OperativeCharacterAim.cpp:134-137` - условие ветки боевой цели в `UpdateCombatFacing`
  дополнено этим правилом (дальность оружия - из данных оружия, `weapons_tuning.json`; новых констант нет).
- Тест `CodexTactics.Squad.ThreatFacing.FacingOrderHoldsAgainstTargetOutOfReach`
  (`Source/CodexTacticsTests/Private/Characters/ThreatFacingRulesTest.cpp`).
- `Scripts/test_map.json`: удалена только запись `RetreatFacingSmoke` knownFail (после 3/3 PASS).
- Огневой гейт и `TurnAimTowards` в пути стрельбы не тронуты (там цель уже проверена `EvaluateShotLine` на дальность).

Коммиты: `9fc39f17` (код + тест), следующий - test_map + этот отчёт.

## Проверки

| Проверка | Результат |
|---|---|
| `Scripts/build.ps1` | Succeeded (133.5 s) |
| `test.ps1 -Filter CodexTactics.Characters` | 85 tests, 0 failed |
| `test.ps1 -Filter CodexTactics.Squad.ThreatFacing` | 2 tests, 0 failed (новый + старый) |
| RetreatFacingSmoke x3 (`-NoRetry -Jobs 1 -ExtraArgs -Multiprocess`) | PASS / PASS / PASS: `a facing order of the player is kept: no turn to the pack (0 deg off the ordered facing, 180 deg off the pack, turning to the threat 0)`; логи `Saved/Logs/claude074-run{1,2,3}.log` |
| FireGateSmoke | PASS |
| LookHoldSmoke | PASS |
| CoverSmoke | FAIL - одна строка `unknown threat, moving to his right (right after the cover entry) - actually playing 'anim_M4_cvr_std_idle_L'`; лог `Saved/Logs/claude074-cover.log` |
| `typesafe_triage.py --audit-diff --agent claude` | PASS (0.0 / 4) |

CoverSmoke: этот FAIL не связан с 074 - укрытие выходит из `UpdateCombatFacing` раньше правки (`if (bInCover) ... return`), приказа
`SetFacingPoint` в CoverSmoke нет; тот же FAIL уже описан в `test_map.json` (`_why` у CoverSmoke, 004 preload) и в отчёте Codex 019 на base
`74e129a2`. Его разбирает задача 075 (CoverSmoke 6/6).

## Решения для дизайнера

1. Принято (по докам, не новое): цель **в** досягаемости оружия по-прежнему разворачивает бойца, даже если игрок приказал смотреть в другую
   сторону (баг #39); цель **за** досягаемостью - нет, приказ держится.
2. Открытый вопрос (не менял): после боя с целью в досягаемости боец **не возвращается** к приказанному направлению - `SetFacingPoint`
   разовый, yaw приказа не хранится (в отличие от точки взгляда 059, к которой он возвращается после огневого приказа). Если нужно
   «после цели - обратно к приказу», это отдельная маленькая задача (хранить yaw приказа до нового приказа движения).

## Не сделано

- Ничего не пушилось и не сливалось.
- CoverSmoke зелёным в этой ветке не подтверждён (см. выше, задача 075).
