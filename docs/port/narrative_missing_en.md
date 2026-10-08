# Narrative: texts that still lack English (for Gemini / the writer)

State 2026-10-08. Rule (user decision): nothing Russian may reach the screen. A missing line shows `[EN missing: <sequence>#<n>]`
and logs `Narrative: ...` (LogCodexTactics, Warning). Do NOT translate these ad hoc: write them in the Google Sheet, then
`python Scripts/Narrative/sync_narrative.py` (see HANDOFF §6 "Narrative pipeline").

## 1. Dialogue sequences (`Content/Data/Narrative/narrative_manifest.json`)

All five sequences have `text_en` + `speaker_en` for every row: nothing missing.
Note for the writer: `DA_DialogueIntro` has **11** rows in the sheet (the Godot briefing had 15 lines); the game plays what the sheet holds.
Sequence ids must equal the asset names: `DA_DialogueIntro`, `DA_DialoguePrep`, `DA_DialogueWaveRest`, `DA_DialogueVictory`,
`DA_DialogueSusaninRecruitment`.

## 2. Level-embedded narrative elements (need sheet rows, key = actor name)

`ANarrativeElementActor` texts live in `L_MovementTest.umap` (Russian, not editable by the agents). At runtime a non-English
title / content / source is replaced from the manifest sequence named like the actor:

| Sequence id (`SequenceID` column) | Row 1 | Row 2 (optional) |
|---|---|---|
| `NarrativeElementActor_0`, `_1`, `_2` | `Speaker_EN` = title, `Text_EN` = body | `Speaker_EN` = author / source |

Until the rows exist the actors show title `Document` and body `[EN missing: NarrativeElementActor_N#1]`.
Russian originals found in the map (which actor holds which: check in the editor, Details > Narrative):

- Note: title "Записка дежурного инженера", author "Инженер 2-й смены Сергеев", body "Гермоворота обесточены из-за аварии. Резервный дизель-генератор пуст. Слейте дизель из бака брошенного БМП в канистру и заправьте станцию!"
- Signpost "Уличный указатель путей КПП": "СЕКТОР А: ГЕРМОВОРОТА И БУНКЕР", "СЕКТОР B: ДИЗЕЛЬ-ГЕНЕРАТОР", "СЕКТОР C: СКЛАД СНАБЖЕНИЯ"
- Poster "Приказ ГО и ЧС: Карантинный рубеж" (signed "Штаб Северного Округа"): "НЕМЕДЛЕННО ЗАНЯТЬ ОБОРОНУ У ГЕРМОВОРОТ.", "ПРИ ПРОРЫВЕ МУТАНТОВ ...", "ВНИМАНИЕ: ЗОНА АНОМАЛЬНОГО ХОЛОДА!", "ВЫХОД ЗА ПЕРИМЕТР БЕЗ ТЕРМОЗАЩИТЫ СТРОГО ВОСПРЕЩЁН!"

## 3. Other Russian strings stored in `L_MovementTest.umap` (the agents may not touch the map)

Interactable / marker display names set on level actors (shown in action menus, overhead labels, the message feed):
"Армейский ящик снабжения (КПП)", "Заминированный ящик аванпоста", "Брошенный БМП-2", "Пустая канистра", "Резервный генератор",
"Пульт управления воротами", "Северные ворота", "Комендатура КПП", "Сектор наблюдения 01", spawn lane / flank labels
"Дальний периметр", "Левый фланг (Прорыв)", "Правый фланг".
Suggested glossary-based English: Army Supply Crate (Checkpoint), Booby-Trapped Outpost Crate, Abandoned APC, Empty Fuel Canister
(`item_canister`: 20L Fuel Canister), Backup Diesel Generator (`obj_generator`), Gate Control Terminal, North Gate, Checkpoint HQ,
Observation Sector 01, Far Perimeter, Left Flank (Breach), Right Flank. Applying them needs the user to edit the map (or to
approve a headless script run while the editor is closed).

## 4. Hard-coded story text outside the agents' narrative scope

`Characters/RecruitSubsystem.cpp` builds the Susanin recruitment dialogue in C++ (titles "Сигнал бедствия: Иван Сусанин",
"Присоединение к отряду", button labels). The manifest already holds `DA_DialogueSusaninRecruitment` (3 lines); switching the
recruitment flow to that sequence is a follow-up (owner: the UI-English pass / writer decision).
