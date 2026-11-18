# 087 - Level generator: PMC squads at strategic places + their mines / tripwires (designer 2026-11-18 evening) - TS + Python builder
Base: main. Branch gemini/087-pmc-posts. Worktree CodexTactics-gemday. Queue: after 085 and 086 (084 after this or before - Claude says).
Spec: docs/design/PMC_DEFENCE.md section D2 (rewritten 2026-11-18: no runtime setup phase - the generator places PMC squads and their
charges), docs/design/TACTICAL_MAP.md (the analysis: chokepoints with exits, hiding spots, overwatch roofs + access, windows, routes, doors),
designer clip notes docs/port/reports/designer-tactics-notes-2026-11-18.md (notes 1, 1b, 2).

Do:
1. Generator stage (TS, after the analysis): seeded-random choice of 1-N PMC posts from the strategic places (weights per kind in config:
   chokepoint exit ambush, overwatch roof, window over the route, hiding spot), each post = squad count, posture (defend / hunt), kind,
   facing; never inside a chokepoint, never on the player start / mandatory route itself; spacing between squads.
2. Charges per defend post (D2 rules): mines on the straight approach lines to the post (`mine_spacing_m`), a mine in the doorway the attack
   route enters, a tripwire on the post's flank, a mine / tripwire on an inside roof access (hatch / stair head); budget per squad from
   config (sapper 2 mines + 1 tripwire, designer Q1); never on the squad's own retreat line. Written into the props layers as PMC-owned
   hostile traps (the same mine / tripwire kinds G003b uses), tagged with the post id.
3. Page: a «ЧВК» layer toggle showing posts (icon by kind + facing) and their charges; «перебросить» re-rolls posts + charges with a new seed;
   inside a region frame it re-rolls only there (reuse 084's frame action when it exists, else whole map).
4. Builder (Scripts/Editor/build_levelgen_map.py): carry the posts into the level JSON `pmc_squads` (keep the current schema keys count /
   weights / posture / delay_s, add `post` {pos xyz, yaw, kind, id} instead of `spawn: random`), keep a per-map tuned pmc_squads on rebuild
   like pressure / cheats.
5. Tests: unit tests (posts never in a chokepoint / on the route, charge budget, retreat line free, determinism per seed); a run on the
   accepted X4 layout listing posts + charges; Python test for the level JSON. Screenshots.
Allowed paths: Tools/TrainingGround/src/**, Scripts/Editor/build_levelgen_map.py, Scripts/Editor/tests/**, Content/Data/LevelGen/levelgen_v2.json (new keys only), docs/port/gemini/outbox/087-*

Forbidden: C++ (the runtime spawn at the post is a separate API-agent task), maps, accepted / level JSON files by hand, Config, worker / ssh.
PLAN first (topic gemini-087, wait for Claude's OK). Report docs/port/gemini/outbox/087-pmc-posts.report.md. FINISH in topic gemini-087.
</content>
</invoke>
<invoke name="Bash">
<parameter name="command">cd "C:/Users/Zephyrus15Duo/Documents/Unreal Projects/CodexTactics"; python - <<'EOF'
import sys; sys.path.insert(0,'Scripts/Tools'); import gemini_gate as g
print(g.parse_allowed_paths(open('docs/port/gemini/inbox/087-levelgen-pmc-posts-and-charges.md',encoding='utf-8').read()))
EOF
git add docs/design/PMC_DEFENCE.md docs/port/gemini/inbox/087-levelgen-pmc-posts-and-charges.md; git commit -q -m "docs(design): PMC_DEFENCE D2/P2 - the generator places PMC squads at strategic places and their mines / tripwires (no runtime setup phase, designer 2026-11-18 evening); Antigravity task 087

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"; CODEX_AGENT=claude git push -q origin main 2>&1|tail -1; git log --oneline -1|cut -c1-12