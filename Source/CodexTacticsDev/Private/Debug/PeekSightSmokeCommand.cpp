// Dev-only headless check of the PEEK SIGHT (game designer 2026-10-19: "when an operative peeks out from cover he sees NOTHING")
// on L_MovementTest, exploring (the squad's symmetric sight runs, d380d83):
//   Scripts/smoke_parallel.ps1 -Commands PeekSightSmoke
// A runtime arena: a 2.5 m high wall (4 m long) 6 m ahead of the leader, a 1.2 m low wall (8 m long) 15 m to his left, a 4 m high
// screen behind the mates (parked prone 9 m back, so only the leader's eyes count) and one frozen frostbitten (senses off) moved
// round the walls. Checks:
//  1. standing free at the wall's end (not in cover): an enemy in the open past the corner is seen; one behind the wall is not;
//  2. hugging the high wall at its corner (no peek): the enemy round the corner is NOT seen (and does not see him - head down); the
//     one in the open past the corner still is (not blind at the corner);
//  3. the manual corner peek (the real click on the wall's end): the eye point leans past the edge, the enemy round the corner is
//     seen, the enemy sees the leaned-out point too (symmetric); an enemy 20 m back behind the wall - beyond the 18 m peek range -
//     is not; his drawn vision cone is the peek cone (90 deg) from the leaned-out point and covers the enemy round the corner;
//  4. the low wall, crouched in cover: an enemy 6 m behind it is hidden; aiming over the top (a cover shot) - seen; one 56 deg
//     off (over the wall, outside the 45 deg half cone) is not, one 35 deg off is;
//  5. the crouched corner lean round the low wall's end (the real click): the eyes stay at 95 cm, past the end; an enemy round
//     that end is seen;
//  6. prone behind the low wall (no peek): nothing behind it is seen (yesterday's rule).
// Nothing is saved.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/PeekSightRules.h"
#include "Combat/SightRules.h"
#include "Combat/TacticalSightSubsystem.h"
#include "Combat/VisionConeComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Tactics/CoverTraceRules.h"

