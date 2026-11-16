# Mantling: выравнивание рук, «подброс» и «замирание» - отчёт (2026-11-16, ветка `fix/mantle-align`)

Баг-репорт дизайнера 2026-11-16 (два видео, `Codex/Videos/MantlingProblem_01.avi` - высокое препятствие,
`MantlingProblem_02.avi` - среднее). Основа: mantling slice 1 (`docs/port/MANTLING_SLICE1_REPORT.md`). Клипы / `.uasset`, карты,
`Config/*.ini`, `L_Gen_Depot_1.json`, ABP не трогались.

## 1. Что показали видео (кадры ffmpeg, 5 к/с)

- **01 (парапет 230 см, TallLedge_ClimbUpNEW):** клип стартует, когда кольцо капсулы ещё ~80 см от стены; руки хватают воздух перед
  стеной, тело «доезжает» до стены уже в подтягивании. Наверху ~4,0 с клипа - резкая смена позы (встаёт из присяда), затем ~1,4 с
  неподвижной стойки, потом смена в присяд.
- **02 (обломок 140 см, HighClimb):** в первом же кадре клипа тело на ~1,5 м выше кольца капсулы (кольцо ещё на земле), весь клип
  тело висит над крышей, в конце падает вниз в присяд.

## 2. Причины (с цифрами)

Измерено новым смоуком `MantleAlignSmoke` (кадр за кадром, кости меша) и дампом самих клипов (`GetMantleClipTrack`: корень, таз,
кисти, стопы в пространстве клипа).

1. **«Подброс» на среднем (HighClimb) - сдвиг корня в первом кадре клипа.** Корень клипа HighClimb в кадре 0 стоит на
   **163,5 см** над началом координат клипа (стопы - 172…195 см). У клипов root lock = первый кадр
   (`setup_mantle_animation.py`), т.е. меш рисует всё тело сдвинутым на позицию корня из кадра 0 - на весь клип. Капсула при этом
   идёт по root motion правильно. Итог до правки: нижняя стопа поднимается на **169,4 см** над своей конечной высотой (на 0,73 с),
   и при blend-out тело «падает» на 1,6 м. Это свойство клипа (ретаргет оставил вертикальный оффсет корня), см. п. 6.
2. **Руки не попадают на кромку (высокий; немного и средний):**
   - тот же сдвиг корня у обоих клипов tall_ledge: корень в кадре 0 на **36,3 см позади** тела → тело нарисовано на 36 см дальше от
     стены, чем капсула;
   - старт считался как `forward_cm - top_inset_cm` (длина хода корня минус отступ посадки) - это ничего не говорит о том, где у
     клипа руки: у TallLedge кисть ложится на кромку в 46 см впереди старта, у HighClimb - в 143 см, у Low_Climb - в 57 см;
   - одно окно warp'а на весь клип раскладывает ошибку по всему движению, а к моменту хвата почти ничего не исправлено.
   До правки (выкл. `Codex.Mantle.Align 0`): ближайшая кисть к кромке **47,2 см** (TallLedge, 30 см перед стеной),
   **43,1 см** (Standing_JumpClimbUpNEW, 37 см перед стеной), **21,7 см** (HighClimb). Низкий (8,0 см) и забор (0,7 см) - в норме.
3. **«Замирание» после подъёма - хвост клипа.** Подъём считался законченным только на последнем кадре клипа, а у tall-ledge клипов
   последние 0,8-1,0 с - неподвижная стойка (TallLedge: движение кончается на 4,43 с из 5,20; Standing_JumpClimbUpNEW: 4,70 из 5,70).
   До правки: неподвижен **0,95 с** перед передачей в присяд (Standing_JumpClimbUpNEW). Резкая «странная поза» в видео - встаёт из
   присяда в самом клипе (~3,9-4,1 с), потом стойка, потом наш переход в присяд.
4. **Попутно найдено:** `EnforceMeshBaseOffset` каждый кадр «возвращает» меш по X/Y, если смещение не учтено, - он бы съедал
   компенсацию из п. 1 по горизонтали (34 предупреждения "mesh off its place" в первом прогоне правки, после учёта - 0).
5. Неверного выбора класса высоты нет: 83 → low_mantle, 138 → high_mantle, 228 → tall_ledge, 133 → high_fence_over.

## 3. Что изменено

