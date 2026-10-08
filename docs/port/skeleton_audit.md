# Skeleton audit - can all humanoids share one UE5 skeleton? (2026-10-08, read-only)

Goal (user): reduce the number of skeletons, ideally every humanoid (operatives, Marksman, Frostbitten, Brute, Cutter) on ONE UE5
skeleton (Manny). Method: `Scripts/Editor/skeleton_audit.py` (headless Python commandlet, **no saves**, run against the main
project; raw output `Saved/Logs/skeleton_audit.json`). "Subset" below = every bone the mesh really has exists in the target
skeleton **with the same parent**, which is what UE checks on Assign Skeleton (`USkeleton::DoesParentChainMatch`, verified in the
5.8 engine source). Bone names alone are not enough.

## 1. Skeleton families found

| Family | Skeleton asset | Bones | Root | Notes |
|---|---|---|---|---|
| UE4 mannequin | `Post_Apo_Survivor/.../UE4_Mannequin_Skeleton` | 71 | `root` | 68 UE4 bones + `Backpack_01..03`. **Operative** |
| UE4 mannequin | `Mutant_monster_1/Meshes/base_mesh/SK_UE4_Mannequin_Skeleton` | 74 | `root` | + `tongue_1..6`. **Brute** (mesh uses 55 of them) |
| UE4 mannequin | `mutant_monster_2/base_mesh/SK_UE4_Mannequin_Skeleton` | 68 | `root` | **Frostbitten** |
| UE4 mannequin | `Biochemical_Monster_2/baze_mech/ue4/SK_UE4_Mannequin_Skeleton` | 68 | `root` | **Cutter** |
| UE4 mannequin | `Biochemical_Monster_1/baze_mech/ue4/SK_UE4_Mannequin_Skeleton` | 68 | `root` | **Marksman's ABP target** |
| UE5 Manny (161) | `Biochemical_Monster_1/baze_mech/ue5/SK_base_mesh_ue5_1_Skeleton` | 161 | `root` | **Marksman's current BP mesh** (see 3) |
| UE5 Manny (161) | `Biochemical_Monster_2/baze_mech/ue5/SK_baze_mesh_ue5_Skeleton` | 161 | `root` | unused Cutter-pack UE5 export |
| UE5 Manny (161) | `Characters/Mannequins/Meshes/SK_Mannequin` | 161 | `root` | template mannequin, 449 clips |
| UE5 Manny (196) | `M4_Cover_Pack/.../Mannequins/Meshes/SK_Mannequin` | 196 | `root` | **superset**: the 161 + 35 `*_end` leaf bones. Knockdown (14 assets) + cover (66 clips) live here |
| UE5 subset | `Characters/Operatives/Explorer/.../Explorer_Skeleton` | 89 | `root` | spine_01..05, ik_*, twists; **0 names outside Manny, 0 parent differences** |
| UE5 (other) | `Fab/Pistol_and_Rifle.../SKM_UEFN_Mannequin_Skeleton` | 88 | `root` | UEFN mannequin, unused by the game |
| Quadruped | `Combat_Dog/Mesh/CH_Combat_Dog` | 84 | `Bip001` | **Hound**, 3ds-max biped names, nothing in common with Manny |
| clip packs (UE4) | `RifleAnims/.../UE4_Mannequin_Skeleton` 70, `Crawl_MocapAnimPack` A 68 / Rifle 69 / Pistol 77, M4 `Mannequin_UE4` 68 | | | animation sources only |
| other | `SKEL_East_APC_BTR82`, `m4_Skeleton` | | | not characters |

The 196 skeleton is the only one that can host every other UE5 Manny mesh (a 161-bone mesh can be assigned to it, not the reverse).

## 2. Character by character

