# Gemini 130 - search fallen bodies (PMC and own squad) with a hover highlight; small, rare loot (designer 2026-11-25) - right after 126 rebase

Designer: "I remember searching bodies used to work; now I could not." Wants: hover the cursor over a fallen enemy (PMC) OR a fallen squad
member -> a LIGHT FRESNEL highlight on the body -> click -> the operative walks to it and searches it (short interact) -> loot.
Loot (data, small and not too often - he must not become invincible): some ammo for weapons the body carried (e.g. 20-60 % of a
magazine, only for weapons the searcher can use), a medkit rarely (data chance, default 10 %), grenade rarely (default 5 %); a body is
searched once; own squad member's body gives what he actually carried (his remaining ammo / items), not random.
Existing code to find and REUSE: Source/CodexTactics/Private/Interactables/CorpseLootRules.cpp, Characters/CorpseRules.cpp /
CorpseSubsystem.cpp, the loot / transfer mechanic (Lake), the interactable hover highlight path (doors / windows / crates hover). First
find WHY it stopped working (disabled in sector mode? corpse not registered as interactable after the ragdoll / freeze? hover trace
channel? PMC bodies never registered?) - name the cause in the report.
Data: corpse loot fields in an existing loot / corpse json (no new asset). Fresnel: reuse the existing hover highlight material /
custom depth stencil used for interactables (no new material unless none exists - then STOP and report).
Tests: unit tests for the loot roll (seeded, chances, caps, once-only) + a smoke on X6: kill a PMC -> hover -> search -> ammo added,
second search gives nothing; same for a dead squad member. Router first; Neon review of the Hydrogen diff. BUILD ONLY ON THE WORKER.
Report docs/port/gemini/outbox/130-corpse-search.report.md + "130 FINISH | ...". Never push.
