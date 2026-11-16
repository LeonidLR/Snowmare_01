# 067 - Ragdoll: проваливание в землю и слишком сильный импульс - отчёт (доделано Claude по ревью)

Ветка `gemini/067-ragdoll-tunnel`, worktree `CodexTactics-interim`. Работа Gemini: 57d40803 (план), 5b5ce177, ac6362b4.
Исправления по ревью Claude: **d734045e**. Не смёржено и не запушено.

## Заявления и доказательства

| Заявление | Доказательство |
|---|---|
| 1. Ослабленный лимит убран: `< 45` для всех субъектов, без 65 / 120 cm по видам | `DeathRagdollSmokeCommand.cpp:38` (`LyingPelvisLimitCm = 45`), `:368` (кость таза / масштаб меша < 45), `:375` (низ тела таза в [-20, 45) от пола); d734045e |
| 2. «Регрессия» Brute на самом деле - баг main: снап капсулы при заморозке утопил Brute на 134 cm | Эксперимент: код main + лог вокруг снапа, 2 прогона: `[EXP] BP_Enemy_Brute_C_0 snap: halfHeight=120.0 relZ -38.0 -> -105.0, pelvisZ 127.4 -> -6.5` (оба прогона одинаково). Тот же снап утапливал и пса: `Hound ... relZ -15.4 -> -45.0` |
| 2. Без снапа Brute лежит ровно на полу | Ветка, финальный гейт x3: `Brute pelvis height 69.2 cm (14.1 skeleton cm at mesh scale 4.90) < 45`, `Brute pelvis body bottom -0.0 cm from the floor in [-20, 45)`; лог заморозки `body bottom Z=-0.0, floor Z=0.0` |
| 2. Сетка безопасности теперь смотрит на низ тела таза, а не на кость | `CorpseSubsystem.cpp:517-545` (`GetPelvisBodyBottomZ` = AggGeom физассета по трансформу кости, `CalculatePelvisFloorCorrection(BottomZ, ...)`); тест `PelvisFloorCorrection` (случай Brute 134 cm -> подъём с капом 100 cm) |
| 2. Трассировка пола видит только то, на чём может лежать рэгдолл | `CorpseSubsystem.cpp:440` (`IsPhysicsCollisionEnabled` + блок `ECC_PhysicsBody`). До фикса: `floor Z=53.8 ... lifted the mesh 52.9 cm` (оверлап-бокс лут-ящика). Multi-трасса по типам объектов возвращает все попадания, отброшенные пропускаются (после фикса тот же Player KIA: `floor found under the pelvis (Z=0.0)`) |
| 2. Пёс: кость таза определяется по скелету | `CorpseSubsystem.cpp:259` `ResolvePelvisBone` -> `'Bip001-Pelvis'`. На main смоук пса мерил точку (0,0,0) (`GetBoneLocation('pelvis')` = ноль, «0.0 cm»), а импульс уходил в несуществующую кость |
| 3. Окно замера скорости = весь полёт; 900 cm/s держится | `DeathRagdollSmokeCommand.cpp:86` `SampleSpeeds` - каждый кадр от смертельного попадания до заморозки. Гейт x3: `Multi-shot target fastest body 900.0 cm/s from the hit until the freeze in (10, 900]` |
| 3. Окно клампа 1.5 s выбрано по цифрам | 0.6 s: `hand_r 901.0 cm/s 0.66 s after the hit` (FAIL). 3.5 s (вся симуляция): Brute заморожен в воздухе `body bottom Z=139.0 ... pelvis speed 135.8`. Кламп без вниз-компоненты: `thigh_r 2903.6 cm/s, 0.01 s` (скорость кости из анимации вниз). 1.5 s: всё зелёное |
| 3. Кламп и до импульса (скорости костей из анимации) | `CorpseSubsystem.cpp:261`; до: пёс `Bip001-L-Forearm 702.3 cm/s, 0.00 s`, после: `600.0` |
| Тайм-аут 3.5 s не замораживает падающее тело | `IsRagdollTimeoutFreeze` (`CorpseSubsystem.cpp:505`, `timeout_freeze_max_speed_cm_s` = 50); тест `SettleDetection`. Гейт: `BP_Enemy_Brute_C_0 freeze 4.76 s after death ... pelvis speed 49.5 cm/s`; потолок 8 s (`FCorpseConfig::MaxSettleSeconds`) остаётся |
| Импульс один раз за смерть | Защита Gemini от повторной активации `CorpseSubsystem.cpp:200-212`; 3 выстрела + взрыв в одном кадре: одна строка `activated death ragdoll` у Multi-shot target |
| 4. Константы в JSON | `Content/Data/Combat/death_ragdoll.json` +11 ключей (root_launch_speed_fraction, launch_clamp_window_s, max_angular_speed_rad_s, reference_body_mass_kg, max_pelvis_sink_cm, max_floor_lift_cm, floor_trace_up/down_cm, floor_min_normal_z, floor_max_above_pelvis_cm, timeout_freeze_max_speed_cm_s) + `FDeathRagdollConfig`; тест `ConfigLoads` |
| Смоук-раскладка: Brute на своём месте | `DeathRagdollSmokeCommand.cpp:429`: 6.5-метровый Brute падал от точки удара на Player KIA (`Player KIA pelvis height 45.4 cm`, `body bottom Z=27.7` на руке Brute) |

