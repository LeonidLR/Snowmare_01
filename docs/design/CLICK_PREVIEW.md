# One building click (R17-3) and the doorway breach in the game (R17-5)

> Designer request 2026-11-17 evening (TASK_BOARD R17-3 / R17-5), his decisions the same evening. Code: `ClickPreviewRules`
> (pure), `ACodexTacticsPlayerController::HandlePlayerClick` / `TickClickPreview` (`CodexTacticsPlayerControllerClickPreview.cpp`), the
> breach session (`CodexTacticsPlayerControllerBreach.cpp`, `DoorwayBreachEntryRules`). Switch: `Codex.Click.Preview` (1 default, 0 = the
> rules before). Tests `CodexTactics.Tactics.ClickPreview.*`, `CodexTactics.Tactics.DoorwayBreach.Entry.*`; smokes `ClickPreviewSmoke`,
> `DoorBreachSmoke`, `MantleClickSmoke` (updated).

## 1. The rules (all modes: Solo, Solo with a box selection, Formation)

| What the player does | What happens |
|---|---|
| hovers a wall / corner / obstacle / doorway | **nothing** (no hover ghost: cover, peek, cover switch, wrap, climb and stealth-wall hover ghosts are gone) |
| FIRST click on a wall face | the cover ghost (hug the wall) - with every member who goes along |
| FIRST click near a wall's end / a corner | the corner peek ghost (in cover there, the squad's corner, or the run-and-peek from the open) |
| FIRST click on an obstacle's top | the climb ghost (B17-2 posed frame, hand on the lip); formation / box selection: every climber posed on his own lane (R17-1) |
| FIRST click into a doorway's opening (+40 cm either side) | the breach stack ghost (R17-5) |
| the cursor moves over the SAME building | the ghost follows: slides along the wall, turns into the peek at the corner, back into cover in the middle |
| the cursor leaves the building | the ghost stays where it was |
| SECOND click on the same building (or on the ghost's feet, 60 cm) | carried out: cover / shimmy / wrap, the peek (run there and peek), the climb (walking; a group: the squad's climb in turns, R17-1), the breach stack |
| a DOUBLE click (0.35 s, 30 px) | the same at a run; on an obstacle (top or face) = run there and climb at once |
| a click on the ground away from it | the ghost goes, the click is the ordinary move |
| a click on another building | that building's ghost instead |
| RMB / Esc, Ctrl / Shift / Alt clicks, objects, bodies, ladders, EXIT arrows, wires | the ghost goes; the ordinary click (unchanged) |

