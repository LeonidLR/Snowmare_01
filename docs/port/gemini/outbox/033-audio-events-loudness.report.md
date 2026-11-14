# Отчёт 033 — Audio Events Registry, Loudness Pass & Training Ground Audio Panel

- **Ветка:** `gemini/033-audio-events` (от `origin/main` `1f8d104c`)
- **Спецификация:** `docs/port/gemini/inbox/033-audio-events-loudness.md` (`#282`)
- **Статус:** Готово (ожидает commit hash после индексации и ревью Claude `audio-director` / дизайнера).

---

## 1. Реестр событий (`docs/design/audio/EVENTS.md`)

Создан в формате 11-колоночной таблицы из `docs/design/audio/AUDIO_PROCESS.md` с верифицированными путями и номерами строк в `Source/CodexTactics/` и `Content/Data/`:

- **Adaptive Music (8 состояний × 2 варианта = 16 треков):** `mus_menu`, `mus_defeat`, `mus_victory`, `mus_combat_heavy`, `mus_combat`, `mus_tension`, `mus_explore_cold`, `mus_explore_calm`.
- **Voice / Barks (все 49 реплик из `voice_lines.json`):** Commander (`Charon`, 11), Engineer (`Puck`, 16), Medic (`Orus`, 13), PMC 1–3 (`Fenrir` / `Alnilam` / `Iapetus`, 9); указаны места вызова (`UGameMessageSubsystem::PostMessage`, `OperativeCharacterCover.cpp`, `OperativeCharacterWeapons.cpp`, `QuestChain.cpp` и др.), 2D/3D attenuation (`att_voice`), concurrency-лимиты, cooldown и ducking.
- **SFX / Ambient / UI (существующие + `[Planned]`):**
  - Оружие, перезарядка, осечка замороженного оружия, гранаты, взрывы (`M16`, `Shotgun`, `Pistol`, `Sniper`, `RPG-7`, турели APC, `mortar`).
  - Ловушки (`ProximityMineActor`, `TripwireActor`).
  - Шаги по 4 поверхностям (`snow`, `concrete`, `metal`, `wood`), укрытия, `Vault`, `LadderClimbComponent`.
  - Объекты мира (ящики, `GateActor` маяк + створки, генератор запуск + петля, слив дизеля, баррикады).
  - Погодные слои и очаги (`amb_wind_base`, `amb_snowfall_wind`, `amb_blizzard_layers`, `amb_fire_puddle_loop`).
  - Враги (`HordeSubsystem::WarningSound`, `FrostHound`, `Frostbitten` / `MILITARY_ZOMBIE`, `Cutter`, `Marksman`, `Brute`, рой орды).
  - UI (`sfx_ui_click`, `sfx_ui_order_confirm`, `sfx_ui_cancel_refuse`, `sfx_ui_tactical_pause`, `sfx_ui_dialogue_next`, `sfx_ui_low_hp_heartbeat`).
- **План на следующую волну SFX:** 5 брифов (`sfx_wpn_m16_shot`, `sfx_trap_mine_whistle`, `sfx_footstep_snow`, `sfx_enemy_hound`, `sfx_enemy_horde_warning`) с частотным характером, длительностью, вариациями и рандомизацией pitch/vol.

### Отдельный BLOCKING-список доступности (`SOUND_BIBLE.md §7`)

Все 49 реплик (`GameMessageSubsystem`), подозрение/тревога врагов (`"?"` / `"❗ ALARM!"`), замеченные мины/растяжки (`"⚠️ MINE SPOTTED!"` / `"⚠️ TRIPWIRE SPOTTED!"`), баннер/стрелки орды (`UHordeAlertWidget` / `UHordeArrowsWidget` в `WaveCombat`), обморожение (`UFrostVignetteWidget`) и тяжелое ранение при переходе (`"🩸 CRITICALLY WOUNDED!"`) уже имеют визуальный дубль.

**Критические звуки БЕЗ достаточного визуального сигнала (BLOCKED до реализации визуала):**

