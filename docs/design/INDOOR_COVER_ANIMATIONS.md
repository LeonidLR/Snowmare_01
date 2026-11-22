# Indoor cover animations (Riffle Cover Animation pack)

Status: **approved** - the designer took every recommendation (2026-11-22): Q1 (a) he binds the ABP Aim Offset Player's Blend Space pin
to `ActiveAimOffset`; Q2 (a) Aim/2 at windows as it is; Q3 (a) Aim/2 mirrored for the right side; Q4 (a) PMC fighters indoors use the set
too; Q5 (a) shimmy start / stop / turn clips for walks of 1.5 m and more, shorter side-steps loop only. Branch `claude/indoor-cover`.
Request (designer 2026-11-22): use the pack `/Game/Riffle_Cover_Animation` for moving along and holding walls **inside buildings**
(outdoors keeps the M4 Cover Pack set); standing + crouched; real aiming (aim offsets), grenades and reloads from cover; at a window the
operative crouches under it and rises to aim / fire through it. Later the same set becomes the stealth wall movement and maybe a second
"wall style" for some operatives outdoors, so the choice of set is **data** (context + operative), never a hard-coded "indoor".

The pack is the designer's download, **untracked, never committed** (like `Free_Interaction_Animation`, `SovietHouses`).

## 1. What the pack is (measured headless, `Saved/Logs/inspect_indoor_cover_pack.txt`)

- 97 sequences on the pack's own UE5 Manny skeleton (`/Game/Riffle_Cover_Animation/Demo/Characters/Mannequins/Meshes/SK_Mannequin`),
  30 fps, **root motion off** on every clip, no notifies, nothing additive (the aim "offsets" are full one-frame poses). Plus a demo mesh,
  control rig, physics asset, materials and `DemoMap` - not used.
- **Back to the wall.** The neutral pose faces -Y; `_Start` walks 93 cm forward to the wall and turns round (back to the wall, facing +Y);
  `_Exit` / Stand R `_End` is the reverse. The enemy side is beyond the wall (-Y): every aim pose points the rifle there.
- **Sides.** Pack `L` = the operative's OWN LEFT (the idle head leans 22 cm to his left, `L` walks to his left). The M4 pack is the opposite
  (`_L` = his own right, `OperativeAnimInstance.cpp`), so the mapping onto `ECoverFacing` is swapped: `ECoverFacing::Left` (index 0) <- pack
  `R`, `ECoverFacing::Right` (index 1) <- pack `L`. The retarget script verifies this against the M4 idles (head offset sign) and stops if not.
- **Root travel lives on the root bone** (the pelvis follows it), so in-place loops must be baked (section 3).

| Clip (Stand / Crouch, L / R) | Length s (S-L / S-R / C-L / C-R) | Root travel | Game state |
|---|---|---|---|
| `_Idle` | 1.30 all | 0 | cover idle loop (`CoverStandIdle` / `CoverCrouchIdle`) |
| `_Start` | 0.97 all | 93 cm to the wall, ends back to it | `CoverStandEnter` / `CoverCrouchEnter` (cover entry from the open) |
| `_Exit` (Stand R: `_End`) | 0.80 / 0.97 / 0.97 / 0.97 | 92 cm away from the wall | NEW: leave cover standing still (today: plain blend out) |
| `_To_R` / `_To_L` | 0.53 / 0.53 / 0.90 / 0.90 | 0-9 cm | NEW: side switch in place (today: none / a shimmy turn) |
| `Walk/_Walk` | 1.13 / 1.13 / 1.10 / 1.10 | 125 cm (110 cm/s) stand, 162 cm (147 cm/s) crouch | shimmy loop (`CoverStandMoveForward` / `CoverCrouchMoveForward`) |
| `Walk/_Walk_Start` / `_Walk_Stop` (Stand R: `_Walk_End`) | 0.83 / 0.63 (S-L) | 65 / 47 cm | NEW: shimmy start / stop |
| `Walk/_Walk_Turn` | 1.30 (S-L), 1.23 crouch | 95-111 cm + 90 deg | NEW: reverse direction while shimmying |
| `Aim/_Aim_Start` / `_Aim_Idle` / `_Aim_End` | 0.50 / 1.30 / 0.50 (S-L) | **steps 43-52 cm out past the edge** | corner peek-aim = `CoverStandFireEnter` / `FireIdle` / `FireExit` |
| `Aim/Offset/_Aim_{L,R}{U,M,D}` | 1.00 (one pose) | 0 | 6-pose aim offset on the peek (yaw L/R, pitch U/M/D) |
| Crouch L `Aim/1/*` | 0.63 / 1.30 / 0.63 | 43 cm out past the edge | crouched side peek (same role as above) |
| Crouch L `Aim/2/*` + offsets `{U,D}{L,M,R}` | 0.50 / 1.30 / 0.50 | 2 cm (no step) | **over the top** (section 2) |
| `_Reload` | 1.63 / 1.97 / 1.63 / 1.97 | 0 | reload in cover (today: the cover loop steps aside for the plain reload) |
| `_Grenade_Throw` | 1.37 / 1.80 / 1.30 / 1.80 | 0 (the body leans ~50 cm out) | grenade from cover (today: plain upper-body throw) |
| `_Dive`, `_Dodge_Jump`, `_Dodge_Turn` | - | - | **skipped** (designer) |

