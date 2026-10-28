# Red team vs. rules v2.5 - interrupted run (2026-10-28)

Status: STOPPED early on the coordinator's request (the user paused). No triage was done.

- Spend: $0.3724 (4 Claude calls, opus-5-5, rounds 13-16, 16 layouts, 864 bot runs). The run started by accident
  (`redteam.ts --help` is not a help flag and ran with the default $5 cap). The planned $12 run was NOT started.
- `npm test`: 149 pass / 0 fail.
- Baseline `--layouts redteam --refresh v2.5` was started and killed before it finished: no baseline counts, no expected_fail refresh
  (old layouts untouched).
- Kept layouts (6 of 16 fail at least once): 13_4, 14_1, 14_3, 15_3, 15_4, 16_4 in Content/Data/FormationLab/redteam.

Raw findings of the 4 rounds (12 distinct failures, NOT triaged; the "looks like" column is the tool's guess):

| check | distinct | looks like | example |
|---|---|---|---|
| 3 flicker | 3 | design question | 13_4 R1 auto n2 s1 t=16.3s: wall -> wedge, back after 0 s |
| 5 spacing | 3 | rule bug? | 13_4 R1 diamond n4 s1 t=16.2s: K-L 20 cm apart |
| 4 formation per zone | 3 | design question | 16_4 R1 auto n2 s1: Z1 wants wall: 0 % |
| 14 covered approach | 1 | rule bug? | 13_4 R1 auto n4 s1: flank 100 %, rear missed |
| 6 rear pace | 1 | rule bug? | 14_1 R1 auto n4 s2 t=22s: p95 203 cm, Z 229 cm behind |
| 1 wall stare | 1 | rule bug? | 14_3 R1 hedgehog n4 s2 t=28.1s: L at L3 |

Next: finish the baseline refresh, triage into A / B / C, then (optionally) the budgeted run.
