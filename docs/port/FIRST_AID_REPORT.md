# Первая помощь по тяжести ранения — отчёт (feat/first-aid, 2026-11-16)

Ветка `feat/first-aid` (от main `2e8b5c5a`), worktree `CodexTactics-claude`. Задание дизайнера 2026-11-16: аптечка больше не мгновенная —
лечение проигрывает клип из пака `Content/HealingAnimations`, поза зависит от тяжести ранения; тяжелораненого лечит товарищ; новый предмет
«Медицинский саквояж». Godot-референса нет (в Godot аптечка мгновенная, `player.gd heal_with_item`).

## 1. Как устроено

| Часть | Файл |
|---|---|
| Чистые правила (тиры, режимы, время, HP, отмены, клипы, разбор JSON) | `Source/CodexTactics/Public/Survival/FirstAidRules.h`, `Private/Survival/FirstAidRules.cpp` |
| Все числа | `Content/Data/Survival/first_aid.json` (код-умолчания зеркалят его, тест `DataFile`) |
| Рантайм (подход, позиция, клипы, момент применения, прерывания, записи для смоуков) | `Public/Survival/FirstAidSubsystem.h`, `Private/Survival/FirstAidSubsystem.cpp` (`UTickableWorldSubsystem`) |
| Проигрывание клипа на слоте `FullBody` | `Private/Characters/OperativeAnimInstanceFirstAid.cpp` (+ объявления в `OperativeAnimInstance.h`) |
| Ретаргет клипов | `Scripts/Editor/setup_first_aid_animation.py` |
| Тесты | `Source/CodexTacticsTests/Private/Survival/FirstAidRulesTest.cpp` (`CodexTactics.Survival.FirstAid.*`, 8 тестов) |
| Смоук | `Source/CodexTacticsDev/Private/Debug/FirstAidSmokeCommand.cpp` (`CodexTactics.FirstAidSmoke`, зарегистрирован в `Scripts/test_map.json`) |

**Соответствие тиров.** В коде уже были тиры `CriticalWoundRules::EWoundTier`: Light / Heavy / Critical. Дизайнерские слова (они же ключи
в `first_aid.json`) ложатся так:

| Дизайнер (`first_aid.json`) | Код (`EWoundTier`) | HP | Что видно | Первая помощь |
|---|---|---|---|---|
| none | None (но HP < max) | ≥ 50 % | ничего | сам, стоя (как light) |
| light | Light | 50–35 % | хромает слегка | **сам, стоя** |
| medium | Heavy | 35–20 % | держится за бок, медленно хромает | **сам, на колене / сидя** |
| heavy | Critical | < 20 % | лежит, ползёт | **сам не может**, лечит товарищ |

Лежащий (prone) с light/medium лечит себя «сидя на земле» (набор `self_lying`), чтобы не вставать ради клипа.

## 2. Клипы пака HealingAnimations (все 42)

Все клипы — скелет пака `/Game/HealingAnimations/Demo/Mannequins/Meshes/SK_Mannequin` (UE5 Manny, `spine_01..05`) — **не совпадает** со
скелетами оперативников (`UE4_Mannequin_Skeleton` у SK_Post_Apo_Survivor_a и `Female_Soldier_Skeleton`). Поэтому — IK-ретаргет как у
мантла / injured: временная копия `RTG_UE5Manny_UE4Manny` из M4-пака (источник = `IK_Mannequin` пака, цель = `IK_UE4_Mannequin`, «Blend
to Source» выкл.), копия перекейвана, root motion выкл. Root motion во всех клипах пака выключен, корень не двигается (travel 0 см) — все
клипы «на месте». Поза по высоте таза (начало / середина / конец, см над стопой) и головы в середине — замер скрипта
(`Saved/Logs/setup_first_aid_animation.json`).

