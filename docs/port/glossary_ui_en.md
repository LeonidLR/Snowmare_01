# UI / HUD English glossary (C++ player text) — 2026-10-08

User decision 2026-10-08: all in-game text is English for now (RU comes back later with a proper localization pass).
This list holds the terms used in the C++ player text (`Source/CodexTactics/**`) that are NOT (yet) in Gemini's
`Content/Data/Narrative/glossary.csv`, so Gemini can reconcile them into the narrative glossary. Terms that ARE in
glossary.csv are used as written there (Frost Hound, MUV-3 Tripwire Mine, AI-2 Military Medkit → "Medkit" in compact HUD
slots, M16A2, Backup Diesel Generator, Outpost Marksman → "Marksman" in compact labels).

Style: HUD labels / floating texts in CAPS like the Russian originals; feed lines sentence case; emojis kept; "pcs" → `x3`
style counts; "m" for metres, "s" for seconds.

| RU | EN | Where |
|---|---|---|
| Командир / Инженер / Медик-сапёр | Commander / Engineer / Medic-Sapper | operative default names (`OperativeCharacter.cpp`) |
| Иван (Сусанин) | Ivan (Susanin) | recruit |
| КОМ / ИНЖ / МЕД / ИВАН / РЕЗ | CMD / ENG / MED / IVAN / RES | action-bar role tags |
| ШТАБ / ОТРЯД / НАБЛЮДЕНИЕ | HQ / SQUAD / SURVEILLANCE | feed speakers |
| Механо-гончая Cutter | Cutter Mech-Hound | enemy name |
| Ледяной стрелок | Frost Spitter | enemy name (Spitter / Cryo Drone) |
| Ледяной громила | Frost Brute | enemy name |
| Снайпер | Marksman | enemy name (glossary: Outpost Marksman) |
| Промёрзший | Frostbitten | enemy name |
| СТОЯ / СИДЯ (ПРИСЕВ) / ЛЁЖА; С / П / Л | STANDING / CROUCHED / PRONE; S / C / P | stance labels / action bar |
| ПАССИВНЫЙ / ОБОРОНИТЕЛЬНЫЙ / АГРЕССИВНЫЙ; ПАСС / ОБОР / АГР; П / О / А | PASSIVE / DEFENSIVE / AGGRESSIVE; PAS / DEF / AGG; P / D / A | fire posture |
| РЕАЛЬНОЕ ВРЕМЯ / ТАКТИЧЕСКАЯ ПАУЗА / ПОШАГОВЫЙ БОЙ | REAL TIME / TACTICAL PAUSE / TURN-BASED | combat time modes |
| ИССЛЕДОВАНИЕ / КАТСЦЕНА / ПОДГОТОВКА / БОЙ / ВОЛНА ОТБИТА / ПОСЛЕ БОЯ / ПРОВАЛ | EXPLORATION / CUTSCENE / PREPARATION / COMBAT / WAVE CLEARED / AFTER COMBAT / MISSION FAILED | game phases |
| ❓ ПОИСК / ОТБОЙ / ❗ ТРЕВОГА! / ЗАСАДА! | ❓ SEARCHING / STAND DOWN / ❗ ALARM! / AMBUSH! | stealth floating texts |
| ОРДА | HORDE | horde warning |
| РУБЕЖ | HOLD LINE | defence-line marker |
| Автономия ВКЛ / ВЫКЛ | Autonomy ON / OFF | squad autonomy |
| Режим соло | Solo mode | leader scouting alone |
| Точка обороны / ОБОР / ЗАФИК | Guard point / GUARD / HELD | guard order |
| норма / озноб / замерзает / гипотермия / ОБМОРОЖЕНИЕ | normal / chills / freezing / hypothermia / FROSTBITE | cold tiers |
| ПАНИКА / СТРЕСС / ЯРОСТЬ | PANIC / STRESS / RAGE | morale badges |
| Аптечка / Консервы / Хлеб / Шоколад / Спички | Medkit / Canned food / Bread / Chocolate / Matches | personal items |
| Граната (Ф-1) / Растяжка / Мина / Турель / Баррикада | Grenade (F-1) / Tripwire / Mine / Turret / Barricade | deployables |
| Бочка / Ящик снабжения / Останки | Barrel / Supply crate / Remains | interactables |
| Автомат M16 / Пистолет Beretta / Дробовик / Тактический нож | M16 Rifle / Beretta Pistol / Shotgun / Tactical Knife | weapon selector |
| Огнемёт / Крио-излучатель / Плазменный карабин | Flamethrower / Cryo Emitter / Plasma Carbine | special weapons |
| Топливо / Хладагент / Плазма / Дробь 12к / Патроны 9мм | Fuel / Coolant / Plasma / 12g shells / 9mm rounds | ammo |
| Осечка (затвор заклинил) | MISFIRE (bolt jammed) | cold weapon jam |
| Оружие замёрзло | WEAPON FROZEN | cold |
| Перенос / Перемещение (ПЕР) | Relocate (MOVE) | relocation mode |
| ХОД | MOVE | move mode (turn-based) |
| Конец хода отряда / Следующий боец | END SQUAD TURN / NEXT OPERATIVE | turn-based panel |
| ОД | AP | action points |
| ИНВ / ПЕР / АКТИВ / АВТО ВКЛ/ВЫКЛ | INV / MOVE / ACTIVE / AUTO ON/OFF | action-bar buttons |
| ТАКТИКА / ИНЖЕНЕРИЯ / СИСТЕМА | TACTICS / ENGINEERING / SYSTEM | feed speakers |
| Разжечь (1 спичка) / Вытолкать / Переместить / Разминировать / Отмена | Ignite (1 match) / Push / Relocate / Defuse / Cancel | action-menu buttons |
| Передача / Передал / Выбросил / Подобрал / Положил в ящик / Взял из ящика | Hand-over / Handed / Dropped on the ground / Picked up / Stowed in the crate / Took from the crate | inventory transfer feed |
| Патроны M16 / 9мм | M16 rounds / 9mm rounds | transfer items, loot |
| УКЛОНЕНИЕ / КРИТИЧЕСКИЙ УДАР / КРИТ x2 / ПРОМАХ | DODGE / CRITICAL HIT / CRIT x2 / MISS | floating combat texts |
| НАЛЁТ / КРАХ / СБИТ В ВОЗДУХЕ / СТРАХ ОГНЯ | POUNCE / CRASH / SHOT DOWN MID-AIR / FEAR OF FIRE | enemy floating texts (Cutter, fire fear) |
| В ЯРОСТИ! НЕ ПОДЧИНЯЕТСЯ | ENRAGED! IGNORING ORDERS | rage |
| В УКРЫТИИ / ГОЛОВА В УКРЫТИИ | IN COVER / HEAD DOWN | cover |
| ОБОРОНА: ФИКСАЦИЯ / В СТРОЙ | GUARD: HELD / REGROUP | guard order floating text |
| Стойкость (Срез) / Опыт / Уровень / Свободные очки | Fortitude (Damage cut) / Experience / Level / Free points | profile dialog |
| ВОЛНА N ОТРАЖЕНА / ПОЛНАЯ ПОБЕДА / ВСЕГО УНИЧТОЖЕНО | WAVE N REPELLED / TOTAL VICTORY / TOTAL KILLED | victory panel |
| ХОД ОТРЯДА / ХОД ПРОТИВНИКА | SQUAD TURN / ENEMY TURN | turn-based panel |
| Сохранение и перезапись / Загрузка игры / Перезаписать | SAVE AND OVERWRITE / LOAD GAME / Overwrite | save / load dialog; default slot `Leonid_01` |
| Северные ворота / Левый фланг / Правый фланг / Дальний периметр | North gate / West flank / East flank / Far perimeter | spawn lane display names (`SpawnLaneRules`) |
| Периметр КПП (Поиск дизеля / канистры) | Checkpoint perimeter (Find diesel / canister) | save stage names |
| Иван Сусанин; Сигнал бедствия / Присоединение к отряду | Ivan Susanin; Distress signal / Joining the squad | recruit (C++ fallback dialogue in `RecruitSubsystem`) |
| Останки | Remains | fallen operative's loot crate |

Kept in Russian on purpose (they match legacy DATA; marked `cyrillic-ok` in C++, skipped by the guard test): Russian lane ids
(`SpawnLaneRules`), attacker sources «Турель / Мина / Баррикада» and operative names in `KillStatsRules` /
`RunTelemetrySubsystem` / `EnemyCharacter`, weapon substrings «пистолет / турел» in `CameraShakeRules`, slot substring «быстр» in
`SaveGameRules`. Drop them once no data asset / LevelJson / save file carries the Russian names.