## 2. Aim/2 - what it really is

Crouch L only. The body turns to face the wall and rises a little (head 103 -> 112 cm) without stepping out; the hands go UP to 116-140 cm, i.e.
the right hand is **18-28 cm above the eyes** in the idle (`probe_riffle_aim2.txt`): the rifle is raised over the top, the eyes stay below
the stock. Offsets: `U*` pitch +22 deg, `D*` pitch -30 deg (hands highest, muzzle down over the edge), `*L / *M / *R` yaw. So it is the
over-the-top shot of a man kneeling behind a ~110 cm wall - closer to a raised / semi-blind shot than to aiming down the sights.

Window fit: our windows have a 95 cm sill (`gen_bgen_plans_check.json` `sill_height` 95). Crouched (head 103 cm, top of the head ~115) he is
**not fully hidden** under a 95 cm sill (the top of the head shows); Aim/2 then pushes the rifle out 20-45 cm over the sill. See question Q2.

## 3. Pipeline (Scripts/Editor/setup_indoor_cover_animation.py, re-runnable, outputs untracked)

Same as `setup_door_animation.py`:
- **Operatives** (UE4 Manny hierarchy, `SK_Post_Apo_Survivor_a`; the Female_Soldier body plays the same copies): IK retarget with the M4
  pack's `RTG_UE5Manny_UE4Manny`, "Blend to Source" off in memory (never saved) -> `/Game/Animations/CoverIndoor/UE4/*`.
- **PMC** (`SKM_PmcOperator`, UE5 hierarchy + `root_inner`): transient retargeter from the `RTG_UE4Manny_UE5Manny` op stack with `IK_Mannequin`
  on both sides, deleted at the end -> `/Game/Animations/CoverIndoor/Pmc/*`.
- **Bakes:** loops (`_Idle`, `_Walk`, `_Aim_Idle`, offsets) in place: the root travel is removed (the capsule moves at the shimmy speed,
  the play rate follows the ground speed as today); the crouch walk's -90 deg root yaw is folded into the pelvis. One-shots (`_Start`,
  `_Exit`, `_Aim_Start` / `_End`, `_To_*`, walk start / stop / turn) keep their root track as **mesh travel only** (root motion off, the capsule
  stays on the slot - the M4 fire idle already steps sideways this way); `_Start` is started at the frame that matches the arrival like the
  M4 enter clips. Offsets -> mesh-space additive on `_Aim_Idle` -> one `AO_CoverIndoor_{StandL,StandR,CrouchL,CrouchR,CrouchL_Over,CrouchR_Over}`
  AimOffset each (centre = `<Aim idle>_AOZero`). Python cannot rebuild a blend space's triangulation (`ResampleData` is not exposed), so until a
  C++ `FillAimOffsetSamples` helper exists the AOs are a cross from `FillAimOffset2D` (corners left out; every measured sample is in the JSON).
  Walk start / stop / turn are baked in place too (lead 2026-11-22: the capsule moves while they play). The mirrored `CrouchR_Aim2_*` set
  holds the rifle LEFT-handed (a mirror swaps the hands): the weapon must follow the left grip / hand IK while it plays.
