# Diagnostic & Delivery Report: Section B (Design Specifications Round 2)

**Branch:** `gemini/design-round2`  
**Date:** 2026-11-07  
**Scope:** Author comprehensive tactical and technical design drafts for upcoming combat, AI director, trap planning, and automated QA systems (no code modified; design specifications only).

---

## 1. Deliverables Summary

| Document | Feature Scope | Key Innovations / Proposals |
|---|---|---|
| [`docs/design/KILL_ZONE_AMBUSH.md`](../design/KILL_ZONE_AMBUSH.md) | **Kill Zone Ambush (#49)** | Rooftop/interior enfilade crossfire in 1–3 m chokepoints; target attention filter (#54); visual/audio player tells (scope glint, snow tracks, Voronov radio cue); smoke/flank counter-play. |
| [`docs/design/AI_DIRECTOR.md`](../design/AI_DIRECTOR.md) | **AI Director & HQ Comms (#47 + #48)** | Mathematical tension model (HP, ammo, cold, noise, sectors, dwell); build-up -> peak -> forced relax (45–90 s) pacing; Voronov (ГБР) context-aware radio line dispatch; blizzard radio static blackout. |
| [`docs/design/TRAP_PLANNER.md`](../design/TRAP_PLANNER.md) | **Procedural Trap Planner (#46a)** | Seeded placement of mines, tripwires, trapped crates, and sentry turrets; geometric NavMesh chokepoint scoring; 35 m start safe radius; no red cheat outlines on crates; guaranteed disarm/bypass routes. |
| [`docs/design/AUTO_QA.md`](../design/AUTO_QA.md) | **Autonomous QA Pipeline (#41)** | 10 runtime watchdog sensors (stuck, off-navmesh, fire arc, weapon reach, T-pose, fall-through); instant incident capture (screenshot, 5s replay clip, seed, log); deduplication & morning report digest. |

---

## 2. Primary Verification & Cross-System Alignment

- **Gorky 17 Narrative Alignment:** Verified consistency with the tone, characters (Sally, Roger, Babai, Ronin), and lore in `NARRATIVE_BIBLE.md` (origin/gemini/t4-narrative-bible).
- **Subsystem Compatibility:** Validated that proposed data structures and parameters interface smoothly with `PmcCommanderRules`, `SquadOrderCatalog`, `LevelGenerator`, `EnvironmentSubsystem`, and `TrapSpotRules`.
- **Smokes Mapped:** Each specification includes dedicated existing and proposed smoke tests (`PmcStandoffSmoke`, `TrapSoloSmoke`, `WeatherLakeSmoke`, `EnemyStuckProbe`).

---

## 3. Open Questions for Director / Lead Designer Review

1. *Interior Window Destruction:* Should the kill zone ambush support shooting through interior glass windows on Depot, or stick to open roof parapets in the first iteration?
2. *HQ Audio Filter:* Should Voronov's radio lines be recorded by voice actors, or use an in-engine procedural military radio DSP filter over text-to-speech for rapid testing?
3. *Trapped Crate Disarm Risk:* If an operative fails a trap disarm attempt on a crate, should it explode instantly or trigger a 2-second fuse allowing an evasive roll?
