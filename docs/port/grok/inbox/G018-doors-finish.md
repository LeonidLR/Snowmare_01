# G018 - finish generated-building DOORS (after G016) - DESIGNER PRIORITY

Designer 2026-11-24: "doors in the building generator" - get them to a finished, playable state. Doors exist
(Source/CodexTactics/Public/Interactables/DoorActor.h, DoorRules.h, DoorSubsystem.h, DoorwaySubsystem.h; data
Content/Data/Interactables/doors.json: enabled 0 globally, enabled_maps = the two Showcase maps only).

Step 1 (report first, short): a GAP LIST of the designer's door spec vs the code, posted as "G018 GAPS | ..." in chat; the Lead
confirms the order before you code. The spec (designer 2026-11-21 / 11-22, final):
- Two kinds: UNLOCKED - single click = open by hand (anim_OpenDoor_L/_R), double click = kick open (anim_OpenDoor_Kick_L/_R);
  LOCKED - key if carried (keys from loot / corpses / quest), kick (one or several attempts, may injure; numbers in data),
  shoot the lock, blow it with any explosive; after key / shot: forced push open (anim_OpenDoor_Push_L/_R).
  Anims: /Game/Free_Interaction_Animation/Animations/Door (untracked pack, present in main and agent copies).
- Every door swings AWAY from the opener; where the open leaf blocks another doorway / stair mouth it closes behind the squad.
- Inner doors all shut at start; story-locked doors are STEEL; the kit entrance door has its own leaf model (never rotate the frame).
- Nav / cover / sight update when a door opens or closes; bots and formation route through doors.
- ENEMIES (PMC) do all the same door actions as operatives.
- Windows are a separate, finished rule - do not change them.
Step 2: implement the confirmed gaps, then enable doors on the kept maps with buildings (L_Gen_StoryShowcase_Kit; X6 if its buildings
carry door data - say so in the gap list). Unit tests for every rule; door smokes (DoorBreachSmoke retargeted to a kept map) on the worker.
Branch grok/G018-doors from origin/main. Report docs/port/grok/outbox/G018-doors.report.md + FINISH.
