# Report: T3. Design Doc - Tactical Doorway Breach

**Task:** T3. Design doc - tactical doorway breach (no code)  
**Branch:** `gemini/t3-doorway-breach`  
**Date:** 2026-11-04  

## Summary of Deliverable

Authored comprehensive tactical CQB architecture specification: [`docs/design/DOORWAY_BREACH.md`](../design/DOORWAY_BREACH.md).

### Key Architectural Elements Covered:
1. **Triad Breach Stack Formation**:
   - Detailed exact spatial coordinates and orientations:
     - **Left Jamb (Slot L):** Hugging wall in cover, leaning past corner into opening (`CornerLeanPoint`), aiming into deep right interior (cross-cover).
     - **Right Jamb (Slot R):** Hugging wall in cover, leaning past corner into opening (`CornerLeanPoint`), aiming into deep left interior (cross-cover).
     - **Center Point-Man (Slot C):** Positioned on normal axis 2.2 m back from threshold, crouched low-ready, aiming straight into center fatal funnel.
     - *(4th operative if present: Slot Z positioned 3.5 m back providing 180° rear security).*

2. **Hover & Cursor Selection UX**:
   - Disambiguated cursor raycasting: hovering over solid wall face (>60 cm from opening) triggers standard linear wall stack; hovering directly over the doorway portal triggers the 3-man breach ghost triad.
   - Dual-mode cycling via mouse wheel or Tab if cursor is ambiguous on threshold edge.

3. **Strict Separation of Squad Control Modes**:
   - **Solo Mode:** Only the active operative moves to the nearest jamb; all other squad mates remain stationary in their holds/cover.
   - **Formation Mode:** Automatic minimal-travel assignment of entire squad to Slots L, R, C.
   - **Box-Selected Group:** Only the selected members participate (e.g. 2 men take L + R; 3 men take L + R + C); non-selected operatives remain stationary.

4. **Slot Assignment & Approach Pathing**:
   - Minimal total travel distance optimization (`AssignMinimalTravel`).
   - Defined wall-hugging approach vectors to keep operatives out of the doorway line of fire during the approach.

5. **Physical Peek & Threshold Trap Spotting**:
   - Reused `OperativeCharacterCornerAim.cpp` and `TrapSpotRules.h`.
   - Jamb operatives physically lean into the opening without stepping their feet across, sweeping interior floor for hidden tripwires, claymores, and PMN mines within 1.5 seconds.

6. **The Follow-Up "Enter / Clear" Action**:
   - Designed 4-step dynamic room entry sequence (Man 1 hooks right, Man 2 hooks left, Man 3 drives center depth, securing all interior sectors).

7. **Architectural Edge Cases & Failure Modes**:
   - Documented narrow doors (<100 cm), damaged doorway rubble, upper floor/catwalk drop links, alerted enemies inside, and locked doors.

8. **Subsystem Reuse**:
   - Mapped implementation directly to existing C++ seams: `CodexTacticsPlayerControllerCover.cpp`, `SquadSubsystemGroupCover.cpp`, `OperativeCharacterCornerAim.cpp`, `TrapSpotRules.h`, and `FORMATION_SECTORS.md`.

---

## Open Questions for the User
1. When ordering "Enter / Clear", should the breach action default to a quick Flashbang / Stun Grenade throw if an operative has grenades available?
2. Should locked doors allow shotgun breaching hinges immediately from Slot C while L and R cover the opening?