- Grenade clips: the release frame is measured (hand_r peak speed) -> JSON `release_s`, so the grenade leaves the hand on time.
- `CODEX_COVERINDOOR_DRYRUN=1` measures only; `CODEX_RESET_COVERINDOOR=1` redoes the copies.

## 4. Runtime (data-driven set choice)

- `Content/Data/Animation/cover_anim_sets.json` (tracked): named sets (`m4` = today's clips, `riffle` = the new copies, per body UE4 / Pmc)
  and the choice: `{"contexts": {"indoor": "riffle", "outdoor": "m4", "stealth": "m4"}, "operatives": {}}` - an operative entry overrides a
  context (`"operatives": {"Sniper": {"outdoor": "riffle"}}`). Stealth later = one JSON change.
- `CoverAnimSetRules` (pure, tested): `ChooseSet(context, operative, json)` and the context: **indoor = `URoofCutawaySubsystem::GetOperativeStorey`
  puts him in a building** (any storey, the ground floor too); a man outside hugging a building's outer wall stays outdoor. Hysteresis: the set
  is only switched when he is NOT in a cover clip (decided at cover entry; a shimmy through a door keeps the set until the next entry).
- `UOperativeAnimInstance`: the existing cover arrays stay the M4 default; the riffle set is a second `FCoverAnimSet` and the layer reads the
  active one (no change to the M4 path when the switch is off). New states the M4 set has no clip for (exit, side switch, walk start / stop /
  turn, cover reload, cover grenade, over-the-top aim) play only when the active set has them.
- Switch: `Codex.Cover.IndoorSet 0/1` (console, default 1 once the designer approves) + the JSON.
- Gameplay: the riffle peek steps 43-52 cm out; the corner fire origin (`GetCoverFireOrigin`) is checked against the clip's muzzle so the
  shot line starts where the rifle shows. No hit-chance / cover rule changes.

## 5. Checks

- Unit `CodexTactics.Animation.CoverAnimSet.*`: set choice (context / operative override / missing set falls back to m4), side mapping, the
  indoor test from a storey answer, the hysteresis.
- Smoke `CoverIndoorSmoke` on `/Game/Maps/KitPilot/L_Gen_StoryShowcase_Kit`: an operative outside -> M4 clips; inside -> riffle clips for
  hug, shimmy (both ways + turn), peek-aim (stand / crouch, both sides, aim offset follows a target), reload, grenade, window over-the-top aim;
  a screenshot per pose for the designer. PMC same on one fighter.

## 6. Questions for the designer (forks)

- **Q1 Aim offsets need one ABP edit.** The pack's own 6-pose aim offsets only work if the ABP's Aim Offset Player reads `ActiveAimOffset`
  (expose its Blend Space pin, bind it - 2 minutes in ABP_Operative / ABP_Operative_Rifle2 / the PMC ABP; it also switches on the sniper AOs
  that wait for the same pin). (a) You bind the pin (recommended: real per-pose aim); (b) no ABP edit: the riffle aim idle + today's rifle pitch
  offset, the yaw by turning the body (cheaper, coarser at wide angles).
- **Q2 Window aim.** Aim/2 raises the rifle ABOVE the eyes (an over-the-top raised shot, section 2), and crouched he is not fully hidden under
  our 95 cm sills. (a) Use Aim/2 at windows as it is (recommended for now: it is the only rising clip in the pack; the head shows a little);
  (b) at a window he rises to a full STANDING aim (the pack's stand aim / our rifle aim, head at 152 cm, eyes over the sill) - truer aiming,
  but no crouch "pop-up"; (c) both: Aim/2 for a quick unaimed burst, standing for aimed fire.
- **Q3 Aim/2 exists only for crouch LEFT.** (a) mirror it for the right side (recommended, generated by the script); (b) at a window he first
  switches to his left side (`To_L`, 0.9 s) and always rises from there.
- **Q4 PMC fighters indoors.** They share the operative body; (a) they use the indoor set too (recommended: same pipeline, both skeletons
  already planned); (b) operatives only for now.
- **Q5 Shimmy start / stop / turn clips** add 0.6-1.3 s per walk. (a) use them for walks of 1.5 m and more, short side-steps loop only
  (recommended); (b) always; (c) loops only (snappier, like today).
