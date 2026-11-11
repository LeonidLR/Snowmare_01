# 013 - Afternoon queue (Claude, 2026-11-11) - work through ALL items in order, no chat wait between them

Rules as always: own branch per item from `main` (`gemini/013-<n>-<slug>`), never main / push / C++ / Source / Config / Content / maps /
.uasset. Small explicit-path commits. After each item: ONE chat line with the commit hash, then go straight to the next item.
No evidence = say so; never invent. Read-only items produce a report in docs/port/gemini/outbox/.

1. **Task board redraw (draft).** From your `board-reconcile.report.md`, write a full new draft `docs/port/gemini/outbox/TASK_BOARD.draft.md`
   in the same format as docs/port/TASK_BOARD.md: done rows moved to "Сделано" with hashes, partial rows with "что осталось", the 17
   untracked works added, the obsolete row moved to "Отменено". Keep every open row's wording; Claude replaces the board from it.
2. **#42 drop-down animation - integration plan (read-only).** Read the drop-link / walk-off code (grep DropLink, FallStates, Falling,
   Landing in Source/ and the ABP notes in docs/), the jump/fall clips used now (MM_Rifle_Jump_Fall_*), gravity x2.25. Report: exact
   files:lines to change, the state flow (start clip -> fall loop -> landing), how to time a start clip against the fall duration, what
   to check on the imported clip /Game/Animations/Humanoids/Jumping_1__UE (skeleton, root motion, length), the smoke to extend.
3. **Stealth smoke gaps - test spec (read-only).** For the 5 blind spots of your stealth review (wall shimmy, fast jog, noise perception,
   female set, death / knockdown) + "weapon parts attached after entering stealth": a precise test spec per case (setup, action,
   assertion, which existing smoke helper to reuse, file:line). Use the interim branch code (`interim/2026-11-12`).
4. **Training Ground T10 - visibility with weather / night + weapon ranges (TypeScript).** Board item T10, register pl-weapon-sim.
   In Tools/TrainingGround (TS only): the sim's sight cone uses the game's data (SightRules / weather / time-of-day JSON under
   Content/Data, read-only) so the visible distance shrinks in blizzard / night, and weapon ranges from Content/Data/Weapons. Unit tests
   (npm test) + typecheck green; a short report with before/after numbers. If the game data needed is not in JSON, stop and report.
5. **Night trainer review (tomorrow morning only)** - 012 item 4, after the pmc-v4 run exists.
