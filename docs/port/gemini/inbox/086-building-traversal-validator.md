# 086 - Buildings P0: traversal validator + per-building panel + «Принять» gate + stair fixes (A) / (B) - TS, HIGH priority (after 085, before 084)
Base: main. Branch gemini/086-building-traversal. Worktree CodexTactics-gemday.
Spec: docs/design/levelgen/BUILDING_GENERATOR.md - section 1 (audit: 3 of 24 stairs walk up on X6, causes A-F), section 4 (rules), section 5
layer 1 (validator), section 6 row P0 a). Designer decisions in section 2 are binding (greybox on the 50 cm grid, storey 300, TS generates).

Do:
1. `Tools/TrainingGround/src/levelgen/buildingTraversal.ts`: walk graph per building from the layout alone (storey cells = slab minus gaps,
   rooms, door graph, flights / ladders / hatches as edges, approach boxes). Checks (section 5.1): every room, storey and walkable roof
   reachable from an exterior door WITHOUT drops; 4.1 clearances (35 cm agent radius); 4.2 approach box at the foot and the top of each
   flight free and the slab gap == the flight footprint (catches A); 4.3 ladder rung counts (36.4 cm pitch); 4.4 breach jamb slots free.
   Result per building into the layout `analysis.buildings[]` (id, enterable, pass, first failure with xy + reason).
2. Fix cause (A): `generate.ts` floors_detail stairwell gap `[pos - 0.4, top + 0.4]` overshoots past the ramp top - the gap must end exactly
   where the flight lands (no 40 cm hole). Fix cause (B): flight foot / top get a free landing (approach box >= 150 x 150 cm, not a corner
   pocket: >= 120 cm from walls on the open sides). Numbers in Content/Data/LevelGen/levelgen_v2.json (or the generator's existing config),
   not literals.
3. Page (department «Генератор уровней»): a per-building list / panel (green / red, click = zoom to the building and draw the first failure);
   «Принять» disabled while an enterable building fails, with an explicit «принять с N сломанными зданиями» override + warning.
4. Tests: unit tests for the validator on small fixtures (good stairwell, cause-A gap, corner-pocket mouth, unreachable roof, ladder rung
   mismatch); a run on the real accepted X6 / X4 layouts reproducing the audit numbers BEFORE the fix (block stairwells fail with cause A) and
   0 errors on a freshly generated X6-size layout AFTER the fix. `npm run typecheck`, full suite green. Screenshots of the panel.
Allowed paths: Tools/TrainingGround/src/**, Content/Data/LevelGen/levelgen_v2.json (new keys only), docs/port/gemini/outbox/086-*

Forbidden: C++, maps, accepted / props / level JSON files, Config, worker / ssh actions.
PLAN first (topic gemini-086, wait for Claude's OK). Report docs/port/gemini/outbox/086-building-traversal.report.md. FINISH in topic gemini-086.
The C++ bot smoke (section 5 layer 2, LevelGenBuildingsSmoke) is a separate task for an API agent - not yours.
</content>
</invoke>
<invoke name="Bash">
<parameter name="command">cd "C:/Users/Zephyrus15Duo/Documents/Unreal Projects/CodexTactics"; python - <<'EOF'
import sys; sys.path.insert(0,'Scripts/Tools'); import gemini_gate as g
print(g.parse_allowed_paths(open('docs/port/gemini/inbox/086-building-traversal-validator.md',encoding='utf-8').read()))
EOF
git add docs/port/gemini/inbox/086-building-traversal-validator.md; git commit -q -m "docs(port): Antigravity 086 - buildings P0 traversal validator, per-building panel, accept gate, stair gap / mouth fixes (BUILDING_GENERATOR.md P0a); order 085 -> 086 -> 084

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"; CODEX_AGENT=claude git push -q origin main 2>&1|tail -1; python Scripts/Tools/agent_chat.py post --as claude --to gemini --topic gemini-086 --text "Claude: NEW priority task 086 (buildings P0, the designer's serious bug - stairs / floors unreachable): docs/port/gemini/inbox/086-building-traversal-validator.md on main $(git rev-parse --short HEAD). New order: finish 085 -> 086 (plan first) -> 084." | tail -1