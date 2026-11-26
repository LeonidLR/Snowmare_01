# Codex 149 (L3; Lead reviews) - the weekend trainer learns from the designer's fights (designer 2026-11-26: "by the weekend the bots must train productively")

Inputs (after 121 + 148): Saved/Demos/*.jsonl (full 121 events from 11-27 on; 44 old-format fights - what demo_tactics.py recovers),
<name>.notes.json drawings / comments (148) decoded by demo_notes.py, the designer doctrine docs/design/ai/DESIGNER_TACTICS_DOCTRINE_2026-11-25.md.
Existing trainer: Scripts/Trainer/* (PMC commander weights JSON, QD candidates, apply_champion.ps1), worker night loop
(Scripts/Tools/worker_night_loop.ps1), A/B on X6 sectors.
Do:
1. demo_dataset.py: all demos -> one dataset: per fight the tagged tactics (121 tagger), outcomes, durations, and the designer's notes
   (drawn routes = "should have" examples). Summary table: which tactics he uses, how often, what wins.
2. Two uses for the weekend, pick what is measurable now and report the choice:
   a) PMC side: score trainer candidates also on "plays like the designer's doctrine" (e.g. reaction to flanks / high ground he uses)
      - harder opponents that punish what he punishes;
   b) OUR squad autonomy (when autonomy is on): weights / authored plays seeded from his tactics (fallback to cover after contact, high
      ground, distract + flank, overwatch while a mate moves).
3. Weekend night runs on the worker (both machines allowed when the designer is away): fixed seeds, paired A/B vs the current champion,
   >= 200 rounds per arm per night, a morning report with win / capture / kills and 3 example rounds; apply a champion only with the
   Lead's OK (apply_champion.ps1).
- Pure dataset / scoring rules -> unit tests. Branch codex/149-demos-to-trainer; report docs/port/codex/outbox/149-demos-to-trainer.report.md;
  FINISH `149 FINISH | ...`. Queue: after 148, before 147 (designer: recording + drawing + weekend training first).
