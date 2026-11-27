# 156 ? cover/open-shot loop

Branch `codex/156-cover-open-shot-loop`, based on fetched `origin/main` `7e4b44f5`. Lead review pending. No main changes or push.

## Fix

UnderFire used a dot-product cover quality, while open-shot exits conflated firing-pose reach with wall protection. Both now use `CoverThreatRules`: opposite sides of the wall face and a line crossing within its exposed ends. UnderFire resolves the same low-cover approach face as the eventual order before scoring, and retains the quality threshold; the open-shot decision and final exit gate refuse to discard a protecting wall even if the pose cannot aim at the threat. This shares the planar protection decision; muzzle-height ray traces remain responsible for actual shot visibility.

Automatic transitions use a shared three-second hold, stamped at cover entry and open-shot exit, checked on escape, UnderFire reacquisition and open-shot return. Same-wall shimmy does not restart it. Player cover intent persists through arrival and shimmy until leaving/replacing the cover order. UnderFire explicitly marks its orders automatic. The decision wrapper and final escape gate both enforce player intent.

## Scope and overlaps

Runtime: `OperativeCharacterAim.cpp` (include-only build fix: existing arrival logging lacked `CodexTactics.h`), `OperativeCharacterCover.cpp`, `OperativeCharacterCornerAim.cpp`, `OperativeCharacterUnderFire.cpp`, `OperativeCharacter.h`, `CoverFacingRules.cpp`, new header-only `CoverThreatRules.h`. Unit cases in `CoverFacingTest.cpp`; extended `UnderFireManoeuvreSmokeCommand.cpp`; `Scripts/test_map.json` reactivates that runtime arena smoke on retained X6 instead of archived Depot. Handoff/changelog/system docs updated. No maps, assets, Config, VFX or designer JSON changed.

153 is not merged in the fetched main. Its branch changes controller click/preview handling and `CoverSwitchLakeSmoke`, with no direct source-file overlap here; conceptual overlap is near-side resolution before `OrderTakeCover`. No 153 code copied. Gemini135b has direct overlaps in `OperativeCharacterCover.cpp`, `OperativeCharacterCornerAim.cpp` and `OperativeCharacter.h` (plus the include-only Aim fix); this task changes decision/intent/timing hunks only, no visual animation code. Lead must reconcile those hunks on merge.

## Validation

Initial worker build failed: first-header order (corrected before results returned) plus baseline `OperativeCharacterAim.cpp` missing the logging declaration include, now fixed. Final worker build and Smart/mapped runs pending. New pure tests cover oblique blocking vs pose reach, own side, finite-end miss, plane boundary, invalid normal, cooldown boundary, clock reversal and player-order retention/release. Extended full command: `CodexTactics.UnderFireManoeuvreSmoke`; part D places a PMC beyond the low wall at an oblique angle outside a deliberately narrowed pose arc and samples uninterrupted player cover for >=5 seconds, including direct requests through the final escape gate.

Impact-map validation and TypeSafe protected-path audit pass. Exact designer play-copy geometry and interactive visual playtest are not reproduced; no claim about animation polish or RPG choice.
