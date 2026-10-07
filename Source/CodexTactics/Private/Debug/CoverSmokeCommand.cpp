// Dev-only headless check of the Sprint 12 tactical cover on L_MovementTest (nothing is saved):
//   Scripts/smoke.ps1 -Command CodexTactics.CoverSmoke -Log Smoke-Cover.log
// A wave fight with the wave removed; a 3 m wall 4 m ahead of the leader and a 60 cm barricade beyond its end are
// spawned at runtime. Checks: the wall is found as high cover (slot 45 cm off it, corners probed) and the barricade as
// low cover; the leader sprints to the slot and enters cover (standing, back to the wall). User design rule
// 2026-10-06: a threat on his right -> he faces right along the wall; a shimmy right plays the forward clip, a shimmy
// left walks backwards still facing right (+-10 deg); the threat moves to the left -> he turns left and walks to the
// edge on that side (corner pose); a hit from behind the wall is absorbed 90 %, a crit from there is no headshot, a
// flank hit passes fully; an enemy diagonally behind the wall (clear trace past the corner, out of earshot) does not
// perceive him until he leans out; Commander Mode: a marksman's laser makes him crouch and hold fire, the laser gone he
// stands and peeks again; the corner shot leaves round the corner; Ctrl + click on an enemy from cover = a corner
// shot with bLeaning during it and back after; the cover ghost wears the operatives' see-through silhouette
// material; a ground order leaves the cover. Clip sides (measured, user decision 2026-10-06): the pack names its sides
// facing the wall, so pack _L = his own RIGHT as he stands back to the wall (CoverFacingRules::ClipIndex Right -> 0).
// Walk clips: _L / _R is the MOVEMENT direction in that naming (threat on his right: shimmy right = walk_fwd_loop_L,
// left = walk_bwd_loop_R). Fire-ready corner pose: a known threat at the exposed edge -> cvr_*_fire_idle_L/R (his
// right corner: fire_idle_L), Ctrl + click on an enemy around the corner (behind the wall plane) = fire_L (no idle ->
// fire transition: already ready) and back to fire_idle_L, the threat gone -> fire_to_idle exit and look_at_idle_L.
// User decision 2026-10-06: the corner shot only at targets behind the wall / around the corner; Ctrl + click on an
// enemy out in front of the wall = a normal open shot off the wall (he leaves the cover pose) and back to the slot.
// User decisions 2026-10-07: the edges are bisected (real distance within a few cm, he stands the clip side's stand-off
// from the true edge); the fire-ready pose only for a threat behind the wall; the sustained corner aim: a burst of >= 3
// shots from the fire stance with no cover idle / fire_to_idle in between, an empty magazine -> fire_to_idle + reload
// behind the corner -> back out to fire_idle, no targets -> back to the plain pose after the grace period.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/MarksmanEnemyCharacter.h"
#include "Characters/OperativeAnimInstance.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadAutonomySubsystem.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/TacticalSightSubsystem.h"
#include "Combat/WaveSubsystem.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Tactics/CoverGhostActor.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "Interactables/BarricadeActor.h"
#include "Survival/ColdSurvivalComponent.h"
#include "Tactics/CoverFacingRules.h"
#include "Tactics/CoverRules.h"
#include "Tactics/CoverTraceRules.h"

namespace CoverSmoke
{
	struct FState
	{
		float Time = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		FVector P = FVector::ZeroVector;
		FVector F = FVector::ForwardVector;
		FVector R = FVector::RightVector;
		float GroundZ = 0.f;
		TWeakObjectPtr<AOperativeCharacter> Op;
		TWeakObjectPtr<AStaticMeshActor> Wall;
		TWeakObjectPtr<AEnemyCharacter> Hound;
		TWeakObjectPtr<AMarksmanEnemyCharacter> Marksman;
		FCoverSlot Slot;
		FVector SlotEntered = FVector::ZeroVector;
		float FortitudeCut = 0.f;
		bool bSampled = false;
		bool bAllowLeaderFire = false;
		bool bSawLean = false;
		bool bAnimSawLean = false;
		float MaxShotYawOff = 0.f;
		int32 LeanShotsBefore = 0;
		int32 ClipsBefore = 0;
		int32 ClipLogMark = 0;
		int32 OpenShotsBefore = 0;
		bool bSawLeftCover = false;
		int32 FrontSpot = 0;
		bool bBurstBroken = false;
		FString BurstBrokenBy;
		int32 ReloadDucksBefore = 0;
		bool bSawReloading = false;
	};

	/** Planar angle between his yaw and Direction, degrees. */
	float YawOff(const AOperativeCharacter& Op, const FVector& Direction)
	{
		return FMath::Abs(FRotator::NormalizeAxis(Op.GetActorRotation().Yaw - Direction.Rotation().Yaw));
	}

	FString LoopName(const UOperativeAnimInstance* Anim)
	{
		return Anim && Anim->GetCoverLoopClip() ? Anim->GetCoverLoopClip()->GetName() : FString(TEXT("none"));
	}