| Клип | Длина, с | Поза (таз нач/сер/кон; голова) | Роль | Используется |
|---|---|---|---|---|
| AS_BandageTreatment | 5.23 | стоя (95/93/95) | сам | medkit + bag: light стоя |
| AS_BandageTreatment2 | 2.70 | стоя (95/92/95) | сам | medkit: light стоя |
| AS_InjectionInTheHand | 4.57 | стоя | сам | medkit: light стоя |
| AS_InjectionInTheShoulder | 4.20 | стоя | сам | medkit: light стоя |
| AS_InjectionInTheLeg | 3.93 | стоя, наклон (95/90/95) | сам | bag: light стоя |
| AS_DrinkHealing | 3.70 | стоя | сам (питьё) | нет |
| AS_DrinkHealing2 | 4.70 | стоя | сам (питьё) | нет |
| AS_BandageTreatmentOnKnee | 7.93 (RateScale → 6.61) | встаёт на колено и поднимается (95/48/95) | сам | medkit: medium |
| AS_BandageTreatmentOnKnee2 | 4.77 | на колено и обратно | сам | medkit: medium |
| AS_InjectionInLegSquating | 4.87 | присед и обратно | сам | medkit: medium |
| AS_HandInjectionSquating | 6.73 (RateScale → 5.61) | присед и обратно | сам | medkit: medium |
| AS_SquatLegBandage | 7.73 | присед и обратно | сам | bag: medium |
| AS_DrinkHealingSquat | 7.70 | присед и обратно | сам (питьё) | нет |
| AS_BandageArmSitting | 9.87 | садится на землю и встаёт (95/14/95; голова 79) | сам | bag: medium |
| ASInjectionInLegSitting2 | 9.70 | садится и встаёт | сам | нет |
| AS_InjectionInArmSitting2 | 9.87 | садится и встаёт | сам | нет |
| AS_DrinkHealingSitting2 | 9.07 | садится и встаёт | сам (питьё) | нет |
| AS_InjectionInArmSitting | 7.00 | сидит всё время (15/15/15; голова 81) | сам | medkit + bag: лёжа (prone) |
| AS_HealingBandageSitting | 6.30 | сидит всё время | сам | medkit + bag: лёжа (prone) |
| AS_DrinkHealingSitting | 5.03 | сидит всё время | сам (питьё) | нет |
| AS_ArmInjectonLying | 10.77 | ложится на спину и встаёт (95/19/95; голова 33) | сам | нет (начинается стоя) |
| AS_LegInjectionLying | 8.60 | ложится и встаёт | сам | нет |
| AS_ShoulderInjectionLying | 8.43 | ложится и встаёт | сам | нет |
| AS_DrinkHealingLying | 3.97 | лежит всё время (голова 26) | сам (питьё) | нет |
| AS_GetHealingDrink | 7.67 | лежит всё время | раненый (пьёт) | нет |
| AS_BanadageHealingFriendFast | 6.27 | помощник: присед и обратно (95/48/95) | помощь другу | **medkit: помощник** |
| AS_BandageFriendSquat | 7.87 | помощник: присед | помощь другу | нет (резерв) |
| AS_BandageHealingFriend | 13.27 | помощник: присед (длинный) | помощь другу | **bag: помощник** |
| AS_InjectionHealingFriend | 9.47 | помощник: присед | помощь другу | **bag: помощник** |
| AS_HealingFriendInShoulder | 8.87 | помощник: присед | помощь другу | нет (резерв) |
| AS_GiveHealingDrinkFriend | 8.83 | помощник: присед | помощь другу (питьё) | нет |
| AS_GiveHealingDrinkFriend2 | 10.23 | помощник: присед | помощь другу (питьё) | нет |
| AS_WoundedGetInjection | 5.77 | лежит всё время (21/22/21; голова 25) | раненый | **medkit + bag: пациент** |
| AS_WoundedLying | 2.67 | лежит (цикл) | раненый | **bag: пациент** |
| AS_WoundedCallsHelpLying | 15.67 | лежит | раненый зовёт | нет |
| AS_WoundedDrink | 9.30 | сидит на земле | раненый (пьёт) | нет |
| AS_WoundedSitting | 7.30 | сидит на земле | раненый | нет |
| AS_WoundedCallsHelp | 14.33 | на коленях | раненый зовёт | нет |
| AS_WoundedCallsHelp2 | 10.47 | на коленях | раненый зовёт | нет |
| AS_WoundedCallsHelpStand | 10.40 | стоя | раненый зовёт | нет |
| AS_WoundedMoving | 7.63 | стоя, ходьба на месте | раненый | нет |
| AS_WoundedMovingRM | 8.63 | стоя (RM-флаг выкл., travel 0) | раненый | нет |

