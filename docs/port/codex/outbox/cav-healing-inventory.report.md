# CaV / Healing filename inventory
Filename-only read; no binary assets opened or modified, no builds/smokes/commits. Main final check a5ff9200. Exact spelling below preserves vendor typos; every clip suffix is .uasset.
Counts: 31 assets under Content/CaV_Anims/Animations/ThirdPerson_Anims; 42 under Content/HealingAnimations/Animations. FP animations excluded.
Heights below are proposed gameplay bins from MANTLING.md, NOT measured authored heights; Run* means a named run variant, other names do not prove the starting stance. Root-motion status is supplied by Claude, not verified from binaries.

## CaV: all 31 TPP clips
Prefix V = Content/CaV_Anims/Animations/ThirdPerson_Anims/.
| Type / folder under V | Clips | Standing vs approach | Proposed height / action + landing candidates |
|---|---|---|---|
| Low vault: Climbing/LowVault | LowVault; RunLowVault | default / Run | Existing vault bin 25-105 cm, over thin obstacle; pair with Landing/LowVaultLand. |
| Low climb: Climbing/LowClimb | Low_Climb; RunLowClimb | default / Run | Low mantle bin 75-130 cm, ON TOP; no explicitly named LowClimbLand; inspect whether landing is baked into clip. |
| High climb: Climbing/HighClimb | HighClimb; RunHighClimb | default / Run | High mantle bin 130-240 cm (1.5/2 m candidates); Landing/TallLand is a possible landing, not a proven pair. |
| High fence: Climbing/HighClimb | HighFenceVault | no Run variant named | Tall thin fence OVER, not standing on its top; possible Landing/TallFence_Land. Needs its own height/landing contract beyond ordinary low vault. |
| Jump climb: Climbing/JumpClimb | JumpClimb; RunJumpClimb | default / Run | Jump-assisted high mantle, proposed upper 130-240 cm; candidate Landing/TallLand only after clip inspection. |
| Jump fence: Climbing/JumpClimb | JumpFenceClimb | no Run variant named | Jump-assisted fence traversal; destination top vs far side ambiguous from name. Possible TallFence_Land only if far-side vault. |
| Jump down: Climbing/JumpDown | JumpDown; JumpDownNoWait | variant NoWait, not Run | Downward drop bin 60-250 cm; candidate TallLand or impact landing, selected by drop contract rather than random pairing. |
| Wall run: Climbing/RunUpWall | Wall_Run_Start; Wall_Run_Loop; Wall_Run_Lunge | wall-run sequence | Not used? Outside tactical mantle scope; do not equate Wall_Run with ordinary RunHighClimb. |
| Split high-climb phases: Climbing/Split | HI_Climb_Climb_Up; HI_Climb_Handandfoot_Hang; HI_Climb_TwoHand_Hang | hang / climb phases | High ledge state-machine candidates; not standalone interchangeable vault clips or ordinary landings. |
| Tall wall: Extras | Standing_Tall_Wall_Climb; Sprint_Footplant_TallWallClimb; TallWall_LandSprintTransition | explicit Standing / Sprint / exit | Proposed upper high-mantle bin, authored height unknown (possibly outside 240 cm). Sprint climb -> TallWall_LandSprintTransition is a name-based exit candidate, not verified contact timing. |
| Tall ledge: Extras | TallLedge_ClimbUpNEW | no Run variant named | High mantle / hang-to-top candidate; start pose, height and landing phase unknown. |
| Updated jump climb: Extras/JumpSmall | Standing_JumpClimbUpNEW; RunJumpClimbNEW | explicit Standing / Run | Jump-assisted mantle variants; folder JumpSmall does not establish a numeric height. Check against original JumpClimb before treating as equivalent. |
| Jump fence vault: Extras/JumpSmall | JumpFenceVault | no Run variant named | Far-side fence vault candidate; possible Landing/TallFence_Land; do not put into low-vault pool without measured clearance. |
| Landings: Landing | LowVaultLand; TallFence_Land; TallLand | exits | Name pairs strongest for LowVault -> LowVaultLand and HighFenceVault/JumpFenceVault -> TallFence_Land; TallLand is generic. All pairings require endpoint/velocity/foot checks. |
| Impact / rolls: Landing/ImpactLanding | Fall_SideRoll; FallHigh; Land_LowRoll | impact variants | Fall/drop recovery candidates, not upward mantle or ordinary vault pool; choose by fall/landing rules, never by name alone. |