## Корневая причина Brute (с цифрами)

Brute - скелет с мировым масштабом меша **4.90** (актор x2), стоя таз ~320-385 cm, голова ~620-730 cm. Его тело таза (капсула
физассета ~20 cm x 4.9) толщиной **~99 cm**: при лежании кость таза на 69-112 cm над полом, а **низ тела таза на полу**
(диагностика: `pelvisBodyMinZ=0.0` при `pelvisZ=98.8`). На main при заморозке срабатывал снап капсулы
(`relZ > -HalfHeight*0.5` -> `relZ = -HalfHeight + 15`): `relZ -38.0 -> -105.0`, x2 = 134 cm вниз, кость `127.4 -> -6.5`.
То есть «-6.5 cm < 45» на main означало, что Brute **ушёл под землю на 1.3 м** - ровно баг 067. Gemini снап убрал (правильно),
и правда вылезла: 99 cm. Ни скорость-кламп, ни угловой кламп, ни заморозка падение не останавливали; трассировка пола
находила настоящий пол (Z=0). Поэтому лимит не ослаблен, а переведён в сантиметры собственного скелета (для людей масштаб 1.0 -
цифры прежние), и добавлена проверка низа тела таза (не утонул, лежит на полу). Утопленного как на main Brute она валит:
низ тела был бы на -134 cm.

Попутно найдено: (а) тайм-аут 3.5 s замораживал медленно падающего Brute в воздухе (низ тела 132-139 cm над полом);
(б) трассировка пола принимала оверлап-боксы (тело KIA поднимали на 53 cm в воздух); (в) у пса нет кости `pelvis` - импульс
терялся, смоук пса ничего не мерил.

## Пользовательские данные Gemini (сверено)

- (a) «Brute 240 cm, таз 150, лежит на 79-104» - высота неверна (масштаб 4.9, стоя таз 320-385), но вывод, что кость таза
  физически не бывает < 45 cm без утопления, верен: тело таза 99 cm толщиной. -6.5 на main = утопление (эксперимент выше).
  Лимит «из собственного скелета» = кость / масштаб меша < 45 плюс низ тела таза в [-20, 45) cm - не общий 120.
- (b) Player KIA 48.4 / 45.4 cm - не склон: пол плоский (StaticMeshActor_0, Z=0); тело лежало на руке упавшего Brute
  (`body bottom Z=27.7`). Исправлено раскладкой.
- (c) Multi-трасса по каналу останавливалась на первом блоке - заменена на запрос по типам объектов (все попадания)
  с отбором поверхностей, блокирующих PhysicsBody.

## Гейты (последние строки)

```
Scripts/build.ps1:            Result: Succeeded
test.ps1 -Filter CodexTactics.Combat.DeathRagdoll:   10 tests, 0 failed
smoke_parallel DeathRagdollSmoke,KnockdownSmoke,DeathCamSmoke -NoRetry (3 прогона подряд, без перезапусков):
  run 1: DeathCamSmoke PASS 47,3 | DeathRagdollSmoke PASS 45,8 | KnockdownSmoke PASS 45,9 - 3 run, 0 failed, 0 passed only on the re-run
  run 2: DeathCamSmoke PASS 45,7 | DeathRagdollSmoke PASS 46,7 | KnockdownSmoke PASS 45,3 - 3 run, 0 failed, 0 passed only on the re-run
  run 3: DeathCamSmoke PASS 46,9 | DeathRagdollSmoke PASS 46,5 | KnockdownSmoke PASS 45,8 - 3 run, 0 failed, 0 passed only on the re-run
smoke_parallel ShotgunSmoke -NoRetry: ShotgunSmoke PASS 43,2 - 1 run, 0 failed
typesafe_triage.py --audit-diff --agent claude: Boundary Breach: PASS, Risk Rating 1.9 / 4
```

Замечание по истории гейтов: промежуточные прогоны на этой ветке падали (кламп 0.6 s: 901 cm/s; Brute на Player KIA:
45.4 cm; Brute в воздухе на тайм-ауте) - каждый раз исправлялась причина, затем финальный набор прогнан заново целиком.

## Что не сделано / открыто

- Brute получает почти нулевой отброс (`delta-V 16.8 cm/s`): импульс x (масса / 80) упирается в `max_impulse_magnitude`
  60000, а масса Brute огромна. Это ощущение, не баг 067 - на решение дизайнера.
- После окна 1.5 s скорость не ограничена (гравитация): Brute при падении - кисть до 892.8 cm/s на 2.72 s. Это не запуск,
  а падение 6.5-метрового тела.
- Тела на склонах / лестницах в смоуке не проверялись (L_MovementTest плоский); пол меряется по мировой Z под костью таза.
- Визуально (скриншоты / клипы) не проверялось; только headless-логи.
- Не смёржено, не запушено; HANDOFF / TASK_BOARD не обновлялись (это делает ведущий при мёрже).