Ретаргетнуты 18 используемых клипов × 2 тела = **36 новых ассетов** (24 МБ, LFS):
`Content/Characters/Animations/FirstAid/Male/<имя>` и `.../Female/<имя>` — AS_BandageTreatment, AS_BandageTreatment2, AS_InjectionInTheHand,
AS_InjectionInTheShoulder, AS_InjectionInTheLeg, AS_BandageTreatmentOnKnee, AS_BandageTreatmentOnKnee2, AS_InjectionInLegSquating,
AS_HandInjectionSquating, AS_SquatLegBandage, AS_BandageArmSitting, AS_InjectionInArmSitting, AS_HealingBandageSitting,
AS_BanadageHealingFriendFast, AS_BandageHealingFriend, AS_InjectionHealingFriend, AS_WoundedGetInjection, AS_WoundedLying.
Копии проверены замером (таз у мужского тела на ~6 % выше — пропорции тела, у женского совпадает с паком ±5 см). Чтобы добавить клип:
вписать имя в `first_aid.json` → `UnrealEditor-Cmd.exe CodexTactics.uproject -run=pythonscript -script=<abs>/Scripts/Editor/setup_first_aid_animation.py -unattended -nullrhi -nosplash`.
Новых монтажей / data-asset'ов нет: клипы играются динамическим монтажом.

## 3. Что играет когда

- **Light / none, стоя** — один случайный клип из `self_standing` (не тот же, что в прошлый раз): перевязка руки или укол.
- **Medium** — `self_kneeling` (на колено / присед / садится на землю и встаёт обратно).
- **Лежит (prone), light / medium** — `self_lying` (сидит на земле).
- **Heavy** — сам отказ («… слишком тяжело ранен, чтобы лечить себя — перетащите аптечку или саквояж на него из инвентаря товарища»).
  Помощник: `helper` (аптечка — быстрый `AS_BanadageHealingFriendFast`, саквояж — длинные `AS_BandageHealingFriend` / `AS_InjectionHealingFriend`);
  раненый (если лежит) — `patient` (`AS_WoundedGetInjection`, у саквояжа ещё цикл `AS_WoundedLying`).
- Скорость клипа подгоняется под длительность лечения в пределах `min_play_rate`…`max_play_rate` (0.6…1.6), короткий клип зацикливается.
- Оружие на время клипа скрыто (`hide_weapon`), левая рука / прицел отключены; по окончании — blend-out 0.35 с, оружие снова видно.
- Если чужой клип того же слота (выстрел / попадание) срезал монтаж — клип перезапускается с нужного места (до 3 раз).

## 4. Числа (`Content/Data/Survival/first_aid.json`)

| | Аптечка | Саквояж |
|---|---|---|
| HP (`heal`) | 80 (как в Godot) | 140 |
| Улучшение тира (`tier_steps`; HP поднимается минимум до нижней границы нового тира + 0.5) | 1 | 2 |
| Сам: none / light / medium, с | 3.5 / 4.0 / 5.5 | 6.0 / 7.0 / 9.0 |
| Товарищ: none / light / medium / heavy, с | 4 / 4 / 5 / **6** | 7 / 7 / 9 / **11** |
| Момент применения (`apply_at`) | 0.8 длительности | 0.85 |
| Носит максимум (`max_carried`) | без лимита | 1 |
| Вес (`weight_kg`, только данные) | 0.5 | 3.0 |