1. **`A11Y-BLK-01` — `sfx_trap_mine_whistle`:** В `AProximityMineActor::Tick` (`ProximityMineActor.cpp:208-211`) мина детонирует мгновенно на `<= TriggerRadius`, меш виден только после `Reveal()`. Нужен пульсирующий красный круг радиуса срабатывания/поражения на земле во время предвзрывного свиста.
2. **`A11Y-BLK-02` — `sfx_wpn_incoming_fire` / внеэкранный огонь снайпера и ЧВК:** `UCombatFeedbackSubsystem::SpawnDamageFlash` даёт ненаправленную вспышку (`CombatFeedbackSubsystem.h:68`), а `UHordeArrowsWidget` активен только в `WaveCombat` (`HordeArrowsWidget.h:15-16`). Нужен направленный индикатор/дуга на краю экрана при обстреле или подавлении из-за кадра во всех фазах.
3. **`A11Y-BLK-03` — `sfx_wpn_mortar_whistle`:** В `Content/Data/Weapons/weapons_tuning.json:84-105` задана миномётная баллистика без визуального маркера зоны падения. Нужен пульсирующий красный декал/кольцо зоны взрыва + стрелка на краю экрана на время полёта мины.
4. **`A11Y-BLK-04` — `sfx_ui_low_hp_heartbeat`:** Надпись `"🩸 CRITICALLY WOUNDED!"` (`OperativeCharacterStance.cpp:387`) всплывает разово при падении. Для постоянного критического предупреждения нужна пульсирующая красная рамка/иконка на портрете бойца в HUD и над головой.

---

## 2. Инструмент громкости и нормализации (`Scripts/Tools/audio_loudness.py`)

- **Реализация:** Стандартная библиотека Python + `ffmpeg` (`ebur128=peak=true` + `silencedetect` + two-pass `loudnorm`), без установки сторонних пакетов (`pyloudnorm` не требуется). Добавлен потоковый парсер RIFF-чанка для 24-битных `WAVE_FORMAT_EXTENSIBLE` WAV (формат-тег `65534`), который отклоняет стандартный `wave.open()`.
- **Нормализация (`--normalize --out <dir>`):** Никогда не перезаписывает исходники; пишет нормализованные копии:
  - `voice`: `-18.0 LUFS`, `TP <= -1.0 dBTP`, обрезка ведущей/хвостовой тишины (`-45 dB`, запас `0.015s`), `48 kHz / 16-bit mono WAV`.
  - `music`: `-23.0 LUFS`, `TP <= -1.0 dBTP`, без обрезки границ лупа, `48 kHz / 24-bit stereo WAV`.
- **Юнит-тесты:** `Scripts/Tools/tests/test_audio_loudness.py` (синтетические WAV-фикстуры — проверка таблицы замеров, допусков библии, раздельного вывода `--normalize` и обрезки тишины у VO).

### Замер текущей пачки `Saved/Orchestrator/audio_v1` (`65` WAV: `49` VO + `16` Music)

**До нормализации (исходники в `MAIN/Saved/Orchestrator/audio_v1`):**
- **Voice (49 файлов, цель `-18.0 LUFS` / `<= -1.0 dBTP`):** LUFS min `-24.6` / mean `-18.0` / max `-12.5`, max true peak `2.0 dBTP`, в допусках библии `0/49` (у 47/49 выраженная тишина на концах, у 5/49 клиппинг пика `> -1.0 dBTP`, доходивший до `+2.0 dBTP` у `pmc3_in_cover_sector`).
- **Music (16 файлов, цель `-23.0 LUFS` / `<= -1.0 dBTP`):** LUFS min `-13.9` / mean `-12.6` / max `-11.1`, max true peak `1.5 dBTP`, в допусках библии `0/16` (вся музыка пережата в среднем на `+10.4 LU` громче цели `-23 LUFS` и имеет пики выше `-1 dBTP` до `+1.5 dBTP` у `tension_v1`).

