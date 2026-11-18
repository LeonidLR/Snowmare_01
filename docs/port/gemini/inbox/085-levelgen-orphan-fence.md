# 085 - Level generator: an outer fence survives a region re-generation and cuts through the new blocks (designer bug 2026-11-18) - TS, URGENT (before 084)
Base: main. Branch gemini/085-orphan-fence. Worktree CodexTactics-gemday.

Designer: on L_Gen_Depot_1_X6 the Unreal map shows a long rotated fence crossing two alleys; the generator page shows nothing there.
Claude's diagnosis (accepted layout Content/Data/LevelGen/accepted/gen_ref_01_02_depot_1_x6.json, read only):
- Wall family `F_x1_8_t1003` (6 fence walls, kind fence, h 135; a ~34 x 8 m rectangle turned ~14 deg, corners (11357,16631) (11162,17418)
  (7924,16617) (8118,15829)) is the fence of the whole expansion block x1_8. Inside it now stand the alleys `Z_x1_8_4_1_1_t1003` and
  `Z_x1_8_5_1_1_t1003` with their own fences `F_x1_8_4_1_1_t1003` / `F_x1_8_5_1_1_t1003` (wall segments cross: the only fence/fence or
  fence/building crossing on the whole map - 288 fences checked).
- The Unreal builder (Scripts/Editor/build_levelgen_map.py) is correct: the saved map equals its plan, actor by actor. The fault is in the
  layout data, plus the page does not draw this fence on the ground layer view, so the designer cannot see it.
Do:
1. Find which step leaves the parent fence: region re-generation (regionRegen.ts: `wallFamily`, `layoutGroups`, `selectByCentre` - the
   family's centre may lie outside the frame while the blocks it encloses were re-generated) and / or the expansion / subdivision that
   made x1_8_4_1_1 / x1_8_5_1_1. Reproduce in a unit test with a small layout.
2. Fix: after any re-generation / expansion, a kept fence family that crosses a fresh group's walls / footprint / alley polygon is either
   removed with its group (when it encloses the re-generated blocks) or the fresh group is dropped (`overlaps_kept`) - pick the rule that
   matches how buildings already behave and say why. Add a final validator over the whole layout: no wall segment of one family crosses
   another family's wall or a building footprint (rotated); the page shows a red marker per conflict and «Принять» warns.
3. The page draws fences (kind fence) on the ground layer like other walls (thin line), so what Unreal builds is visible.
4. Do NOT edit the accepted file by hand; the report says how the designer fixes X6 (re-generate the frame over x1_8, or the new validator's
   "remove orphan fence" action if you add one).
Checks: unit tests (repro + validator on the real X6 accepted file: exactly the F_x1_8_t1003 conflicts found, 0 after the fix path),
`npm run typecheck`, full Training Ground suite, screenshot of the page with the fence drawn and the conflict marker.
Allowed paths: Tools/TrainingGround/src/**, docs/port/gemini/outbox/085-*

Forbidden: C++, Content (no layout / map edits), Config, worker / ssh actions.
PLAN first (topic gemini-085, wait for Claude's OK). Report docs/port/gemini/outbox/085-orphan-fence.report.md. FINISH in topic gemini-085.
</content>
</invoke>
<invoke name="Bash">
<parameter name="command">cd "C:/Users/Zephyrus15Duo/Documents/Unreal Projects/CodexTactics"; git add docs/port/gemini/inbox/085-levelgen-orphan-fence.md; git commit -q -m "docs(port): Antigravity 085 - orphan block fence F_x1_8_t1003 crosses re-generated alleys on X6 (designer bug 2026-11-18), before 084

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"; CODEX_AGENT=claude git push -q origin main 2>&1|tail -1; python Scripts/Tools/agent_chat.py post --as claude --to gemini --topic gemini-085 --text "Claude: URGENT designer bug 085 (before 084): docs/port/gemini/inbox/085-levelgen-orphan-fence.md on main $(git rev-parse --short HEAD) - an outer block fence survives region re-generation and cuts through the new alleys on X6; the page does not draw fences. Order: finish 083c -> 085 (plan first) -> 084." | tail -1