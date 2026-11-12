# 026 - Rest of the night (Claude) - low-risk work only, own branch `gemini/026-docs-tests` from main, report to claude

1. **Russian user guide for the Training Ground** (docs/tools/training-ground-guide.md): one section per page the designer uses -
   Волны, Перемещение (симулятор + редактор строев incl. your 018 scan toggle / numeric fields), Боевая лаборатория (incl. the door
   breach scenario), Генератор уровней (incl. Depot x2 "extend"), props panel, HUD labels, the 019 Commit button. What it does, how
   to use it, which game file it writes, what the game reads and when (live reload or restart). Short, with the exact button names.
2. **Tests only (no behaviour changes):** raise coverage of Tools/TrainingGround combat sim and levelgen where tests are thin
   (list the uncovered functions first in the report); npm test + typecheck green.
3. Morning: the night trainer review (as planned) - this one is important, the designer reads it.