Общие: `reach_cm` 160, `approach_stop_cm` 85, `approach_timeout_s` 25, `move_tolerance_cm` 45, `cancel_min_damage` 1 HP за тик,
`consume_on_cancel` false, `cancel_on_damage_while_approaching` false, `treat_on_drop_tiers` ["heavy"], `helper_may_be_medium` true,
`start_medical_bags` {"MedicSapper": 1}. Пример: тяжелораненый 15/100 HP → аптечка 95 HP (тир none), саквояж 100 HP.

## 5. Как пользоваться

- **Сам**: открыть инвентарь (INV) и кликнуть строку «Medkit [H]» или «Medical bag», либо нажать H (аптечка). Light — стоя, medium — на колене.
- **Тяжелораненый**: выбрать здорового оперативника (он лидер, его инвентарь в ящике), перетащить строку аптечки или саквояжа на 3D-модель
  раненого или на его портрет в панели. Помощник подходит (если дальше 1.6 м), присаживается рядом лицом к раненому, оба играют клипы,
  в момент применения — HP и тир. С зажатым **Shift** перетаскивание = обычная передача предмета (как у грелки).
- Перетаскивание на НЕ тяжелораненого — как раньше, передача предмета.
- **Прерывания**: урон (≥ 1 HP за тик) помощнику или раненому, новый приказ движения / смена стойки / вход в укрытие у любого из двух, сдвиг
  тела больше 45 см, помощник дальше досягаемости, смерть любого, нокдаун помощника, неудачный подход. До момента применения предмет
  остаётся (feed: «Treatment interrupted (…) - the item is kept»), после — HP уже начислено.
- **Пошаговый бой**: аптечка — как раньше мгновенно (заканчивает ход), саквояж в пошаговом бою не используется; drag в пошаговом = передача.
- Саквояж: в инвентаре (строка «Medical bag x?/1»), в ящиках (`FLootContents.MedicalBags`), в останках павшего, на земле, в сохранении
  (`medical_bags_count`), в переносе кампании, в level JSON kit (`"medical_bags"`), старт MedicSapper = 1. Иконка / цвет — от аптечки (🩹).

## 6. ABP: что проверить дизайнеру

ABP не менялись. Клипы играют через существующий слот **`FullBody`** (`UOperativeAnimInstance::FullBodySlot`, тот же, что у нокдауна /
ревайва, мантла, взаимодействий, укрытия). Нужен только этот слот — он уже есть, добавлять ничего не надо. Проверить глазами:
1. Раненый лежит в prone (лицом вниз, поза ползания), а клипы пациента — лёжа на спине: на старте лечения тело «переворачивается» за
   blend-in 0.25 с. Если это выглядит плохо — нужен клип перехода prone → на спину (в паке его нет) или blend-in подольше (`blend_in_s`).
2. Помощник ставится на 85 см от центра капсулы раненого лицом к нему; клипы пака авторены с другом на своём расстоянии (руки вперёд
   на 23–37 см от таза) — руки могут не попадать точно на тело. Подстройка — `approach_stop_cm` / `reach_cm`.
3. Клипы self_kneeling / sitting начинаются и кончаются стоя (встаёт сам), это хорошо стыкуется со стоячей капсулой.
4. Оружие прячется целиком (`hide_weapon`); если нужен «за спиной» — это работа ABP / сокета, не сделано.

## 7. Решения по умолчанию

- Тиры: designer heavy = код Critical, medium = код Heavy (см. таблицу §1).
- Саквояж можно применять и на себя (light / medium), дольше и сильнее аптечки.
- Лечить может помощник с medium-ранением (`helper_may_be_medium`), с heavy — нет.
- Урон во время подхода помощника лечение не отменяет (отменяет только во время клипа) — `cancel_on_damage_while_approaching`.
- Отменённое до `apply_at` лечение не тратит предмет.
- Пошаговый бой — старое мгновенное поведение аптечки.
- Отказ в укрытии / на лестнице / в мантле / с ношей («busy»).
- Три старых смоука (InventorySmoke, EventBusSmoke, RealtimeSelectSmoke), проверявшие МГНОВЕННУЮ аптечку, на время своей проверки
  выставляют `enabled=false` (фолбэк); новое поведение проверяет FirstAidSmoke.