"The same building" (`ClickPreviewRules::IsOnStructure`): a piece the ghost was already shown on; or a wall-like piece of the same actor, the
same level-generator `building=<id>`, or touching the pieces seen so far (30 cm: a corner's cross wall, a lintel, the wall past a door gap).
A floor-like hit counts only if the ghost stood on it (a wall's own top, the obstacle's top): the floor beside / inside a building is a move.

## 2. The whole group in the ghost

The ghost shows the placement the order itself makes (nothing new invented):

- **Formation, a wall** - `USquadSubsystem::PreviewFormationWall` = `OrderFormationWall`'s `FormationWallRules` layout and minimal-travel
  roles (the formation's state is put back after the preview); the leader's own silhouette moves to his Commander place.
- **Box selection / legacy group cover** - `PlanGroupCoverPlaces` (split out of `OrderGroupCover`: probed slots along the wall, nearby covers,
  prone spots).
- **The squad's corner (formation)** - `PlanFormationCornerPeek` (B17-1 corner pick, F-2 leader corner): the corner man + the v2.4 stack.
- **Box selection in cover at the wall** - `PlanGroupCornerPeek`; **box selection from the open** - `PlanRunGroupCornerPeek`;
  **formation run-and-peek** - the wall layout at that corner.
- **Doorway** - `DoorwayBreachRules::AssignSlots` (Gemini 050) on `ADoorwayBreachGhostActor`.
- **An obstacle, formation / box selection (R17-1)** - `USquadSubsystem::PreviewSquadMantle`: `OrderSquadMantle`'s lanes (`MakeSquadMantleLanes`,
  each checked with that man's own clip from its base, landings apart - `IsSquadMantleLaneFor`; the climb state is put back), each man posed on
  his lane; the second click = `OrderSquadMantle` (the ladder-like queue, FORMATION_CONTRACT.md 22), a double click = the same at a run.

## 3. Why it worked on some buildings and not on others

Measured by the ClickPreviewSmoke sweep (case j: every ground-floor wall face the camera sees, the cursor at 1 / 2.5 / 4 m up its middle and
30 cm from both ends; `-ClickSweepOnly -ClickSweepAll` = every tall static mesh of the map, not only the generator's tagged walls), 2026-11-17
on L_Gen_Depot_1:

1. **Tall buildings: a cursor hit high on the wall put the ghost in the air.** The cover probe (`CoverTraceRules::FindCoverSlotAt`) looks for
   the ground 80 cm in front of the HIT, from the hit's own height down 3 m; a hit 2.5-4 m up a two-storey wall finds no ground and assumes
   "the ground = the hit minus the chest height", so the slot - and the ghost, and the walk - stood 1.5-3 m above his floor (off the navmesh:
   no way there). A low wall was always clicked low (it works), a tall one mostly high (it "does not work"). Raw hits: 12 of 45 high hits on
   the generator's walls off his floor, 29 of 88 over every mesh. Fix: `LowerBuildingHit` - a face hit more than 1.6 m over his feet is traced
   again at his chest on the same face (`Codex.Click.LowerHighHits`, 1 default): 0 of 45 (6 of 88 over every mesh: platforms / blocks whose
   foot is not his floor).
2. **Walls ending at a doorway**: their end within 40 cm of the opening is now the breach (designer answer 4) - 12 of 49 exposed ends on the
   Depot. Before R17-5 they were an ordinary end.
3. **The hover itself**: the old hover peek ghost ran only in some states (in peekable cover, the squad's wall, the run-and-peek on), the cover
   ghost never on hover - the same wall looked "alive" or "dead" by the operative's state, not by the building. Now nothing hovers; the first
   click gives every building its ghost in every mode.

Every cursor hit on a seen face has a building meaning (125 hits, 0 misses; 17 hits on pieces in front of a face - a cross wall, a pillar -
0 misses). Still open: the run-and-peek finder (`FindRunCornerPeekForHit`, unchanged) sees no corner at the far end of some very long walls
(> 12 m from the operative) - 1 of 37 exposed ends on the Depot, 3 of 78 over every mesh: there the first click shows the cover ghost and the
slide along the wall gets the peek only once the operative is nearer. The smoke lists each such end with the finder's verdict (gate: >= 95 %).

## 4. Doorway breach (R17-5)

- First click into the opening (the camera's ray through the portal plane - `ClickPreviewRules::RayThroughPortal`: the room's floor behind it,
  the threshold, a jamb face within 40 cm of the opening; a wall more than 40 cm off = ordinary wall cover, designer answer 4) -> the stack
  ghost. Who: Solo = the selected man alone at the nearest jamb; a box selection = the selected ones; Formation = the leader + his free
  followers (`GetGroupCoverMembers`), up to the four slots.
- Second click -> the stack: each jamb man gets the run-and-peek into his jamb (the real wall's cover slot at the jamb stand point, the opening's
  edge as the peek edge: `OrderRunCornerPeek` - he leans in round the frame without crossing it); the centre / rear men walk to their spots, then
  take the slot's stance (centre crouched) and look along its sector (look hold point). In formation the followers hold
  (`SetFollowersHolding`) for the session and get it back after.
- Once everyone is there (or after 30 s), a click INSIDE the room (its floor / roof over it, or a ray into the opening + 40 cm - from outside
  the camera often sees only the jambs; a click on a stacked jamb man's body leaning into the frame counts as the room behind him) = the entry
  (`DoorwayBreachEntryRules::PlanEntry`, DOORWAY_BREACH.md 7.2): the right-hand jamb man hooks right, the left one 0.3 s later hooks left, the
  centre man drives 2.5 m in and crouches; rear security stays outside. Then "Room clear! Hold sectors."; any other order ends the session.
- The Codex step-4 patch (`breach-step4.patch`, 11-15) was a HOVER preview - against the 11-17 decision "hover shows nothing"; it was not
  applied. Its ideas (ray vs the portal plane, controller-owned ghost) are in the code above.

## 5. Not done

- A man with no lane left on the obstacle has no silhouette (he would hold at the base).
- Breach: the Tab / wheel toggle (designer answer 4: none), locked doors, the threshold trap sweep promise (the jamb men peek; the trap spotting
  runs where the peek sight runs), the exterior-facade approach waypoints (5.2), the amber "enemy inside" warning, more than four men.
- The members' silhouettes in the cover idle stand for "who stands where"; the prone spots are not shown prone.