| Game unit | Blueprint / mesh | Skeleton (bones, mesh bones) | Hierarchy vs UE5 Manny | Assign Skeleton to Manny as is? | ABP (target skeleton) |
|---|---|---|---|---|---|
| Operative | `BP_Operative` / `Post_Apo_Survivor/Models/SK_Post_Apo_Survivor_a` (also b, c, and 3 Maskless variants, same skeleton, no sockets, own PhysicsAssets) | Post_Apo UE4 (71, 71) | UE4: spine_01..03, neck_01, no metacarpals. **12 bones with a different parent** (`clavicle_l/r` spine_03 vs spine_05, `neck_01`, `head` (neck_01 vs neck_02), 8 finger roots hand vs `*_metacarpal`); extras `Backpack_01..03` | **No** (parent chain differs) | `ABP_Operative` (Post_Apo UE4) |
| Marksman | `BP_Enemy_Marksman` / `Biochemical_Monster_1/baze_mech/ue5/SKM_base_mesh_2_ue5` (user-modified in main, mesh switched to the UE5 export) | BM1 ue5 (161, 161) | identical to Manny: 0 names outside, 0 parent differences vs the 196 skeleton | **Yes** (to the 196 skeleton; the 35 `_end` bones simply stay unweighted) | `ABP_Enemy_Marksman` still targets the **BM1 ue4** skeleton - mismatch with the BP's mesh, check in the editor |
| Cutter | `BP_Enemy_Cutter` / `Biochemical_Monster_2/baze_mech/ue4/SKM_baze_mesh` | BM2 ue4 (68, 68) | UE4, the same 12 parent differences, no extras | **No** as is. A UE5 export of the same pack exists (`.../ue5/SKM_baze_mesh_ue5`, + armor/blade/generator parts) which **is** Manny-exact | `ABP_Enemy_Cutter` (BM2 ue4) |
| Frostbitten | `BP_Enemy_Frostbitten` / `mutant_monster_2/base_mesh/SK_base_mesh` | MM2 UE4 (68, 68) | UE4, 12 parent differences, no extras, **no UE5 export in the pack** | **No** | `ABP_Enemy_Frostbitten` (MM2) |
| Brute | `BP_Enemy_Brute` / `Mutant_monster_1/Meshes/base_mesh/SK_base_mesh_1` | MM1 UE4 (74, 55) | UE4, parent differences (clavicles, neck/head, middle/ring finger roots), + `tongue_1..6` that Manny has no bone for; no UE5 export | **No** | `ABP_Enemy_Brute` (MM1) |
| Hound | `BP_Enemy_Hound` / `Combat_Dog/Mesh/SK_Combat_Dog` | CH_Combat_Dog (84, 84) | quadruped, `Bip001-*` | impossible | `ABP_Enemy_Hound` |
| Spitter, Cryo Drone, Base | **no Blueprint / mesh in Content** (only the `EEnemyArchetype` entries) | - | - | - | - |

Not game characters but present: Explorer (89-bone Manny subset, no BP yet - a ready-made Manny-compatible operative body),
M4 Manny/Quinn, template `BP_ThirdPersonCharacter` etc.

Compatible-skeleton links that exist today (all one-directional, set on the listed side):

- Post_Apo UE4 -> RifleAnims UE4, Crawl A / Rifle / Pistol, `Characters/Mannequins` SK_Mannequin (161), M4 SK_Mannequin (196)
- BM1 ue4 -> RifleAnims UE4, Crawl A / Rifle / Pistol
- Crawl A / Rifle / Pistol -> Post_Apo UE4, BM1 ue4;  RifleAnims UE4 -> Post_Apo UE4, BM1 ue4, SK_Mannequin 161;  SK_Mannequin 161 -> Post_Apo UE4, RifleAnims UE4
- **none** on: MM1 (Brute), MM2 (Frostbitten), BM2 ue4 (Cutter), BM1 ue5, BM2 ue5, dog, M4 196 (itself).

That is why the operative plays the Manny knockdown/cover clips today: by the Post_Apo -> M4 link (bone-name playback, so the 12
mismatched-parent bones are driven with a slightly wrong parent frame; Sprint 14 shots looked acceptable).

Clips per skeleton (AnimSequence/Montage/BlendSpace): Post_Apo UE4 7/0/4, M4 Manny 167/7/2, Characters Manny 449/1/3, RifleAnims UE4 79/2/8, Crawl A 103, Crawl Rifle 22, Crawl Pistol 22,
BM1 ue4 28/0/1, BM2 ue4 19/0/1, MM1 22/0/1, MM2 24/0/1, BM1 ue5 21, BM2 ue5 12, dog 16.

Duplicated knockdown/death clips: operative set 7 clips + 7 montages on the M4 Manny skeleton; **Brute 6** and **Frostbitten 6**
copies (`Animations_KnockDown/Brute|Frostbitten`, imported onto their own skeletons because they are not linked). Marksman/Cutter
would add 6 more each if they ever fall. Cover clips (66 in `M4_Cover_Pack/Animations/crouch|stand`) exist once, on M4 Manny.

Sockets / IK bones: no character mesh has skeletal-mesh sockets. The weapon is attached to bone `hand_r`
(`AOperativeCharacter::WeaponSocket`), `LeftHandGrip` is a socket on the *weapon* static mesh, left-hand IK reads bones `hand_l` /
`hand_r` (`OperativeAnimInstance.cpp`). All of these exist on Manny; the Manny mesh additionally has `weapon_r_muzzle`,
`foot_*_Socket`, `HandGrip_*` sockets that the game does not use. Physics assets are per mesh (bodies keyed by bone name) and
survive Assign Skeleton as long as bone names stay.

## 3. What UE 5.8 actually offers (checked in the engine plugins / source)

- **Assign Skeleton** is allowed only when each mesh bone has the same name and the same parent chain in the target skeleton
  (`USkeleton::DoesParentChainMatch`); missing bones are merged into the skeleton (`MergeAllBonesToBoneTree`). It does **not** require equal rest poses: the mesh keeps its own bind pose and
  animations are retargeted by the skeleton's per-bone translation-retargeting modes.