| Где | Что |
|---|---|
| `Source/CodexTactics/Private/Characters/OperativeCharacterMantle.cpp:62` `GetMantleClipTrack` | движение клипа (root/pelvis/hands/feet) снимается с самой анимации через `UMotionWarpingUtilities::ExtractComponentSpacePose` с частотой `align.sample_hz`, кэш в `UMantleSubsystem` |
| `Source/CodexTactics/Private/Characters/MantleRules.cpp:665` `AnalyseClipTrack`, `:742` `ContactStartCm` | чистая логика: момент хвата (кисть дотянулась, поднялась, замерла, пока корень ещё в пути), точка кисти, момент окончания движения клипа (exit), старт без warp'а |
| `OperativeCharacterMantle.cpp:326` + `:388` `UpdateMantleMeshShift` (+ `OperativeCharacter.cpp:338`) | компенсация root lock: меш сдвигается на −(корень кадра 0) × вес монтажа (blend in/out, отмена), обновление в callback'е финализации костей и в Tick |
| `OperativeCharacterMantle.cpp:335/341/347` | два окна SkewWarp: старт → хват (кисть на кромку + `align.grip_*`), хват → посадка; только для посадки на верх |
| `OperativeCharacterMantle.cpp:433` | подъём заканчивается в exit клипа, хвост пропускается |
| `OperativeCharacterMantle.cpp:189-200` | стоячий клип на верх: дистанция подхода = старт по точке хвата (`ContactStartCm`) |
| `OperativeCharacterStance.cpp:304`, `OperativeCharacterCoverEntry.cpp:557` | смена капсулы стойки и `EnforceMeshBaseOffset` учитывают сдвиг меша |
| `OperativeCharacterMantle.cpp:47` | CVar `Codex.Mantle.Align` (1 = новое поведение; 0 = поведение slice 1 - A/B для дизайнера и смоуков) |
| `Content/Data/Movement/mantle.json` `align.*` | все пороги: sample_hz, plant_speed/reach/min_rise_fraction/hold, exit_root_cm, grip_forward/up, приёмка hand_edge_tolerance/pop_tolerance/freeze_limit/still_speed |
| `Source/CodexTacticsDev/Private/Debug/MantleAlignSmokeCommand.cpp`, `Scripts/test_map.json` | новый смоук MantleAlignSmoke (в реестре и в группе mantling) |
| `Source/CodexTacticsTests/Private/Characters/MantleRulesTest.cpp` | тест `CodexTactics.Characters.Mantle.ClipContact` (синтетический клип: хват, exit, висящие руки, слишком низкий хват) |

## 4. До / после (MantleAlignSmoke, Depot, точка клика 80 см от грани)

До = `-PreCmds "Codex.Mantle.Align 0"` (код slice 1), после = по умолчанию. Строки FAIL «до»:

```
Smoke FAIL: medium 140 cm wreck (HighClimb): the hands meet the edge - nearest 21.7 cm (18.4 cm in front of the face, 11.5 cm over the top at 0.02 s; tolerance 15)
Smoke FAIL: medium 140 cm wreck: no pop over the top - the lowest foot rises 169.4 cm over its rest (at 0.73 s; the clip's own step 6.4 -> excess 163.0), the capsule 11.6 cm over the landing (tolerance 15)
Smoke FAIL: high 230 cm parapet (1st clip) (TallLedge_ClimbUpNEW): the hands meet the edge - nearest 47.2 cm (30.1 cm in front of the face, 36.4 cm over the top at 2.70 s; tolerance 15)
Smoke FAIL: high 230 cm parapet (2nd clip) (Standing_JumpClimbUpNEW): the hands meet the edge - nearest 43.1 cm (36.6 cm in front of the face, -22.7 cm over the top at 2.07 s; tolerance 15)
Smoke FAIL: high 230 cm parapet (2nd clip): no freeze at the handoff - longest still stretch 0.95 s (ending 5.68 s; the climb ended at 5.70 s; limit 0.35)
```