## 8. Проверки

- `Scripts/build.ps1` — Succeeded; `find_unity_clashes.py` — 0 конфликтов (в первом варианте был `using namespace FirstAidRules` в
  субсистеме — убран, всё квалифицировано).
- `Scripts/test.ps1 -Filter CodexTactics.Survival+CodexTactics.Characters.CriticalWound+CodexTactics.Animation.Injured+CodexTactics.Squad.WoundPace+CodexTactics.Characters.Transfer+CodexTactics.Transfer+CodexTactics.Loot`
  — **66 / 66 PASS**, из них 8 новых `CodexTactics.Survival.FirstAid.*` (TierToMode, SelfCheck, AidCheck, HealAndTiers, ClockAndClips,
  Interrupts, Parse, DataFile).
- `smoke_parallel.ps1 -NoRetry -ExtraArgs -Multiprocess -Commands FirstAidSmoke,InjuredLocomotionSmoke,KnockdownSmoke,WoundedFollowerSmoke,InventorySmoke,TransferSmoke,EventBusSmoke,RealtimeSelectSmoke`
  — **2 раза подряд 8 / 8 PASS** (плюс 4 одиночных прогона FirstAidSmoke PASS).
- FirstAidSmoke (реальный путь ввода: клик строки инвентаря, drag-операция ящика → портрет / 3D-модель): light — стоячий клип
  (AS_InjectionInTheHand / Shoulder / BandageTreatment…), 4.0 с, HP 56 → 136 из 140, аптечка −1, оружие скрыто и возвращено; medium —
  клип на колене, 5.5 с, 39 → 119; heavy на себя — отказ; heavy аптечкой от товарища с 6 м — подход 3 с, помощник
  AS_BanadageHealingFriendFast + раненый AS_WoundedGetInjection, 6.0 с, 22 → 102 из 150, тир heavy → none; саквояжем — 11.0 с,
  22 → 150 (+128 против +80), клипы AS_BandageHealingFriend / AS_InjectionHealingFriend; отмена приказом движения на 2.4 с — прервано,
  аптечка сохранена, HP не изменилось, клипы остановлены.
- `python Scripts/Tools/check_smoke_names.py` — OK; `typesafe_triage.py --audit-diff --agent claude` — см. коммит (аудит незакоммиченных
  изменений чистый; запрещённых путей — Content/Maps, Config/*.ini, Content/VFX — в диффе ветки нет).
- Найденная ловушка: у части клипов пака свой RateScale (AS_BandageTreatmentOnKnee 7.93 с → монтаж 6.61 с, AS_HandInjectionSquating
  6.73 → 5.61): скорость подгоняется под длину динамического монтажа, не под длину последовательности (иначе клип кончался на 1 с раньше).
- Не проверено глазами: рендер-кадры не снимались (headless). Дизайнеру — посмотреть в PIE (§6).

## 9. Не сделано

- ИИ отряда / медик-автономия (`SquadAutonomySubsystem` → `HealAlly`) и PMC-приказ Heal — остались мгновенными; перевести на
  `UFirstAidSubsystem::RequestAid` — отдельная задача (нужно согласовать с логикой подхода автономии).
- Бот плейтеста (`UsePersonalItem`) — мгновенный.
- Клипы питья (Drink*) и «зовёт на помощь» (CallsHelp*) не используются.
- Нет клипа перехода prone → лёжа на спине (см. §6.1).
- Вес предметов — только данные, системы веса нет.
- Wave Editor не знает поле kit `medical_bags` (level JSON читается, редактор не показывает).
