# 005 - Dead enemies stay STANDING + bring the ragdoll (#16) forward (designer 2026-11-11)

On interim/2026-11-12, branch `codex/death-ragdoll`. Rules: README.md (work rules 2026-11-11). Order: after 004 (hitch), before C5.

## The bug
"Very often killed enemies freeze in poses like this - they stay standing on the spot instead of lying." Screenshot (2 Military
Zombies standing dead, one lying): MAIN Saved/Orchestrator/dead-standing/2026-11-11-zombies-standing.webp, log copy
Saved/Orchestrator/dead-standing/play.log.

## Clue from the log (Claude, not yet confirmed)
Line 7526-7529: an RPG-7 burst kills BP_Enemy_MilitaryZombie_C_3 and _C_2 (`[Health] ... died`), and in the SAME frame:
`[Knockdown] Military Zombie knocked down by the blast, falls on the back (fall 0.87 s, downed 0.60 s, get-up 0.80 s at 2.08x)`.
A knockdown with a GET-UP is applied after death -> the get-up plays and the corpse ends standing. Check: knockdown / stagger /
hit-react / suppression / spawn-entrance / patrol paths must ignore dead characters; the death state must win over every later
montage / layer / AI move. Also check deaths by rifle (the Engineer kill at 14:09:49) - is there a second path (death clip missing
for the zombie, anim BP back to idle after the death montage ends, BlendOut)?

## The fix the designer asks for: ragdoll on death (#16, brought forward)
- On death: stop AI + movement, then simulate physics on the body (the mesh's Physics Asset), with an impulse from the killing hit /
  blast (direction + strength from data), settle, then freeze (sleep) and keep the corpse (no tickers after settle). A death clip can
  stay as a short blend-in if data says so (`death.ragdoll_delay_s`, default 0).
- Every enemy archetype on Depot (Hound, Frostbitten, MilitaryZombie, Cutter, Marksman) + PMC operators + our operatives (the
  designer decides later for ours - default: ragdoll too, data toggle). Missing Physics Assets: list them; create only in the new
  folder /Game/Characters/Physics/ while the editor is CLOSED, and ask the designer in chat first. Never modify packs.
- Data: Content/Data/Combat/death_ragdoll.json (enabled, impulse per damage type, settle time, max simultaneous ragdolls + fallback
  to the death clip above the cap). Gemini's map: docs/port/gemini/outbox (016 item 3c, ragdoll) when it lands.
- Smoke `DeathRagdollSmoke`: kill each archetype by rifle and by an RPG blast -> after settle the pelvis is near the ground (lying),
  never standing; knockdown on a dead body is rejected; no frame > 250 ms; corpse count / tickers bounded.
Report: docs/port/codex/outbox/005-dead-enemies-standing-ragdoll.report.md, frames (game camera) into MAIN Saved/Orchestrator/dead-standing/.
