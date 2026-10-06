// Dev-only headless check of the Sprint 12 tactical cover on L_MovementTest (nothing is saved):
//   Scripts/smoke.ps1 -Command CodexTactics.CoverSmoke -Log Smoke-Cover.log
// A wave fight with the wave removed; a 3 m wall 4 m ahead of the leader and a 60 cm barricade beyond its end are
// spawned at runtime. Checks: the wall is found as high cover (slot 45 cm off it, corners probed) and the barricade as
// low cover; the leader sprints to the slot and enters cover (back to the wall, standing); a click along the wall
// shimmies him (side-step, still in cover); a hit from behind the wall is absorbed 90 %, a crit from there is no
// headshot, a flank hit passes fully; an enemy diagonally behind the wall (clear trace past the corner, out of earshot)
// does not perceive him until he leans out; Commander Mode: a marksman's laser makes him crouch and hold fire, the
// laser gone he stands and peeks again; the corner shot leaves round the corner; a ground order leaves the cover.

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
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "Interactables/BarricadeActor.h"
#include "Survival/ColdSurvivalComponent.h"
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
	};

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
			Member->bTacticalCeaseFire = true; // nobody shoots the frozen enemies
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
			const float YawOff = FMath::Abs(FRotator::NormalizeAxis(Op->GetActorRotation().Yaw - (-State.F).Rotation().Yaw));
			Check(State, YawOff < 10.f, FString::Printf(TEXT("back to the wall (facing %.0f deg off the normal)"), YawOff));
			Check(State, FVector::Dist2D(Op->GetActorLocation(), State.Slot.WorldLocation) < 60.f, TEXT("standing on the slot"));
			const UOperativeAnimInstance* Anim = Op->GetMesh() ? Cast<UOperativeAnimInstance>(Op->GetMesh()->GetAnimInstance()) : nullptr;
			Check(State, Anim && Anim->bInCover && Anim->CoverHeight == ECoverHeight::HighCover, TEXT("AnimInstance: bInCover / CoverHeight set"));
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: cover clips played so far %d (loop %s)"), Anim ? Anim->GetCoverClipsPlayed() : -1,
				Anim && Anim->GetCoverLoopClip() ? *Anim->GetCoverLoopClip()->GetName() : TEXT("none"));
			Check(State, Op->CanFireFromCover(), TEXT("a corner to fire round"));
			Check(State, FVector::Dist2D(Op->GetCoverFireOrigin(), Op->GetMuzzleLocation()) >= 59.f, TEXT("the corner shot leaves round the corner"));
			State.SlotEntered = Op->GetActorLocation();
			// Shimmy 1.5 m along the wall, away from the corner (towards the wall's middle).
			FCoverSlot Shimmy;
			const FVector ClickAlong = State.P + State.F * 380.f + State.R * 100.f + FVector(0.f, 0.f, 90.f);
			const bool bShimmy = CoverTraceRules::FindShimmySlot(World, Op->GetCoverSlot(), ClickAlong, Shimmy);
			Check(State, bShimmy && CoverTraceRules::IsSameWall(Op->GetCoverSlot(), Shimmy), TEXT("a click 1.5 m along the wall is a shimmy target"));
			const bool bShimmyOrdered = bShimmy && Op->OrderShimmyTo(Shimmy) == EOperativeOrderResult::Accepted;
			Check(State, bShimmyOrdered && Op->bShimmying && Op->ShimmyDirection != 0.f,
				FString::Printf(TEXT("shimmy ordered (direction %.0f)"), Op->ShimmyDirection));
			State.Stage = 3;
			State.Time = 0.f;
			return true;
		}
		case 3:
		{
			if (Op->bShimmying && State.Time < 8.f)
			{
				return true;
			}
			const float Moved = FVector::Dist2D(Op->GetActorLocation(), State.SlotEntered);
			Check(State, !Op->bShimmying && Op->bInCover && Moved >= 100.f, FString::Printf(TEXT("shimmied %.0f cm along the wall, still in cover (%.1f s)"), Moved, State.Time));
			const float YawOff = FMath::Abs(FRotator::NormalizeAxis(Op->GetActorRotation().Yaw - (-State.F).Rotation().Yaw));
			Check(State, YawOff < 10.f, TEXT("back still to the wall after the shimmy"));
			// Damage through the wall: the frozen hound 3 m behind it.
			AEnemyCharacter* Hound = State.Hound.Get();
			if (!Hound)
			{
				Check(State, false, TEXT("hound alive"));
				return Finish(State);
			}
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
			// Flank: the hound at the wall's end, along it (90 deg off the wall) -> full damage.
			Hound->SetActorLocation(FVector(State.P.X, State.P.Y, State.GroundZ + Hound->GetSimpleCollisionHalfHeight() + 2.f) + State.F * 355.f + State.R * 600.f);
			Op->ForcedDodgeRollForTesting = 0.f;
			Before = Health->GetCurrentHealth();
			Taken = Op->TakeHit(100.f, TEXT("smoke"), false, false, Hound);
			Check(State, FMath::IsNearlyEqual(Taken, Base, 0.5f), FString::Printf(TEXT("a flank hit passes fully: %.1f"), Taken));
			Check(State, Op->RecentIncomingDamage > 0.f, TEXT("recent incoming damage tracked for the cover decisions"));
			// Sight: diagonally behind the wall (77 deg off its normal, 23 m: out of earshot), where the straight line to him passes
			// the wall's end (he stands 2 m from it, the line clears the near face by ~0.7 m) — only the cover rule hides him.
			Hound->SetActorLocation(FVector(Op->GetActorLocation().X, Op->GetActorLocation().Y, State.GroundZ + Hound->GetSimpleCollisionHalfHeight() + 2.f)
				+ State.F * 500.f + State.R * 2250.f);
			State.Stage = 4;
			State.Time = 0.f;
			return true;
		}
		case 4:
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
			State.Stage = 5;
			State.Time = 0.f;
			return true;
		}
		case 5:
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
			State.Stage = 6;
			State.Time = 0.f;
			return true;
		}
		case 6:
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
			State.Stage = 7;
			State.Time = 0.f;
			return true;
		}
		case 7:
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
			// A ground order leaves the cover.
			Check(State, Op->OrderMoveTo(State.P, false) == EOperativeOrderResult::Accepted && !Op->bInCover, TEXT("a move order leaves the cover"));
			State.Stage = 8;
			State.Time = 0.f;
			return true;
		}
		case 8:
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
			State.Stage = 9;
			State.Time = 0.f;
			return true;
		}
		default:
		{
			if (!Op->bInCover && State.Time < 10.f)
			{
				return true;
			}
			Check(State, Op->bInCover && Op->CurrentCoverHeight == ECoverHeight::HighCover, FString::Printf(TEXT("... and enters the cover on arrival (%.1f s)"), State.Time));
			Op->LeaveCover(TEXT("smoke end"));
			if (State.Wall.IsValid())
			{
				State.Wall->Destroy();
			}
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
