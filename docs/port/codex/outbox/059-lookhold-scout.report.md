# 059 look-hold scout
Read-only code/filename inspection; assets not opened, no builds/smokes/commits. HEAD at final check: 42d5df5a (main advanced during inspection).
Paths are repository-relative; all asset patterns below end in .uasset; braces enumerate filenames, not verified asset contents.
F/B/L/R = forward/back/left/right; FL/FR/BL/BR = diagonals; yes/no means filename coverage in the stated family only.

## Clip inventory
| Mode / family | F | L/R | B | FL/FR | BL/BR | Path / filename pattern |
|---|---|---|---|---|---|---|
| Walk, mannequin rifle | yes | yes | yes | yes | yes | Content/Characters/Mannequins/Anims/Rifle/Walk/MF_Rifle_Walk_{Fwd,Left,Right,Bwd,Fwd_Left,Fwd_Right,Bwd_Left,Bwd_Right} |
| Jog, mannequin rifle | yes | yes | yes | yes | yes | Content/Characters/Mannequins/Anims/Rifle/Jog/MF_Rifle_Jog_{Fwd,Left,Right,Bwd,Fwd_Left,Fwd_Right,Bwd_Left,Bwd_Right} |
| Walk/jog/run, RifleAnims | yes | yes | yes | no | yes | Content/RifleAnims/Animations/BlendSpaces/Standing_IdleWalkJogRun/AS_Rifle_{Walk,Jog,Run}{Fwd,Left,Right,Bwd,BwdLeft,BwdRight} |
| Crouch, RifleAnims | yes | yes | yes | no | yes | Content/RifleAnims/Animations/BlendSpaces/Crouch_IdleWalk/AS_Rifle_CrouchWalk{Fwd,Left,Right,Bwd,BwdLeft,BwdRight} (also _Aim siblings in Crouch_IdleWalk_Aim) |
| Run/crouch, pistol | yes | yes | yes | yes | yes | Content/Characters/Animations/Pistol/{Male,Female}/M_Neutral_{Run,Crouch}_Loop_{F,LL,RL,B,FL,FR,BL,BR}_Pistol |
| Walk/jog, shotgun | yes | yes | yes | yes | yes | Content/ShotgunLocomotionPack/Animations/{Walk,Jog}/anim_shotgun_{walk,jog}_{fwd,left,right,bwd,45_fwd_left,45_fwd_right,45_bwd_left,45_bwd_right}_loop (matching mode/folder) |
| Crouch, shotgun | yes | yes | yes | no | no | Content/ShotgunLocomotionPack/Animations/Crouch/anim_shotgun_crouch_{fwd,bwd,walk_left,walk_right}_loop |
| Prone/crawl, retargeted | yes | yes | yes | yes | yes | Content/Characters/Animations/Crawl/{Male,Female}/Crawl_Walk_{F,L,R,B,45L,45R,135L,135R}_IPC |
| Walk starts/stops, Rifle2 | yes | yes | yes | yes | yes | Content/RifleAnims/Rifle_2/Walk/M_Neutral_Walk_{Start,Stop}_{F,B,FL,FR,BL,BR,LL,LR,RL,RR}_{Lfoot,Rfoot}_Rifle; loops use same sectors (extra variants exist) |
| Walk/jog starts/stops, shotgun | yes | yes | yes | no | no | Same Walk/Jog folders: anim_shotgun_{walk,jog}_{fwd,bwd}_{start,stop}_{l,r}; lateral {left,right}_{start,stop} |
| Crouch starts/stops, shotgun | yes | partial | yes | no | no | Crouch folder: anim_shotgun_crouch_{fwd,bwd}_{start,stop}; lateral entry crouch_to_walk_{left,right}, no matching lateral stop filename |
| Crawl starts/stops, source | yes | yes | yes | yes | yes | Content/Crawl_MocapAnimPack/Animations/Locomotion_Set/Loco_Start_Stop/{FB,LR,Diag}/Crawl_{Start,Stop}_Walk_{F,B,L,R,45L,45R,135L,135R}_IPC (matching direction folder; non-IPC variants too) |
Crawl retarget folders contain only idle + 8 movement loops; source start/stop clips are not present there. Mannequin rifle Walk/Jog folders contain 8 loops each, no named starts/stops.

