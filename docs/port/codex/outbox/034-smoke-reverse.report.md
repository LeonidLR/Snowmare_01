# Task 034 — reverse smoke registry coverage

Branch: `codex/034-smoke-reverse`. Implementation commit: `04da87dd49014b046dab41d0dfdcc956f84044b5`.

Added `--reverse` to `Scripts/Tools/check_smoke_names.py`, using the existing registration regex and Debug/*.cpp scope. It checks only `CodexTactics.*Smoke` commands, reports missing registry entries and missing impact-map `smokes` coverage separately, and exits 1 only for missing registry entries. Default forward validation is preserved.

Before registry edits (actual repository scan):
- Missing from smokeRegistry: **[]**.
- Missing from any impact-map smokes list: **[]**.
- Scanned 296 Debug cpp files: 336 registered commands, including 284 matching smoke commands; smokeRegistry has 284 entries.

No registry additions were necessary. `Scripts/test_map.json` is byte-for-byte unchanged (`git diff --exit-code -- Scripts/test_map.json`: exit 0), preserving all formatting, impact lists and flags.

Validation:
- `python -m unittest discover -s Scripts/Tools/tests -p test_check_smoke_names.py`: **6/6 PASS**, exit 0. Existing forward success/failure fixtures retained; new cases cover both missing categories independently, impact-only gaps returning 0, prefix normalization/nested lists, and exclusion of other prefixes/non-Smoke suffixes.
- Repository `python Scripts/Tools/check_smoke_names.py --reverse`: exit **0** before registry edits and final state; 0 missing registry entries, 0 missing impact-list entries.
- Repository `python Scripts/Tools/check_smoke_names.py`: exit **0**, all map names registered.
- `git diff --check`: PASS.
- `python Scripts/Tools/typesafe_triage.py --audit-diff --agent codex`: **PASS**, risk 0.0/4 before implementation commit.

Implementation diff: 2 files, 78 insertions, 5 deletions. No fixtures changed, no engine/maps/assets/config edits, no Unreal build needed. Pre-existing untracked linked content/config/cache files were left untouched. Root reviewed the reverse checks, exit codes and regression cases and ran the final audit before this report commit.

Routing: **2 | gpt-6.1-sol | pass** (supplied task classification confidence 0.92).