namespace PeekSightSmoke
{
	constexpr float StepSeconds = 0.1f;

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		FVector P = FVector::ZeroVector;
		FVector F = FVector::ForwardVector;
		FVector R = FVector::RightVector;
		float GroundZ = 0.f;
		TWeakObjectPtr<AOperativeCharacter> Op;
		TWeakObjectPtr<AEnemyCharacter> Walker;
		TWeakObjectPtr<AStaticMeshActor> HighWall;
		TWeakObjectPtr<AStaticMeshActor> LowWall;
		TWeakObjectPtr<AStaticMeshActor> Screen;
		/** The mates' parking spots behind the screen (kept there: the formation would follow the leader round it). */
		TMap<TWeakObjectPtr<AOperativeCharacter>, FVector> Parked;
	};

	AStaticMeshActor* PeekSightSmokeSpawnBlock(UWorld* World, const FVector& Centre, const FRotator& Rotation, const FVector& Scale)
	{
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		AStaticMeshActor* Block = Cube ? World->SpawnActorDeferred<AStaticMeshActor>(AStaticMeshActor::StaticClass(), FTransform(Rotation, Centre, Scale),
			nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn) : nullptr;
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

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("PeekSightSmoke"));
		return false;
	}

	void Next(FState& State, int32 Stage)
	{
		State.Stage = Stage;
		State.StageTime = 0.f;
	}

	/** A point of the arena: Forward cm along F, Right cm along R from the leader's start, on the ground. */
	FVector At(const FState& State, float Forward, float Right)
	{
		const FVector Point = State.P + State.F * Forward + State.R * Right;
		return FVector(Point.X, Point.Y, State.GroundZ);
	}

	/** Moves the frozen frostbitten to the arena point and runs the squad's sight; true when the squad sees it. */
	bool PlaceWalkerAndLook(const FState& State, UTacticalSightSubsystem* Sight, float Forward, float Right)
	{
		AEnemyCharacter* Walker = State.Walker.Get();
		if (!Walker)
		{
			return false;
		}
		const FVector Spot = At(State, Forward, Right);
		Walker->SetActorLocation(FVector(Spot.X, Spot.Y, State.GroundZ + Walker->GetSimpleCollisionHalfHeight() + 2.f));
		Walker->SetActorRotation((-State.F).Rotation());
		Sight->Refresh();
		return Sight->IsVisibleToSquad(Walker) && !Walker->IsHidden();
	}

	/** Coordinates of a world point in the arena frame (F, R from the leader's start), for the log. */
	FString Frame(const FState& State, const FVector& Where)
	{
		const FVector D = Where - State.P;
		return FString::Printf(TEXT("(F %.0f, R %.0f, Z %.0f)"), FVector::DotProduct(D, State.F), FVector::DotProduct(D, State.R), Where.Z - State.GroundZ);
	}

	/** The real input: a click on the near face of Wall (F 585 in the arena frame) 20 cm in from its end at EndRight (PeekMinesSmoke). */
	bool ClickWallEnd(ACodexTacticsPlayerController* PC, AOperativeCharacter* Op, FState& State, AStaticMeshActor* Wall, float EndRight, float HeightCm)
	{
		const FVector Normal = -State.F;
		const FVector EdgePoint = At(State, 585.f, EndRight - FMath::Sign(EndRight - FVector::DotProduct(Op->GetActorLocation() - State.P, State.R)) * 20.f)
			+ FVector(0.f, 0.f, HeightCm);
		const FHitResult EdgeHit(Wall, Wall->GetStaticMeshComponent(), EdgePoint, Normal);
		ECoverFacing Side = ECoverFacing::Left;
		float Edge = 0.f;
		const bool bTarget = PC->FindCornerPeekForHit(Op, EdgeHit, Side, Edge);
		Check(State, bTarget, FString::Printf(TEXT("the wall's end at %s is a corner peek target (side %s, %.0f cm)"), *Frame(State, EdgePoint),
			Side == ECoverFacing::Left ? TEXT("left") : TEXT("right"), Edge));
		return bTarget && PC->TryHandleCoverClick(EdgeHit, nullptr) && Op->IsManualCornerPeek();
	}

	bool TakeCoverAt(UWorld* World, AOperativeCharacter* Op, const FState& State, float Forward, float Right, float HeightCm)
	{
		const FVector Click = At(State, Forward, Right) + FVector(0.f, 0.f, HeightCm);
		FCoverSlot Slot;
		const bool bFound = CoverTraceRules::FindCoverSlotAt(World, Click, State.F, Slot);
		return bFound && Op->OrderTakeCover(Slot, /*bSprint*/ false) == EOperativeOrderResult::Accepted;
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World)
		{
			return false;
		}
		if (State.Time < 3.f)
		{
			return true;
		}
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UTacticalSightSubsystem* Sight = World->GetSubsystem<UTacticalSightSubsystem>();
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(World->GetFirstPlayerController());
		if (!Flow || !Squad || !Sight || !PC || !Squad->GetLeader() || State.Time > 180.f)
		{
			Check(State, false, FString::Printf(TEXT("subsystems / controller / timeout (stage %d)"), State.Stage));
			return Finish(State, false);
		}
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			Member->ColdLevel = 0.f;
			Member->bTacticalCeaseFire = true; // nobody shoots the frozen frostbitten
		}
		for (const TPair<TWeakObjectPtr<AOperativeCharacter>, FVector>& Pair : State.Parked)
		{
			AOperativeCharacter* Mate = Pair.Key.Get();
			if (Mate && (FVector::Dist2D(Mate->GetActorLocation(), Pair.Value) > 50.f || Mate->GetStance() != EOperativeStance::Prone))
			{
				Mate->StopOperative();
				Mate->TeleportTo(Pair.Value + FVector(0.f, 0.f, Mate->GetSimpleCollisionHalfHeight() + 5.f), State.F.Rotation(), false, true);
				Mate->SetStance(EOperativeStance::Prone);
			}
		}
		AOperativeCharacter* Op = State.Op.Get();
		AEnemyCharacter* Walker = State.Walker.Get();
		if (State.Stage > 0 && (!Op || !Walker))
		{
			Check(State, false, TEXT("leader / frostbitten lost"));
			return Finish(State, false);
		}
		switch (State.Stage)
		{
		case 0:
		{
			Check(State, Flow->GetPhase() == ECodexGamePhase::Exploration && Sight->IsSquadSightActive() && !Sight->IsActive(),
				TEXT("exploring: the squad's sight runs"));
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				It->Destroy();
			}
			State.Op = Squad->GetLeader();
			Op = State.Op.Get();
			State.F = Op->GetActorForwardVector().GetSafeNormal2D();
			State.R = FVector::CrossProduct(FVector::UpVector, State.F);
			State.P = Op->GetActorLocation();
			State.GroundZ = Op->GetActorLocation().Z - Op->GetSimpleCollisionHalfHeight();
			const FRotator Facing = State.F.Rotation();
			// The high wall: faces at F 585 / 615, ends at R -200 / +200, 2.5 m. The low wall: same depth, R -1900..-1100, 1.2 m.
			State.HighWall = PeekSightSmokeSpawnBlock(World, At(State, 600.f, 0.f) + FVector(0.f, 0.f, 125.f), Facing, FVector(0.3f, 4.f, 2.5f));
			State.LowWall = PeekSightSmokeSpawnBlock(World, At(State, 600.f, -1500.f) + FVector(0.f, 0.f, 60.f), Facing, FVector(0.3f, 8.f, 1.2f));
			State.Screen = PeekSightSmokeSpawnBlock(World, At(State, -700.f, -500.f) + FVector(0.f, 0.f, 200.f), Facing, FVector(0.3f, 40.f, 4.f));
			const FVector WalkerSpot = At(State, 2000.f, 3000.f);
			State.Walker = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::Frostbitten, WalkerSpot + FVector(0.f, 0.f, 100.f), (-State.F).Rotation());
			Walker = State.Walker.Get();
			Check(State, State.HighWall.IsValid() && State.LowWall.IsValid() && State.Screen.IsValid() && Walker != nullptr, TEXT("arena: high wall, low wall, screen, frostbitten"));
			if (!Walker || !State.HighWall.IsValid() || !State.LowWall.IsValid())
			{
				return Finish(State, false);
			}
			Walker->bOverridePerception = true;
			Walker->PerceptionOverride.SightRangeCm = 0.f;
			Walker->PerceptionOverride.ProximityCm = 0.f;
			Walker->PerceptionOverride.HearWalkCm = Walker->PerceptionOverride.HearRunCm = 0.f;
			Walker->PerceptionOverride.HearCrouchWalkCm = Walker->PerceptionOverride.HearCrawlCm = 0.f;
			Walker->PerceptionOverride.SmellRadiusCm = 0.f;
			Walker->CustomTimeDilation = 0.f;
			Walker->SetActorTickEnabled(false);
			int32 Index = 0;
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Member->StopOperative();
				if (Member != Op)
				{
					// Prone behind the screen: only the leader's eyes count.
					const FVector Back = At(State, -900.f, Index++ % 2 == 0 ? -200.f : 200.f);
					Member->TeleportTo(Back + FVector(0.f, 0.f, Member->GetSimpleCollisionHalfHeight() + 5.f), Facing, false, true);
					Member->SetStance(EOperativeStance::Prone);
					State.Parked.Add(Member, Back);
				}
			}
			Op->bIgnoreCoverThreatForTesting = true; // no automatic corner hold: hugging stays hugging until the peek is ordered
			Op->bShowVisionCone = true;
			// Standing free at the high wall's +R end (not in cover), 45 cm in front of it.
			const FVector Free = At(State, 540.f, 150.f);
			Op->TeleportTo(Free + FVector(0.f, 0.f, Op->GetSimpleCollisionHalfHeight() + 5.f), Facing, false, true);
			Op->SetStance(EOperativeStance::Standing);
			Next(State, 1);
			return true;
		}
		case 1:
		{
			if (State.StageTime < 2.f)
			{
				return true;
			}
			Check(State, !Op->bInCover && Op->GetPeekSightPose() == EPeekSightPose::None, FString::Printf(TEXT("standing free at the wall's end %s"),
				*Frame(State, Op->GetActorLocation())));
			Check(State, PlaceWalkerAndLook(State, Sight, 800.f, 800.f), TEXT("standing at the END of the wall: an enemy in the open past the corner (F 800, R 800) is seen"));
			Check(State, PlaceWalkerAndLook(State, Sight, 540.f, 1300.f), TEXT("... and one along the wall line beyond the corner (F 540, R 1300)"));
			Check(State, !PlaceWalkerAndLook(State, Sight, 1015.f, 350.f), TEXT("... one 4 m behind the wall, 1.5 m past its end, is hidden by the wall"));
			Check(State, TakeCoverAt(World, Op, State, 570.f, 150.f, 90.f), TEXT("he takes cover at the high wall near its +R end"));
			Next(State, 2);
			return true;
		}
		case 2:
		{
			if ((!Op->bInCover || Op->bShimmying) && State.StageTime < 15.f)
			{
				return true;
			}
			if (State.StageTime < 2.f)
			{
				return true; // settled (a snap to the corner)
			}
			Check(State, Op->bInCover && Op->IsInHighCover(), FString::Printf(TEXT("in high cover %s (%.1f s)"), *Frame(State, Op->GetActorLocation()), State.StageTime));
			Check(State, Op->GetPeekSightPose() == EPeekSightPose::None, TEXT("hugging the wall: no peek view"));
			Check(State, !PlaceWalkerAndLook(State, Sight, 1015.f, 350.f), TEXT("hugging at the corner: the enemy round the corner (F 1015, R 350) is NOT seen"));
			Check(State, Op->IsHiddenInCoverFrom(Walker->GetActorLocation())
				&& !Sight->HasClearSight(UTacticalSightSubsystem::EyePoint(*Walker), UTacticalSightSubsystem::ProfilePoint(*Op)),
				TEXT("... and it does not see him (head down behind the wall)"));
			Check(State, PlaceWalkerAndLook(State, Sight, 800.f, 800.f), TEXT("hugging at the corner: an enemy in the open past the corner is still seen (not blind)"));
			Check(State, ClickWallEnd(PC, Op, State, State.HighWall.Get(), 200.f, 150.f), TEXT("a click on the wall's +R end orders the corner peek"));
			Next(State, 3);
			return true;
		}
		case 3:
		{
			if (!Op->IsPeekingRoundCorner() && State.StageTime < 15.f)
			{
				return true;
			}
			if (State.StageTime < 0.6f)
			{
				return true;
			}
			FVector Eye;
			FVector Direction;
			const bool bPeek = Op->GetPeekSight(Eye, Direction);
			const float EyeRight = static_cast<float>(FVector::DotProduct(Eye - State.P, State.R));
			Check(State, Op->IsPeekingRoundCorner() && bPeek && Op->GetPeekSightPose() == EPeekSightPose::CornerLean,
				FString::Printf(TEXT("the corner peek holds: peek view from %s, aim yaw %.0f deg off F"), *Frame(State, Eye),
					FMath::RadiansToDegrees(FMath::Atan2(FVector::DotProduct(Direction, State.R), FVector::DotProduct(Direction, State.F)))));
			Check(State, bPeek && EyeRight >= 215.f && FMath::IsNearlyEqual(static_cast<float>(Eye.Z - State.GroundZ), SightRules::EyeHeight(EOperativeStance::Standing), 15.f),
				FString::Printf(TEXT("the eye point leans past the corner edge (R %.0f > 200) at the standing eyes"), EyeRight));
			Check(State, PlaceWalkerAndLook(State, Sight, 1015.f, 350.f), TEXT("peeking: the enemy round the corner is SEEN"));
			Check(State, !Op->IsHiddenInCoverFrom(Walker->GetActorLocation())
				&& Sight->HasClearSight(UTacticalSightSubsystem::EyePoint(*Walker), UTacticalSightSubsystem::ProfilePoint(*Op)),
				FString::Printf(TEXT("... and it sees his leaned-out head (symmetric; exposed point %s)"), *Frame(State, UTacticalSightSubsystem::ProfilePoint(*Op))));
			Check(State, Op->CanHitEnemy(Walker), TEXT("... and targetable from the corner (a shot line from the lean-out)"));
			Check(State, !PlaceWalkerAndLook(State, Sight, 2600.f, 240.f), TEXT("an enemy 20 m back behind the wall (beyond the 18 m peek range) is not seen"));
			Check(State, PlaceWalkerAndLook(State, Sight, 1015.f, 350.f), TEXT("back round the corner: seen again"));
			Next(State, 4);
			return true;
		}
		case 4:
		{
			if (State.StageTime < 0.6f)
			{
				return true; // the cone's occluded rebuild
			}
			UVisionConeComponent* Cone = Op->VisionCone;
			Check(State, Cone && Cone->IsConeShown() && Cone->HasViewOverride() && FMath::IsNearlyEqual(Cone->GetConeHalfAngleDeg(), Op->PeekSightConfig.HalfAngleDeg, 0.5f)
				&& FMath::IsNearlyEqual(Cone->GetConeRangeCm(), Op->PeekSightConfig.RangeCm, 1.f),
				FString::Printf(TEXT("his drawn cone is the peek cone (half %.0f deg, %.0f cm) from the leaned-out point"), Cone ? Cone->GetConeHalfAngleDeg() : -1.f,
					Cone ? Cone->GetConeRangeCm() : -1.f));
			Check(State, Cone && Cone->GetDrawnLevelAt(At(State, 1015.f, 350.f)) > 0 && Cone->GetDrawnLevelAt(At(State, 1015.f, -150.f)) == 0,
				TEXT("... it covers the enemy round the corner, not the space behind the wall's middle"));
			Op->LeaveCover(TEXT("PeekSightSmoke: to the low wall"));
			Check(State, TakeCoverAt(World, Op, State, 570.f, -1500.f, 60.f), TEXT("he takes cover at the 1.2 m low wall"));
			Next(State, 5);
			return true;
		}
		case 5:
		{
			if ((!Op->bInCover || Op->bShimmying) && State.StageTime < 20.f)
			{
				return true;
			}
			if (State.StageTime < 1.5f)
			{
				return true;
			}
			Check(State, Op->bInCover && Op->CurrentCoverHeight == ECoverHeight::LowCover && Op->GetStance() == EOperativeStance::Crouching,
				FString::Printf(TEXT("crouched in low cover %s (wall top %.0f cm)"), *Frame(State, Op->GetActorLocation()), Op->GetCoverSlot().WallTopCm));
			Check(State, Op->GetPeekSightPose() == EPeekSightPose::None, TEXT("crouched behind it, no aim: no peek view"));
			Check(State, !PlaceWalkerAndLook(State, Sight, 1215.f, -1450.f), TEXT("crouched (eyes 95 cm) behind the 1.2 m wall: an enemy 6 m behind it is hidden"));
			Check(State, !PlaceWalkerAndLook(State, Sight, 1100.f, -900.f), TEXT("... and one round its +R end"));
			Op->SetCoverFireMode(ECoverFireMode::CornerLean);
			Op->PlayCoverShot(nullptr, false); // aims over the top (the cover shot's corner aim)
			Next(State, 6);
			return true;
		}
		case 6:
		{
			if (State.StageTime < 0.3f)
			{
				return true;
			}
			FVector Eye;
			FVector Direction;
			const bool bPeek = Op->GetPeekSight(Eye, Direction);
			Check(State, bPeek && Op->GetPeekSightPose() == EPeekSightPose::OverTop && Eye.Z - State.GroundZ >= 130.f,
				FString::Printf(TEXT("aiming over the low wall: the eyes above its top %s"), *Frame(State, Eye)));
			Check(State, PlaceWalkerAndLook(State, Sight, 1215.f, -1450.f), TEXT("over the top: the enemy 6 m behind the wall is SEEN"));
			// From the over-the-top eye (F ~565, R -1500): 35 deg off -> in the 45 deg half cone; 56 deg off -> outside.
			Check(State, PlaceWalkerAndLook(State, Sight, 565.f + 600.f * 0.819f, -1500.f + 600.f * 0.574f), TEXT("... one 35 deg off across the wall is seen"));
			Check(State, !PlaceWalkerAndLook(State, Sight, 565.f + 600.f * 0.559f, -1500.f + 600.f * 0.829f), TEXT("... one 56 deg off (outside the peek cone) is not"));
			// Closer to the low wall's +R end (its corner peek target is found within the probe range of the slot).
			Op->LeaveCover(TEXT("PeekSightSmoke: to the low wall's end"));
			Check(State, TakeCoverAt(World, Op, State, 570.f, -1250.f, 60.f), TEXT("he takes cover 1.5 m from the low wall's +R end"));
			Next(State, 61);
			return true;
		}
		case 61:
		{
			if ((!Op->bInCover || Op->bShimmying) && State.StageTime < 20.f)
			{
				return true;
			}
			if (State.StageTime < 1.5f)
			{
				return true;
			}
			const FCoverSlot& Slot = Op->GetCoverSlot();
			Check(State, Op->bInCover && Op->CurrentCoverHeight == ECoverHeight::LowCover, FString::Printf(TEXT("in low cover near its end %s (slot %s, edges L %d %.0f / R %d %.0f)"),
				*Frame(State, Op->GetActorLocation()), *Frame(State, Slot.WorldLocation), Slot.bLeftEdgeExposed ? 1 : 0, Slot.LeftEdgeDistanceCm,
				Slot.bRightEdgeExposed ? 1 : 0, Slot.RightEdgeDistanceCm));
			Check(State, !PlaceWalkerAndLook(State, Sight, 1100.f, -900.f), TEXT("crouched behind it near the end: the enemy round the end is hidden"));
			Check(State, ClickWallEnd(PC, Op, State, State.LowWall.Get(), -1100.f, 90.f), TEXT("a click on the low wall's +R end orders the crouched corner lean"));
			Next(State, 7);
			return true;
		}
		case 7:
		{
			if (!Op->IsPeekingRoundCorner() && State.StageTime < 20.f)
			{
				return true;
			}
			if (State.StageTime < 0.6f)
			{
				return true;
			}
			FVector Eye;
			FVector Direction;
			const bool bPeek = Op->GetPeekSight(Eye, Direction);
			const float EyeRight = static_cast<float>(FVector::DotProduct(Eye - State.P, State.R));
			Check(State, Op->IsPeekingRoundCorner() && bPeek && Op->GetPeekSightPose() == EPeekSightPose::CornerLean
				&& FMath::IsNearlyEqual(static_cast<float>(Eye.Z - State.GroundZ), SightRules::EyeHeight(EOperativeStance::Crouching), 15.f) && EyeRight > -1100.f,
				FString::Printf(TEXT("crouched corner lean: crouched eyes past the low wall's end %s"), *Frame(State, Eye)));
			Check(State, PlaceWalkerAndLook(State, Sight, 1100.f, -900.f), TEXT("... the enemy round that end is SEEN"));
			Op->SetStance(EOperativeStance::Prone);
			Next(State, 8);
			return true;
		}
		case 8:
		{
			if (State.StageTime < 1.f)
			{
				return true;
			}
			Check(State, Op->GetPeekSightPose() == EPeekSightPose::None, FString::Printf(TEXT("prone (in cover %d): no peek view"), Op->bInCover ? 1 : 0));
			Check(State, !PlaceWalkerAndLook(State, Sight, 1215.f, -1450.f), TEXT("prone behind the low wall, no peek: the enemy behind it is not seen"));
			Check(State, !PlaceWalkerAndLook(State, Sight, 1100.f, -900.f), TEXT("... nor the one round its end"));
			return Finish(State, true);
		}
		default:
			return Finish(State, false);
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.PeekSightSmoke"),
		TEXT("Dev check of the peek sight (2026-10-19): hugging a wall sees nothing round its corner, the corner peek / corner lean / aim over a low wall sees in the limited peek cone from the leaned-out eyes; PASS / FAIL."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
