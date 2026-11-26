# Codex 146 (Lead reviews) - critically wounded operative recovers on his own if he lies still (designer 2026-11-26)

Designer: an operative left critically wounded (CriticalWoundRules / downed state) who does NOT move for ~40 s regains health automatically
up to 30 %, after which he can give HIMSELF first aid (self-treat, the normal first-aid action on himself).
Rules (defaults; tunables in DA_GameBalanceConfig): still timer 40 s -> HP rises to 30 % of max (smoothly over the timer, or at its end - pick
the one that reads better, report it); moving / crawling, taking damage, or being dragged resets the timer; being treated by a squad mate
still works as now (whichever comes first). Bleed-out / death rules of the critical state stay as they are during the timer - report the
current bleed-out timing; if bleed-out kills him before 40 s, report it instead of changing it (designer decides).
After recovery: self first aid available (order / autonomy), uses his own medkit if the first-aid rules need one (Grok/Gemini G022 medkit
is still queued - do not depend on it). Same for PMC operatives if they share the component (weapons / tactics for all sides) - report.
HUD: a small "recovering 23 s" next to the name while the timer runs.
- Pure rules (timer, reset causes, HP curve, self-aid gate) -> unit tests. Smoke on X6: down an operative, keep him still -> 30 % at ~40 s ->
  self-aid works; second run: he crawls at 20 s -> timer restarts.
- Branch codex/146-critical-self-recovery from origin/main; report docs/port/codex/outbox/146-critical-self-recovery.report.md;
  FINISH `146 FINISH | ...`. Queue: after 143, before 136.
