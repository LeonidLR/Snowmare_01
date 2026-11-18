# G007 план — бой на пандусе STAIR_ext_7 (B17-6)

Фикс в этом коммите не начат. Ветка `grok/G007-ramp-fight`. Роутер: level 3 | grok-4.7 (основная сессия; confidence 0.58, порог 0.60, route false) | смоук-диагноз записан, поведение не менялось.

Сборка: `Scripts/build.ps1`, Result Succeeded, 296.51 с, exit 0. Лог `Saved/Logs/Build.log`. `RampFightSmokeCommand.cpp` собран отдельно (`[40/56] Compile RampFightSmokeCommand.cpp`).

Смоук один раз: `smoke_parallel.ps1 -NoRetry -ExtraArgs -Multiprocess -Commands RampFightSmoke`. FAIL 44.8 с. `[smoke_parallel] 1 run, 1 failed`. Лог `Saved/Logs/Smoke-RampFightSmoke.log`.

Карта `L_Gen_Depot_1`, лестница `STAIR_ext_7` (те же точки, что у `LevelGenRampSmoke`: низ (−3920, 1622), верх пролёта (−3920, 2480), +6 м). Один оперативник с верхней части вниз, один Frostbitten у нижней. Остальные в Solo и Passive, давление и ЧВК выключены. Раз в 0.1 с: видит / цель / выстрел / линия / блокер трассы / Z удара / здоровье.

## Первые 5 с

Постановка, строка 1597:

```
[RampFight] placed STAIR_ext_7 operative Commander feet (-3920, 2326, 505) frostbitten feet (-3920, 1776, 135) goal (-3920, 1665, 68) reach 180
```

Дальше оба уже видят друг друга. Трасса из дула и из глаз до конца окна `clear` — пол пандуса и его край выстрел не режут. `canMelee` в строке смоука — это ноги к ногам; живой ИИ считает иначе (ниже).

| t | sees | fires | line | enHP | opHP | dist2 | dz | enV | canMelee (ноги) |
|---|---|---|---|---|---|---|---|---|---|
| 0.1 | 1/1 | 0 | SHOT ok, clear | 60 | 140 | 549 | 370 | 0 | 0 |
| 0.3 | 1/1 | 1 | SHOT ok, clear | 60 | 140 | 519 | 363 | 171 | 0 |
| 0.9 | 1/1 | 1 | SHOT ok, clear | 42 | 140 | 244 | 171 | 100 | 0 |
| 1.0 | 1/1 | 0 | no shot: dead zone | 42 | 140 | 234 | 163 | 100 | 0 |
| 1.6 | 1/1 | 0 | dead zone | 42 | 140 | 144 | 98 | 46 | 1 |
| 2.1 | 1/1 | 0 | dead zone | 42 | 140 | 124 | 83 | 0 | 1 |
| 2.7 | 1/1 | 0 | dead zone | 42 | 140 | 121 | 80 | 0 | 1 |
| 2.8 | 1/1 | 0 | SHOT ok, clear | 42 | 140 | 121 | 80 | 0 | 1 |
| 3.0 | 1/1 | 1 | SHOT ok, clear | 24 | 140 | 120 | 79 | 0 | 1 |
| 3.6 | 1/1 | 1 | SHOT ok, clear | 5 | 140 | 116 | 75 | 0 | 1 |
| 4.2 | 1/1 | 0 | SHOT ok, clear | 5 | 140 | 121 | 79 | 0 | 1 |

Сырые кадры смены режима (`Smoke-RampFightSmoke.log` 1607, 1609, 1616, 1617, 1623, 1628, 1635, 1638):

```
t=0.1 sees=1 enSees=1 target=BP_Enemy_Frostbitten_C_0 enTarget=BP_Operative_C_0 fires=0 auto=1 posture=2 line={SHOT: ok | ... clear} muzBlock=clear eyeBlock=clear ... canMelee=0 dist3=662 dist2=549 dz=370 reach=180 opZ=506 enZ=135 opHP=140 enHP=60
t=0.3 ... fires=1 ... SHOT: ok ... clear ... enHP=60 opV=220 enV=171
t=0.9 ... fires=1 ... SHOT: ok ... clear ... dist2=244 dz=171 opHP=140 enHP=42
t=1.0 ... target=- ... fires=0 ... line={no shot: dead zone} muzBlock=clear eyeBlock=clear ... dist2=234 dz=163 enHP=42
t=1.6 ... fires=0 ... dead zone ... meleeClear=1 canMelee=1 dist3=175 dist2=144 dz=98 reach=180 enV=46 opHP=140 enHP=42
t=2.1 ... fires=0 ... dead zone ... canMelee=1 dist2=124 dz=83 enV=0 opHP=140 enHP=42
t=2.8 ... target=BP_Enemy_Frostbitten_C_0 fires=0 line={SHOT: ok | ... clear} ... dz=80 enHP=42
t=3.0 ... fires=1 ... SHOT: ok ... enHP=24
```

