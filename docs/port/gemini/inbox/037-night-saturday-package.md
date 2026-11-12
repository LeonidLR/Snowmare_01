# 037 - Night queue Thu 11-12: Saturday package part 1 - AFTER 034 + 035; in order; report to claude
Суть: подготовить субботнее вливание так, чтобы Claude проверял итоги, а не код.
Реестр: -
1. **Opus reviews of C++ written before the review rule:** gemini/020-trial-scan-toggle, gemini/028-buildings-in-game, and Codex's
   stealth on interim/2026-11-12 (commits 90a6bf64, 558e4a8c, 57fa466e, 66917e4e and later). Use your gate's -OpusReview
   (claude-opus-5-5, --effort medium, --max-tokens 8000, label gemini-review-<branch>); results -> MAIN Saved/Orchestrator/reviews/.
   Fix HIGHs in YOUR branches (020, 028) with a gate run; for Codex's code only list them (Claude decides).
2. **Merge plan v2** (dry runs on a throw-away copy, never commit to interim / main): every finished gemini/* branch not yet in main
   + interim/2026-11-12, against CURRENT main (027 and 031 are already in main) -> docs/port/gemini/outbox/037-merge-plan-v2.md:
   order, conflicts, tests per step, which branches to SKIP and why.
3. **Audio:** run audio_loudness.py --normalize into MAIN Saved/Orchestrator/audio_v1_norm/ (originals untouched); check the TG
   approval panel loads catalog.json with v3-v5; turn the 4 BLOCKING gaps of EVENTS.md into concrete visual-cue proposals (RU, short).
Chat: one line per item with --topic gemini-037 and the hash; questions in Russian with a default.