## Stealth look-hold: entry points and reuse
Code aliases below: C = Source/CodexTactics/Private/Characters/; P = Source/CodexTactics/Private/Core/.
- P/CodexTacticsPlayerControllerOrders.cpp:398,402: anchor command dispatch requires all selected actors stealthed; nearby Shift-click flow also handles suppression (:411). Generalize command eligibility/precedence for solo explicitly.
- C/OperativeCharacterStealth.cpp:109 SetStealthManualLookTarget rejects !bStealthMode; :189 GetStealthLookPoint prefers manual anchor over auto target, rejecting points within 50 cm.
- C/OperativeCharacterStealth.cpp:134 UpdateStealthLookLock is gated by stealth/alive/not-in-cover; C/StealthModeRules.cpp:178 UpdateLookLock provides target visibility/FOV retention and switching. Keep manual-only reuse separate from any decision to extend automatic threat selection.
- C/OperativeCharacterAim.cpp:90 UpdateCombatFacing; :197-199 look eligibility is stealth-only and yields to combat/peek/grid/explicit facing/suppression/quickfire/sprint/carry/panic/vault/ladder; :245 rotates toward anchor, :255 prevents movement-facing overwrite.
- C/OperativeAnimInstanceStealth.cpp:34 UpdateStealthLayer is crouched-stealth-only (:44); :86 bLookHoldsBody excludes wall attachment; :108 relative velocity heading feeds smoothed directional blending; :183 selects directional gait start.
- C/StealthModeRules.cpp:120 GetDirectionSector quantizes 8 x 45 degrees; :126 GetGaitStartSector uses lead sector when look holds body, otherwise F. Extract neutral look/facing policy and sector helpers rather than enabling stealth pose/material state.
- C/OperativeAnimInstanceStealth.cpp:89-92 clamps residual look twist to +/-45 degrees; :384 applies bone twist. Standing/prone need their own limits/ownership, not a direct crouched-pose copy.

## Ordinary locomotion / existing directional infrastructure
- C/OperativeAnimInstance.cpp:1071 UpdateState; :1084 computes velocity yaw minus actor yaw, smooths Direction; :1093 grid overrides to F. Existing heading-relative direction can drive sideways/backwards motion with an independent look-facing actor.
- C/OperativeAnimInstance.cpp:220,260 UpdateRifle2Locomotion consumes speed/direction; :300,348,361,374 selects sector-specific starts/stops. :264-265 clamps walk sampling and increases rate above walk speed: do not assume this path selects distinct run clips.
- C/OperativeAnimInstance.cpp:1321 GetClip and :1336 UpdateNativeBlend use speed/stance idle-walk-run weights (:1349-1366), not 8-direction clip selection. Check active graph/native route before altering it.
- C/OperativeAnimInstanceShotgun.cpp:133 LoadWeaponAnimSet; :592 UpdateShotgunLayer; :698 StepLayerDirection; :986 BlendWeaponLayer: reusable directional weapon layer already exists.
- C/OperativeAnimInstanceProneCrawl.cpp:38 IsProneCrawlEligible, :55 UpdateProneCrawlLayer, :107 relative-heading calculation, :113 BlendDirections, :180 BlendProneCrawlLayer: 8-direction crawl already exists outside stealth; preserve wounded/full-body action exclusions.
- Blendspace filenames exist: Content/RifleAnims/Animations/BlendSpaces/Standing_IdleWalkJogRun/BS_Rifle; Crouch_IdleWalk/BS_Rifle_Crouch; aim siblings; Content/Characters/Operatives/Anims/{BS_Rifle2_Walk,BS_Rifle_Prone,BS_Rifle_Prone_Aim}.
- Code exposes direction and blend speed axes (OperativeAnimInstance.cpp:1025-1027,1084), but asset axes/samples and active AnimBP wiring cannot be confirmed from filenames. Directional C++ layers are confirmed; blendspace contents are not.

## Gaps, risks, open questions
- No full forward-diagonal coverage in the listed RifleAnims standing/crouch families; mannequin and pistol alternatives have 8 loops. Asset name presence proves neither compatible skeleton nor good blending/foot contact.
- Source shotgun loops and root-motion variants coexist; retargeted Shotgun copies mix suffixes. Verify actual root motion/retarget compatibility before wiring ordinary run/strafe; do not infer from suffix alone.
- Define walk vs jog vs run vs sprint policy: current look branch excludes bSprinting; Rifle2 can speed up walk. Do not silently allow locked-body sprint using walk clips.
- Resolve anchor cancellation on move/order/cover/combat/stance changes, suppression Shift-click precedence, and solo vs formation ownership. Crawl remembered-threat branch (OperativeCharacterAim.cpp:173-176) currently checks stealth look only.
- Determine active per-character AnimBP/blendspaces and weapon sets, then validate 8 headings at walk/run/crawl speeds; this scout intentionally did not inspect binary assets or run gameplay.