## Mix with current vault / new mantle
- Source/CodexTactics/Private/Characters/OperativeAnimInstance.cpp:447-465 UpdateVaultClip alternates two configured L/R clips and matches the clip landing time to traversal duration. It is not yet a general random pool.
- Candidate same low-vault family: LowVault / RunLowVault alongside existing M_Neutral_Traversal_Hurdle_1_0_* L/R and jump_fence candidates. Add per-clip gait, permitted geometry/height, landing marker and root-motion policy; preserve foot-pair alternation and fallback within compatible subsets.
- Low_Climb/RunLowClimb, HighClimb/RunHighClimb, JumpClimb/RunJumpClimb, updated JumpSmall climbs, tall-wall/ledge and split phases belong to a new mantle selection/state machine, not the existing two-foot vault fields.
- HighFenceVault / JumpFenceVault need a high-fence FAR-SIDE path category; JumpFenceClimb needs visual classification first. JumpDown/NoWait and impact clips belong to drop/fall handling. Wall run: not used pending designer decision.
- Current vault capsule uses a manual MOVE_Flying arc (OperativeCharacterVault.cpp:146,198-211). CaV root motion must be consumed/warped OR deliberately ignored while driving a matching trajectory: do not double-apply root translation. Retarget and validate action/land transitions before use; no source pack edits.

## Healing: all 42 clips (prefix H = Content/HealingAnimations/Animations/)
| Category / apparent recipient | Exact filenames without suffix | Battlefield first-aid coverage / stance evidence |
|---|---|---|
| Bandage, apparent self | AS_BandageTreatment; AS_BandageTreatment2; AS_BandageTreatmentOnKnee; AS_BandageTreatmentOnKnee2; AS_BandageArmSitting; AS_HealingBandageSitting; AS_SquatLegBandage | Bandage treatment; explicit on-knee/sitting/squat variants. Unqualified Treatment stance/recipient needs viewing. |
| Bandage teammate | AS_BanadageHealingFriendFast; AS_BandageHealingFriend; AS_BandageFriendSquat | Medic-to-teammate bandaging, including fast/squat names; partner pose and alignment not established. |
| Injection, apparent self / no stance suffix | AS_InjectionInTheHand; AS_InjectionInTheLeg; AS_InjectionInTheShoulder | Injection candidates; standing only a hypothesis until viewed. |
| Injection, sitting/squat | AS_InjectionInArmSitting; AS_InjectionInArmSitting2; ASInjectionInLegSitting2; AS_HandInjectionSquating; AS_InjectionInLegSquating | Explicit stance variants for self treatment; no claim about exact prop/hand socket. |
| Injection, lying | AS_ArmInjectonLying; AS_LegInjectionLying; AS_ShoulderInjectionLying | Explicit lying treatment candidates; no assumption these are CPR or revival. |
| Injection/help teammate | AS_InjectionHealingFriend; AS_HealingFriendInShoulder | Friend assistance; shoulder treatment action type remains ambiguous for the second name. |
| Drink, apparent self | AS_DrinkHealing; AS_DrinkHealing2; AS_DrinkHealingLying; AS_DrinkHealingSitting; AS_DrinkHealingSitting2; AS_DrinkHealingSquat; AS_GetHealingDrink | Drink/retrieve consumable and explicit lying/sitting/squat variants; unqualified stance unknown. |
| Give teammate drink | AS_GiveHealingDrinkFriend; AS_GiveHealingDrinkFriend2 | Caregiver-side consumable assistance; check synchronization with receiver. |
| Wounded calls | AS_WoundedCallsHelp; AS_WoundedCallsHelp2; AS_WoundedCallsHelpLying; AS_WoundedCallsHelpStand | Help signal variants, explicitly lying/standing for named clips. |
| Patient / movement | AS_WoundedDrink; AS_WoundedGetInjection; AS_WoundedLying; AS_WoundedSitting; AS_WoundedMoving; AS_WoundedMovingRM | Receiver-side drink/injection, wounded poses/movement. RM suffix alone is not verified root-motion metadata or a teammate carry. |
No explicit carry/drag/lift/stretcher filename found in this pack. Names cover bandage, injection, consumable aid and wounded recipient poses; they do not establish resuscitation, two-person carry, treatment duration or gameplay effects.

## Skeleton-name evidence / integration risks
- CaV: Content/CaV_Anims/Demo/CharacterMesh/ThirdPersonMesh/{SK_Mannequin,UE4_Mannequin_Skeleton}.uasset strongly suggests UE4 Epic mannequin content. FirstPersonMesh arms assets are excluded from TPP use.
- Healing: Content/HealingAnimations/Demo/Mannequins/Meshes/{SK_Mannequin,SKM_Manny}.uasset; no *_Skeleton filename found by the pack-name search. Manny/Epic mannequin is suggested, but exact sequence-to-skeleton links cannot be inferred from filenames.
- Both packs need actual skeleton/retarget, root curves, duration, weapon hiding, hand/prop contact and helper/patient synchronization review before hookup. Existing male/female operative compatibility is unverified; no assets were imported, retargeted or saved here.