Между 1.0 и 2.7 каждая десятая такая же: `fires=0`, `dead zone`, трасса `clear`, enHP 42. С 2.8 линия снова `SHOT ok`, здоровье падает 42 → 24 → 5. Строк `[Melee]` в логе нет. Frostbitten умирает около 5 с, смоук судит раньше 20 с.

```
Smoke ok  : operative sees the Frostbitten at 0.1 s
Smoke ok  : Frostbitten took damage 0.80 s after sight (hp 60 -> 5)
Smoke FAIL: Frostbitten melee never hit the operative
Smoke RESULT: FAIL
```

Поза 2 = Aggressive (`FirePostureRules.h`). Первый урон проходит за 0.8 с после видимости — лимит 1.5 с этот заход не ломает. Ломается ближний бой и окно, где выстрел запрещён при чистой трассе.

## Что это доказывает

1. Пол и край пандуса выстрел не закрывают. `muzBlock=clear`, `eyeBlock=clear` все 5 с. «Тот же этаж» видимость тоже не режет: `sees=1` и `enSees=1` с 0.1 с. Гипотезы «трасса в пол» и «Z этажа закрывает обзор» на этом заходе не подтвердились.

2. Окно без урона — мёртвая зона, не промах. `SquadFireRules::IsInDeadZone` (`SquadFireRules.cpp:51-59`): `DeltaZ = ноги стрелка + 100 − ноги цели`, порог 180 см, горизонталь ≤ 240 см, угол ≥ 45°. На t=1.0: DeltaZ = 406+100−243 = 263, горизонталь 234, угол ~48°. С t=2.8 DeltaZ = 399+100−320 = 179, зона кончается, урон снова идёт. Правило написано про человека под платформой. На уклоне грудь-к-ногам даёт ≥ 45°, пока трасса из дула пустая. Это и есть «несколько секунд стреляют / целятся, жизни не снимаются», потом обычный огонь. С дальней дистанции (t=0.1, угол ещё < 45°) первый выстрел проходит — «никто не стреляет» в начале этого захода не повторилось.

3. Frostbitten в радиусе по ногам и не бьёт. С t=1.6 `dist3≤180`, `dz` ног 75–98 ≤ 120, `meleeClear=1`, цель — оперативник, с t=2.1 скорость 0. `AttackTarget` не вызывался (нет `[Melee]`). Живая проверка другая: `EnemyCharacterBehavior.cpp:314-316` кормит `CanMelee` точкой `GodotPosition` = ноги + 100 см (`EnemyCharacter.cpp:200-205`). На t=2.1 высота для ИИ = |399+100−317| = 182 > 120, дистанция до этой точки ~220 > 180. Оба предела мимо, хотя тела уже рядом. `MeleeApproachAcceptance` при высоте больше радиуса даёт 0 (`EnemyAIRules.cpp:240-249`), он останавливается и не бьёт. На ровном полу dz ног ~0, высота Годота ~100 ≤ 120, удар живой. Кап 120 см на склоне фактически требует |dz ног| ≤ 20 см.

## После ревью (не сейчас)

Порог 1.5 с и проверку досягаемости не ослаблять.

- Мёртвая зона: правка в `SquadFireRules` (это правило линии огня, оно в Allowed paths). Числа 180 / 240 / 45 / +100 сейчас литералы. Если порог остаётся — вынести в JSON конфига огня, не новым магическим числом в C++. Смысл правки: зона не включается, когда трасса до цели пустая и оба стоят на одном уклоне, а не когда стрелок на площадке над целью. Плоский тест `IsInDeadZone` «прямо под платформой» (`SquadFireRulesTest.cpp`) должен остаться красным для настоящей платформы. `OperativeCharacterAim.cpp`, `FireGateRules.*`, `ThreatFacingRules.*` не трогаю. Если окажется, что без них правило не отключить — одна строка в чат и жду.
- Удар: мерить высоту ногами к ногам против того же капа 120 см (снять лишние 100 см `GodotPosition` из сравнения), а не опускать 120. Иначе уступ в 1.1 м, который сейчас вне удара, станет в зоне — это уже ослабление, его не делаю без явного да. Число капа, если поедет, в конфиг архетипа / `EnemyAIRules`, не литерал. `AttackRange` 180 не увеличивать.
- Смоук: колонка `canMelee` должна считать так же, как ИИ (сейчас ноги к ногам, поэтому в логе 1, пока ИИ видит 0). Гейт «удар в нормальной досягаемости» не расширять. Лимит урона 1.5 с не двигать.

Гейт после фикса, не этого плана: `RampFightSmoke` 3/3, `LevelGenRampSmoke`, `FireGateSmoke`, `CoverSmoke`, `EnemyArchetypeSmoke`, юниты зрения и `CodexTactics.Characters.EnemyAI`, `typesafe_triage.py --audit-diff --agent grok`.
