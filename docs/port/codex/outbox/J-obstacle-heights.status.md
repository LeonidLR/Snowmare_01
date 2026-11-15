# J obstacle heights — WIP status

Standing user rules forbid branch switching/rebasing and commits/pushes by Codex. Lead handles branch/commit; coordination sent in #466/#467. No live levelgen/data/spec files edited for J.

Temporary working copy: C:\Users\ZEPHYR~1\AppData\Local\Temp\codex_J_heights_0r5nxuos\work
Metadata: C:\Users\ZEPHYR~1\AppData\Local\Temp\codex_J_heights_0r5nxuos\metadata.json
Base copied: 0c73fdb6842f544ad9146051d32b3c034b3fdd45; generator baseline files read-only from codex/obstacle-heights. No branch operation occurred.

Done in temporary copies:
- Real CSV processed through apply_measured_heights API; explicit #465 targets/tolerances applied; source warnings/spread/outliers retained.
- New tall_ledge / very_tall / high_fence_over; four NEEDS_REVIEW clips excluded from classes.
- Dynamic classifier and explicit-class matching for overlaps; blocked-wall fallback above320; valid 300cm storeys preserved.
- Proposed unmeasured wall placeholder330±5 because300 overlaps very_tall300..320; copied §11 documents it.
- Generator fences use high_fence_over, training low mantle walls use low_mantle; copied tests updated.
- Copied MANTLING §11 updated with measurements, targets and review table.

Validation so far:
- Node regressions in external copy: tests18 / pass18 / fail0; duration_ms393.1428. The initially missing reference-profile fixture was supplied and tests rerun successfully.
- Strict TypeScript check of temporary height classifier/gate/generator/trainingArenas: exit0.
- Main Python script tests from #452: 22 PASS.

Handoff artifact (external, not applied): C:\Users\ZEPHYR~1\AppData\Local\Temp\codex_J_heights_0r5nxuos\J-obstacle-heights.patch
Patch base: 0c73fdb6842f544ad9146051d32b3c034b3fdd45
Patch checks: clean HEAD and current main git apply --check --whitespace=error-all exit0.
Data/doc checks: six exact approved targets, four review-only clips excluded, spec before section11 unchanged.

Remaining for lead:
1. Review 330±5 unmeasured blocked-wall placeholder and retained low-vault source outliers. Source-proxy conflicts are preserved; approved #465 targets were explicitly passed to the script API rather than pretending the default CLI accepts those conflicts.
2. Apply the external patch to the lead-owned codex/obstacle-heights branch and commit there. Codex did not switch/rebase/create branches or commit/push.
3. Regenerate accepted maps separately as planned; untouched here. Run complete TrainingGround suite after regeneration (old snapshots still describe old heights).

Commands for lead:
- git apply --check followed by git apply with the external patch path above.
- python -m unittest discover -s Scripts/Tools/tests -p test_apply_measured_heights.py -v (22 tests green for #452).
- In Tools/TrainingGround: node --test src/levelgen/props/obstacleHeightGate.test.ts src/levelgen/combatBuildingsGenerate.test.ts src/levelgen/combatBuildingsSchema.test.ts (18 green in external copy).
- npm run typecheck; full npm test after lead's accepted-map regeneration.

No UE build, maps/assets edit, live levelgen/data/spec change, branch operation or commit performed. Only this authorized status file was written in the repository for J.
