# Animation setup (ABP, clips, retargets)

> The pose is blended natively in `UOperativeAnimInstance` (ABP_Operative's AnimGraph holds only the Output Pose plus the user's IK / aim-offset nodes).
> Clips are wired by idempotent scripts in `Scripts/Editor/setup_*animation.py`; the sections below list per feature what C++ does and what the user adds in the ABP.
>
> Key files:
> - `Characters/OperativeAnimInstance`, `EnemyAnimInstance`, `Scripts/Editor/setup_operative_*animation.py`, `setup_enemy_animation.py`, `create_frozen_pmc_fighter.py`, `create_military_zombie.py`, `UOperativeAnimGraphLibrary`
> - Smokes `AimOffsetSmoke`, `GrenadeAnimSmoke`, `SniperSmoke`, `WarmIdleSmoke`, `DeathCamSmoke`, `BlizzardWalkSmoke` (storm upper body in a blizzard); weather idles / warming rules: [survival-cold-heat](survival-cold-heat.md)
>
> Moved verbatim from the old `docs/port/HANDOFF.md` (split of 2026-10-19; nothing was dropped). [HANDOFF](../port/HANDOFF.md) | [system docs index](README.md) | [CHANGELOG](../port/CHANGELOG.md)

## OperativeAnimInstance (old §5)

- `Characters/OperativeAnimInstance` — state for AnimBP + native locomotion blend (proxy `Evaluate`) until the user's
  graph exists (`bUseNativeLocomotion`). Godot `locomotion_controller.gd`.

## Art: operatives, enemies, placeholder meshes (old §6)

- Operatives (user art, 2026-09-30): BP_Operative uses the user's `/Game/Post_Apo_Survivor` mesh (UE4 mannequin rig) and
  the `/Game/RifleAnims` pack. `Scripts/Editor/setup_operative_rifle_animation.py` makes the two UE4_Mannequin_Skeleton
  assets compatible, builds ABP_Operative's graph with `UOperativeAnimGraphLibrary` (editor module: stand / crouch / aim
  blend spaces by Direction + speed axis, prone = crouch set for now, cached locomotion, upper-body Layered blend from
  spine_01 with the pack's "Fire" montage slot; fire montages and the reload clip stretched to the reload time are
  played by `UOperativeAnimInstance`) and sets `AOperativeCharacter::SquadOutfits` (hoodie + pants: base / _Inst_2nd /
  _Inst_3rd for commander / engineer / medic-sapper). Re-running keeps hand edits: the graph is built only while empty (`CountAnimGraphNodes`; CODEX_REBUILD_ANIM_GRAPHS=1 forces it), clips / outfits only while unset.
  The old Explorer clips were deleted by the user; the M16 offset on hand_r is still the Explorer one.