	/** The clip is really on the character: the mesh's active montage plays it with weight > 0.9 and the FullBody slot node is live. */
	void CheckPlaying(FState& State, const UOperativeAnimInstance* Anim, const TCHAR* ClipSuffix, const FString& What)
	{
		FString Clip;
		float MontageWeight = 0.f;
		float SlotWeight = 0.f;
		if (Anim)
		{
			Anim->GetCoverPlayback(Clip, MontageWeight, SlotWeight);
		}
		const bool bOk = Clip.EndsWith(ClipSuffix) && MontageWeight > 0.9f && SlotWeight > 0.9f;
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s - actually playing '%s' (montage weight %.2f, FullBody slot node %.2f)"),
			bOk ? TEXT("ok  ") : TEXT("FAIL"), *What, *Clip, MontageWeight, SlotWeight);
		State.Failures += bOk ? 0 : 1;
	}

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("CoverSmoke"));
		return false;
	}

	AStaticMeshActor* SpawnBlock(UWorld* World, const FVector& Centre, const FRotator& Rotation, const FVector& Scale)
	{
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		if (!Cube)
		{
			return nullptr;
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AStaticMeshActor* Block = World->SpawnActorDeferred<AStaticMeshActor>(AStaticMeshActor::StaticClass(), FTransform(Rotation, Centre, Scale),
			nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Block)
		{
			return nullptr;
		}
		Block->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
		Block->GetStaticMeshComponent()->SetStaticMesh(Cube);
		Block->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
		Block->GetStaticMeshComponent()->SetCanEverAffectNavigation(true);
		Block->FinishSpawning(FTransform(Rotation, Centre, Scale));
		return Block;
	}

	AEnemyCharacter* SpawnFrozen(UWorld* World, EEnemyArchetype Type, const FVector& Feet, const FVector& Facing)
	{
		AEnemyCharacter* Enemy = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(Type, Feet + FVector(0.f, 0.f, 100.f), Facing.Rotation());
		if (Enemy)
		{
			Enemy->CustomTimeDilation = 0.f; // we place it; its own behaviour never runs
			Enemy->SetActorTickEnabled(false);
			Enemy->GetHealthComponent()->SetMaxHealth(100000.f);
			Enemy->SetActorLocation(FVector(Feet.X, Feet.Y, Feet.Z + Enemy->GetSimpleCollisionHalfHeight() + 2.f));
		}
		return Enemy;
	}

	/** Puts a frozen enemy on the ground at Where (feet height of the test floor). */
	void PlaceEnemy(AEnemyCharacter& Enemy, const FState& State, const FVector& Where)
	{
		Enemy.SetActorLocation(FVector(Where.X, Where.Y, State.GroundZ + Enemy.GetSimpleCollisionHalfHeight() + 2.f));
	}

	void SampleAnimLean(const AOperativeCharacter& Op, FState& State)
	{
		const UOperativeAnimInstance* Anim = Op.GetMesh() ? Cast<UOperativeAnimInstance>(Op.GetMesh()->GetAnimInstance()) : nullptr;
		State.bAnimSawLean |= Anim && Anim->bLeaning;
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += 0.1f;
		UWorld* World = WeakWorld.Get();
		if (!World)
		{
			return false;
		}
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UTacticalSightSubsystem* Sight = World->GetSubsystem<UTacticalSightSubsystem>();
		USquadAutonomySubsystem* Autonomy = World->GetSubsystem<USquadAutonomySubsystem>();
		AOperativeCharacter* Op = State.Op.Get();
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			Member->ColdLevel = 0.f;
			Member->bTacticalCeaseFire = !(State.bAllowLeaderFire && Member == Op); // nobody shoots the frozen enemies (but the Ctrl + click check)
		}
		switch (State.Stage)
		{
		case 0:
		{
			if (State.Time < 3.f)
			{
				return true;
			}
			Flow->TriggerCombatZone();
			Flow->FinishCutscene();
			Flow->FinishPreparation();
			State.Op = Squad->GetLeader();
			// This smoke checks the per-shot lean / sustained-aim cover rules (the legacy mode behind the tunables); the
			// default corner hold at the edge (user decision 2026-10-07) is CodexTactics.CornerHoldSmoke.
			State.Op->bCornerHoldAtEdge = false;
			State.Op->bCornerAutoDuck = true;
			Op = State.Op.Get();
			// The wave must stay alive (WaveCleared stops the world): our frozen hound joins it 60 m away first, then the
			// spawned wave goes.
			State.Hound = SpawnFrozen(World, EEnemyArchetype::FrostHound, Op->GetActorLocation() - FVector(0.f, 0.f, Op->GetSimpleCollisionHalfHeight())
				- Op->GetActorForwardVector().GetSafeNormal2D() * 6000.f, Op->GetActorForwardVector());
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				if (*It != State.Hound.Get())
				{
					It->Destroy();
				}
			}
			Check(State, State.Hound.IsValid() && Flow->GetPhase() == ECodexGamePhase::WaveCombat, TEXT("wave fight running with the parked hound"));
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Member->HealthComponent->SetMaxHealth(100000.f);
			}
			State.F = Op->GetActorForwardVector().GetSafeNormal2D();
			State.R = FVector::CrossProduct(FVector::UpVector, State.F);
			State.P = Op->GetActorLocation();
			State.GroundZ = Op->GetActorLocation().Z - Op->GetSimpleCollisionHalfHeight();
			State.FortitudeCut = FMath::Clamp((Op->ColdSurvival ? Op->ColdSurvival->Fortitude : 15.f) * 0.015f, 0.f, 0.5f);
			// The others out of the way (they would stand in the wall / the slots).
			int32 Index = 0;
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				if (Member != Op)
				{
					Member->StopOperative();
					Member->TeleportTo(State.P - State.F * 400.f + State.R * (Index++ % 2 == 0 ? 250.f : -250.f), State.F.Rotation(), false, true);
				}
			}
			// A 3 m high, 6 m wide, 40 cm thick wall 4 m ahead across the leader's path; a 60 cm barricade beyond its right end.
			const FVector WallCentre = State.P + State.F * 400.f;
			State.Wall = SpawnBlock(World, FVector(WallCentre.X, WallCentre.Y, State.GroundZ + 150.f), State.F.Rotation(), FVector(0.4f, 6.f, 3.f));
			Check(State, State.Wall.IsValid(), TEXT("3 m wall spawned 4 m ahead"));
			const FVector BarricadeSpot = State.P + State.F * 400.f + State.R * 550.f;
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			World->SpawnActor<ABarricadeActor>(FVector(BarricadeSpot.X, BarricadeSpot.Y, State.GroundZ + ABarricadeActor::HeightCm * 0.5f),
				FRotator(0.f, State.F.Rotation().Yaw + 90.f, 0.f), Params);
			State.Stage = 1;
			State.Time = 0.f;
			return true;
		}
		case 1:
		{
			if (State.Time < 2.f)
			{
				return true; // the navmesh rebuilds around the wall
			}
			// A click on the wall's middle: high cover, no corner within 2 m.
			FCoverSlot Middle;
			const FVector MiddlePoint = State.P + State.F * 380.f + FVector(0.f, 0.f, 90.f);
			const bool bMiddle = CoverTraceRules::FindCoverSlotAt(World, MiddlePoint, State.F, Middle);
			Check(State, bMiddle && Middle.Height == ECoverHeight::HighCover, FString::Printf(TEXT("the wall is high cover (top %.0f cm)"), Middle.WallTopCm));
			Check(State, bMiddle && FVector::DotProduct(Middle.WallNormal, -State.F) > 0.95f, TEXT("wall normal faces the leader"));
			const float Off = bMiddle ? FVector::DotProduct(Middle.WorldLocation - Middle.WallPoint, Middle.WallNormal) : -1.f;
			Check(State, bMiddle && Off >= 30.f && Off <= 95.f, FString::Printf(TEXT("slot %.0f cm off the wall, on the navmesh"), Off));
			Check(State, bMiddle && !Middle.bLeftEdgeExposed && !Middle.bRightEdgeExposed, TEXT("middle of a 6 m wall: no corner within reach"));
			// Near the right end (50 cm short of the corner): the right corner is exposed.
			const FVector EndPoint = State.P + State.F * 380.f + State.R * 250.f + FVector(0.f, 0.f, 90.f);
			const bool bEnd = CoverTraceRules::FindCoverSlotAt(World, EndPoint, State.F, State.Slot);
			// Facing -F his right is -R: the wall's +R end is his LEFT corner.
			Check(State, bEnd && State.Slot.Height == ECoverHeight::HighCover && State.Slot.bLeftEdgeExposed && !State.Slot.bRightEdgeExposed,
				FString::Printf(TEXT("near the wall's end: the corner on his left is exposed (L %d at %.0f cm, R %d)"), State.Slot.bLeftEdgeExposed ? 1 : 0,
					State.Slot.LeftEdgeDistanceCm, State.Slot.bRightEdgeExposed ? 1 : 0));
			// User decision 2026-10-07: the edge is bisected, the measured distance is the real one (the wall ends at +-300 cm).
			if (bEnd)
			{
				const float RealLeft = 300.f - static_cast<float>(FVector::DotProduct(State.Slot.WallPoint - State.P, State.R));
				Check(State, FMath::Abs(State.Slot.LeftEdgeDistanceCm - RealLeft) <= 10.f,
					FString::Printf(TEXT("edge probing is precise: measured %.1f cm, real %.1f cm"), State.Slot.LeftEdgeDistanceCm, RealLeft));
			}
			// The barricade: low cover.
			FCoverSlot Low;
			const FVector BarricadePoint = State.P + State.F * 375.f + State.R * 550.f + FVector(0.f, 0.f, 30.f);
			const bool bLow = CoverTraceRules::FindCoverSlotAt(World, BarricadePoint, State.F, Low);
			Check(State, bLow && Low.Height == ECoverHeight::LowCover && Low.WallActor.IsValid() && Low.WallActor->IsA<ABarricadeActor>(),
				FString::Printf(TEXT("the 60 cm barricade is low cover (top %.0f cm)"), Low.WallTopCm));
			if (!bEnd)
			{
				return Finish(State);
			}
			Check(State, Op->OrderTakeCover(State.Slot, true) == EOperativeOrderResult::Accepted, TEXT("cover order accepted (sprint)"));
			State.Stage = 2;
			State.Time = 0.f;
			return true;
		}
		case 2:
		{
			if (!Op->bInCover && State.Time < 10.f)
			{
				return true;
			}
			Check(State, Op->bInCover && Op->CurrentCoverHeight == ECoverHeight::HighCover, FString::Printf(TEXT("entered the high cover after %.1f s"), State.Time));
			Check(State, Op->GetStance() == EOperativeStance::Standing, TEXT("stands at the full wall"));
			// User design rule 2026-10-06: the back against the wall, the body ALONG it (never out of the wall).
			Check(State, YawOff(*Op, -State.F) < 10.f, FString::Printf(TEXT("back to the wall, yaw = wall normal (%.0f deg off)"), YawOff(*Op, -State.F)));
			const float AlongSlot = FMath::Abs(FVector::DotProduct(Op->GetActorLocation() - State.Slot.WorldLocation, State.R));
			Check(State, AlongSlot < 200.f && FMath::Abs(FVector::DotProduct(Op->GetActorLocation() - State.Slot.WorldLocation, State.F)) < 60.f,
				FString::Printf(TEXT("on the slot or walked to its corner (%.0f cm along)"), AlongSlot));
			const UOperativeAnimInstance* Anim = Op->GetMesh() ? Cast<UOperativeAnimInstance>(Op->GetMesh()->GetAnimInstance()) : nullptr;
			Check(State, Anim && Anim->bInCover && Anim->CoverHeight == ECoverHeight::HighCover, TEXT("AnimInstance: bInCover / CoverHeight set"));
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: cover clips played so far %d (loop %s); leader class %s, anim class %s"), Anim ? Anim->GetCoverClipsPlayed() : -1,
				*LoopName(Anim), *Op->GetClass()->GetName(), Anim ? *Anim->GetClass()->GetName() : TEXT("none"));
			Check(State, Op->GetClass()->GetName().Contains(TEXT("BP_Operative")), TEXT("the leader is the game's real Blueprint operative (BP_Operative)"));
			Check(State, Op->CanFireFromCover(), TEXT("a corner to fire round"));
			Check(State, FVector::Dist2D(Op->GetCoverFireOrigin(), Op->GetMuzzleLocation()) >= 59.f, TEXT("the corner shot leaves round the corner"));
			// A threat out on the open side to his RIGHT (along the wall towards -R).
			AEnemyCharacter* Hound = State.Hound.Get();
			if (!Hound)
			{
				Check(State, false, TEXT("hound alive"));
				return Finish(State);
			}
			PlaceEnemy(*Hound, State, Op->GetActorLocation() - State.F * 800.f - State.R * 500.f);
			Sight->Refresh();
			State.Stage = 3;
			State.Time = 0.f;
			return true;
		}
		case 3:
		{
			if (State.Time < 1.2f)
			{
				return true;
			}
			Sight->Refresh();
			Check(State, Op->bHasCoverThreat && Op->CoverFacing == ECoverFacing::Right,
				FString::Printf(TEXT("threat on the right: faces right (facing %s)"), Op->CoverFacing == ECoverFacing::Left ? TEXT("left") : TEXT("right")));
			Check(State, YawOff(*Op, -State.F) < 10.f, FString::Printf(TEXT("back to the wall, threat side picks the clip (%.0f deg off)"), YawOff(*Op, -State.F)));
			Check(State, !Op->bAtCoverCorner, TEXT("no corner pose: the right side has no exposed edge"));
			State.SlotEntered = Op->GetActorLocation();
			// Shimmy to the RIGHT (towards the threat): forward.
			FCoverSlot Shimmy;
			const FVector ClickAlong = State.P + State.F * 380.f + State.R * 60.f + FVector(0.f, 0.f, 90.f);
			const bool bShimmy = CoverTraceRules::FindShimmySlot(World, Op->GetCoverSlot(), ClickAlong, Shimmy);
			Check(State, bShimmy && CoverTraceRules::IsSameWall(Op->GetCoverSlot(), Shimmy), TEXT("a click along the wall is a shimmy target"));
			const bool bShimmyOrdered = bShimmy && Op->OrderShimmyTo(Shimmy) == EOperativeOrderResult::Accepted;
			Check(State, bShimmyOrdered && Op->bShimmying && Op->ShimmyDirection > 0.f && Op->IsShimmyForward(),
				FString::Printf(TEXT("shimmy right ordered: forward (direction %.0f)"), Op->ShimmyDirection));
			State.bSampled = false;
			State.Stage = 4;
			State.Time = 0.f;
			return true;
		}
		case 4:
		{
			const UOperativeAnimInstance* Anim = Op->GetMesh() ? Cast<UOperativeAnimInstance>(Op->GetMesh()->GetAnimInstance()) : nullptr;
			if (Op->bShimmying && State.Time < 8.f)
			{
				if (!State.bSampled && State.Time >= 0.5f)
				{
					State.bSampled = true;
					Check(State, Anim && Anim->bShimmying && Anim->bCoverShimmyForward, FString::Printf(TEXT("shimmy towards the threat plays the forward clip (loop %s)"), *LoopName(Anim)));
					CheckPlaying(State, Anim, TEXT("walk_fwd_loop_L"), TEXT("shimmy right, threat right"));
					Check(State, !Anim || !Anim->GetCoverLoopClip() || LoopName(Anim).EndsWith(TEXT("walk_fwd_loop_L")), FString::Printf(TEXT("... the pack's walk_fwd_loop_L (face-forward towards his right, loop %s)"), *LoopName(Anim)));
					Check(State, YawOff(*Op, -State.F) < 10.f, FString::Printf(TEXT("... facing the threat while side-stepping (%.0f deg off)"), YawOff(*Op, -State.F)));
				}
				return true;
			}
			const float Moved = FVector::Dist2D(Op->GetActorLocation(), State.SlotEntered);
			Check(State, !Op->bShimmying && Op->bInCover && Moved >= 100.f, FString::Printf(TEXT("shimmied %.0f cm right, still in cover (%.1f s)"), Moved, State.Time));
			Check(State, YawOff(*Op, -State.F) < 10.f, TEXT("still facing the threat (right) after the shimmy"));
			// Shimmy back LEFT (away from the threat): backwards, still facing right.
			State.SlotEntered = Op->GetActorLocation();
			FCoverSlot Shimmy;
			const FVector ClickAlong = State.P + State.F * 380.f + State.R * 190.f + FVector(0.f, 0.f, 90.f);
			const bool bShimmyOrdered = CoverTraceRules::FindShimmySlot(World, Op->GetCoverSlot(), ClickAlong, Shimmy)
				&& Op->OrderShimmyTo(Shimmy) == EOperativeOrderResult::Accepted;
			Check(State, bShimmyOrdered && Op->bShimmying && Op->ShimmyDirection < 0.f && !Op->IsShimmyForward(),
				FString::Printf(TEXT("shimmy left ordered: backwards (direction %.0f)"), Op->ShimmyDirection));
			State.bSampled = false;
			State.Stage = 5;
			State.Time = 0.f;
			return true;
		}
		case 5:
		{
			const UOperativeAnimInstance* Anim = Op->GetMesh() ? Cast<UOperativeAnimInstance>(Op->GetMesh()->GetAnimInstance()) : nullptr;
			if (Op->bShimmying && State.Time < 8.f)
			{
				if (!State.bSampled && State.Time >= 0.5f)
				{
					State.bSampled = true;
					Check(State, Anim && Anim->bShimmying && !Anim->bCoverShimmyForward, FString::Printf(TEXT("shimmy away from the threat plays the backward clip (loop %s)"), *LoopName(Anim)));
					CheckPlaying(State, Anim, TEXT("walk_bwd_loop_R"), TEXT("shimmy left, threat right"));
					Check(State, !Anim || !Anim->GetCoverLoopClip() || LoopName(Anim).EndsWith(TEXT("walk_bwd_loop_R")), FString::Printf(TEXT("... the pack's walk_bwd_loop_R (backing away to his left, facing right, loop %s)"), *LoopName(Anim)));
					Check(State, YawOff(*Op, -State.F) < 10.f, FString::Printf(TEXT("... walking backwards, still facing the threat (%.0f deg off)"), YawOff(*Op, -State.F)));
				}
				return true;
			}
			const float Moved = FVector::Dist2D(Op->GetActorLocation(), State.SlotEntered);
			Check(State, !Op->bShimmying && Op->bInCover && Moved >= 60.f, FString::Printf(TEXT("shimmied %.0f cm back left, still in cover"), Moved));
			Check(State, YawOff(*Op, -State.F) < 10.f, TEXT("still facing right after walking backwards"));
			// The enemy flanks to the LEFT side.
			PlaceEnemy(*State.Hound.Get(), State, Op->GetActorLocation() - State.F * 800.f + State.R * 600.f);
			Sight->Refresh();
			State.Stage = 6;
			State.Time = 0.f;
			return true;
		}
		case 6:
		{
			if (State.Time < 1.5f || (Op->bShimmying && State.Time < 8.f))
			{
				Sight->Refresh();
				return true; // turns round, then walks to the corner on that side
			}
			Check(State, Op->CoverFacing == ECoverFacing::Left && YawOff(*Op, -State.F) < 10.f,
				FString::Printf(TEXT("threat moved to the left: back still to the wall, left clip side (%.0f deg off)"), YawOff(*Op, -State.F)));
			const UOperativeAnimInstance* Anim = Op->GetMesh() ? Cast<UOperativeAnimInstance>(Op->GetMesh()->GetAnimInstance()) : nullptr;
			const float EdgeLeft = 300.f - FVector::DotProduct(Op->GetActorLocation() - State.P, State.R);
			Check(State, Op->bAtCoverCorner && Anim && Anim->bCoverAtCorner,
				FString::Printf(TEXT("edge on the threat side: at the corner, corner pose (%.0f cm from the wall end, loop %s)"), EdgeLeft, *LoopName(Anim)));
			State.bSampled = false;
			// User decision 2026-10-07: the hound is out IN FRONT of the wall: no fire-ready corner pose (it gets an open shot).
			Check(State, Anim && !Anim->bCoverFireReady && !LoopName(Anim).Contains(TEXT("fire_idle")),
				FString::Printf(TEXT("... a threat in front of the wall: no fire-ready pose, the plain cover pose (loop %s)"), *LoopName(Anim)));
			const float StandOffLeft = CoverFacingRules::CornerStandOff(ECoverFacing::Left, false, Op->GetCoverFacingConfig());
			Check(State, FMath::Abs(EdgeLeft - StandOffLeft) <= 15.f,
				FString::Printf(TEXT("... standing the _R stand-off from the real wall end (%.0f cm, stand-off %.0f; the clip leans 40 cm out)"), EdgeLeft, StandOffLeft));

			// Damage through the wall: the frozen hound 3 m behind it.
			AEnemyCharacter* Hound = State.Hound.Get();
			Hound->SetActorLocation(FVector(State.P.X, State.P.Y, State.GroundZ + Hound->GetSimpleCollisionHalfHeight() + 2.f) + State.F * 700.f + State.R * 100.f);
			UHealthComponent* Health = Op->HealthComponent;
			const float Base = 100.f * (1.f - State.FortitudeCut); // standing, no dodge
			Op->ForcedDodgeRollForTesting = 0.f;
			float Before = Health->GetCurrentHealth();
			float Taken = Op->TakeHit(100.f, TEXT("smoke"), false, false, Hound);
			Check(State, FMath::IsNearlyEqual(Taken, Base * 0.1f, 0.5f) && FMath::IsNearlyEqual(Before - Health->GetCurrentHealth(), Taken, 0.01f),
				FString::Printf(TEXT("a hit from behind the wall: 100 -> %.1f (90 %% absorbed)"), Taken));
			Op->ForcedDodgeRollForTesting = 0.f;
			Taken = Op->TakeHit(200.f, TEXT("smoke"), true, false, Hound, 2.f);
			Check(State, FMath::IsNearlyEqual(Taken, Base * 0.1f, 0.5f), FString::Printf(TEXT("a crit (x2) from behind the wall is no headshot: %.1f"), Taken));
			// Flank: the hound beyond the wall's end, along it (90 deg off the wall) -> full damage.
			Hound->SetActorLocation(FVector(State.P.X, State.P.Y, State.GroundZ + Hound->GetSimpleCollisionHalfHeight() + 2.f) + State.F * 355.f + State.R * 800.f);
			Op->ForcedDodgeRollForTesting = 0.f;
			Before = Health->GetCurrentHealth();
			Taken = Op->TakeHit(100.f, TEXT("smoke"), false, false, Hound);
			Check(State, FMath::IsNearlyEqual(Taken, Base, 0.5f), FString::Printf(TEXT("a flank hit passes fully: %.1f"), Taken));
			Check(State, Op->RecentIncomingDamage > 0.f, TEXT("recent incoming damage tracked for the cover decisions"));
			// Sight: diagonally behind the wall (77 deg off its normal, 23 m: out of earshot), where the straight line to him passes
			// the wall's end (he stands at it) — only the cover rule hides him.
			Hound->SetActorLocation(FVector(Op->GetActorLocation().X, Op->GetActorLocation().Y, State.GroundZ + Hound->GetSimpleCollisionHalfHeight() + 2.f)
				+ State.F * 500.f + State.R * 2250.f);
			State.Stage = 7;
			State.Time = 0.f;
			return true;
		}
		case 7:
		{
			if (State.Time < 1.f)
			{
				return true;
			}
			AEnemyCharacter* Hound = State.Hound.Get();
			Sight->Refresh();
			FVector Belief;
			bool bPerceived = true;
			bool bKnows = Sight->GetBelief(Hound, Op, Belief, &bPerceived);
			Check(State, Sight->IsActive() && !(bKnows && bPerceived), TEXT("an enemy diagonally behind the wall does not perceive him (head down)"));
			{
				FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(CoverSmokeSight), false);
				for (TActorIterator<APawn> It(World); It; ++It)
				{
					TraceParams.AddIgnoredActor(*It);
				}
				FHitResult Blocker;
				const FVector Eye = UTacticalSightSubsystem::EyePoint(*Hound);
				const FVector Profile = UTacticalSightSubsystem::ProfilePoint(*Op);
				const bool bBlocked = World->LineTraceSingleByChannel(Blocker, Eye, Profile, ECC_Visibility, TraceParams);
				Check(State, !bBlocked, FString::Printf(TEXT("... although the straight line to him passes the wall's end (blocked by %s at (%.0f, %.0f, %.0f); eye (%.0f, %.0f, %.0f) -> profile (%.0f, %.0f, %.0f))"),
					bBlocked && Blocker.GetActor() ? *Blocker.GetActor()->GetName() : TEXT("nothing"), Blocker.ImpactPoint.X, Blocker.ImpactPoint.Y, Blocker.ImpactPoint.Z,
					Eye.X, Eye.Y, Eye.Z, Profile.X, Profile.Y, Profile.Z));
			}
			Check(State, Op->IsHiddenInCoverFrom(Hound->GetActorLocation()), TEXT("IsHiddenInCoverFrom: behind the wall"));
			Op->bIsCornerLeaning = true;
			Sight->Refresh();
			bKnows = Sight->GetBelief(Hound, Op, Belief, &bPerceived);
			Check(State, bKnows && bPerceived, TEXT("leaning out of the corner: perceived"));
			Op->bIsCornerLeaning = false;
			// An enemy on his open side is perceived as usual.
			Hound->SetActorLocation(FVector(Op->GetActorLocation().X, Op->GetActorLocation().Y, State.GroundZ + Hound->GetSimpleCollisionHalfHeight() + 2.f) - State.F * 600.f);
			State.Stage = 8;
			State.Time = 0.f;
			return true;
		}
		case 8:
		{
			if (State.Time < 1.f)
			{
				return true;
			}
			AEnemyCharacter* Hound = State.Hound.Get();
			Sight->Refresh();
			FVector Belief;
			bool bPerceived = false;
			Check(State, Sight->GetBelief(Hound, Op, Belief, &bPerceived) && bPerceived, TEXT("an enemy on his open side perceives him"));
			// The cover fire decisions below need an enemy behind the wall plane, round the corner (user decision 2026-10-06:
			// an enemy out in front gets an open shot, no peek): past the +R end, 3 m behind the wall face, 6.6 m off.
			PlaceEnemy(*Hound, State, State.P + State.F * 700.f + State.R * 800.f);
			// Commander Mode: the marksman's laser -> crouch at once and hold fire.
			Squad->SetAutonomousSquadCombat(true);
			State.Marksman = Cast<AMarksmanEnemyCharacter>(SpawnFrozen(World, EEnemyArchetype::Marksman,
				FVector(Op->GetActorLocation().X, Op->GetActorLocation().Y, State.GroundZ) - State.F * 900.f + State.R * 200.f, State.F));
			AMarksmanEnemyCharacter* Marksman = State.Marksman.Get();
			if (!Marksman)
			{
				Check(State, false, TEXT("marksman spawned"));
				return Finish(State);
			}
			Marksman->ForceAimForTesting(Op);
			Check(State, Marksman->IsAimingAtTarget() && Marksman->GetCurrentTarget() == Op, TEXT("the marksman's laser rests on him"));
			State.Stage = 9;
			State.Time = 0.f;
			return true;
		}
		case 9:
		{
			if (State.Time < 0.5f)
			{
				return true;
			}
			Autonomy->RunDecisions();
			Check(State, Op->bInCover && Op->GetStance() == EOperativeStance::Crouching, TEXT("laser on him: crouches at the wall at once (Commander Mode)"));
			Check(State, Op->bCoverHoldFire, TEXT("laser on him: holds fire behind the wall"));
			Check(State, Autonomy->GetStats().CoverCrouchForLaser >= 1 && Autonomy->GetStats().CoverHolds >= 1, TEXT("autonomy stats: crouch for laser, hold"));
			// The marksman gone: the calm hound in front -> stand and peek.
			State.Marksman->Destroy();
			State.Stage = 10;
			State.Time = 0.f;
			return true;
		}
		case 10:
		{
			if (State.Time < 0.5f)
			{
				return true;
			}
			Autonomy->RunDecisions();
			Check(State, Op->GetStance() == EOperativeStance::Standing, TEXT("laser gone, calm: stands again"));
			Check(State, !Op->bCoverHoldFire && Op->CoverFireMode == ECoverFireMode::CornerLean, TEXT("calm, enemy 6 m: corner peek"));
			Check(State, Autonomy->GetStats().CoverPeeks >= 1, TEXT("autonomy stats: peek"));
			// Blind fire by the player's key, then back.
			Check(State, Op->ToggleCoverFireMode() == ECoverFireMode::BlindFire, TEXT("N: blind fire mode"));
			Op->SetCoverFireMode(ECoverFireMode::CornerLean);
			Squad->SetAutonomousSquadCombat(false);
			// Ctrl + click on an enemy round the left corner, behind the wall plane: the cover fire path, never a plain shot.
			PlaceEnemy(*State.Hound.Get(), State, State.P + State.F * 700.f + State.R * 800.f);
			Sight->Refresh();
			State.Stage = 26;
			State.Time = 0.f;
			return true;
		}
		case 26:
		{
			Sight->Refresh();
			if (State.Hound->IsHidden() && State.Time < 3.f)
			{
				return true; // the sight reveals it round the corner
			}
			Check(State, Op->IsCornerShotTarget(State.Hound->GetActorLocation()),
				FString::Printf(TEXT("the enemy round the left corner is a corner-shot target (%.0f cm behind the wall face)"),
					-CoverFacingRules::DepthInFrontOfWall(Op->GetCoverSlot(), State.Hound->GetActorLocation())));
			State.bAllowLeaderFire = true;
			// The frontal-hit checks above left him "hit hard a moment ago": that alone would make him duck right back
			// (DecideCornerAim, big hit) — this check is about the held corner aim.
			Op->RecentIncomingDamage = 0.f;
			State.LeanShotsBefore = Op->GetCoverLeanShots();
			State.ClipsBefore = Op->GetMesh() && Cast<UOperativeAnimInstance>(Op->GetMesh()->GetAnimInstance())
				? Cast<UOperativeAnimInstance>(Op->GetMesh()->GetAnimInstance())->GetCoverClipsPlayed() : 0;
			State.bSawLean = false;
			State.MaxShotYawOff = 0.f;
			if (ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(UGameplayStatics::GetPlayerController(World, 0)))
			{
				PC->IssueTargetedShot(State.Hound.Get());
			}
			else
			{
				Op->SetManualPriorityTarget(State.Hound.Get());
			}
			State.Stage = 11;
			State.Time = 0.f;
			return true;
		}
		case 11:
		{
			Sight->Refresh();
			SampleAnimLean(*Op, State);
			if (Op->bIsCornerLeaning)
			{
				State.bSawLean = true;
				State.MaxShotYawOff = FMath::Max(State.MaxShotYawOff, YawOff(*Op, -State.F));
			}
			if (Op->GetCoverLeanShots() <= State.LeanShotsBefore && State.Time < 5.f)
			{
				return true;
			}
			Check(State, Op->GetCoverLeanShots() > State.LeanShotsBefore, FString::Printf(TEXT("Ctrl + click from cover: a corner shot (%.1f s)"), State.Time));
			Check(State, Op->bIsCornerLeaning || State.bSawLean, TEXT("... leaning out round the corner during the shot (bLeaning)"));
			Check(State, Op->bInCover && YawOff(*Op, -State.F) < 20.f, FString::Printf(TEXT("... still in cover, back to the wall (%.0f deg off)"), YawOff(*Op, -State.F)));
			// One shot is enough: hold fire again.
			State.bAllowLeaderFire = false;
			Op->AssignPriorityTarget(nullptr);
			State.Stage = 12;
			State.Time = 0.f;
			return true;
		}
		case 12:
		{
			SampleAnimLean(*Op, State);
			if (State.Time < 0.5f)
			{
				if (!State.bSampled)
				{
					State.bSampled = true;
					Check(State, Op->IsCornerAimActive(), TEXT("... he holds the corner fire stance after the shot (sustained aim)"));
				}
				return true;
			}
			if (Op->IsCornerAimActive() && State.Time < 5.f)
			{
				return true; // no target any more (fire held): back after the grace period
			}
			const UOperativeAnimInstance* Anim = Op->GetMesh() ? Cast<UOperativeAnimInstance>(Op->GetMesh()->GetAnimInstance()) : nullptr;
			Check(State, State.bAnimSawLean, TEXT("AnimInstance: bLeaning during the shot"));
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: cover fire clips played %d -> %d"), State.ClipsBefore, Anim ? Anim->GetCoverClipsPlayed() : -1);
			Check(State, !Op->IsCornerAimActive() && Op->GetLastCornerAimBreak() == ECornerAimDecision::ReturnNoTargets && State.Time >= 1.4f,
				FString::Printf(TEXT("... the aim ends with no target after the grace period (%.1f s, %s)"), State.Time,
					CoverDecisionRules::CornerAimDecisionName(Op->GetLastCornerAimBreak())));
			Check(State, !Op->bIsCornerLeaning && Op->bInCover && Anim && !Anim->bLeaning, TEXT("... and back behind the corner"));
			// The cover ghost wears the operatives' see-through silhouette (OverlayMaterial MID of M_Silhouette).
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			if (ACoverGhostActor* Ghost = World->SpawnActor<ACoverGhostActor>(State.Slot.WorldLocation, FRotator::ZeroRotator, Params))
			{
				Ghost->ShowFor(*Op, State.Slot);
				const USkeletalMeshComponent* Body = Ghost->GetBody();
				const UMaterialInterface* Expected = Op->SilhouetteMaterial ? Op->SilhouetteMaterial.Get()
					: LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/VFX/Materials/M_Silhouette.M_Silhouette"));
				const UMaterialInterface* Overlay = Body ? Body->GetOverlayMaterial() : nullptr;
				const UMaterialInterface* Slot0 = Body && Body->GetNumMaterials() > 0 ? Body->GetMaterial(0) : nullptr;
				Check(State, Expected && Overlay && Overlay->GetMaterial() == Expected->GetMaterial(),
					FString::Printf(TEXT("cover ghost: overlay = the see-through silhouette material (%s)"), Overlay ? *Overlay->GetMaterial()->GetName() : TEXT("none")));
				Check(State, Expected && Slot0 && Slot0->GetMaterial() == Expected->GetMaterial(), TEXT("cover ghost: no opaque body (base slots in the silhouette too)"));
				const FVector GhostForward = Ghost->GetActorForwardVector();
				Check(State, FVector::DotProduct(GhostForward, -State.F) > 0.98f, TEXT("cover ghost: back to the wall (yaw = wall normal)"));
				Ghost->Destroy();
			}
			else
			{
				Check(State, false, TEXT("cover ghost spawned"));
			}
			// A ground order leaves the cover.
			Check(State, Op->OrderMoveTo(State.P, false) == EOperativeOrderResult::Accepted && !Op->bInCover, TEXT("a move order leaves the cover"));
			State.Stage = 13;
			State.Time = 0.f;
			return true;
		}
		case 13:
		{
			if (State.Time < 0.5f)
			{
				return true;
			}
			const UOperativeAnimInstance* Anim = Op->GetMesh() ? Cast<UOperativeAnimInstance>(Op->GetMesh()->GetAnimInstance()) : nullptr;
			Check(State, Anim && !Anim->bInCover, TEXT("AnimInstance: bInCover cleared"));
			// The tactical pause's release path: the slot remembered, then a plain move order to it (USquadSubsystem's planned move).
			Op->SetPendingCover(State.Slot);
			Check(State, Op->OrderMoveTo(State.Slot.WorldLocation, true) == EOperativeOrderResult::Accepted && Op->HasPendingCover(),
				TEXT("a planned walk to the remembered slot keeps the pending cover"));
			State.Stage = 14;
			State.Time = 0.f;
			return true;
		}
		case 14:
		{
			if (!Op->bInCover && State.Time < 10.f)
			{
				return true;
			}
			Check(State, Op->bInCover && Op->CurrentCoverHeight == ECoverHeight::HighCover, FString::Printf(TEXT("... and enters the cover on arrival (%.1f s)"), State.Time));
			Op->LeaveCover(TEXT("smoke: next wall end"));
			State.Stage = 15;
			State.Time = 0.f;
			return true;
		}
		case 15:
		{
			// The wall's other end: its corner is on his RIGHT (-R). Fire-ready pose with the threat on that side.
			FCoverSlot RightEnd;
			const FVector EndPoint = State.P + State.F * 380.f - State.R * 250.f + FVector(0.f, 0.f, 90.f);
			const bool bFound = CoverTraceRules::FindCoverSlotAt(World, EndPoint, State.F, RightEnd);
			Check(State, bFound && RightEnd.bRightEdgeExposed && !RightEnd.bLeftEdgeExposed, TEXT("the wall's other end: the corner on his right is exposed"));
			if (!bFound)
			{
				return Finish(State);
			}
			State.Slot = RightEnd;
			Op->CoverFireReadyHoldSeconds = 1.f; // tunable: a short hold for the smoke
			PlaceEnemy(*State.Hound.Get(), State, State.P + State.F * 700.f - State.R * 800.f); // behind the wall, round his right corner
			Sight->Refresh();
			Check(State, Op->OrderTakeCover(RightEnd, true) == EOperativeOrderResult::Accepted, TEXT("cover order to the right-hand corner accepted"));
			State.Stage = 16;
			State.Time = 0.f;
			return true;
		}
		case 16:
		{
			Sight->Refresh();
			if (!Op->bInCover && State.Time < 10.f)
			{
				return true;
			}
			Check(State, Op->bInCover, FString::Printf(TEXT("in the right-hand corner cover after %.1f s"), State.Time));
			if (const UOperativeAnimInstance* Anim0 = Op->GetMesh() ? Cast<UOperativeAnimInstance>(Op->GetMesh()->GetAnimInstance()) : nullptr)
			{
				State.ClipLogMark = Anim0->GetCoverClipLog().Num();
			}
			State.Stage = 17;
			State.Time = 0.f;
			return true;
		}
		case 17:
		{
			Sight->Refresh();
			if (State.Time < 3.5f)
			{
				return true; // faces right, walks to the corner, plays the idle -> fire transition, settles into fire_idle_R
			}
			const UOperativeAnimInstance* Anim = Op->GetMesh() ? Cast<UOperativeAnimInstance>(Op->GetMesh()->GetAnimInstance()) : nullptr;
			// A clip that is stopped again every tick (the graph / other code fighting the cover layer) would flood the log.
			Check(State, Anim && Anim->GetCoverClipLog().Num() - State.ClipLogMark <= 8,
				FString::Printf(TEXT("the cover clips are not restarted every frame (%d plays in 3.5 s)"), Anim ? Anim->GetCoverClipLog().Num() - State.ClipLogMark : -1));
			Check(State, Op->bHasCoverThreat && Op->CoverFacing == ECoverFacing::Right, TEXT("threat on the right: faces right (clip side _R)"));
			Check(State, Op->bAtCoverCorner && Op->IsCoverThreatActive() && Op->IsCoverFireReady(), TEXT("at the right corner with a live threat: IsCoverFireReady"));
			Check(State, Anim && Anim->bCoverFireReady, TEXT("AnimInstance: bCoverFireReady"));
			{
				const float EdgeRight = 300.f + static_cast<float>(FVector::DotProduct(Op->GetActorLocation() - State.P, State.R));
				Check(State, EdgeRight <= 69.f - 10.f, FString::Printf(TEXT("... the _L peek (69 cm) clears the real edge (%.0f cm away)"), EdgeRight));
			}
			const bool bCrouched = Op->GetStance() == EOperativeStance::Crouching;
			const TArray<TObjectPtr<UAnimSequenceBase>>* FireIdle = Anim ? (bCrouched ? &Anim->CoverCrouchFireIdle : &Anim->CoverStandFireIdle) : nullptr;
			const int32 RightIndex = CoverFacingRules::ClipIndex(ECoverFacing::Right); // pack _L = his own right
			const UAnimSequenceBase* IdleRight = FireIdle && FireIdle->IsValidIndex(RightIndex) ? (*FireIdle)[RightIndex].Get() : nullptr;
			if (IdleRight)
			{
				Check(State, Anim->GetCoverLoopClip() == IdleRight && LoopName(Anim).EndsWith(TEXT("fire_idle_L")) && Anim->IsInCoverFirePose(),
					FString::Printf(TEXT("fire-ready pose at his right edge = fire_idle_L (loop %s)"), *LoopName(Anim)));
				CheckPlaying(State, Anim, TEXT("fire_idle_L"), TEXT("fire-ready pose"));
			}
			else
			{
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke: fire_idle clips not assigned (M4 pack missing): clip checks skipped"));
			}
			// Ctrl + click on the enemy round his right corner (behind the wall plane): fire_L at once (already ready), then
			// back to fire_idle_L. First the sight reveals it round the corner: case 25 clicks.
			State.ClipLogMark = Anim ? Anim->GetCoverClipLog().Num() : 0;
			PlaceEnemy(*State.Hound.Get(), State, State.P + State.F * 700.f - State.R * 800.f);
			Sight->Refresh();
			State.Stage = 25;
			State.Time = 0.f;
			return true;
		}
		case 25:
		{
			Sight->Refresh();
			if (State.Hound->IsHidden() && State.Time < 3.f)
			{
				return true;
			}
			State.LeanShotsBefore = Op->GetCoverLeanShots();
			State.bAllowLeaderFire = true;
			if (ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(UGameplayStatics::GetPlayerController(World, 0)))
			{
				PC->IssueTargetedShot(State.Hound.Get());
			}
			else
			{
				Op->SetManualPriorityTarget(State.Hound.Get());
			}
			State.Stage = 18;
			State.Time = 0.f;
			return true;
		}
		case 18:
		{
			Sight->Refresh();
			if (Op->GetCoverLeanShots() <= State.LeanShotsBefore && State.Time < 5.f)
			{
				return true;
			}
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: right-corner shot diag: hold %d mode %d yawOff %.0f hidden %d dist %.0f stance %d"), Op->bCoverHoldFire ? 1 : 0, static_cast<int32>(Op->CoverFireMode),
				YawOff(*Op, -State.F), State.Hound->IsHidden() ? 1 : 0, FVector::Dist2D(Op->GetActorLocation(), State.Hound->GetActorLocation()), static_cast<int32>(Op->GetStance()));
			Check(State, Op->GetCoverLeanShots() > State.LeanShotsBefore, FString::Printf(TEXT("Ctrl + click from the right corner: a shot (%.1f s)"), State.Time));
			// User request 2026-10-07: a burst from the held fire stance â€” no cover idle / fire -> idle between the shots.
			State.bBurstBroken = false;
			State.BurstBrokenBy.Reset();
			State.Stage = 35;
			State.Time = 0.f;
			return true;
		}
		case 35:
		{
			Sight->Refresh();
			const UOperativeAnimInstance* Anim = Op->GetMesh() ? Cast<UOperativeAnimInstance>(Op->GetMesh()->GetAnimInstance()) : nullptr;
			FString Clip;
			float MontageWeight = 0.f;
			float SlotWeight = 0.f;
			if (Anim)
			{
				Anim->GetCoverPlayback(Clip, MontageWeight, SlotWeight);
			}
			const bool bFireStance = Clip.Contains(TEXT("fire_idle")) || Clip.EndsWith(TEXT("fire_L")) || Clip.EndsWith(TEXT("fire_R"));
			if (!bFireStance && MontageWeight > 0.5f && !State.bBurstBroken)
			{
				State.bBurstBroken = true;
				State.BurstBrokenBy = Clip;
			}
			if (Op->GetCoverLeanShots() < State.LeanShotsBefore + 3 && State.Time < 8.f)
			{
				return true;
			}
			Check(State, Op->GetCoverLeanShots() >= State.LeanShotsBefore + 3, FString::Printf(TEXT("sustained corner aim: a burst of %d shots (%.1f s)"),
				Op->GetCoverLeanShots() - State.LeanShotsBefore, State.Time));
			Check(State, !State.bBurstBroken, FString::Printf(TEXT("... the playing clip stays fire_idle / fire between the shots (broken by '%s')"), *State.BurstBrokenBy));
			Check(State, Op->IsCornerAimActive() && Op->bIsCornerLeaning, TEXT("... still leaned out in the fire stance"));
			bool bExitInBurst = false;
			if (Anim)
			{
				const TArray<FString>& Log = Anim->GetCoverClipLog();
				for (int32 Index = FMath::Min(State.ClipLogMark, Log.Num()); Index < Log.Num(); ++Index)
				{
					bExitInBurst |= Log[Index].Contains(TEXT("fire_to_"));
				}
			}
			Check(State, !bExitInBurst, TEXT("... no fire_to_idle exit during the burst"));
			// The magazine runs dry: back behind the corner to reload, then out again (the priority target is still there).
			Op->CurrentClip = 1;
			State.ReloadDucksBefore = Op->GetCornerAimBreakCount(ECornerAimDecision::DuckToReload);
			State.ClipLogMark = Anim ? Anim->GetCoverClipLog().Num() : 0;
			State.bSawReloading = false;
			State.Stage = 36;
			State.Time = 0.f;
			return true;
		}
		case 36:
		{
			Sight->Refresh();
			State.bSawReloading |= Op->bIsReloading;
			if (Op->GetCornerAimBreakCount(ECornerAimDecision::DuckToReload) <= State.ReloadDucksBefore && State.Time < 5.f)
			{
				return true;
			}
			Check(State, Op->GetCornerAimBreakCount(ECornerAimDecision::DuckToReload) > State.ReloadDucksBefore && !Op->IsCornerAimActive(),
				FString::Printf(TEXT("magazine empty: he ducks back behind the corner to reload (%.1f s)"), State.Time));
			State.Stage = 37;
			State.Time = 0.f;
			return true;
		}
		case 37:
		{
			Sight->Refresh();
			State.bSawReloading |= Op->bIsReloading;
			if ((Op->bIsReloading || !Op->IsCornerAimActive()) && State.Time < 10.f)
			{
				return true; // reload behind the corner, then the next shot leans out again
			}
			const UOperativeAnimInstance* Anim = Op->GetMesh() ? Cast<UOperativeAnimInstance>(Op->GetMesh()->GetAnimInstance()) : nullptr;
			Check(State, State.bSawReloading, TEXT("... reloaded behind the corner"));
			bool bExit = false;
			bool bOutAgain = false;
			if (Anim)
			{
				const TArray<FString>& Log = Anim->GetCoverClipLog();
				for (int32 Index = FMath::Min(State.ClipLogMark, Log.Num()); Index < Log.Num(); ++Index)
				{
					bExit |= Log[Index].Contains(TEXT("fire_to_"));
					bOutAgain |= bExit && (Log[Index].Contains(TEXT("_to_fire")) || Log[Index].Contains(TEXT("fire_idle")) || Log[Index].EndsWith(TEXT("fire_L")));
				}
			}
			Check(State, bExit, TEXT("... through the fire_to_idle exit"));
			Check(State, Op->IsCornerAimActive() && bOutAgain, FString::Printf(TEXT("... and back out in the fire stance after the reload (%.1f s)"), State.Time));
			// Fire held, no target: the aim ends after the grace period.
			State.bAllowLeaderFire = false;
			Op->AssignPriorityTarget(nullptr);
			State.ClipLogMark = Anim ? Anim->GetCoverClipLog().Num() : 0;
			State.Stage = 19;
			State.Time = 0.f;
			return true;
		}
		case 19:
		{
			Sight->Refresh();
			if (Op->IsCornerAimActive() && State.Time < 6.f)
			{
				return true;
			}
			Check(State, !Op->IsCornerAimActive() && Op->GetLastCornerAimBreak() == ECornerAimDecision::ReturnNoTargets && State.Time >= 1.4f,
				FString::Printf(TEXT("targets gone: back to cover after the grace period (%.1f s, %s)"), State.Time,
					CoverDecisionRules::CornerAimDecisionName(Op->GetLastCornerAimBreak())));
			const UOperativeAnimInstance* Anim = Op->GetMesh() ? Cast<UOperativeAnimInstance>(Op->GetMesh()->GetAnimInstance()) : nullptr;
			if (Anim && Anim->CoverStandFireIdle.IsValidIndex(CoverFacingRules::ClipIndex(ECoverFacing::Right))
				&& Anim->CoverStandFireIdle[CoverFacingRules::ClipIndex(ECoverFacing::Right)])
			{
				const TArray<FString>& Log = Anim->GetCoverClipLog();
				int32 FireAt = INDEX_NONE;
				int32 IdleAt = INDEX_NONE;
				bool bTransition = false;
				for (int32 Index = FMath::Min(State.ClipLogMark, Log.Num()); Index < Log.Num(); ++Index)
				{
					bTransition |= Log[Index].Contains(TEXT("_to_fire"));
					if (FireAt == INDEX_NONE && Log[Index].EndsWith(TEXT("fire_L")))
					{
						FireAt = Index;
					}
					else if (FireAt != INDEX_NONE && IdleAt == INDEX_NONE && Log[Index].EndsWith(TEXT("fire_idle_L")))
					{
						IdleAt = Index;
					}
				}
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke: since the reload: fire_L at %d, fire_idle_L at %d, idle -> fire %d"), FireAt, IdleAt, bTransition ? 1 : 0);
			}
			// No threat any more: the exit transition (logged since the fire was held) and the look-around corner idle.
			Op->bIgnoreCoverThreatForTesting = true;
			State.Stage = 20;
			State.Time = 0.f;
			return true;
		}
		case 20:
		{
			if (State.Time < 4.f)
			{
				return true;
			}
			const UOperativeAnimInstance* Anim = Op->GetMesh() ? Cast<UOperativeAnimInstance>(Op->GetMesh()->GetAnimInstance()) : nullptr;
			Check(State, !Op->IsCoverThreatActive() && !Op->IsCoverFireReady() && Op->bAtCoverCorner, TEXT("no threat: still at the corner, no longer fire-ready"));
			Check(State, Anim && !Anim->bCoverFireReady && !Anim->IsInCoverFirePose(), TEXT("AnimInstance: fire-ready cleared"));
			const bool bCrouched = Op->GetStance() == EOperativeStance::Crouching;
			const TArray<TObjectPtr<UAnimSequenceBase>>* Corner = Anim ? (bCrouched ? &Anim->CoverCrouchCorner : &Anim->CoverStandCorner) : nullptr;
			if (Corner && Corner->IsValidIndex(CoverFacingRules::ClipIndex(ECoverFacing::Right)) && (*Corner)[CoverFacingRules::ClipIndex(ECoverFacing::Right)])
			{
				bool bExit = false;
				const TArray<FString>& Log = Anim->GetCoverClipLog();
				for (int32 Index = FMath::Min(State.ClipLogMark, Log.Num()); Index < Log.Num(); ++Index)
				{
					bExit |= Log[Index].Contains(TEXT("fire_to_"));
				}
				Check(State, bExit, TEXT("... the fire_to_idle exit transition played"));
				Check(State, LoopName(Anim).EndsWith(TEXT("look_at_idle_L")), FString::Printf(TEXT("... the corner pose is look_at_idle_L (his right corner, loop %s)"), *LoopName(Anim)));
			}
			// User decision 2026-10-06: an enemy out IN FRONT of the wall (his open side) gets a normal shot off the wall.
			Op->bIgnoreCoverThreatForTesting = false;
			State.SlotEntered = Op->GetActorLocation();
			PlaceEnemy(*State.Hound.Get(), State, Op->GetActorLocation() - State.F * 700.f + State.R * 100.f);
			Sight->Refresh();
			State.Stage = 30;
			State.Time = 0.f;
			return true;
		}
		case 30:
		{
			Sight->Refresh();
			if (State.Hound->IsHidden() && State.Time < 6.f)
			{
				// Not revealed in 1.5 s (map props in the line): the next spot out in front.
				const int32 Spot = FMath::FloorToInt(State.Time / 1.5f);
				if (Spot != State.FrontSpot && Spot < 4)
				{
					State.FrontSpot = Spot;
					static const FVector2D Spots[] = { { 700.f, 100.f }, { 600.f, -300.f }, { 500.f, 300.f }, { 800.f, -500.f } };
					PlaceEnemy(*State.Hound.Get(), State, State.SlotEntered - State.F * Spots[Spot].X + State.R * Spots[Spot].Y);
				}
				return true;
			}
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: open-shot enemy spot %d, hidden %d"), State.FrontSpot, State.Hound->IsHidden() ? 1 : 0);
			Check(State, !Op->IsCornerShotTarget(State.Hound->GetActorLocation()),
				FString::Printf(TEXT("an enemy 7 m in front of the wall is no corner-shot target (%.0f cm in front of the face)"),
					CoverFacingRules::DepthInFrontOfWall(Op->GetCoverSlot(), State.Hound->GetActorLocation())));
			State.LeanShotsBefore = Op->GetCoverLeanShots() + Op->GetCoverBlindShots();
			State.OpenShotsBefore = Op->GetCoverOpenShots();
			State.bSawLeftCover = false;
			State.bAllowLeaderFire = true;
			if (ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(UGameplayStatics::GetPlayerController(World, 0)))
			{
				PC->IssueTargetedShot(State.Hound.Get());
			}
			else
			{
				Op->SetManualPriorityTarget(State.Hound.Get());
			}
			State.Stage = 31;
			State.Time = 0.f;
			return true;
		}
		case 31:
		{
			Sight->Refresh();
			State.bSawLeftCover |= !Op->bInCover && Op->IsCoverOpenShotActive();
			if (Op->GetCoverOpenShots() <= State.OpenShotsBefore && State.Time < 5.f)
			{
				return true;
			}
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: open-shot diag: hound hidden %d, reloading %d, clip %d, in cover %d, cease %d, hold %d, dist %.0f"),
				State.Hound->IsHidden() ? 1 : 0, Op->bIsReloading ? 1 : 0, Op->CurrentClip, Op->bInCover ? 1 : 0, Op->bTacticalCeaseFire ? 1 : 0,
				Op->bCoverHoldFire ? 1 : 0, FVector::Dist2D(Op->GetActorLocation(), State.Hound->GetActorLocation()));
			Check(State, Op->GetCoverOpenShots() > State.OpenShotsBefore, FString::Printf(TEXT("Ctrl + click on an enemy in front of the wall: an open shot (%.1f s)"), State.Time));
			Check(State, State.bSawLeftCover && !Op->bInCover, TEXT("... he stepped off the wall for it (left the cover pose)"));
			Check(State, Op->GetCoverLeanShots() + Op->GetCoverBlindShots() == State.LeanShotsBefore, TEXT("... no corner lean / blind shot"));
			State.bAllowLeaderFire = false;
			Op->AssignPriorityTarget(nullptr);
			State.Stage = 32;
			State.Time = 0.f;
			return true;
		}
		case 32:
		{
			if (!Op->bInCover && State.Time < 5.f)
			{
				return true;
			}
			const UOperativeAnimInstance* Anim = Op->GetMesh() ? Cast<UOperativeAnimInstance>(Op->GetMesh()->GetAnimInstance()) : nullptr;
			Check(State, Op->bInCover && FVector::Dist2D(Op->GetActorLocation(), State.SlotEntered) < 60.f,
				FString::Printf(TEXT("... and back to the same slot after the shot (%.1f s, %.0f cm off)"), State.Time, FVector::Dist2D(Op->GetActorLocation(), State.SlotEntered)));
			Check(State, Op->IsQuietCoverEntry() && Anim && !LoopName(Anim).Contains(TEXT("idle_fwd_to_cvr")), TEXT("... without the Cover_Enter clip (he only stepped off the wall)"));
			// Threat UNKNOWN (the flag keeps it so): a fresh wall entry in the middle, every shimmy face-forward in its direction.
			Op->bIgnoreCoverThreatForTesting = true;
			Op->LeaveCover(TEXT("smoke: unknown-threat run"));
			State.Stage = 21;
			State.Time = 0.f;
			return true;
		}
		case 21:
		{
			FCoverSlot Middle;
			const bool bFound = CoverTraceRules::FindCoverSlotAt(World, State.P + State.F * 380.f + FVector(0.f, 0.f, 90.f), State.F, Middle);
			Check(State, bFound && Op->OrderTakeCover(Middle, true) == EOperativeOrderResult::Accepted, TEXT("cover order near the wall's middle accepted (no threat known)"));
			if (!bFound)
			{
				return Finish(State);
			}
			State.Stage = 22;
			State.Time = 0.f;
			return true;
		}
		case 22:
		{
			if (!Op->bInCover && State.Time < 10.f)
			{
				return true;
			}
			Check(State, Op->bInCover && !Op->bHasCoverThreat, TEXT("in cover, threat unknown"));
			FCoverSlot Shimmy;
			const FVector ClickAlong = State.P + State.F * 380.f - State.R * 120.f + FVector(0.f, 0.f, 90.f); // his right (-R)
			const bool bOk = CoverTraceRules::FindShimmySlot(World, Op->GetCoverSlot(), ClickAlong, Shimmy) && Op->OrderShimmyTo(Shimmy) == EOperativeOrderResult::Accepted;
			Check(State, bOk && Op->ShimmyDirection > 0.f && Op->IsShimmyForward() && Op->CoverFacing == ECoverFacing::Right,
				TEXT("unknown threat, moving right: forward, faces right"));
			State.bSampled = false;
			State.Stage = 23;
			State.Time = 0.f;
			return true;
		}
		case 23:
		{
			if (State.Time < 0.6f)
			{
				return true;
			}
			const UOperativeAnimInstance* Anim = Op->GetMesh() ? Cast<UOperativeAnimInstance>(Op->GetMesh()->GetAnimInstance()) : nullptr;
			if (!State.bSampled)
			{
				State.bSampled = true;
				CheckPlaying(State, Anim, TEXT("walk_fwd_loop_L"), TEXT("unknown threat, moving to his right (right after the cover entry)"));
			}
			if (Op->bShimmying && State.Time < 8.f)
			{
				return true;
			}
			FCoverSlot Shimmy;
			const FVector ClickAlong = State.P + State.F * 380.f + State.R * 120.f + FVector(0.f, 0.f, 90.f); // his left (+R)
			const bool bOk = CoverTraceRules::FindShimmySlot(World, Op->GetCoverSlot(), ClickAlong, Shimmy) && Op->OrderShimmyTo(Shimmy) == EOperativeOrderResult::Accepted;
			Check(State, bOk && Op->ShimmyDirection < 0.f && Op->IsShimmyForward() && Op->CoverFacing == ECoverFacing::Left,
				TEXT("unknown threat, moving left: forward, faces left"));
			State.Stage = 24;
			State.Time = 0.f;
			return true;
		}
		case 24:
		{
			if (State.Time < 0.6f)
			{
				return true;
			}
			const UOperativeAnimInstance* Anim = Op->GetMesh() ? Cast<UOperativeAnimInstance>(Op->GetMesh()->GetAnimInstance()) : nullptr;
			CheckPlaying(State, Anim, TEXT("walk_fwd_loop_R"), TEXT("unknown threat, moving to his left"));
			Op->bIgnoreCoverThreatForTesting = false;
			Op->LeaveCover(TEXT("smoke end"));
			if (State.Wall.IsValid())
			{
				State.Wall->Destroy();
			}
			return Finish(State);
		}
		default:
		{
			return Finish(State);
		}
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), 0.1f);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.CoverSmoke"),
		TEXT("Dev check of the Sprint 12 cover: wall / barricade slots, enter, shimmy, 90 % frontal absorb, flank, sight, laser -> crouch; PASS / FAIL."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
