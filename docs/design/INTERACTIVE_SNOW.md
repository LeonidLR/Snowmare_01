# Interactive snow - implementation plan (draft 2026-10-22; phases 1, 2 (ground overlay) and 3 built 2026-10-23)

User spec (2026-10-22): extend the snow-tracks render target from footsteps to any interaction - prone / crawl furrow, knockdown / death
imprint, barrel / barricade drag trail, vehicle ruts, explosion crater with a raised rim, bullet dents, enemy / hound tracks; depth
(material parallax / normal first, Nanite displacement only as a measured prototype), berms, compacted vs loose snow, refill by snowfall;
the RT follows the camera.

**Status (2026-10-23):** phase 1 (core + walkers) and phase 3 (event stamps) are built - as built in
`docs/systems/environment-weather.md` §14 and §14.3; the ground overlay `M_SnowGroundOverlay` (§12.d) reads the RT (phase 2 on the current
floors). Phase 3 used the approved additive hooks of §5 (one broadcast line each, explosion gameplay byte-identical: §14.3). Open: phase 4,
`NS_SnowPuff` (skipped), the landscape material path.

## 1. What exists today

- Nothing draws into `RT_SnowTracks` (1024² RGBA16F, clamp): no C++ references it, there are no anim notifies, `BP_SnowTrackManager` is an
  empty shell (regenerated after the 2026-10-20 incident, no map references it). `M_SnowFootprint_Stamp` (additive unlit) and
  `M_SnowTracks_Fade` (modulate, `FadeMultiplier`) exist.
- `M_Landscape_Master` (`04_SnowTracks`: `TrackCenterLocation`, `TrackAreaSize`, `TrackIndentColor`, `TrackRoughnessDrop`, `TrackMaxWPODepth`)
  does not compile on SM6 and no map uses it; no level has a landscape (grounds are `SM_Template_Map_Floor` + `MI_ProcGrid`).

## 2. Stamp sources

| Stamp | Source | Status |
|---|---|---|
| Operative prints, prone furrow / imprint | `USquadSubsystem::GetMembers()`, `AOperativeCharacter::GetStance()`, `IsProneDiving()` | exists (poll) |
| Knockdown imprint | `AOperativeCharacter::IsKnockedDown()`, enemy `KnockdownComponent->IsDown()` | exists (poll, rising edge) |
| Death imprint | `UCodexEventBus::OnSoldierDowned`, `GameplayEvents::EnemyKilled` (Location) | exists |
| Enemies / hounds | `UEnemyRegistrySubsystem::GetEnemyCharacters()`, `GetArchetype()` | exists |
| APC ruts | `UVehicleSubsystem::GetVehicles()`, `AVehiclePawn::GetWheelSpinRate()` | exists |
| Drag trail (turn-based) | `UTurnBasedCombatSubsystem::GetRelocatingObject()` | exists |
| Drag trail (real time) | `URelocationSubsystem::GetMovingObjects` | built (phase 3) |
| Explosions | `GameplayEvents::Explosion` (`RaiseExplosion`, 5 raise sites) | built (phase 3) |
| Bullet dents | `UCombatFeedbackSubsystem::OnTracerSpawned` + one capped trace | built (phase 3) |

Walkers stamp only on a floor that is a landscape or tagged `SnowGround` (`CharacterMovement->CurrentFloor`, no trace). Polling at 20 Hz
through the registries (no actor iterators), per-actor tracker state; footprints alternate ±12 cm at stride distance (no notifies needed).

## 3. Design

- **RT**: RG16F (R compaction depth, G berm; height = G·(1−R)·Berm − R·Depth); window 64 m around the tactical camera focus (+8 m along the
  view); High 2048² (3.1 cm/texel, 16 MB + 16 MB scratch), Epic 4096², Low 1024² / 48 m. Scroll in 256-texel (8 m) steps with a shift pass
  (`M_SnowTracks_Shift`, RT → scratch → RT, clamp to 4), the window in `MPC_Environment.SnowTrackWindow` (origin XY, 1/size, enabled);
  edge fade 2 m. Fade at 5 Hz on world time × `MPC.SnowRefill` (the pause stops refill). Clear at world begin / restart.
- **Stamps**: `USnowInteractionSubsystem` (tickable world subsystem) + pure `SnowInteractionRules`; one canvas draw per frame
  (`K2_DrawMaterialTriangle`, 2 tris / stamp, procedural shapes in `M_SnowStamp` via vertex colour); trails as segment quads;
  `DA_SnowStamps` (`USnowStampSet`: shape, size, depth, berm, spacing, align, random, rate). ~120 stamps/s for a 60-enemy horde: CPU < 0.05 ms,
  GPU stamps < 0.02 ms, fade ~0.05 ms, scroll ~0.1 ms (rare); cap 512 stamps / flush.
- **Material**: `MF_SnowInteraction` (BaseColor / Roughness / Normal / AO in → out): MPC window, normals from 3 samples (shared clamp
  sampler), bump offset (no POM), compacted = darker / smoother / AO, berm = brighter / rougher; drop the landscape WPO (useless at landscape
  vertex density, invalidates VSM). ~+30 ALU, +3 samples; empty RT = identical image.
- **Nanite displacement**: prototype only (experimental tessellation, est. +0.5-2 ms GPU, VSM invalidation); measure on a test map at Epic,
  do not ship.

## 4. Phases

1. Core + walkers (no gameplay edits): subsystem, rules, stamp set / materials, RT reformat, MPC vector, fade, polling; tests
   `CodexTactics.Environment.SnowInteraction.*`; rendered smoke `SnowTracksSmoke` (RT read-back).
2. Look: `MF_SnowInteraction` in the ground material(s), stats, A/B (empty RT = 0 diff).
3. Event stamps (approved, **built 2026-10-23**): explosions, dents, real-time drag (imprints were already in phase 1); `NS_SnowPuff` skipped.
4. Polish: re-apply recent stamps into scrolled-in strips, CVars, Nanite measurement.

## 5. Gameplay classes to touch (approval list - approved by the orchestrator, done 2026-10-23)

1. `PlayerCommands.h/.cpp`: `GameplayEvents::Explosion` + `RaiseExplosion` helper.
2. Raise sites: `AGrenadeActor::Detonate`, `AInteractableActor::ApplyBlast` (barrel / mine / tripwire), `UVehicleTurretComponent::FireCannon`,
   `UTurnBasedCombatSubsystem` barrel / mine paths - or the smaller variant: inside `UKnockdownComponent::NotifyExplosion` + `FireCannon`.
3. `UCombatFeedbackSubsystem`: native `OnTracerSpawned` delegate.
4. `URelocationSubsystem`: const `GetMovingObjects`.

## 6. Risks / blockers

No ground renders a snow-track material yet (needs the fixed landscape material on a real terrain, or the stamp read in the floor / overlay
path); the shared MPC asset (serialize edits); `Content/VFX/SnowTracks` was wiped once through a junction (tracked now; keep a regeneration
script); landscape sampler budget; LWC precision far from the origin; additive pile-up (clamped); the RT persists across PIE (clear it);
smokes need a real RHI.
