# 025 - Saturday merge prep (Claude, 2026-11-11 night) - after the 024 item-07 correction; report to claude

1. **Saturday merge plan (read-only + dry runs in YOUR worktree only).** For every gemini/* branch not yet in interim/2026-11-12
   (016-1, 016-2, 016-3, 017-1, 018, 019, 020, 021-*, 022-*, 023-*, 024): try `git merge --no-commit --no-ff` onto a scratch copy
   of interim (throw it away after), record conflicts; run the branch's own tests (npm test + typecheck for TS, unittest for Python;
   for 020 the Formation tests). Output docs/port/gemini/outbox/025-saturday-merge-plan.md: a table branch | hash | files | tests
   result | conflicts | proposed merge order | risk notes. Never commit to interim, never push.
2. **Task board draft refresh:** update your TASK_BOARD.draft.md with today's new items (stealth 003 + 6 fixes, 004 hitch, 005
   ragdoll, 006 -> your 020, 018, 019, local model, Tailscale phone chat, Codex wake-up, second machine; mortars postponed; AI Director
   removed) - statuses by evidence only.
3. Morning: the night trainer review (as planned).
