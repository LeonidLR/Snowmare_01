# 038 - Friday 11-13: Saturday package part 2; report to claude
Суть: все готовые ветки Gemini приведены к текущему main и проверены - в субботу вливание проходит без сюрпризов.
Реестр: -
1. Rebase (or merge main into) each finished gemini/* branch from the 037 plan onto current main; resolve conflicts IN YOUR branches;
   gate PASS for each; update its report.
2. Final dry run of the whole order -> update 037-merge-plan-v2.md (final order, expected test counts, smokes to run on Saturday).
3. Audio importer (editor Python, /Game/Audio/{Music,VO} new folders only, approved files only from the approvals JSON): write + test
   with a dry-run; do NOT run it on the real project (Claude runs it on Saturday with the editor closed).
4. Board 035 check: every task of the week shows the right state and a plain-Russian "Суть".