| Случай (клип) | Кисть ↔ кромка до → после | Подброс (стопа сверх шага клипа / капсула) до → после | Замирание до → после | Конец подъёма |
|---|---|---|---|---|
| 85 ящик (Low_Climb) | 8,0 → 5,9 см | −0,6 / 0,0 → −0,3 / 0,9 см | 0 → 0 с | 1,02 с |
| 140 обломок (HighClimb) | 21,7 → **0,3** см | **163,0** / 11,6 → **1,2** / 13,4 см | 0 → 0 с | 1,03 с |
| 230 парапет (TallLedge_ClimbUpNEW) | 47,2 → **0,0** см | 1,8 / 0 → 4,8 / 0 см | 0 → 0 с | 5,20 → **4,43** с |
| 230 парапет (Standing_JumpClimbUpNEW) | 43,1 → **0,1** см | 12,0 / 0 → 12,4 / 0 см | **0,95** → 0 с | 5,70 → **4,70** с |
| 135 забор (HighFenceVault) | 0,7 → 0,7 см | - | 0 → 0 с | 2,10 → 2,07 с |

У TallLedge_ClimbUpNEW хвост «до» смоук неподвижностью не засчитал (кисти в 4,4-4,7 с ещё дрейфуют чуть быстрее порога 20 см/с), но
подъём теперь всё равно кончается на 4,43 с вместо 5,20 с - на 0,77 с раньше.
Капсула HighClimb поднимается на 13,4 см над посадкой - это сам клип (корень на 0,65 с на 15 см выше конечной точки), в допуске.
Посадка везде 2 см от плана (как в slice 1).

## 5. Гейты

| Гейт | Результат |
|---|---|
| `Scripts/build.ps1` | Succeeded |
| `test.ps1 -Filter CodexTactics.Characters.Mantle` | 8 / 8 (новый ClipContact + 7 старых, TuningFileMirrorsDefaults покрывает новые `align.*`) |
| `test.ps1 -Filter CodexTactics.Characters.Vault+CodexTactics.Movement.Vault` | 2 / 2 |
| `smoke_parallel.ps1 -NoRetry -Commands MantleSmoke,MantleAlignSmoke,VaultSmoke,DepotVaultSmoke,LadderClimbSmoke` (`-ExtraArgs -Multiprocess`: иначе старт ждёт чужой UBT) | 1-й прогон 5 / 5 PASS, 2-й прогон 5 / 5 PASS, без перезапусков (`LadderSmoke` в реестре нет - взят `LadderClimbSmoke`) |
| `check_smoke_names.py` | OK |
| `typesafe_triage.py --audit-diff --agent claude` | PASS, риск 2,0 / 4 |

Существующие проверки не ослаблялись.

## 6. Что проверить дизайнеру (PIE, `L_Gen_Depot_1`, Solo)

1. Клик на верх ящика 140 / обломка: тело не должно подниматься над кольцом капсулы и падать в конце.
2. Клик на парапет 230 два раза (два клипа по очереди): кисти ложатся на кромку, нет «доезда» к стене; после того как встал наверху,
   сразу переход в присяд, без секундной стойки.
3. Ящик 85 и забор 135 - как раньше.
4. A/B: консоль `Codex.Mantle.Align 0` вернёт поведение slice 1 для сравнения.
5. На TallLedge при старте с 80 см он теперь чуть «подаётся» к стене на прыжке (окно 1 сдвигает до ~35 см за 0,4 с) - если заметно,
   можно уменьшить `standing_slack_cm`, чтобы он подходил ближе пешком.

## 7. Не сделано / открыто

- **Клипы (не правились, отчёт):** у ретаргет-копий HighClimb (корень 163,5 см вверх в кадре 0) и обоих tall-ledge (корень 36 см
  позади тела) смещён корень первого кадра; сейчас это компенсируется в рантайме. Правильнее в ассете: root lock `Zero`/`RefPose` или
  переснять копии с корнем под стопами (`setup_mantle_animation.py`) - тогда компенсация станет нулевой сама.
- Standing_JumpClimbUpNEW: при вставании корень клипа уходит на ~48 см ниже стоп, нижняя стопа на ~15 см над крышей (~3,1 с) - и до,
  и после правки (сверх шага клипа 12,4 см, в допуске). Возможно, вклад IK стоп в ABP - посмотреть вместе.
- Беговые клипы и женские копии - тот же код (окно хвата + компенсация), но MantleAlignSmoke меряет только стоячие мужские; на Depot
  нет женщины в отряде. MantleSmoke (бег RunHighClimb) зелёный.
- Для «через» (забор) окно хвата не включается - там кисть и так на кромке (0,7 см).
- Хват по горизонтали выравнивается только вперёд/вверх, боковой дрейф клипа сохраняется.