**После тестового прогона `--normalize --out Saved/AudioGen/normalized_v1`:**
- **Music (`16/16` OK):** `48000 Hz / 24-bit stereo`, LUFS `-23.2 .. -22.7` (mean `-22.9`), max true peak `-8.4 dBTP` — **16/16 в допусках библии**.
- **Voice (`45/49` OK):** `48000 Hz / 16-bit mono`, LUFS `-18.4 .. -16.8` (mean `-18.0`), **4 файла с остаточным флагом:**
  - `eng_generator_dry`: true peak `-0.9 dBTP` (`peak_over_-1dBTP`, +0.1 dB на 16-bit квантовании после `loudnorm TP=-1.0`).
  - `eng_found_canister` (trail `0.15s`), `pmc1_behind_cover` (lead `0.16s`), `pmc3_in_cover_sector` (lead `0.22s`): `trim_edge_silence` (после усиления громкости мягкое дыхание/хвост попало в окно `-45 dB`).
  *(Исходники в `Saved/Orchestrator/audio_v1` не трогались.)*

<details>
<summary>Полная таблица замеров до нормализации (65 файлов из Saved/Orchestrator/audio_v1)</summary>

| id | kind | dur (s) | sample/bit | LUFS (tgt) | delta | peak dBTP | lead sil (s) | trail sil (s) | status |
|---|---|---:|---|---:|---:|---:|---:|---:|---|
| `combat_heavy_v1` | music | 30.77 | 44100Hz/16b | -13.6 (-23) | +9.4 | 0.1 | 0.04 | 0.00 | lufs_off_target,peak_over_-1dBTP |
| `combat_heavy_v2` | music | 30.77 | 44100Hz/16b | -12.7 (-23) | +10.3 | 0.5 | 0.05 | 0.00 | lufs_off_target,peak_over_-1dBTP |
| `combat_v1` | music | 30.77 | 44100Hz/16b | -12.7 (-23) | +10.3 | 0.4 | 0.04 | 0.00 | lufs_off_target,peak_over_-1dBTP |
| `combat_v2` | music | 30.56 | 44100Hz/16b | -13.9 (-23) | +9.1 | -0.3 | 0.09 | 0.00 | lufs_off_target,peak_over_-1dBTP |
| `defeat_sting_v1` | music | 27.01 | 44100Hz/16b | -11.5 (-23) | +11.5 | -0.1 | 0.05 | 0.04 | lufs_off_target,peak_over_-1dBTP |
| `defeat_sting_v2` | music | 28.53 | 44100Hz/16b | -11.6 (-23) | +11.4 | -0.1 | 0.05 | 0.04 | lufs_off_target,peak_over_-1dBTP |
| `explore_calm_v1` | music | 30.77 | 44100Hz/16b | -11.1 (-23) | +11.9 | -0.1 | 0.09 | 0.00 | lufs_off_target,peak_over_-1dBTP |
| `explore_calm_v2` | music | 30.77 | 44100Hz/16b | -12.7 (-23) | +10.3 | -0.2 | 0.05 | 0.00 | lufs_off_target,peak_over_-1dBTP |
| `explore_cold_v1` | music | 30.77 | 44100Hz/16b | -11.6 (-23) | +11.4 | 1.1 | 0.04 | 0.00 | lufs_off_target,peak_over_-1dBTP |
| `explore_cold_v2` | music | 30.77 | 44100Hz/16b | -13.9 (-23) | +9.1 | 0.4 | 0.05 | 0.00 | lufs_off_target,peak_over_-1dBTP |
| `menu_v1` | music | 30.77 | 44100Hz/16b | -13.4 (-23) | +9.6 | 0.6 | 0.05 | 0.00 | lufs_off_target,peak_over_-1dBTP |
| `menu_v2` | music | 30.77 | 44100Hz/16b | -13.9 (-23) | +9.1 | 0.5 | 0.05 | 0.00 | lufs_off_target,peak_over_-1dBTP |
| `tension_v1` | music | 29.47 | 44100Hz/16b | -12.8 (-23) | +10.2 | 1.5 | 0.05 | 0.04 | lufs_off_target,peak_over_-1dBTP |
| `tension_v2` | music | 30.77 | 44100Hz/16b | -12.6 (-23) | +10.4 | 0.3 | 0.05 | 0.00 | lufs_off_target,peak_over_-1dBTP |
| `victory_sting_v1` | music | 26.02 | 44100Hz/16b | -12.4 (-23) | +10.6 | -0.0 | 0.05 | 0.37 | lufs_off_target,peak_over_-1dBTP |
| `victory_sting_v2` | music | 24.84 | 44100Hz/16b | -11.5 (-23) | +11.5 | -0.0 | 0.05 | 0.00 | lufs_off_target,peak_over_-1dBTP |
| `cmd_ambush` | voice | 3.04 | 24000Hz/16b | -17.5 (-18) | +0.5 | -2.3 | 0.11 | 0.36 | trim_edge_silence |
| `cmd_autonomy_off` | voice | 3.44 | 24000Hz/16b | -19.2 (-18) | -1.2 | -2.2 | 0.28 | 0.32 | trim_edge_silence |
| `cmd_autonomy_on` | voice | 7.04 | 24000Hz/16b | -19.7 (-18) | -1.7 | -1.5 | 0.28 | 0.46 | lufs_off_target,trim_edge_silence |
| `cmd_contact` | voice | 1.20 | 24000Hz/16b | -19.6 (-18) | -1.6 | -4.0 | 0.37 | 0.35 | lufs_off_target,trim_edge_silence |
| `cmd_gate_exit_open` | voice | 4.16 | 24000Hz/16b | -15.6 (-18) | +2.4 | -1.1 | 0.24 | 0.29 | lufs_off_target,trim_edge_silence |
| `cmd_generator_starts` | voice | 5.80 | 24000Hz/16b | -13.8 (-18) | +4.2 | -1.3 | 0.18 | 0.17 | lufs_off_target,trim_edge_silence |
| `cmd_orders_received` | voice | 2.24 | 24000Hz/16b | -21.1 (-18) | -3.1 | -4.1 | 0.15 | 0.35 | lufs_off_target,trim_edge_silence |
| `cmd_panic_flee` | voice | 4.08 | 24000Hz/16b | -15.1 (-18) | +2.9 | -1.5 | 0.11 | 0.04 | lufs_off_target |
| `cmd_solo_leash` | voice | 3.60 | 24000Hz/16b | -15.3 (-18) | +2.7 | -1.4 | 0.23 | 0.29 | lufs_off_target,trim_edge_silence |
| `cmd_suppressive_fire` | voice | 2.76 | 24000Hz/16b | -14.0 (-18) | +4.0 | -1.0 | 0.21 | 0.24 | lufs_off_target,trim_edge_silence |
| `cmd_wave_advancing` | voice | 2.28 | 24000Hz/16b | -18.8 (-18) | -0.8 | -2.8 | 0.30 | 0.30 | trim_edge_silence |
| `eng_along_wall_corner` | voice | 2.28 | 24000Hz/16b | -17.6 (-18) | +0.4 | -3.7 | 0.23 | 0.27 | trim_edge_silence |
| `eng_behind_cover` | voice | 1.24 | 24000Hz/16b | -23.0 (-18) | -5.0 | -4.7 | 0.36 | 0.33 | lufs_off_target,trim_edge_silence |
| `eng_changing_mags` | voice | 3.12 | 24000Hz/16b | -18.9 (-18) | -0.9 | -1.5 | 0.24 | 0.21 | trim_edge_silence |
| `eng_corner_peek` | voice | 1.92 | 24000Hz/16b | -18.6 (-18) | -0.6 | -1.8 | 0.21 | 0.32 | trim_edge_silence |
| `eng_cover_group_stack` | voice | 3.04 | 24000Hz/16b | -17.7 (-18) | +0.3 | -1.5 | 0.22 | 0.26 | trim_edge_silence |
| `eng_dash_other_cover` | voice | 1.52 | 24000Hz/16b | -15.5 (-18) | +2.5 | -1.8 | 0.29 | 0.28 | lufs_off_target,trim_edge_silence |
| `eng_draining_diesel` | voice | 7.28 | 24000Hz/16b | -22.5 (-18) | -4.5 | -6.2 | 0.48 | 0.02 | lufs_off_target,trim_edge_silence |
| `eng_fortifications_dismantled` | voice | 4.48 | 24000Hz/16b | -22.5 (-18) | -4.5 | -5.2 | 0.29 | 0.28 | lufs_off_target,trim_edge_silence |
| `eng_found_canister` | voice | 5.56 | 24000Hz/16b | -23.5 (-18) | -5.5 | -6.6 | 0.29 | 0.15 | lufs_off_target,trim_edge_silence |
| `eng_generator_dry` | voice | 6.44 | 24000Hz/16b | -22.2 (-18) | -4.2 | -1.8 | 0.27 | 0.22 | lufs_off_target,trim_edge_silence |
| `eng_generator_restored` | voice | 3.52 | 24000Hz/16b | -20.0 (-18) | -2.0 | -3.2 | 0.24 | 0.26 | lufs_off_target,trim_edge_silence |
| `eng_guard_off` | voice | 1.88 | 24000Hz/16b | -19.6 (-18) | -1.6 | -2.6 | 0.19 | 0.21 | lufs_off_target,trim_edge_silence |
| `eng_guard_on` | voice | 2.92 | 24000Hz/16b | -16.7 (-18) | +1.3 | -1.2 | 0.28 | 0.25 | trim_edge_silence |
| `eng_hugging_wall` | voice | 2.68 | 24000Hz/16b | -20.0 (-18) | -2.0 | -3.0 | 0.36 | 0.30 | lufs_off_target,trim_edge_silence |
| `eng_rage_shout` | voice | 3.00 | 24000Hz/16b | -14.2 (-18) | +3.8 | -2.5 | 0.00 | 0.29 | lufs_off_target,trim_edge_silence |
| `eng_take_cover` | voice | 1.04 | 24000Hz/16b | -17.3 (-18) | +0.7 | -4.3 | 0.24 | 0.30 | trim_edge_silence |
| `med_apc_tank_needs_canister` | voice | 7.32 | 24000Hz/16b | -23.9 (-18) | -5.9 | -6.3 | 0.30 | 0.28 | lufs_off_target,trim_edge_silence |
| `med_barricade_cover` | voice | 5.04 | 24000Hz/16b | -22.6 (-18) | -4.6 | -4.5 | 0.00 | 0.42 | lufs_off_target,trim_edge_silence |
| `med_critically_down` | voice | 6.24 | 24000Hz/16b | -18.8 (-18) | -0.8 | -1.9 | 0.20 | 0.04 | trim_edge_silence |
| `med_grenade_away` | voice | 2.48 | 24000Hz/16b | -12.7 (-18) | +5.3 | -0.8 | 0.25 | 0.24 | lufs_off_target,peak_over_-1dBTP,trim_edge_silence |
| `med_grenade_out` | voice | 1.84 | 24000Hz/16b | -12.5 (-18) | +5.5 | 0.9 | 0.32 | 0.28 | lufs_off_target,peak_over_-1dBTP,trim_edge_silence |
| `med_in_cover` | voice | 2.20 | 24000Hz/16b | -19.3 (-18) | -1.3 | -1.9 | 0.41 | 0.19 | trim_edge_silence |
| `med_looted_crate` | voice | 3.64 | 24000Hz/16b | -22.8 (-18) | -4.8 | -6.3 | 0.11 | 0.39 | lufs_off_target,trim_edge_silence |
| `med_panic_grip` | voice | 3.40 | 24000Hz/16b | -15.4 (-18) | +2.6 | -1.6 | 0.00 | 0.30 | lufs_off_target,trim_edge_silence |
| `med_patched_up` | voice | 4.00 | 24000Hz/16b | -24.6 (-18) | -6.6 | -6.6 | 0.34 | 0.22 | lufs_off_target,trim_edge_silence |
| `med_reloading` | voice | 2.84 | 24000Hz/16b | -14.7 (-18) | +3.3 | -1.5 | 0.18 | 0.27 | lufs_off_target,trim_edge_silence |
| `med_thawed` | voice | 7.56 | 24000Hz/16b | -22.6 (-18) | -4.6 | -3.5 | 0.15 | 0.40 | lufs_off_target,trim_edge_silence |
| `med_tripwire` | voice | 4.88 | 24000Hz/16b | -16.2 (-18) | +1.8 | -1.5 | 0.14 | 0.32 | lufs_off_target,trim_edge_silence |
| `med_watch_back` | voice | 2.36 | 24000Hz/16b | -14.8 (-18) | +3.2 | -1.4 | 0.27 | 0.11 | lufs_off_target,trim_edge_silence |
| `pmc1_behind_cover` | voice | 1.40 | 24000Hz/16b | -14.4 (-18) | +3.6 | -1.8 | 0.16 | 0.31 | lufs_off_target,trim_edge_silence |
| `pmc1_hugging_wall` | voice | 2.72 | 24000Hz/16b | -20.0 (-18) | -2.0 | -2.0 | 0.36 | 0.20 | lufs_off_target,trim_edge_silence |
| `pmc1_reloading` | voice | 1.72 | 24000Hz/16b | -16.2 (-18) | +1.8 | -1.1 | 0.29 | 0.26 | lufs_off_target,trim_edge_silence |
| `pmc2_behind_cover` | voice | 1.28 | 24000Hz/16b | -16.6 (-18) | +1.4 | -2.6 | 0.25 | 0.36 | trim_edge_silence |
| `pmc2_changing_mags` | voice | 2.04 | 24000Hz/16b | -13.6 (-18) | +4.4 | 0.1 | 0.24 | 0.14 | lufs_off_target,peak_over_-1dBTP,trim_edge_silence |
| `pmc2_hugging_wall` | voice | 2.60 | 24000Hz/16b | -12.8 (-18) | +5.2 | -0.1 | 0.26 | 0.27 | lufs_off_target,peak_over_-1dBTP,trim_edge_silence |
| `pmc3_at_corner` | voice | 1.64 | 24000Hz/16b | -18.7 (-18) | -0.7 | -1.2 | 0.00 | 0.29 | trim_edge_silence |
| `pmc3_grenade_out` | voice | 1.76 | 24000Hz/16b | -13.0 (-18) | +5.0 | -1.3 | 0.18 | 0.19 | lufs_off_target,trim_edge_silence |
| `pmc3_in_cover_sector` | voice | 2.32 | 24000Hz/16b | -14.2 (-18) | +3.8 | 2.0 | 0.17 | 0.33 | lufs_off_target,peak_over_-1dBTP,trim_edge_silence |

