# Gemini 105 - X6 Training Sectors Specification (WIP Report)

## Summary
In accordance with designer drawing (`105-x6-sectors-designer.webp`) and `LEAD ADDENDUM 2026-11-24` (frozen schema):
- Split `L_Gen_Depot_1_X6` into 6 tactical training sectors (2 rows x 3 columns).
- Zero buildings cut: snapped boundary edges to open streets and lanes (horizontal divider along central avenue at X = 0, vertical divider 1 at Y = -4700 cm, vertical divider 2 at Y = 9600 cm).
- All box sizes exceed the 60 x 60 m minimum (ranging from 121 x 152 m to 170 x 152 m).
- Schema written to `Content/Data/LevelJson/L_Gen_Depot_1_X6.sectors.json`.
- Visualization generated and saved to `docs/port/gemini/outbox/105-x6-sectors.png`.

## Sector Bounds & Contents Summary

| Sector | Dimensions (Y x X) | Bounds Box (X: [min, max], Y: [min, max]) | Enterable Bldgs | Total Covers | Fences (m) | Ladders | Posture | Primary Dist |
|---|---|---|---|---|---|---|---|---|
| **1** | 170 m x 152 m | X: [0, 15200], Y: [-21700, -4700] | 15 | 80 | 567.1 | 7 | DEFEND | 50.0 m |
| **2** | 143 m x 152 m | X: [0, 15200], Y: [-4700, 9600] | 7 | 235 | 553.6 | 3 | PATROL | 50.0 m |
| **3** | 121 m x 152 m | X: [0, 15200], Y: [9600, 21700] | 6 | 52 | 495.7 | 4 | DEFEND | 45.0 m |
| **4** | 170 m x 152 m | X: [-15200, 0], Y: [-21700, -4700] | 7 | 174 | 905.1 | 5 | AMBUSH | 50.0 m |
| **5** | 143 m x 152 m | X: [-15200, 0], Y: [-4700, 9600] | 8 | 166 | 709.1 | 4 | DEFEND | 45.0 m |
| **6** | 121 m x 152 m | X: [-15200, 0], Y: [9600, 21700] | 3 | 85 | 285.3 | 2 | PATROL | 45.0 m |

Total buildings covered: 68/68 (100% accounted for, 0 cut).
All candidate player starts and PMC posts verified with >100 cm obstacle clearance and 40–50 m engagement distance.
