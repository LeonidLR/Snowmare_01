# G017 - R3: a zone defender aims instead of suppressing at <= 10 m (enemy AI, PMC)

Owner: Grok. Branch from main (after R1 is merged; until then from `ai/r1-fallback-cover` / `codex/063-r1-fixes`).
Worktree under `C:/Work/Lanes`. Heavy runs only on the worker. Report `docs/port/grok/outbox/G017-*.report.md` + chat FINISH.

## Why (data, r1-cover arena baseline, 300 seeds)

- Defender shots in Suppress: 11 332, hit 15 %; at <= 10 m 5 950 shots, hit 19 %.
- Hold (aimed fire from the spot): 2 014 shots, hit 46 %; at <= 10 m hit 62 %.
- Same-bucket check (out of cover, 1 attacker within 10 m: crouched 60 % vs 18 %, prone 64 % vs 20 %; 2+ attackers: 38-66 % vs
  18-20 %). In cover Suppress at <= 10 m almost never happens - close Suppress is always fire from the open.
- Deaths in Suppress: 281 of 488 defender deaths; killer at a median 3.9 m.

## Rule

Arena key `AimedClose=1` (default 0 = old behaviour): a zone defender (team objective role Defender) with a SEEN threat at
<= 10 m does not take Suppress - he takes Hold (aimed fire) instead. Pure function in `PmcCommanderRules` (e.g.
`ShouldAimInsteadOfSuppress(Role, ThreatSeen, ThreatDistCm)`), wired where the commander picks the order
(`UPmcSquadSubsystem`, next to the R1 FallBackCover branch). Counter `r3_aimed_close` in the round JSON (side stats).

## Tests / gate

- Unit: the pure rule (role / seen / distance edges 999 / 1000 / 1001 cm; non-defenders unchanged).
- Flag off = byte-identical replays: 30 seeds on ONE machine vs the same build without the flag (cross-machine replays differ).
- Worker: the PMC smoke set (PmcArena, PmcRoofDefence, PmcStalemate, PmcCommander, PmcEncounter*).

## Acceptance (pre-agreed with Lead, X6 arena, same machine, same -Jobs)

Pair R1 vs R1+R3 on seeds 1060-1359 (the X6 base from 2026-11-24 is the R1 arm). Accept if wins >= R1 AND captures <= R1,
McNemar not worse. Not-a-no-op: defender Suppress shots at <= 10 m drop by at least half and hit rate at <= 10 m rises.
Report also the kill exchange at <= 10 m (defender kills / defender deaths there) and the open-field skill counters.