</details>

---

## 3. Панель прослушки и приёмки аудио в Training Ground

- **`Tools/TrainingGround/server/audioApi.ts`:**
  - Читает `Saved/Orchestrator/audio_v1/catalog.json` (fallback `Saved/AudioGen`), сохраняет `audio_approvals.json` строго **рядом с `catalog.json`** (`writeAtomic`).
  - Включены защиты безопасности: голые ID (`isValidAudioId`, блок traversal/путей), фиксированные папки `<audioDir>/<kind>/<id>.wav`, проверка `isSameOriginOrLocal` на `POST /api/audio/review`.
- **`Tools/TrainingGround/src/utils/audioReview.ts` и `src/components/AudioReviewPanel.tsx`:**
  - Вкладка `Аудио` в `src/App.tsx`, подключённая к `setupAudioApi` в `vite.config.ts`.
  - Прослушка WAV через `<audio>`, кнопки **Принять / Отклонить / Ожидает** + поле заметки по каждому файлу, фильтры по типу (`voice`/`music`), статусу, **спикеру** (`speaker`) и **cue/состоянию** (`cue`) + текстовый поиск и сводка расхода/приёмки.

---

## 4. Проверки после финальных правок

- `python -m unittest Scripts/Tools/tests/test_audio_loudness.py -v`: **2 passed** (`OK`).
- `Tools/TrainingGround`: `npm test`: **331 passed, 0 failed, 0 skipped**.
- `Tools/TrainingGround`: `node --test server/audioApi.test.ts`: **3 passed, 0 failed, 0 skipped**.
- `Tools/TrainingGround`: `npm run typecheck` (`tsc && tsc -p tsconfig.node.json`): **exit 0**.