- **Skeletal Mesh Editor / Modeling tools** (plugin `SkeletalMeshModelingTools`, `USkeletonEditingTool`): create, remove, transform, **re-parent**, rename and mirror the bones of the mesh's reference skeleton (commit on Accept, updates the mesh description). `USkinWeightsPaintTool` (paint / edit weights) and `USkinWeightsBindingTool` (auto bind: Direct Distance or Geodesic Voxel) are available.
- **Geometry Script** `MeshBoneWeightFunctions` has `TransferBoneWeights` (ClosestPointOnSurface / InpaintWeights) and `CopyBonesFromMesh` - scriptable from Python / Editor Utility, but I have not run it on these meshes.
- I did **not** find a one-click "rebind this skin to another skeleton and carry the weights over"; the practical in-engine route is: conform the mesh hierarchy to Manny's (add `spine_04/05`, `neck_02`, `*_metacarpal_*`; re-parent) then Assign Skeleton. **Unsure** whether the result is clean enough without re-painting the spine/shoulder weights (new spine_04/05 start unweighted: Manny clips bend them, the chest skinned to spine_03 would not follow, shoulders/neck would). It needs a one-mesh spike before committing to all of them.
- **Runtime retarget** (c): AnimGraph node `Retarget Pose From Mesh` + IK Rig / IK Retargeter assets (Manny source rig -> each target rig); a hidden Manny "driver" mesh runs the shared ABP. It removes clip duplication and the 12-bone mismatch problem but **does not reduce the number of skeletons** (each character keeps its own) and costs a second skeleton evaluation per character.

## 4. Recommendation

Per character:

| Character | Path | Why / what to do | Effort |
|---|---|---|---|
| Marksman | **(a)** | Its UE5 mesh is already Manny-exact. Assign Skeleton -> M4 196. Rebuild `ABP_Enemy_Marksman` on it (`setup_marksman_animation.py` / retarget-duplicate the ABP) and add the 196 skeleton to the RifleAnims/Crawl compat lists (or retarget those clips). Fixes the ABP/mesh mismatch. | 0.5-1 d |
| Cutter | **(a)** | Swap the mesh to `SKM_baze_mesh_ue5` (+ parts) and assign to 196; reapply materials/capsule offset. Re-check its own clips (BM2 ue4 set, 19 clips) on the new skeleton. | 1 d |
| Frostbitten | **(b)** | UE4 only. Conform hierarchy in the Skeletal Mesh Editor (or Blender / DCC weight re-bind) then Assign Skeleton. | 1 d spike + 1 d |
| Brute | **(b)** | As Frostbitten + `tongue_1..6` (fold their weights into the jaw/head and delete, or add the 6 bones to the shared skeleton). Hulking proportions: validate foot placement and hit boxes with Manny clips. | 2 d |
| Operative | **(a) via Explorer** or **(b)** | Explorer is already Manny-compatible (assign, no rebind) - needs an art decision. For Post_Apo a/b/c (+ Maskless, 6 meshes, one skeleton): conform once with a script. `ABP_Operative` is user-polished: retarget-duplicate rather than rebuild, then re-tune weapon attach offsets and left-hand IK (hand bone orientation of UE4 vs UE5 may differ - verify), run the IK/cover/vault/knockdown smokes. | 2-3 d (+1 d verify) |
| Hound | **(d)** leave | Quadruped `Bip001` rig, 16 own clips. | - |
| Spitter / Cryo Drone | - | no assets yet; build them on Manny (humanoid) or leave non-humanoids alone. | - |

Staged plan (lowest risk first):

1. **Phase A - one UE4 hub, almost free.** Frostbitten, Cutter and BM1-ue4 have the same 68 names and **zero parent differences** against Post_Apo (superset, 71). Assign their meshes to the Post_Apo skeleton (Brute too: its 6 `tongue_*` bones merge in, parents match). UE4 skeletons 5 -> 1, no reskin, no clip changes except deleting the 12 duplicated knockdown clips (they then play via the same compat links as the operative). Risk: ABPs of the four monsters need their target skeleton changed (Retarget Anim Assets / Duplicate and Retarget).
2. **Phase B - Marksman and Cutter to the 196 Manny** (path a, section above).
3. **Phase C - conform the hub to Manny** (one spike on Frostbitten, then script for operative + monsters). End state: Manny 196 + dog = 2 character skeletons. Phase A makes C a single conform job instead of five.
4. Cleanup: the 161-bone `Characters/Mannequins` skeleton, BM1/BM2 ue5 skeletons and UEFN become unreferenced by the game and can go.

Cross-cutting risks: ABP target skeleton cannot be changed in place; all `.uasset` edits are binary (claim them in TANDEM, user owns the ABP polish);
Manny clips on stocky monsters need translation-retarget settings (root/pelvis/IK bones) or the feet slide; `ik_*` bones exist only on Manny (the UE4 rigs have none, so IK goals come from the shared clips); materials/morph targets of each mesh stay with the mesh;
every change must be re-verified with `verify_all.ps1` (IK, cover, vault, knockdown, marksman smokes).

Re-run the audit: `UnrealEditor-Cmd.exe CodexTactics.uproject -run=pythonscript -script=Scripts/Editor/skeleton_audit.py -unattended -nullrhi` (the editor may stay open; the "GetBoneParent: Bone ... not found" log errors are expected noise, it probes which skeleton bones a mesh really has).