- Enemies (user art, 2026-09-30): `Scripts/Editor/setup_enemy_animation.py` builds `/Game/Characters/Enemies/<Type>/`
  ABP_Enemy_* (graph: idle / walk / run sequence players + full-body slot; clips on the class defaults) and BP_Enemy_*
  for HOUND = Combat_Dog, BRUTE = Mutant_monster_1, FROSTBITTEN = mutant_monster_2, CUTTER = Biochemical_Monster_2 (ue4
  set, the only one with a death clip) — the mapping follows the clip names in Godot resources/enemies/anims/*.tres.
  `UEnemyAnimInstance` plays attacks / hits / the pounce / the held death; `ACodexTacticsGameMode::EnemyClasses` spawns
  them. The Blueprints own their look (user decision 2026-09-30): capsule, mesh offset / rotation / scale are authored in
  BP_Enemy_* (the script wrote the Godot per-type values once — hound / cutter 35/60 cm, frostbitten 40/90, brute 60/120,
  model scale 0.8 / 0.85 / 1.0 / 1.4 — and tags the CDO `LookAuthored`); `AEnemyCharacter::ApplyArchetypeDefaults` only
  sizes the C++ placeholder body. Stats stay Godot balance data. Re-running the script keeps every hand edit. With a death
  clip the body stays `DeathDecayDelay` 5 s (Godot death_decay_delay), otherwise 2 s.
- Placeholder meshes (engine shapes) for barrels, barricades, mines, quest objects; the user will replace them.
- The user reported the character looks "перекручено" (twisted) — not investigated yet (ask what exactly: spine /
  hands / pose), see §8.

## ABP: left-hand IK, aim offsets, grenade throw (old §6)

### ABP: left-hand IK (the user adds the node; C++ side done 2026-10-07)

Why: the M4 cover pack's clips hold their own M4 differently in the right hand — on our rifle (`m16_01`, rigid on
`hand_r`, offset tuned for RifleAnims) their left hand ends 38-42 cm off the handguard (RifleAnims: ~7 cm). The rifle
mesh has the socket `LeftHandGrip` (weapon-mesh space loc (-5.8, 4.4, 54.5), rot P 58.7 / Y -85.2 / R -170.8 = the
AS_Rifle_Aim left hand; `Scripts/Editor/add_weapon_left_hand_socket.py`). `UOperativeAnimInstance` exposes
`LeftHandIKOffset` (the grip in **hand_r bone space**, ≈ (-26.9, 9.1, -1.1)), `LeftHandIKRotation`, `LeftHandIKAlpha`
(smoothstep 0↔1 over `LeftHandIKBlendSeconds` 0.15 s; 0 with no grip socket / melee weapon, weapon hidden, reloading,
grenade throw and hit reaction windows, vaulting, dead, prone — crawl clips put the hand on the ground, prone aim
already holds the grip at 8-9 cm), `bLeftHandIKGripValid`, toggle `bLeftHandIK`. Graph in `ABP_Operative` (and Rifle2 if used):

1. AnimGraph: after the `FullBody` slot node (and after `UpperBody`, whichever is last), before Output Pose (after an
   Inertialization node, if one is added later): a **Two Bone IK** node.
2. IK Bone: `hand_l` (the chain hand_l → lowerarm_l → upperarm_l follows).
3. Effector: Location Space = **Bone Space**, Effector Target = **hand_r**; expose `Effector Location` as a pin ← the
   anim variable `LeftHandIKOffset`.
4. Joint Target: Location Space = **Parent Bone Space**, Joint Target = **lowerarm_l**, location (0, 0, 0) (the elbow
   stays where the clip has it); if the elbow flips, (0, -30, -30) in `upperarm_l` space.
5. Allow Stretching off, Take Rotation from Effector Space off, Maintain Effector Rel Rot off.
6. Alpha Input Type = Float Value, `Alpha` pin ← `LeftHandIKAlpha`.
7. Optional (hand rotation): a **Transform (Modify) Bone** on `hand_l` after it: Rotation Mode = Replace, Rotation Space =
   Bone Space (relative to hand_r is not offered: use World Space with a computed rotation later if needed), Alpha ←
   `LeftHandIKAlpha`; start without it.
**2026-10-07 PIE feedback, diagnosed:** (A) the user's node reads `LeftHandIKAlpha` but its **Effector Location pin is NOT
bound to `LeftHandIKOffset`** (the asset references no such variable) — with the default (0,0,0) in hand_r bone space it
pulls the left hand onto the RIGHT hand (grip / magazine). CoverCrouchSmoke measures it: final hand_l -> target 21.6 cm
with alpha 1 vs 36.8 cm with alpha 0 ("the node moves the hand but not onto the target"). Bind the pin. (A2) the user's
socket on the handguard (loc (2.45, 0.04, 73.3) in mesh space) is 72-74 cm from `upperarm_l` on the pack's fire stances,
the Two Bone IK chain reaches 57 cm: the C++ slides the target back along the barrel into 95 % of reach
(`LeftHandIKReachFraction`, `LeftHandIKMaxSlideCm` 40, `LeftHandIKSlideCm`; ~29 cm = the receiver). To reach the handguard
itself use a **FABRIK** from `clavicle_l` instead (reach 74 cm, the socket 68.5 cm away) and set the ABP class default
`LeftHandIKChainRoot = clavicle_l`. (B) the straight arm in the cover idle came from the unbound pin (target = hand_r):
with the pin bound (user, 14:06) the IK is ON in every pose where the right hand holds the rifle (cover idle /
look-around / shimmy / enter / stance switches included); off only for reload, grenade, hit reaction, vault, death,
hidden weapon, prone and clips listed in `LeftHandIKFreeClips` (none needed: no pack clip puts the left hand on the
wall). A target still beyond reach after the slide fades the alpha over `LeftHandIKFadeCm` 15 cm (`ReachFade`,
`LeftHandIKExcessCm`). Predicted (read-only commandlet, the user's socket): with the Two Bone IK chain (57 cm) every
cover pose is reachable after a 26-37 cm slide standing / 6-21 cm crouched — the standing hand lands on the receiver,
behind the handguard; with a FABRIK from clavicle_l (73 cm) the slide is <= 12 cm standing and 0-1 cm in the fire
stances: **switch to the FABRIK for a hand on the handguard**. `CodexTactics.LeftHandIKSmoke` measures every pose with
the real ABP (final hand -> target, arm extension, hand on the wall).
Debug: `Codex.Debug.LeftHandIK 1` draws red = socket, green = IK target, yellow = hand_l, blue = the chain root.
FABRIK settings (alternative to Two Bone IK): Tip Bone `hand_l`, Root Bone `clavicle_l`, Effector Transform Space = Bone
Space, Effector Target = `hand_r`, Effector Transform pin <- Make Transform (Location = `LeftHandIKOffset`), Effector
Rotation Source = Keep Local Rotation, Precision 1, Max Iterations 10, Alpha <- `LeftHandIKAlpha`.
Note: the IK puts the WRIST (hand_l origin) on the socket; the palm lies ~8 cm further along the hand — place the socket
where the wrist should be (a little behind / below the palm's spot on the handguard).
The `ik_hand_*` bones do not help: in the M4 clips `ik_hand_l` equals `hand_l` (they follow the pack's own hand).

### ABP: aim offset (the user adds the node; C++ side done 2026-10-07)

Why: a hound running at the operative is LOWER than his eyes, but every clip aims level. The tracer already flies from the
muzzle to the target's capsule centre (`ShootAtTarget` / `ShootAtObject`: `End = Target->GetActorLocation()`), so the shot's
geometry was right; only the pose was level. The user's asset `/Game/RifleAnims/Animations/AimOffsets/AO_Idle/AO1D_Rifle_Idle`
(read-only commandlet 2026-10-07): class `AimOffsetBlendSpace1D`, skeleton `UE4_Mannequin_Skeleton` (the operative's), ONE axis
**"Pitch" -90..90** (grid 4, snap on), 3 samples `AS_Rifle_Idle_FwdDown` (-90), `FwdCenter` (0), `FwdUp` (+90) = one-frame
clips, **additive `AAT_RotationOffsetMeshSpace`**, ref pose = frame 0 of `AS_Rifle_Idle` (also the asset's preview base pose).
So: + = up, - = down; only mesh-space rotation offsets, applied on top of whatever pose comes in. Interpolation is linear
between 0 and +-90 only: 18 deg = 20 % of the FwdDown pose - if the visible lean is too weak, move the samples in the AO asset
(e.g. to +-45 and clamp `AimPitchClampDegrees` to 45), not the C++.

C++ (`UOperativeAnimInstance::UpdateAimOffset`, pure rules `AimOffsetRules`): `AimPitch` (deg, + up / - down; eased by
`AimPitchInterpSpeed` 10), `AimPitchTarget` (raw), `bHasAimTarget`, `AimOffsetAlpha` (0..1 over `AimOffsetBlendSeconds` 0.2 s),
`bAimOffsetActive`, `AimPitchClampDegrees` (90 = the AO range), `bAimOffset`, `bAimOffsetInCover`. The pitch is measured from
`AOperativeCharacter::GetMuzzleLocation()` (the stance's muzzle height: stable, it does not move with the pose the AO bends) to
`AOperativeCharacter::GetAimTargetPoint()` = the target's capsule centre (the tracer's end, a blind shot's silhouette): the
target of a shot fired within `AimTargetHoldSeconds` (1 s: covers turn-based shots and targeted shots at barrels / mines), else
`CurrentCombatTarget` (manual priority, Commander Mode target or the closest visible enemy; all of them end there). No target:
pitch eases to 0. Alpha is 0 while reloading, in the grenade-throw / hit-reaction windows, vaulting, sprinting, prone, dead,
with a melee weapon or a hidden weapon (and in cover when `bAimOffsetInCover` is off).

Graph in `ABP_Operative` (and Rifle2 if used):
1. AnimGraph: after the last slot node (`FullBody`, then `UpperBody`) and BEFORE the left-hand Two Bone IK node (which stays
   last): the AO is additive mesh-space, so it must see the final pose incl. the cover clips that play on FullBody, and the
   left-hand IK then re-pins `hand_l` onto the handguard of the bent rifle. Order: ... -> FullBody -> UpperBody ->
   **Aim Offset Player** -> Two Bone IK (hand_l) -> Output Pose.
2. Add the node: right-click -> *Aim Offset Player* (or drag `AO1D_Rifle_Idle` from the content browser into the graph);
   Aim Offset = `AO1D_Rifle_Idle`. Connect the incoming pose to its **Base Pose** input.
3. Expose the pins (Details -> tick the pin boxes or Pin Options): the axis pin **X** (the Pitch axis; for a 1D asset the node
   offers X only) <- variable `AimPitch`; **Alpha** (Alpha Input Type = Float Value) <- `AimOffsetAlpha`. Nothing else
   (Play Rate 1, Use Blend Space Settings off is fine).
4. Test in PIE: a hound running at him from the open - the barrel dips as it comes close; a target on higher ground lifts it.
   Test both, then cover: the M4 cover fire poses get the offset on top (corner shot at a lower enemy). If a cover clip twists
   the spine oddly, untick `bAimOffsetInCover` (ABP class defaults) - alpha fades to 0 in cover.
5. If the legs / pelvis shift (the AO samples rotate all of the spine chain from pelvis): use **Layered blend per bone**
   instead - Base = the pose, Blend Pose 0 = Aim Offset Player on the same pose, Mesh Space Rotation Blend ON, branch filter
   `spine_01`, Blend Depth 0, Alpha <- `AimOffsetAlpha`; X <- `AimPitch` on the AO node (its own alpha 1).
Caveats: the AO needs the node's base pose to be the same skeleton (UE4_Mannequin_Skeleton - it is); on a pose that is not the
standing idle (crouch, cover) the offset is the same rotation, so check the crouched fire stance too; the node does nothing
in the editor preview until the PIE variables feed it. Checks: `CodexTactics.Anim.AimOffset.PitchToTarget / AlphaRules`,
`CodexTactics.AimOffsetSmoke` (hound low 6 m / high 3 m, hold, sprint fade).

**ABP: 2D aim offset AO_Rifle_Aim (yaw + pitch; user request 2026-10-07; C++ done, the user swaps the node).** Asset
`/Game/RifleAnims/Animations/AimOffsets/AO_Aim/NotUsed_AimOffset2D/AO_Rifle_Aim` (read-only commandlet 2026-10-07):
`AimOffsetBlendSpace`, axis 0 **"Yaw" -180..180**, axis 1 **"Pitch" -90..90**, 15 samples `AS_Rifle_IdleAim_{Fwd,Left,Right,
BackL,BackR}{Center,Up,Down}` - **RightCenter at Yaw +90, LeftCenter at -90** (checked on the sample poses: in RightCenter the
rifle line hand_r -> hand_l turns 90 deg to the character's right), so **+AimYaw = to his right** (UE yaw, clockwise from above),
same sign as the C++. Additive mesh space on `AS_Rifle_Aim`, skeleton UE4_Mannequin_Skeleton (the operative's).
C++: `UOperativeAnimInstance::AimYaw` (deg, + right, eased by `AimYawInterpSpeed` 12, clamped to +-`AimYawClampDegrees` 60;
`AimYawTarget` raw) = the angle from the CURRENT POSE's aim direction to the target: in the corner hold the corner fire
stance's lean-out direction (`CoverFacingRules::CornerAimDirection`, `AOperativeCharacter::CornerAimOutwardDeg` = 100 deg past
the along-wall line, measured on the real barrel of `cvr_std_fire_idle_L`: 100-104 deg), else the barrel's yaw (actor yaw +
`BarrelYawOffset` - the twist already applied). 0 in the plain cover idle and without a target. Alpha = `AimOffsetAlpha` (same
rules as the pitch). **`bAimOffsetYaw`** (ABP Class Defaults, default OFF): tick it once the 2D node is in - only then does the
gameplay count on the upper-body twist (the shot is allowed once the rest of the angle is within `AimConeDeg` 30 and the corner
hold claims targets within 60 + 30 - 5 deg of its aim); with the 1D node keep it off (the shot waits for the pose itself).
**Turn-based (user 2026-10-08):** the AO does not work well there, so `bAimOffsetInTurnBased` (ABP class defaults, default OFF) fades `AimOffsetAlpha` to 0 over `AimOffsetBlendSeconds` and zeroes the AimYaw / AimPitch targets while `UTurnBasedCombatSubsystem::IsActive()`; real time / tactical pause keep it and it eases back in. `bAimOffsetTurnBasedOff` (read-only) shows the state; `GetAimTwistLimitDeg` reports 0 then, so the shot cone and `FaceAimAt` rely on the body turn, not the twist (`GetAppliedAimYaw` is x alpha = 0).
Steps in `ABP_Operative`:
1. Select the existing **Aim Offset Player** node (AO1D_Rifle_Idle) -> Details -> Aim Offset: **AO_Rifle_Aim** (or delete it
   and drag AO_Rifle_Aim in at the same place). Keep it where it is: after `FullBody` -> `UpperBody`, **before the left-hand
   FABRIK / Two Bone IK** (which stays last, so `hand_l` re-pins on the twisted rifle). Base Pose <- the incoming pose.
2. Pins: **X (Yaw) <- `AimYaw`**, **Y (Pitch) <- `AimPitch`**, **Alpha <- `AimOffsetAlpha`** (Alpha Input Type = Float Value).
3. Class Defaults: tick **bAimOffsetYaw**; leave `AimYawClampDegrees` 60 (lower it if the twist looks too strong).
4. PIE: a hound running at his side while he holds a corner - the upper body turns to it up to 60 deg, beyond that he steps
   off the wall and turns his whole body; tracers always leave along the barrel (`CodexTactics.CornerHoldSmoke` logs the angle
   between barrel and target for every shot). If the legs twist, use the Layered-blend-per-bone variant of step 5 above.
   Back to the corner (bug hunt 2026-10-18): once off the wall, an enemy WAITING right at his side round the wall's end
   (within 3 m, covered by the corner stance, no line of fire from where he stands, not moving -
   `CoverFacingRules::ClassifyOpenShotThreat` / `OpenShotThreatsKeepHimOut`) sends him back to the corner hold, unless
   another enemy still pins him in the open (one he can hit from there, one the corner does not cover, one on the move);
   enemies within 2 m are seen without a line of sight (`SightRules::SquadTouchRevealCm`).
Keeping the 1D node: nothing to do (`bAimOffsetYaw` off, `AimYaw` is simply unused). Checks:
`CodexTactics.Anim.AimOffset.YawToTarget`.

**ABP: grenade throw (user clips 2026-10-07).** The user's `/Game/Animations_Grenade/Grenade_01root_root_{Walk,RunAnd,Crouch,Prone}...Throw..._UE` clips (UE5 `SK_Mannequin` of the M4 pack, already compatible with the operative skeleton; in place, no root motion, no notifies; 2.04 / 2.96 / 2.54 / 2.75 s) play on `UpperBodySlot` = "Fire" (the graph's Layered blend per bone from spine_01, so the legs keep the locomotion; the full-body slot is not used). `Scripts/Editor/setup_operative_grenade_animation.py` assigns them to `GrenadeThrow{Walk,Run,Crouch,Prone}Animation` of ABP_Operative and ABP_Operative_Rifle2 (only while empty; CODEX_RESET_GRENADE_CLIPS=1 overwrites). Release = the throwing hand's (hand_r) peak speed, measured per frame: walk 1.28 s, run 1.73, crouch 0.89, prone 1.14 -> `GrenadeThrow*ReleaseSeconds` (anim instance class defaults, <= 0 = 70 % of the clip); the anim instance hands it to `AOperativeCharacter::GrenadeReleaseSeconds`, `GrenadeSubsystem::ThrowAt` passes `GrenadeRules::ReleaseDelay(duration, release)` to the grenade (hidden in the hand until then). Blend 0.15 in / 0.2 out (`GrenadeThrowBlendIn/OutSeconds`). The left-hand IK and aim offset are blocked for the whole clip (`LeftHandIKBlockSeconds`). Clip choice: prone / crouch by stance, else run when sprinting or > 300 cm/s, else walk. Check: `CodexTactics.GrenadeAnimSmoke`. Eyeball in PIE: the prone clip is a lying-down throw layered over the crawl from spine_01 (reload uses FullBody when prone).

## ABP: knockdown, hit reactions + death, Female Soldier body + sniper set (old §6)

**ABP: knockdown (Sprint 14, 2026-10-08).** Nothing to add to the graph: `UKnockdownComponent` plays `/Game/Animations_KnockDown/AM_Knocked_*` (held), `AM_Revive_*` and `AM_Death_*` on the existing `FullBody` slot and stops `UpperBody`; `UOperativeAnimInstance::bKnockedDown` is available for a graph state if wanted. The UpperBody slot comes after FullBody in the graph, so the C++ blocks every upper-body clip while knocked down. Eyeball in PIE: Death_Back's start pose differs from Knocked_Back's end (pelvis ~19 cm lower, mirrored sideways; blended 0.2 s).

**ABP: hit reactions + death (2026-10-08).** Nothing to add to the graph. `Scripts/Editor/setup_operative_hit_animation.py` (only empty fields; `CODEX_RESET_HIT_CLIPS=1` overwrites) set ABP_Operative / ABP_Operative_Rifle2 Class Defaults: `HitStandAnimation` = `MM_HitReact_Front_Med_01`, `HitCrouchAnimation` = `MM_HitReact_Front_Lgt_02`, `PistolHitAnimation` = `MM_HitReact_Front_Lgt_01`, new `HitBackAnimation` = `MM_HitReact_Back_Med_01` (all `/Game/Characters/Mannequins/Anims/Rifle/HitReact`, UE5 SK_Mannequin, in place, 0.7-0.87 s); `HitProneAnimation` stays `Crawl_Hit_F`. They play on `UpperBodySlot` "Fire" (layered from spine_01: legs keep walking), the prone one on FullBody; tunables `HitReactionMinIntervalSeconds` 0.8, `HitReactionMinDamage` 1, `bHitReactionInCover` false (cover keeps its pose), `HitReactionBlendIn/OutSeconds` 0.08 / 0.2; left-hand IK + aim offset are off for the clip (`LeftHandIKBlockSeconds`). Deaths: `DeathStandAnimations` / `DeathCrouchAnimation` stay EMPTY on purpose — the pack's `MM_Death_*` end half-way down (pelvis 83-88 cm, measured) — so a standing / crouched death falls with the knockdown `AM_Knocked_Back` / `AM_Knocked_Front` (side of the killing blow) held as the corpse pose (`UKnockdownComponent::PlayDeathFall`); downed -> `AM_Death_*`, prone -> `Crawl_Death01`. Assign real lying death clips to the arrays to override.

### ABP: Female Soldier body + sniper rifle set (user request 2026-10-09)

**Body.** The Medic-Sapper (and only she) wears `/Game/Female_Soldier/Mesh/SK_Female_soldier`: `ACodexTacticsGameMode::SquadRoster[2].BodyMesh`
-> `AOperativeCharacter::ApplyBodyMeshOverride` before BeginPlay (BP_Operative untouched; the BP's index-based material overrides are
dropped, `SquadOutfits` / `RoleColorMaterialSlot` find no Post_Apo slot names on her, so she keeps the pack's own materials). Same
`ABP_Operative`. Skeleton facts (`Saved/Logs/inspect_female.json`, read-only commandlet): `Female_Soldier_Skeleton` = UE4 mannequin, 68
bones, the operative's Post_Apo UE4 skeleton minus `Backpack_01..03`, **0 parent differences**; ref pose head 159.6 vs 165.5 cm, hand_r
rotation within 0.5 deg, feet at Z 0 -> the BP's mesh offset (-87.6) fits (SniperSmoke: mesh origin 2.4 cm above the capsule bottom).
So the M16 offset on hand_r, the left-hand FABRIK from clavicle_l, the aim offset, cover, knockdown and death clips run on her by bone
name like on the men. The pack has **no UE5 variant** (only `Separated_Mesh` parts and UE4 ThirdPerson demo clips), the full mesh was
taken. `Scripts/Editor/setup_operative_sniper_animation.py` made `Female_Soldier_Skeleton` compatible with the Post_Apo skeleton and with
everything it lists (RifleAnims, Crawl A / Rifle / Pistol, Characters Manny, M4 Manny 196 = cover / grenade / knockdown, Sniper_Animation
SK_Mannequin), and Post_Apo with her (pack assets, saved in the main checkout through the junction). Runtime playback does not need the
flags (bone-name remapping); they are for the editor (preview / pickers).

**Sniper rifle.** `/Game/Data/Weapons/DA_Weapon_sniper_rifle` (`EWeaponHandling::SniperRifle`, `HandMesh` = the M16 model as a stand-in:
swap the model there, `bOverrideHandMeshTransform` + `HandMeshAttachTransform` for its grip offset, `HandMeshMuzzleOffset` when it has no
"Muzzle" socket); numbers in `weapons_tuning.json` "sniper_rifle" (Wave Editor). Only the Medic-Sapper carries it
(`FSquadMemberSpawn::ExtraWeapons`, `AOperativeCharacter::AddArsenalWeapon`: right after the M16, so X goes M16 -> sniper -> pistol...).
Rules (`Combat/SniperRules`): fires only kneeling (crouched) or prone and standing still; a direct order (priority target, Ctrl + click,
planned / turn-based shot) while moving stops her (`SniperOrderHoldSeconds` 3 s: the formation does not pull her away), standing she
kneels by herself, the shot waits for the kneel clip; the automatic fire of the postures does NOT stop a player's move order (user decision 2026-10-09, confirmed) (no shot
while she walks; once she stands still she kneels and fires); prone stays prone; standing idle with the rifle is allowed. Ctrl + click
objects wait for the kneel (`SniperPendingObject`, `SniperPendingShotSeconds` 6 s). Turn-based: standing the attack costs `StanceAPCost`
+ `AttackAPCost` (`UTurnBasedCombatSubsystem::GetActiveAttackCost`); without the AP the attack mode is not entered and AttackCell is
refused with a feed line; with it she kneels on the grid (stance AP spent) and fires. Commander Mode never stands a sniper up
(`SniperRules::AutonomyStance`). The shot cone uses the body's forward (the sniper clips aim along it), not the stand-in barrel.

**Clips** (`UOperativeAnimInstance` class defaults, `[stand, knee, prone]`, filled by `setup_operative_sniper_animation.py`, only empty
fields, `CODEX_RESET_SNIPER_CLIPS=1` overwrites): `SniperIdle` AS_Stand/Knee/Prone_Aim_Idle; `SniperAimStart` AS_Stand_Aim_Start (relaxed ->
aim, when she comes to rest standing), AS_Knee_Aim_Start (stand -> kneel, pelvis 95 -> 41 cm, 0.97 s), AS_Prone_Aim_Idle_Start (stand ->
prone, 2.63 s); `SniperAimEnd` AS_Stand_Aim_End, AS_Knee_Aim_End, AS_Prone_Aim_Idle_End (rise); `SniperFire` AS_*_Aim_Fire (0.5 s;
stand unused); `SniperBoltCycle` AS_*_Aim_Bullet_Reload (1.73 / 1.73 / 2.63 s, after a shot while rounds remain); `SniperReload`
AS_*_Aim_Magazine_Reload (3.3 / 3.3 / 4.0 s, stretched to the reload time); `SniperHitReact` AS_*_Aim_HitReact. **Nothing was added to
the AnimGraph**: the C++ layer (`OperativeAnimInstanceSniper.cpp`) plays them on the existing `FullBody` slot while the rifle is in her
hands, she stands still (< `SniperStillSpeed` 20 cm/s) out of cover, alive, not knocked down / vaulting / sprinting / carrying; walking
keeps the normal locomotion (no sniper walk clips). Knee <-> prone has no pack clip (the loops cross-blend 0.25 s). `bSniperPose` /
`bSniperWeapon` are exposed for your own graph states; `bUseSniperClips` turns the layer off. Left-hand IK stays on in the prone sniper
pose (off during the bolt); the rifle aim offset fades out in the sniper poses unless `bSniperAimOffsetWired`.
**Aim offsets (optional, your graph):** the script built `/Game/Characters/Operatives/Sniper/AimOffsets/AO_Sniper_Stand|Knee|Prone`
(the pack's Offset_F/L/R/U/D poses copied as additive mesh-space rotation offsets on the F pose; axes Yaw -180..180 / Pitch -90..90 like
AO_Rifle_Aim; the poses are only ~10 deg off centre (prone yaw ~14), so the samples sit at +-10 / +-14 and the offset saturates there).
To use them: Aim Offset Player node -> Details -> expose the **Blend Space** pin and bind it to `ActiveAimOffset` (= the stance's sniper
AO in a sniper pose, else `RifleAimOffset` = AO_Rifle_Aim), then tick **bSniperAimOffsetWired** in Class Defaults.

## Interaction clips (old §6)

### Interaction clips: crate open / search, pickup after a disarm, cold idle variant; the disarm walk-up (game designer 2026-10-15)

**Pack:** `/Game/Polygonmaker/Animations/Interactions/UE4/` (untracked user content in the MAIN folder; the agents worktree got a junction
`Content/Polygonmaker` like the other packs - not committed). **Skeleton check (read-only editor query, `setup_operative_interaction_animation.py`
log):** the clips sit on the pack's own UE4 mannequin `SK_Mannequin_Skeleton` (68 bones) = the same rig as the operatives: Post_Apo
`UE4_Mannequin_Skeleton` (same 68 + Backpack_01..03, reference pose 0 deg difference on every key bone) and `Female_Soldier_Skeleton` (same 68;
reference pose 2-13 deg local rotation apart, evaluated clips match within a few cm: pelvis identical, hands 45 / 54 cm vs 50 in the pack). So they
are used DIRECTLY by bone name (UE 5.8 skeleton remapping; `Montage_Play` does not check the compatible-skeleton list - nothing added to the user's
skeletons). No retarget. **Check by eye:** the Female Soldier body in these clips (the roster uses the regular operative for all three now).
**Montages (Claude-owned):** `Scripts/Editor/setup_operative_interaction_animation.py` (commandlet, idempotent, `CODEX_RESET_INTERACTION_CLIPS=1`
recreates) makes `/Game/Characters/Operatives/Anims/Interactions/AM_Interact_{ChestOpen,ChestSearch,PickupGround}` (slot FullBody) and `*_Upper`
(slot `Fire` = ABP_Operative's UpperBodySlot, read from the ABP), blend 0.2 / 0.25 s. **ABP: nothing changed** - the FullBody slot and the existing
upper-body layered blend from spine_01 (slot "Fire") play them; `UOperativeAnimInstance` loads the montages / the variant clip by path when its
fields are empty (like the prone dive clips). Polish later in the montages (sections, notifies) or by assigning own montages in the ABP class
defaults (`ChestOpenClip` / `ChestSearchClip` / `PickupGroundClip`: FullBody, UpperBody, PlayRate, FullBodyStartSeconds; `ColdIdleVariants`).
**What plays when (`InteractionAnimRules::ChooseLayer`: standing -> full body, crouched -> upper body with the legs in the crouch, prone -> nothing
yet).** **Rev 2026-10-19 (game designer play-test: "the loot window first, the animation after"; "the mine disappears first, then he bends down"):
ANIMATION FIRST, THEN THE EFFECT, everywhere.** Each clip has a data value `FOperativeInteractionClip::EffectFraction` (ABP class defaults: ChestOpen
0.9, ChestSearch 0.5, PickupGround 0.5) = its effect moment as a fraction of the whole clip; `InteractionAnimRules::GetEffectDelay(length, start,
rate, fraction)` turns it into seconds from the clip's (re)start; `UOperativeAnimInstance::PlayInteractionTimed` returns {played, seconds, effect
delay}. (1) **crate** (`ALootCrateActor`, session `ELootSessionState` Closed -> Opening -> Open -> Searching): click -> he walks up, `StartOpening`
faces it, `AM_Interact_ChestOpen` (3.0 s); the loot window opens at its lid-open moment (2.7 s; no clip: the crate's `OpenSeconds` 1.8); while it
opens nothing can be taken (`RequestSearch` refused) and the window is not there; opener gone / knocked down -> no window. (2) **Take / Take All**
(`UInteractionSubsystem::LootItem` / `LootAll` -> `ALootCrateActor::RequestSearch`): `AM_Interact_ChestSearch` (3.17 s) starts (restarts for every
take), the window shows **"Searching the crate..."** with every button (and the drag of a line) off, the items move into his kit only at the clip's
grab (1.58 s; no clip: `SearchFallbackSeconds` 0.6) - then `ItemLooted.*`, the feed line (`HandleSearchFinished`), the list refreshes (Take All:
the window closes). The collector walked off / went down before the grab -> nothing moves ("search interrupted"). Closing the window during the
search: the search still finishes. (3) **successful mine / PMC wire disarm** (`AInteractableActor::BeginDisarmPickup` -> `CompleteDisarmPickup`):
`AM_Interact_PickupGround` by the stance he had BEFORE the crouched work: standing -> he stands up again and the full-body clip starts at
`FullBodyStartSeconds` 1.0 (the low pose; the grab 0.43 s later), crouched -> upper body (the grab at 1.43 s), prone -> none (at once). Until the
grab the trap STAYS, harmless (`IsAwaitingPickup`: the mine's `Detonate` / the wire's `Tick` / `Trip` do nothing, no menu); at the grab: the
"disarmed" feed line (`AttemptDefusal` keeps it back: `bDeferDefusalSuccessLine`), the mine into a kit / the grenades, `TripwireDisarmed`, the actor
goes. A cut clip (knockdown, order) does not stop it: the trap still goes at the grab time. The squad's own wire (prone) -> at once. **Tactical
pause** (time stands still): the opening / the search / the pickup is QUEUED (`EInteractionClipDispatch::WaitForRelease`) and plays when the pause
ends - the old rule opened the window at once with no clip (effect before cause). A click on a crate in the pause still relocates it (unchanged).
**Root cause of "window first":** the designer's log (`CodexTactics.log` 2026-10-19 06:43) shows the open clip started 2.75 s before the window,
so the open order held in real time; what read as effect-first was (a) Take / Take All moved the items AT the click and the search clip played
afterwards (purely cosmetic), with the window usable during it, (b) the crouched commander's upper-body open clip barely reads, (c) the
tactical-pause path opened the window instantly without a clip.
(4) **cold idle variant:** inside the shivering pose (`ColdIdle`), at the end of each loop clip, `Anim_Regular_idle_cold` (0.83 s x
`cold_idle_variant_loops` 3) replaces the next loop clip with `cold_idle_variant_chance` 0.35, at most once per `cold_idle_variant_cooldown_seconds`
8 (from the pose start / last variant), then the loop goes on (`EWarmClipStep::Variant`). Data: `Content/Data/Survival/tactical_heat.json`.
**Interplay:** an interaction clip hides the weapon (`bHideWeaponInInteraction`), blocks left-hand IK / aim offset, keeps the weather pose and the
sniper idle off; any movement (> 20 cm/s), knockdown, death, cover, vault, carrying or a wrong stance cuts it (0.15 s).
**Disarm walk-up (every level, `TrapApproachRules`):** a click on a spotted live mine / a laid visible wire walks him to a point
`DisarmStandOffCm` 60 from the charge / the closest point of the wire line, offset TOWARDS him (never onto the mine; the walk's goal 15 cm inside,
the move's 25 cm acceptance radius ends it at ~70 cm), he stops and faces it, the menu opens there (`IsInInteractionReach`: <= 70 cm walking,
<= 85 cm stopped). Data per instance / BP: `AInteractableActor::DisarmStandOffCm` / `DisarmReachToleranceCm` (Details -> CodexTactics | Trap).
The mine's trigger (1.6 m) now contains the defuser: the approaching defuser (a double click on a live mine marks him too now) holds a **lease**
once inside the radius until he steps out (+30 cm) - menu, cancel, crouched work and a slip are safe; anyone else still sets it off. Disarm AP in
turn-based unchanged (the wire's 2 AP attempt from 2.2 m, no walk).
**Verify:** `CodexTactics.Characters.InteractionAnim.*` (3: LayerByStance, LootWindowDelay = effect timings, Dispatch = pause queue / search
completion), `CodexTactics.Interactables.TrapApproach.*` (2), `Survival.WarmIdle.ColdVariant`;
LootSmoke, DeployableSmoke, PMCTripwireSmoke, WarmIdleSmoke (rows in §3). Rendered stills: `CodexTactics.InteractionAnimShots` (`UnrealEditor.exe
... /Game/Maps/L_MovementTest -game -RenderOffscreen -ResX=1280 -ResY=720 -nosound -benchmark -fps=30 -ExecCmds="CodexTactics.InteractionAnimShots"`)
-> `Saved/Screenshots/anim_chest_open(_2).png`, `anim_chest_search(_2).png`, `anim_pickup_crouched_upper(_2).png`, `anim_pickup_standing.png`.


## Blizzard walk: the storm upper body (game designer 2026-10-22)

**Request:** "in a blizzard the operatives walk with the arms stretched out - the storm clips of the freezing set, on the UPPER BODY."
**Clips:** `/Game/90+_Weather_Animations/Animations/NEW_version/Storm` (24: `AS_{He,She}_Storm_{1,2}_{1Start,2Loop,3Tranzit,4Loop,5Tranzit,6End}`;
OLD_version `storm-1..3_*` are older takes). Measured read-only by `Scripts/Editor/measure_storm_clips.py` (`Saved/Logs/measure_storm_clips.txt`): all storm clips
stand in place (no root motion; the loops' feet do not move, Start / End take one step), so there is no walk cycle to take the legs from - which is fine, the
designer wants the upper body. Storm_1 loops hold both hands before the face (pack: He 54 / 47 cm in front of spine_03, 45 / 27 cm above it; She 42 / 58, 46 / 44);
Storm_2 crouches lower (pelvis He 85, She 76 cm) and one hand hangs back in its 2Loop -> **Storm_1 loops + transits** are used (Start / End not needed: the layer
blends 0.3 s while he walks).
**Retarget:** as the cold idles (survival-cold-heat "Weather idle poses"): the pack's UE5 Manny skeleton -> IK retarget with the M4 pack's `RTG_UE5Manny_UE4Manny`
("Blend to Source" off in memory, never saved) by `Scripts/Editor/setup_operative_storm_animation.py` -> `/Game/Characters/Operatives/Anims/Weather/Storm/Male/AS_He_Storm_1_*`
(SK_Post_Apo_Survivor_a) and `.../Storm/Female/AS_She_Storm_1_*` (SK_Female_soldier), 8 clips, posture error (head / hands vs pelvis) <= 13 cm
(`Saved/Logs/setup_operative_storm_animation.txt`). Only the copies are saved (re-run: `CODEX_RESET_STORM_CLIPS=1`; `CODEX_STORM_DRYRUN=1` measures only).
**ABP: nothing changed.** `UOperativeAnimInstance` (`OperativeAnimInstanceBlizzardWalk.cpp`) loads the copies by path into the empty class defaults
`BlizzardWalkMale` / `BlizzardWalkFemale` (`FWarmIdleClipSet`; assign own clips there to override) and plays them as dynamic montages on `UpperBodySlot`
("Fire" = the graph's Layered blend per bone from spine_01, the path the grenade / hit / crouched interaction clips use): the legs keep the locomotion. Loop A
x 2-4 (`BlizzardWalkLoopsMin/Max`) -> 3Tranzit -> loop B -> 5Tranzit ... (the weather idles' `WarmIdleRules::NextClipStep`), the first loop random (two operatives
side by side do not mirror). Blend in / out `BlizzardWalkBlendSeconds` 0.3, out `BlizzardWalkInterruptBlendSeconds` 0.15 when the rifle is needed. While it
shows: weapon hidden (`bHideWeaponInBlizzardWalk`, the clips hold both hands before the face; it shows again as the pose blends out), left-hand IK and aim
offset off. Graph hooks: `bBlizzardWalkActive`, `BlizzardAmount`.
**Gate (pure `Characters/BlizzardWalkRules`, tests `CodexTactics.Characters.BlizzardWalk.*`):** blizzard amount = (weather == Blizzard ? 1 - blend : 0) + (next ==
Blizzard ? blend : 0) from `UEnvironmentSubsystem` (`GetWeather` / `GetNextWeather` / `GetTransitionBlend`, per frame) - on at >= 0.5, off below 0.35, so the 45 s
transition starts / ends it half-way (phase 4's `GetWeatherAmount` can replace the formula; the rule takes the amount). Off: indoors (a Closed / Shelter camera zone -
`ACameraZoneVolume::IsShelteredLocation`, shared with the campfire's indoor noise - or a heat source's warm zone; checked every 0.25 s), dead / knocked down,
crouched / prone, not walking (starts above `BlizzardWalkStartSpeed` 40, keeps it down to `BlizzardWalkStopSpeed` 20 cm/s), sprinting or faster than
`BlizzardWalkMaxSpeed` 450 (walk 220 / jog: on), aiming / a shot's aim hold / a held combat target (`GetAimTargetPoint`) / reloading, in cover / vaulting / carrying,
another upper-body clip (fire, reload, hit, grenade, crouched interaction, working device, `LeftHandIKBlockSeconds`), a full-body clip (weather idle, stance
transition, sniper pose, knockdown, vault). `GetBlizzardWalkBlock()` names the reason.
**Check:** `CodexTactics.BlizzardWalkSmoke` (L_MovementTest, own lane via FindClearArena, weather forced by `ForceWeather(..., instant)`): Clear -> 0; Blizzard ->
weight > 0.8 in 0.25 s, both hands > 25 cm before the chest and > 15 cm above it (measured 36 / 50 and 30 / 24 cm; the rifle walk holds hand_l 55 cm out on the
handguard at chest height, so "further forward than the normal walk" is not the measure for the left hand - the pose RAISES the hands +18 cm), feet still swing,
no full-body clip; a shot -> 0 in 0.05 s and stays 0 during the aim hold, back after it; a Closed zone over the lane -> 0; stop -> 0; Clear -> 0; crouch-walk -> 0.
`CodexTactics.BlizzardWalkSmoke clip` (rendered, `-RenderOffscreen -benchmark -fps=30`) writes ~10 s of frames to `Saved/Screenshots/BlizzardWalk/`.
**Eyeball in PIE:** the storm pose with the weapon hidden reads as "shielding the face"; if the user prefers the rifle kept, untick `bHideWeaponInBlizzardWalk`
(the rifle then follows hand_r up to the face) or give the rifle a back socket later.

## Frozen PMC Fighter: the Frostbitten's animation on the Cyberpunk_Explorer_A body (game designer 2026-10-20)

**Skeleton check** (read-only, logged by `Scripts/Editor/create_frozen_pmc_fighter.py` into `Saved/Logs/create_frozen_pmc_fighter.txt`):

| | Frostbitten `mutant_monster_2/base_mesh/SK_base_mesh` | PMC `Cyberpunk_Explorer_A/Meshes/Cyberpunk_Explorer_A_UE5` |
|---|---|---|
| Skeleton asset | `mutant_monster_2/base_mesh/SK_UE4_Mannequin_Skeleton` (UE4 mannequin, 68 bones; modes 9 Animation, 49 Skeleton, 10 AnimationRelative) | `/Game/Characters/Mannequins/Meshes/SK_Mannequin` (Epic UE5 Manny template, 161; 145 AnimationScaled) |
| Mesh bones | 68 | **162**: the 161 + its FBX scene root `SKM_Cyberpunk_Explorer_A` ABOVE `root` - not in SK_Mannequin |
| Names | all 68 also in the PMC | + 93: spine_04/05, neck_02, metacarpals, twist / corrective, weapon_l/r, interaction, center_of_mass |
| Hierarchy | | 12 shared bones with another parent: clavicle_l/r + neck_01 on spine_05 (UE4: spine_03), head on neck_02, 8 finger roots on the metacarpals |
| Reference pose | 177 cm, A-pose, arms ~34 deg down, faces +Y, feet at the origin | 181 cm (helmet), A-pose, arms ~55 deg down, faces +Y, feet at the origin |

So **no direct use and no "Compatible Skeletons"**: `ABP_Enemy_Frostbitten` targets the UE4 skeleton (other hierarchy: bone-name playback would
leave spine_04/05 / neck_02 at rest and drive the shoulders in the wrong parent frame), and a compatible-skeleton link would re-save the user's /
Epic's skeleton without fixing the hierarchy. On top, the pack's mesh is **not compatible with its own skeleton**: `USkeleton::IsCompatibleMesh`
fails on the extra root bone (nothing can merge a second root into SK_Mannequin), so `USkeletalMeshComponent::NeedToSpawnAnimScriptInstance`
never spawns ANY Animation Blueprint on SK_Mannequin for it (T-pose, verified in `PmcFighterSmoke`: "anim none"); the editor logs "The Skeleton
SK_Mannequin is missing bones that SkeletalMesh Cyberpunk_Explorer_A_UE5 needs ... Please save the Skeleton!" at every load and the Layered-blend node
ensures (`AnimNode_LayeredBoneBlend.cpp:91`) on the unmapped bone.

**Route = IK retarget onto Claude-owned assets** (`/Game/Characters/Enemies/PMC/FrozenPMC_Fighter/`, the user's mesh / clips / ABP / skeletons are only read):

1. `Anim/SK_FrozenPMC_Fighter_Skeleton` - built from the pack mesh's own bone tree (`UOperativeAnimGraphLibrary::CreateSkeletonFromMesh` =
   `MergeAllBonesToBoneTree` on a new skeleton, the mesh not re-pointed), SK_Mannequin's translation retargeting modes / slot groups copied by name,
   SK_Mannequin in its compatible list (pickers).
2. `Mesh/SKM_FrozenPMC_Fighter` - a copy of `Cyberpunk_Explorer_A_UE5` on that skeleton (`SetSkeletalMeshSkeleton`); the pack's materials and
   physics asset by reference (editing those material instances still shows on him; a new material SLOT assignment on the pack mesh does not).
3. `Anim/IK_Frostbitten_Source` (21 auto chains, auto FBIK) and `Anim/IK_FrozenPMC_Fighter` (29 chains incl. metacarpals), `Anim/RTG_Frostbitten_To_FrozenPMC`
   (UE 5.8 default op stack Pelvis Motion / FK Chains / Run IK Rig / Root Motion / Remap Curves, chains auto-mapped (fuzzy), target retarget pose
   auto-aligned chain-to-chain to the source - the arm angle difference).
4. `IKRetargetBatchOperation.run_batch_retarget` on `ABP_Enemy_Frostbitten` (include referenced assets): `Anim/ABP_Enemy_FrozenPMC_Fighter` = the same
   18-node graph (idle / walk / run players, upper-body hit layer from spine_01 on slot `UpperBody`, full-body `DefaultSlot`), the same class defaults
   (rates 1.2 attack, run threshold 200, walk / run clip speeds 150 / 400, death decay 5 s) with the clips remapped to the retargeted copies
   `Anim/anim_idle_1, anim_walking, anim_run, anim_attack_1, anim_attack_2, anim_hit, anim_death` (all on the PMC skeleton). Note: the batch returns only
   the clips; the script saves everything new in `Anim/` (the duplicated ABP included).
5. Hit / death / attack are sequences on the ABP class defaults (played as dynamic montages on `DefaultSlot` / `UpperBody` by `UEnemyAnimInstance`), so
   they come with step 4. Knockdown: `KnockdownClipFolder` "" = the Manny originals of `/Game/Animations_KnockDown` (remapped by bone name like on the operatives).

**Checked:** proportions of every retargeted clip on the body x1.00 (torso / legs / arms chain lengths), lowest toe -1.2..2.2 cm; in game
(`PmcFighterSmoke`, fixed 60 fps step): idle 45 cm off the reference pose, toe 0.6 cm above the capsule bottom, walk toe -2.6..8 cm, attack clip on
`DefaultSlot` (the leader loses 12 HP), hit clip, Manny `Knocked_Back` (pelvis 11 cm above the floor), `Death_Back` from the ground, `anim_death` held
standing; no ensure / missing-bone error. Renders: `Saved/Screenshots/pmc_fighter_{idle,walk,attack,hit,knockdown,death_down,death}.png`
(`UnrealEditor.exe ... -game -RenderOffscreen -ResX=1280 -ResY=720 -nosound -benchmark -fps=30 -ExecCmds="CodexTactics.PmcFighterSmoke shot"`).

**For the user to check / decide:** (1) the Frostbitten's MONSTER clips (hunched idle, clawing attacks) on a human soldier is what was asked; a
human set (e.g. the Manny / RifleAnims clips) would be one ABP class-default change. (2) Material variant: the copy uses the pack mesh's default slots
(jacket 4th, arms 2nd, pants 2nd, boots / gloves / helmet 1st); other `M_*_Inst` variants go into the BP's Override Materials. (3) Fixing the pack mesh
(Skeletal Mesh editor -> Skeleton Editing: delete `SKM_Cyberpunk_Explorer_A`, or re-import without the scene root) would let a body share SK_Mannequin
directly; then re-run the batch with the pack mesh as the target and point the BP at it.


## Military Zombie: the Frostbitten's ABP logic with the pack's own clips (game designer 2026-10-20)

**Inspection** (read-only, logged by `Scripts/Editor/create_military_zombie.py` into `Saved/Logs/create_military_zombie.txt`):

| | Finding |
|---|---|
| Skeleton | `/Game/military_zombie_set2/Meshes/base_mesh/SK_Mannequin` - the pack's own copy of the Epic UE5 Manny (161 bones, same names in the same order as `/Game/Characters/Mannequins/Meshes/SK_Mannequin`; no compatible skeletons listed). Not the Frostbitten's (`mutant_monster_2/.../SK_UE4_Mannequin_Skeleton`, 68 bones: all 68 names shared, 93 more and another spine / neck / finger hierarchy here). |
| Meshes | `base_mesh/SKM_base_mesh_set2_1/2/3` and every `mech_separate/*` part are on that skeleton; no stray FBX root bone (root has no parent - unlike Cyberpunk_Explorer_A), so an ABP on it spawns. 180-185 cm, feet at the origin, facing +Y. |
| Variants | `SKM_base_mesh_set2_1/2/3` are three ASSEMBLED bodies, one mesh each (1: helmet + body kit + armor, 2: blouse + armor + hair, 3: bare body); the `mech_separate` parts are the same pieces split out (for custom assemblies: leader-pose components - not needed). `material/set_1/2/3` = three texture sets of the same slots (`MI_<part>_set_N_Inst`; helmet: set 1 only; hair: one). |
| Clips | All on the pack skeleton (no retarget), all IN PLACE (root motion off, the root bone never moves). Lengths: idle_1/3/4/5/6 3.5 / 6.2 / 4.2 / 17 / 8.3 s, walking_1..4 2.6 / 3.9 / 8.4 / 4.5 s (slow shambles: ~20 / 30 / 15 / 25 cm/s at rate 1, from the planted foot's stride), run_1 / run_2 0.70 / 0.73 s (~360 / ~660 cm/s), attack_1..4 3.8 / 2.6 / 4.6 / 2.7 s, hit 2.0, Dying 3.3 (falls forward ~1.6 m), rage 3.4, devour 24.5 (kneels and feeds from 1.5 s to 23 s), transformation 20.5 (standing; twitching 7.5-17.5 s, the arms come up at the end). No notifies. `Demo/` = Manny demo clips (not used). |

**Route (user guidance "use the Frostbitten Animation Blueprint, just swap its animations"):** the Frostbitten graph references no asset - its
sequence players are bound to `IdleAnimation` / `WalkAnimation` / `RunAnimation` and the one-shots are played by `UEnemyAnimInstance` from class
defaults. So `ABP_Enemy_MilitaryZombie` is a **child Animation Blueprint of `ABP_Enemy_Frostbitten`** (same graph, slots `DefaultSlot` /
`UpperBody`, upper-body hit layer from `spine_01`, same C++ logic; the user's ABP is untouched and every later edit of its graph carries over) whose
target skeleton is the pack's skeleton (`AnimBlueprintFactory` with the Frostbitten ABP class as parent - no Asset Override needed, no clip
retarget, the user's skeleton not re-saved: the script reports "pack packages dirtied: none"). Its class defaults hold the zombie clips:

| Role | Clip(s) | When |
|---|---|---|
| Idle loop | anim_idle_1/3/4/5/6, one per instance by the spawn seed (`bSeededIdleVariant`) | standing |
| Idle variety | the same 5 as `IdleBreakAnimations`: every 6-12 s of calm idle another idle clip plays once (never the loop it plays), cross-faded 0.35 s | standing calm (not moving / attacking / hit / knocked down / in the turn-based fight) |
| Devour | `IdleActivityAnimations` = anim_devour, 35 % of the idle breaks | only while it does not hunt the squad (guard / patrol pauses). The game keeps no lasting corpses (enemy bodies decay after 5 s), so it is a random calm activity, not "feeding on a body". |
| Walk | `WalkAnimationVariants` = walking_1..4 with their ground speeds, one per instance (seed) | moving below `RunSpeedThreshold` 200 cm/s: patrols (190), lure investigation |
| Run | `RunAnimationVariants` = run_1 (360) / run_2 (660), one per instance | the charge (2.8 m/s) |
| Attacks | anim_attack_1..4, one at random per blow (x1.2 like the Frostbitten: 3.4 s average vs its 3.6 s) | melee |
| Hit | anim_hit (upper body while moving, full body standing) | damage |
| Death | anim_Dying, held | killed standing; killed while knocked down = Manny `Death_*` |
| Alert roar | `AlertAnimations` = anim_rage x1.2 (2.8 s), standing, facing the alarm | a guard / patrol notices the squad (`BreakPatrol`) |
| Spawn entrance | `EntranceAnimations` = anim_transformation from 14.5 s x1.5 (~4 s), standing | wave / horde spawn (`UWaveSubsystem::SpawnEnemy`), placed ones with `bPlayEntranceWhenPlaced` |
| Knockdown | Epic Manny `/Game/Animations_KnockDown/Knocked_*`, `Revive_*`, `Death_*` (same bone names) | a >= 40 HP hit (Frostbitten rule) |

**New `UEnemyAnimInstance` API (all empty = no change for the other enemies):** `WalkAnimationVariants` / `RunAnimationVariants`
(`FEnemyLocomotionVariant` clip + ground speed), `bSeededIdleVariant`, `IdleBreakAnimations`, `IdleActivityAnimations` + `IdleActivityChance`,
`IdleBreakInterval` / `IdleBreakPlayRate` / `IdleBreakBlendSeconds`, `AlertAnimations` + `AlertPlayRate` / `AlertMaxSeconds`, `EntranceAnimations` +
`EntrancePlayRate` / `EntranceStartOffset` / `EntranceMaxSeconds`; `NotifyAlert()` / `NotifyEntrance()` (return the stand-still seconds),
`IsPerformancePlaying`, `StopPerformance`, `GetIdleBreakClip`, state `bIsPerforming` / `bIsIdleBreak` for graphs. The variants are picked from
`AEnemyCharacter::GetVariantSeed()` at BeginPlay (a deferred spawn updates the anim once before its seed is final: `RepickVariants`).

**Checked** (`MilitaryZombieSmoke`, fixed 60 fps and a rendered 30 fps run): no T-pose (idle 30-68 cm off the reference pose), feet on the
ground (idle toe -2..0.6 cm, walk 0..10, run -1.6..16), idle break + devour seen, walk variant at rate 1.8, roar 2.7 s standing, run variant,
attack clip + damage, hit clip, Manny knockdown (pelvis 11 cm) + `Death_Back`, entrance 3.7 s standing then hunting, anim_Dying held, 0 missing-bone /
ensure lines. Renders: `Saved/Screenshots/military_zombie_{idle,devour,walk,rage,run,attack,hit,knockdown,death_down,transformation,death}.png`
(`UnrealEditor.exe ... -game -RenderOffscreen -ResX=1280 -ResY=720 -nosound -benchmark -fps=30 -ExecCmds="CodexTactics.MilitaryZombieSmoke shot"`).

**For the user to check / decide:** (1) the walks are slow shambles (15-30 cm/s at rate 1) while the Frostbitten's patrol walks at 190 cm/s: the
walk plays at the 1.8x cap and the feet slide; a slower `PatrolWalkSpeed` on `BP_Enemy_MilitaryZombie` (gameplay: patrol coverage) or faster
walk play would fix it. (2) run_2 is a sprint (~660 cm/s) played at 0.6x for the 280 cm/s charge (the feet move slower than the body).
(3) Devour blends out over 0.35 s when the zombie starts to walk (it rises while stepping off). (4) The roar delays the charge by ~2.8 s
(gameplay). (5) The dying clip falls ~1.6 m forward of the capsule (in place clip): the corpse lies in front of where it stood.
(6) Look variants are random per spawn; pin one with `VariantSeedOverride`, or edit `LookVariants` on the Blueprint.

