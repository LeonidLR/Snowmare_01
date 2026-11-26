// Runtime smoke for task 145 on L_Gen_Depot_1_X6:
//  1. Multi-select Shift+click via HandleWorldHit gives identical world yaw to both operatives; L-shaped walk keeps locked yaw +-10 deg.
//  2. Standing on an X6 roof looking down via Shift+click on ground below drives AnimInstance AimPitch < -10 deg
//     and bends the skeletal pose/muzzle pitch downward relative to level baseline.
//  3. Part B: stale SetFacingPoint and stale ManualLookDirection each clear cleanly on sprint; arrival stays within +-15 deg of travel yaw.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeAnimInstance.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "CollisionQueryParams.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Debug/SmokeUtils.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Tactics/LookHoldRules.h"

namespace LookDirectionSmoke
{
	constexpr float StepSeconds = 0.05f;
	constexpr float TimeoutSeconds = 120.f;

	enum class EStage : uint8
	{
		Setup,
		WalkLeg1,
		WalkLeg2,
		RoofLevel,
		RoofDown,
		RampFootToRoof,
		RoofSprintClear,
		RoofSprintSettle,
		SprintClearFacing,
		SettleFacing,
		SprintClearLook,
		SettleLook,
		Done
	};

	struct FRoofTestSpot
	{
		bool bFound = false;
		FVector RoofStandFeet = FVector::ZeroVector;
		FVector Outward = FVector::ForwardVector;
		FVector GroundBelow = FVector::ZeroVector;
	};

	struct FRampRoofSpot
	{
		bool bFound = false;
		FString BuildingId;
		FVector RampFoot = FVector::ZeroVector;
		FVector RoofArrive = FVector::ZeroVector;
		FVector RoofSprintGoal = FVector::ZeroVector;
		float RoofZ = 0.f;
	};

	struct FState
	{
		TWeakObjectPtr<UWorld> World;
		SmokeUtils::FPreloadGate Gate;
		EStage Stage = EStage::Setup;
		float TotalTime = 0.f;
		float StageTime = 0.f;
		int32 Failures = 0;
		bool bDone = false;

		TWeakObjectPtr<AOperativeCharacter> Leader;
		TWeakObjectPtr<AOperativeCharacter> Mate;
		FVector ArenaOrigin = FVector::ZeroVector;
		FVector ArenaForward = FVector::ForwardVector;
		FVector ArenaRight = FVector::RightVector;
		FVector Leg1Goal = FVector::ZeroVector;
		FVector Leg2Goal = FVector::ZeroVector;
		FVector SprintGoal = FVector::ZeroVector;
		float LockedYawDeg = 0.f;
		int32 Leg1Samples = 0;
		int32 Leg1Bad = 0;
		int32 Leg2Samples = 0;
		int32 Leg2Bad = 0;

		FRoofTestSpot RoofSpot;
		FRampRoofSpot RampSpot;
		FVector RoofSprintStart = FVector::ZeroVector;
		float LastMovingTravelYaw = 0.f;
		bool bHasMovingTravelYaw = false;
		float RestMeshYaw = 0.f;
		float LevelAimPitch = 0.f;
		float LevelPosePitch = 0.f;
		float LevelMuzzlePitch = 0.f;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("LookDirectionSmoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("LookDirectionSmoke RESULT: %s"), (State.Failures == 0 && bComplete) ? TEXT("PASS") : TEXT("FAIL"));
		State.bDone = true;
		FPlatformMisc::RequestExit(false, TEXT("LookDirectionSmoke"));
		return false;
	}

	void Next(FState& State, EStage NewStage)
	{
		State.Stage = NewStage;
		State.StageTime = 0.f;
	}

	void Arm(AOperativeCharacter& Op)
	{
		if (USkeletalMeshComponent* Mesh = Op.GetMesh())
		{
			Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			Mesh->bPauseAnims = false;
			Mesh->RefreshBoneTransforms();
		}
	}

	float MeasureSpineHeadPitchDeg(const AOperativeCharacter& Op)
	{
		const USkeletalMeshComponent* Mesh = Op.GetMesh();
		if (!Mesh)
		{
			return 0.f;
		}
		const FVector Spine = Mesh->GetBoneLocation(TEXT("spine_01"));
		const FVector Head = Mesh->GetBoneLocation(TEXT("head"));
		const FVector Delta = Head - Spine;
		const float Horiz = FMath::Max(static_cast<float>(Delta.Size2D()), 1.f);
		return FMath::RadiansToDegrees(FMath::Atan2(static_cast<float>(Delta.Z), Horiz));
	}

	float MeasureMuzzlePitchDeg(const AOperativeCharacter& Op)
	{
		const USkeletalMeshComponent* Mesh = Op.GetMesh();
		if (!Mesh)
		{
			return 0.f;
		}
		const FVector Shoulder = Mesh->GetBoneLocation(TEXT("clavicle_r"));
		const FVector Muzzle = Op.GetWeaponMuzzleLocation();
		const FVector Delta = Muzzle - Shoulder;
		const float Horiz = FMath::Max(static_cast<float>(Delta.Size2D()), 5.f);
		return FMath::RadiansToDegrees(FMath::Atan2(static_cast<float>(Delta.Z), Horiz));
	}

	FRoofTestSpot FindX6RoofLookSpot(UWorld* World, const TArray<const AActor*>& Ignored)
	{
		FRoofTestSpot Out;
		FString LevelText;
		TSharedPtr<FJsonObject> Level;
		const FString LevelFile = FPaths::ProjectContentDir() / TEXT("Data/LevelJson/L_Gen_Depot_1_X6.json");
		FString LayoutRelPath;
		if (!FFileHelper::LoadFileToString(LevelText, *LevelFile)
			|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(LevelText), Level) || !Level.IsValid()
			|| !Level->TryGetStringField(TEXT("levelgen_layout"), LayoutRelPath))
		{
			return Out;
		}
		FString LayoutText;
		TSharedPtr<FJsonObject> Layout;
		const TArray<TSharedPtr<FJsonValue>>* Buildings = nullptr;
		if (!FFileHelper::LoadFileToString(LayoutText, *(FPaths::ProjectDir() / LayoutRelPath))
			|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(LayoutText), Layout) || !Layout.IsValid()
			|| !Layout->TryGetArrayField(TEXT("buildings"), Buildings) || !Buildings)
		{
			return Out;
		}
		double GroundZRaw = 0.0;
		Layout->TryGetNumberField(TEXT("ground_z"), GroundZRaw);
		const float GroundZ = static_cast<float>(GroundZRaw);
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		if (!Nav)
		{
			return Out;
		}
		FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(LookDirRoofFind), false);
		for (const AActor* Ig : Ignored)
		{
			TraceParams.AddIgnoredActor(Ig);
		}
		for (const TSharedPtr<FJsonValue>& BV : *Buildings)
		{
			const TSharedPtr<FJsonObject> B = BV.IsValid() ? BV->AsObject() : nullptr;
			if (!B.IsValid())
			{
				continue;
			}
			bool bWalkable = false;
			B->TryGetBoolField(TEXT("roof_walkable"), bWalkable);
			double FloorsRaw = 0.0;
			const TArray<TSharedPtr<FJsonValue>>* Footprint = nullptr;
			if (!bWalkable || !B->TryGetNumberField(TEXT("floors"), FloorsRaw)
				|| !B->TryGetArrayField(TEXT("footprint"), Footprint) || !Footprint || Footprint->Num() < 2)
			{
				continue;
			}
			const TArray<TSharedPtr<FJsonValue>>* MinArr = nullptr;
			const TArray<TSharedPtr<FJsonValue>>* MaxArr = nullptr;
			if (!(*Footprint)[0]->TryGetArray(MinArr) || !(*Footprint)[1]->TryGetArray(MaxArr)
				|| !MinArr || !MaxArr || MinArr->Num() < 2 || MaxArr->Num() < 2)
			{
				continue;
			}
			const float RoofZ = GroundZ + static_cast<float>(FloorsRaw) * 300.f;
			const FVector2D Min((*MinArr)[0]->AsNumber(), (*MinArr)[1]->AsNumber());
			const FVector2D Max((*MaxArr)[0]->AsNumber(), (*MaxArr)[1]->AsNumber());
			const FVector2D Centre = (Min + Max) * 0.5;
			const TPair<FVector, FVector> Edges[] = {
				{ FVector(Max.X - 110.f, Centre.Y, RoofZ), FVector(1.f, 0.f, 0.f) },
				{ FVector(Min.X + 110.f, Centre.Y, RoofZ), FVector(-1.f, 0.f, 0.f) },
				{ FVector(Centre.X, Max.Y - 110.f, RoofZ), FVector(0.f, 1.f, 0.f) },
				{ FVector(Centre.X, Min.Y + 110.f, RoofZ), FVector(0.f, -1.f, 0.f) }
			};
			for (const TPair<FVector, FVector>& Edge : Edges)
			{
				FNavLocation RoofNav;
				if (!Nav->ProjectPointToNavigation(Edge.Key, RoofNav, FVector(200.f, 200.f, 150.f))
					|| FMath::Abs(static_cast<float>(RoofNav.Location.Z) - RoofZ) > 90.f)
				{
					UE_LOG(LogCodexTactics, Verbose, TEXT("LookDirectionSmoke roof edge skip (no RoofNav at %s, got Z=%.1f vs %.1f)"),
						*Edge.Key.ToCompactString(), RoofNav.Location.Z, RoofZ);
					continue;
				}
				for (const float DistCm : { 500.f, 700.f, 900.f, 1100.f, 1400.f, 1800.f })
				{
					const FVector GroundQuery = RoofNav.Location + Edge.Value * DistCm + FVector(0.f, 0.f, -RoofZ + GroundZ);
					FNavLocation GroundNav;
					if (!Nav->ProjectPointToNavigation(GroundQuery, GroundNav, FVector(250.f, 250.f, 150.f))
						|| FMath::Abs(static_cast<float>(GroundNav.Location.Z) - GroundZ) > 100.f)
					{
						continue;
					}
					const FVector OverEye = RoofNav.Location + Edge.Value * 60.f + FVector(0.f, 0.f, 155.f);
					FHitResult LosHit;
					if (World->LineTraceSingleByChannel(LosHit, OverEye,
						GroundNav.Location + FVector(0.f, 0.f, 90.f), ECC_Visibility, TraceParams))
					{
						continue;
					}
					if (LookHoldRules::DirectionFromClick(RoofNav.Location + FVector(0.f, 0.f, 88.f), GroundNav.Location).PitchDeg >= -12.f)
					{
						continue;
					}
					Out.bFound = true;
					Out.RoofStandFeet = RoofNav.Location;
					Out.Outward = Edge.Value;
					Out.GroundBelow = GroundNav.Location;
					return Out;
				}
			}
		}
		UE_LOG(LogCodexTactics, Warning, TEXT("LookDirectionSmoke: FindX6RoofLookSpot found no roof/ground pair across %d buildings (GroundZ=%.1f)"),
			Buildings->Num(), GroundZ);
		return Out;
	}

	FRampRoofSpot FindX6RampToRoof(UWorld* World)
	{
		FRampRoofSpot Out;
		FString LevelText;
		TSharedPtr<FJsonObject> Level;
		const FString LevelFile = FPaths::ProjectContentDir() / TEXT("Data/LevelJson/L_Gen_Depot_1_X6.json");
		FString LayoutRelPath;
		if (!FFileHelper::LoadFileToString(LevelText, *LevelFile)
			|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(LevelText), Level) || !Level.IsValid()
			|| !Level->TryGetStringField(TEXT("levelgen_layout"), LayoutRelPath))
		{
			return Out;
		}
		FString LayoutText;
		TSharedPtr<FJsonObject> Layout;
		const TArray<TSharedPtr<FJsonValue>>* Buildings = nullptr;
		if (!FFileHelper::LoadFileToString(LayoutText, *(FPaths::ProjectDir() / LayoutRelPath))
			|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(LayoutText), Layout) || !Layout.IsValid()
			|| !Layout->TryGetArrayField(TEXT("buildings"), Buildings) || !Buildings)
		{
			return Out;
		}
		double GroundZRaw = 0.0;
		Layout->TryGetNumberField(TEXT("ground_z"), GroundZRaw);
		const float GroundZ = static_cast<float>(GroundZRaw);
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		if (!Nav)
		{
			return Out;
		}
		for (const TSharedPtr<FJsonValue>& BV : *Buildings)
		{
			const TSharedPtr<FJsonObject> B = BV.IsValid() ? BV->AsObject() : nullptr;
			if (!B.IsValid())
			{
				continue;
			}
			bool bWalkable = false;
			B->TryGetBoolField(TEXT("roof_walkable"), bWalkable);
			double FloorsRaw = 0.0;
			const TArray<TSharedPtr<FJsonValue>>* Footprint = nullptr;
			const TArray<TSharedPtr<FJsonValue>>* Stairs = nullptr;
			if (!bWalkable || !B->TryGetNumberField(TEXT("floors"), FloorsRaw)
				|| !B->TryGetArrayField(TEXT("footprint"), Footprint) || !Footprint || Footprint->Num() < 2
				|| !B->TryGetArrayField(TEXT("stairs"), Stairs) || !Stairs)
			{
				continue;
			}
			const TArray<TSharedPtr<FJsonValue>>* MinArr = nullptr;
			const TArray<TSharedPtr<FJsonValue>>* MaxArr = nullptr;
			if (!(*Footprint)[0]->TryGetArray(MinArr) || !(*Footprint)[1]->TryGetArray(MaxArr)
				|| !MinArr || !MaxArr || MinArr->Num() < 2 || MaxArr->Num() < 2)
			{
				continue;
			}
			const int32 Floors = static_cast<int32>(FloorsRaw);
			const float RoofZ = GroundZ + static_cast<float>(Floors) * 300.f;
			const FVector2D Min((*MinArr)[0]->AsNumber(), (*MinArr)[1]->AsNumber());
			const FVector2D Max((*MaxArr)[0]->AsNumber(), (*MaxArr)[1]->AsNumber());
			for (const TSharedPtr<FJsonValue>& SV : *Stairs)
			{
				const TSharedPtr<FJsonObject> S = SV.IsValid() ? SV->AsObject() : nullptr;
				if (!S.IsValid())
				{
					continue;
				}
				FString Kind;
				double FromRaw = 0.0, ToRaw = 0.0;
				const TArray<TSharedPtr<FJsonValue>>* PosArr = nullptr;
				const TArray<TSharedPtr<FJsonValue>>* TopArr = nullptr;
				if (!S->TryGetStringField(TEXT("kind"), Kind) || Kind != TEXT("external")
					|| !S->TryGetNumberField(TEXT("from"), FromRaw) || !S->TryGetNumberField(TEXT("to"), ToRaw)
					|| static_cast<int32>(FromRaw) != 0 || static_cast<int32>(ToRaw) != Floors
					|| !S->TryGetArrayField(TEXT("pos"), PosArr) || !PosArr || PosArr->Num() < 2
					|| !S->TryGetArrayField(TEXT("top"), TopArr) || !TopArr || TopArr->Num() < 2)
				{
					continue;
				}
				const FVector2D Foot2D((*PosArr)[0]->AsNumber(), (*PosArr)[1]->AsNumber());
				const FVector2D Top2D((*TopArr)[0]->AsNumber(), (*TopArr)[1]->AsNumber());
				const FVector2D Mid2D = (Min + Max) * 0.5;
				const FVector2D Inward = (Mid2D - Top2D).GetSafeNormal();
				FNavLocation FootNav, ArriveNav, SprintNav;
				if (!Nav->ProjectPointToNavigation(FVector(Foot2D.X, Foot2D.Y, GroundZ + 30.f), FootNav, FVector(120.f, 120.f, 150.f))
					|| FMath::Abs(static_cast<float>(FootNav.Location.Z) - GroundZ) > 80.f)
				{
					continue;
				}
				const FVector ArriveQuery(Top2D.X + Inward.X * 150.f, Top2D.Y + Inward.Y * 150.f, RoofZ + 30.f);
				if (!Nav->ProjectPointToNavigation(ArriveQuery, ArriveNav, FVector(150.f, 150.f, 150.f))
					|| FMath::Abs(static_cast<float>(ArriveNav.Location.Z) - RoofZ) > 80.f)
				{
					continue;
				}
				const FVector2D Side(-Inward.Y, Inward.X);
				const FVector SprintQuery(ArriveNav.Location.X + Side.X * 450.f, ArriveNav.Location.Y + Side.Y * 450.f, RoofZ + 30.f);
				if (!Nav->ProjectPointToNavigation(SprintQuery, SprintNav, FVector(180.f, 180.f, 150.f))
					|| FMath::Abs(static_cast<float>(SprintNav.Location.Z) - RoofZ) > 80.f
					|| FVector::Dist2D(ArriveNav.Location, SprintNav.Location) < 250.f)
				{
					continue;
				}
				const UNavigationPath* ClimbPath = Nav->FindPathToLocationSynchronously(World, FootNav.Location, ArriveNav.Location);
				const UNavigationPath* RoofPath = Nav->FindPathToLocationSynchronously(World, ArriveNav.Location, SprintNav.Location);
				if (!ClimbPath || !ClimbPath->IsValid() || ClimbPath->IsPartial()
					|| !RoofPath || !RoofPath->IsValid() || RoofPath->IsPartial())
				{
					continue;
				}
				Out.bFound = true;
				B->TryGetStringField(TEXT("id"), Out.BuildingId);
				Out.RampFoot = FootNav.Location;
				Out.RoofArrive = ArriveNav.Location;
				Out.RoofSprintGoal = SprintNav.Location;
				Out.RoofZ = RoofZ;
				return Out;
			}
		}
		return Out;
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, TSharedRef<FState> StateRef)
	{
		FState& State = *StateRef;
		if (State.bDone)
		{
			return false;
		}
		UWorld* World = WeakWorld.Get();
		const SmokeUtils::EPreloadGate Pre = State.Gate.Poll(World);
		if (Pre == SmokeUtils::EPreloadGate::Waiting)
		{
			return true;
		}
		if (Pre == SmokeUtils::EPreloadGate::Failed)
		{
			Check(State, false, FString::Printf(TEXT("World preload failed: %s"), *State.Gate.FailReason));
			return Finish(State, false);
		}
		State.TotalTime += StepSeconds;
		State.StageTime += StepSeconds;
		if (!World || State.TotalTime > TimeoutSeconds)
		{
			Check(State, false, TEXT("LookDirectionSmoke timed out"));
			return Finish(State, false);
		}

		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(World->GetFirstPlayerController());
		if (!Squad || !PC)
		{
			return true;
		}
		AOperativeCharacter* Leader = State.Leader.Get();
		AOperativeCharacter* Mate = State.Mate.Get();

		switch (State.Stage)
		{
		case EStage::Setup:
		{
			TArray<AOperativeCharacter*> Members = Squad->GetMembers();
			if (Members.Num() < 2)
			{
				return true;
			}
			SmokeUtils::StopLevelPmc(World);
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				SmokeUtils::ParkEnemyFarAway(*It);
			}
			Squad->EnterSoloMode();
			Leader = Members[0];
			Mate = Members[1];
			State.Leader = Leader;
			State.Mate = Mate;
			Leader->bTacticalCeaseFire = true;
			Mate->bTacticalCeaseFire = true;
			Arm(*Leader);
			Arm(*Mate);
			State.RestMeshYaw = Leader->GetMesh() ? static_cast<float>(Leader->GetMesh()->GetRelativeRotation().Yaw) : -90.f;

			TArray<const AActor*> Ignored;
			for (AOperativeCharacter* M : Members)
			{
				Ignored.Add(M);
			}
			const SmokeUtils::FClearArena Arena = SmokeUtils::FindClearArena(World, Leader->GetActorLocation(), FVector(1.f, 0.f, 0.f),
				FBox2D(FVector2D(-250.f, -250.f), FVector2D(850.f, 850.f)), Ignored);
			if (!Arena.bFound)
			{
				Check(State, false, TEXT("Found clear X6 arena for L-path and sprint checks"));
				return Finish(State, false);
			}
			State.ArenaOrigin = Arena.Origin;
			State.ArenaForward = Arena.Forward;
			State.ArenaRight = Arena.Right;

			const float HalfH = Leader->GetCapsuleComponent() ? Leader->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 88.f;
			Leader->TeleportTo(State.ArenaOrigin + FVector(0.f, 0.f, HalfH + 4.f), State.ArenaForward.Rotation(), false, true);
			Mate->TeleportTo(State.ArenaOrigin - State.ArenaRight * 160.f + FVector(0.f, 0.f, HalfH + 4.f), State.ArenaForward.Rotation(), false, true);
			Leader->StopOperative();
			Mate->StopOperative();

			Squad->SetSelectedGroup({ Leader, Mate }, false);
			const FVector ClickPoint = Leader->GetActorLocation() + State.ArenaForward * 700.f;
			FHitResult LookHit(nullptr, nullptr, ClickPoint, FVector::UpVector);
			LookHit.bBlockingHit = true;
			PC->SetSimulatedShiftModifier(true);
			PC->HandleWorldHit(LookHit);
			PC->SetSimulatedShiftModifier(false);
			State.LockedYawDeg = Leader->GetManualLookYawDeg();

			const float YawMatch = FMath::Abs(FMath::FindDeltaAngleDegrees(Leader->GetManualLookYawDeg(), Mate->GetManualLookYawDeg()));
			Check(State, Leader->HasManualLookPoint() && Mate->HasManualLookPoint() && YawMatch < 0.05f,
				FString::Printf(TEXT("HandleWorldHit Shift+click shares identical yaw across selection (diff %.2f deg, yaw %.1f)"),
					YawMatch, State.LockedYawDeg));

			State.Leg1Goal = SmokeUtils::FreeSpot(World, State.ArenaOrigin + State.ArenaRight * 450.f, Leader);
			UE_LOG(LogCodexTactics, Display, TEXT("LookDirectionSmoke: Setup ArenaOrigin=%s Leader=%s Leg1Goal=%s"),
				*State.ArenaOrigin.ToCompactString(), *Leader->GetActorLocation().ToCompactString(), *State.Leg1Goal.ToCompactString());
			Check(State, Leader->OrderMoveTo(State.Leg1Goal, false) == EOperativeOrderResult::Accepted, TEXT("L-path leg 1 plain move accepted"));
			Next(State, EStage::WalkLeg1);
			return true;
		}
		case EStage::WalkLeg1:
		{
			if (!Leader || !Mate)
			{
				return Finish(State, false);
			}
			const float Speed2D = static_cast<float>(Leader->GetVelocity().Size2D());
			const float DistToGoal = static_cast<float>(FVector::Dist2D(Leader->GetActorLocation(), State.Leg1Goal));
			if (State.Leg1Samples == 0 && (FMath::Abs(State.StageTime - 0.5f) < 0.03f || FMath::Abs(State.StageTime - 1.5f) < 0.03f))
			{
				UE_LOG(LogCodexTactics, Display, TEXT("LookDirectionSmoke: WalkLeg1 t=%.2f speed=%.1f dist=%.1f moving=%d"),
					State.StageTime, Speed2D, DistToGoal, Leader->HasOwnMoveOrder() ? 1 : 0);
			}
			if (Speed2D > 25.f && State.StageTime > 0.20f)
			{
				++State.Leg1Samples;
				const float Err = FMath::Abs(FMath::FindDeltaAngleDegrees(static_cast<float>(Leader->GetActorRotation().Yaw), State.LockedYawDeg));
				if (Err > 10.f)
				{
					++State.Leg1Bad;
				}
			}
			const bool bArrived = State.StageTime > 0.5f && DistToGoal < 100.f && Speed2D < 15.f;
			if (!bArrived && State.StageTime < 8.f)
			{
				return true;
			}
			const float MateErr = FMath::Abs(FMath::FindDeltaAngleDegrees(static_cast<float>(Mate->GetActorRotation().Yaw), State.LockedYawDeg));
			Check(State, State.Leg1Samples >= 3 && State.Leg1Bad == 0 && MateErr <= 10.f,
				FString::Printf(TEXT("L-path leg 1 keeps locked compass yaw +-10 deg (%d bad / %d samples, mate off %.1f, dist %.0f)"),
					State.Leg1Bad, State.Leg1Samples, MateErr, FVector::Dist2D(Leader->GetActorLocation(), State.Leg1Goal)));

			State.Leg2Goal = SmokeUtils::FreeSpot(World, State.Leg1Goal + State.ArenaForward * 380.f, Leader);
			Check(State, Leader->OrderMoveTo(State.Leg2Goal, false) == EOperativeOrderResult::Accepted, TEXT("L-path leg 2 plain move accepted"));
			Next(State, EStage::WalkLeg2);
			return true;
		}
		case EStage::WalkLeg2:
		{
			if (!Leader)
			{
				return Finish(State, false);
			}
			if (Leader->GetVelocity().Size2D() > 25.f && State.StageTime > 0.35f)
			{
				++State.Leg2Samples;
				const float Err = FMath::Abs(FMath::FindDeltaAngleDegrees(static_cast<float>(Leader->GetActorRotation().Yaw), State.LockedYawDeg));
				if (Err > 10.f)
				{
					++State.Leg2Bad;
				}
			}
			const bool bArrived = FVector::Dist2D(Leader->GetActorLocation(), State.Leg2Goal) < 100.f && Leader->GetVelocity().Size2D() < 15.f;
			if (!bArrived && State.StageTime < 8.f)
			{
				return true;
			}
			Check(State, State.Leg2Samples >= 3 && State.Leg2Bad == 0,
				FString::Printf(TEXT("L-path leg 2 keeps locked compass yaw +-10 deg (%d bad / %d samples)"), State.Leg2Bad, State.Leg2Samples));

			TArray<const AActor*> Ignored;
			for (AOperativeCharacter* M : Squad->GetMembers())
			{
				Ignored.Add(M);
			}
			State.RoofSpot = FindX6RoofLookSpot(World, Ignored);
			Check(State, State.RoofSpot.bFound, TEXT("Found X6 walkable roof + clear ground spot below for pitch-down check"));
			if (!State.RoofSpot.bFound)
			{
				return Finish(State, false);
			}
			Leader->StopOperative();
			Leader->ClearManualLookPoint();
			const float HalfH = Leader->GetCapsuleComponent() ? Leader->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 88.f;
			const float OutYaw = static_cast<float>(State.RoofSpot.Outward.Rotation().Yaw);
			Leader->TeleportTo(State.RoofSpot.RoofStandFeet + FVector(0.f, 0.f, HalfH + 4.f), FRotator(0.f, OutYaw, 0.f), false, true);
			Leader->SetManualLookDirection(OutYaw, 0.f);
			Arm(*Leader);
			Next(State, EStage::RoofLevel);
			return true;
		}
		case EStage::RoofLevel:
		{
			if (!Leader)
			{
				return Finish(State, false);
			}
			if (State.StageTime < 0.7f)
			{
				return true;
			}
			Arm(*Leader);
			const UOperativeAnimInstance* Anim = Leader->GetMesh() ? Cast<UOperativeAnimInstance>(Leader->GetMesh()->GetAnimInstance()) : nullptr;
			State.LevelAimPitch = Anim ? Anim->AimPitch : 0.f;
			State.LevelPosePitch = MeasureSpineHeadPitchDeg(*Leader);
			State.LevelMuzzlePitch = MeasureMuzzlePitchDeg(*Leader);

			const LookHoldRules::FLookDirection DownDir = LookHoldRules::DirectionFromClick(Leader->GetActorLocation(), State.RoofSpot.GroundBelow);
			Leader->SetManualLookDirection(DownDir.YawDeg, DownDir.PitchDeg);
			Check(State, Leader->GetManualLookPitchDeg() < -15.f,
				FString::Printf(TEXT("Roof Shift+click below stores downward pitch (%.1f deg)"), Leader->GetManualLookPitchDeg()));
			Next(State, EStage::RoofDown);
			return true;
		}
		case EStage::RoofDown:
		{
			if (!Leader)
			{
				return Finish(State, false);
			}
			if (State.StageTime < 0.9f)
			{
				return true;
			}
			Arm(*Leader);
			const UOperativeAnimInstance* Anim = Leader->GetMesh() ? Cast<UOperativeAnimInstance>(Leader->GetMesh()->GetAnimInstance()) : nullptr;
			const float DownAimPitch = Anim ? Anim->AimPitch : 0.f;
			const float DownAlpha = Anim ? Anim->AimOffsetAlpha : 0.f;
			const float DownPosePitch = MeasureSpineHeadPitchDeg(*Leader);
			const float DownMuzzlePitch = MeasureMuzzlePitchDeg(*Leader);
			const float PoseDelta = State.LevelPosePitch - DownPosePitch;
			const float MuzzleDelta = State.LevelMuzzlePitch - DownMuzzlePitch;
			Check(State, DownAimPitch < -10.f && DownAlpha > 0.5f,
				FString::Printf(TEXT("Roof downward Shift+click drives AO AimPitch (%.1f deg, alpha %.2f)"), DownAimPitch, DownAlpha));
			Check(State, PoseDelta > 3.f || MuzzleDelta > 3.f,
				FString::Printf(TEXT("Roof downward AO pitches skeleton down (spine->head %.1f -> %.1f, muzzle %.1f -> %.1f)"),
					State.LevelPosePitch, DownPosePitch, State.LevelMuzzlePitch, DownMuzzlePitch));

			// Part B repro: walk up a real X6 external ramp onto the roof, then clear stale lock/facing on sprint across the roof.
			State.RampSpot = FindX6RampToRoof(World);
			Check(State, State.RampSpot.bFound, TEXT("Found X6 external ramp to walkable roof for Part B ramp climb check"));
			if (!State.RampSpot.bFound)
			{
				return Finish(State, false);
			}
			const float HalfH = Leader->GetCapsuleComponent() ? Leader->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 88.f;
			Leader->ClearManualLookPoint();
			Leader->StopOperative();
			Leader->TeleportTo(State.RampSpot.RampFoot + FVector(0.f, 0.f, HalfH + 4.f),
				(State.RampSpot.RoofArrive - State.RampSpot.RampFoot).GetSafeNormal2D().Rotation(), false, true);
			// Arm stale facing + look before climbing the ramp, then walk up the ramp onto the roof.
			Leader->SetFacingPoint(State.RampSpot.RampFoot - (State.RampSpot.RoofArrive - State.RampSpot.RampFoot).GetSafeNormal2D() * 400.f);
			Check(State, Leader->OrderMoveTo(State.RampSpot.RoofArrive, false) == EOperativeOrderResult::Accepted,
				FString::Printf(TEXT("Ramp climb to %s roof accepted"), *State.RampSpot.BuildingId));
			Next(State, EStage::RampFootToRoof);
			return true;
		}
		case EStage::RampFootToRoof:
		{
			if (!Leader)
			{
				return Finish(State, false);
			}
			const float HalfH = Leader->GetCapsuleComponent() ? Leader->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 88.f;
			const float FeetZ = static_cast<float>(Leader->GetActorLocation().Z) - HalfH;
			const bool bOnRoof = FMath::Abs(FeetZ - State.RampSpot.RoofZ) < 80.f
				&& FVector::Dist2D(Leader->GetActorLocation(), State.RampSpot.RoofArrive) < 140.f
				&& Leader->GetVelocity().Size2D() < 15.f;
			if (!bOnRoof && State.StageTime < 15.f)
			{
				return true;
			}
			Check(State, bOnRoof, FString::Printf(TEXT("Climbed %s ramp onto roof (feet Z %.0f vs roof %.0f, %.1f s)"),
				*State.RampSpot.BuildingId, FeetZ, State.RampSpot.RoofZ, State.StageTime));
			if (!bOnRoof)
			{
				return Finish(State, false);
			}
			// Simulate Shift+click look + SetFacingPoint on the roof after ramp climb, then plain sprint across the roof.
			State.RoofSprintStart = Leader->GetActorLocation();
			const float RoofSprintYaw = static_cast<float>((State.RampSpot.RoofSprintGoal - State.RoofSprintStart).Rotation().Yaw);
			Leader->SetFacingPoint(State.RoofSprintStart - (State.RampSpot.RoofSprintGoal - State.RoofSprintStart).GetSafeNormal2D() * 400.f);
			Leader->SetManualLookDirection(FRotator::NormalizeAxis(RoofSprintYaw - 110.f), 0.f);
			Check(State, Leader->OrderMoveTo(State.RampSpot.RoofSprintGoal, true) == EOperativeOrderResult::Accepted,
				TEXT("Roof plain sprint after ramp climb accepted"));
			Check(State, !Leader->HasManualLookPoint(), TEXT("Roof sprint after ramp climb clears ManualLookPoint"));
			State.bHasMovingTravelYaw = false;
			State.LastMovingTravelYaw = RoofSprintYaw;
			Next(State, EStage::RoofSprintClear);
			return true;
		}
		case EStage::RoofSprintClear:
		{
			if (!Leader)
			{
				return Finish(State, false);
			}
			if (Leader->GetVelocity().Size2D() > 50.f)
			{
				State.LastMovingTravelYaw = static_cast<float>(Leader->GetVelocity().Rotation().Yaw);
				State.bHasMovingTravelYaw = true;
			}
			const bool bArrived = FVector::Dist2D(Leader->GetActorLocation(), State.RampSpot.RoofSprintGoal) < 130.f
				&& Leader->GetVelocity().Size2D() < 10.f;
			if (!bArrived && State.StageTime < 10.f)
			{
				return true;
			}
			Check(State, bArrived, FString::Printf(TEXT("Roof sprint after ramp climb arrived (%.1f s)"), State.StageTime));
			Next(State, EStage::RoofSprintSettle);
			return true;
		}
		case EStage::RoofSprintSettle:
		{
			if (!Leader)
			{
				return Finish(State, false);
			}
			if (State.StageTime < 3.5f)
			{
				return true; // wait past F-1's 3.0 s quiet delay to confirm SetFacingPoint did not survive
			}
			const float TravelYaw = State.bHasMovingTravelYaw
				? State.LastMovingTravelYaw
				: static_cast<float>((State.RampSpot.RoofSprintGoal - State.RoofSprintStart).Rotation().Yaw);
			const float FinalYaw = static_cast<float>(Leader->GetActorRotation().Yaw);
			const float OffDeg = FMath::Abs(FMath::FindDeltaAngleDegrees(FinalYaw, TravelYaw));
			const float MeshRelYaw = Leader->GetMesh() ? static_cast<float>(Leader->GetMesh()->GetRelativeRotation().Yaw) : State.RestMeshYaw;
			const float MeshDriftDeg = FMath::Abs(FMath::FindDeltaAngleDegrees(MeshRelYaw, State.RestMeshYaw));
			Check(State, State.bHasMovingTravelYaw && OffDeg <= 15.f && MeshDriftDeg <= 2.f,
				FString::Printf(TEXT("After ramp climb, roof sprint arrival holds travel yaw +-15 deg past 3.5s (actor %.1f, travel %.1f, off %.1f, meshDrift %.1f)"),
					FinalYaw, TravelYaw, OffDeg, MeshDriftDeg));

			// Part B arena check 1: SetFacingPoint must clear on plain move/sprint without F-1 returning after 3s.
			const float HalfH = Leader->GetCapsuleComponent() ? Leader->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 88.f;
			Leader->ClearManualLookPoint();
			Leader->TeleportTo(State.ArenaOrigin + FVector(0.f, 0.f, HalfH + 4.f), FRotator(0.f, State.ArenaForward.Rotation().Yaw, 0.f), false, true);
			Leader->StopOperative();
			Leader->SetFacingPoint(State.ArenaOrigin - State.ArenaForward * 500.f);
			State.SprintGoal = SmokeUtils::FreeSpot(World, State.ArenaOrigin + State.ArenaForward * 550.f, Leader);
			Check(State, Leader->OrderMoveTo(State.SprintGoal, true) == EOperativeOrderResult::Accepted, TEXT("Part B sprint clearing SetFacingPoint accepted"));
			Next(State, EStage::SprintClearFacing);
			return true;
		}
		case EStage::SprintClearFacing:
		{
			if (!Leader)
			{
				return Finish(State, false);
			}
			const bool bArrived = FVector::Dist2D(Leader->GetActorLocation(), State.SprintGoal) < 120.f && Leader->GetVelocity().Size2D() < 10.f;
			if (!bArrived && State.StageTime < 8.f)
			{
				return true;
			}
			Check(State, bArrived, FString::Printf(TEXT("Part B sprint (after SetFacingPoint) arrived (%.1f s)"), State.StageTime));
			Next(State, EStage::SettleFacing);
			return true;
		}
		case EStage::SettleFacing:
		{
			if (!Leader)
			{
				return Finish(State, false);
			}
			if (State.StageTime < 1.0f)
			{
				return true;
			}
			const float TravelYaw = static_cast<float>((State.SprintGoal - State.ArenaOrigin).Rotation().Yaw);
			const float FinalYaw = static_cast<float>(Leader->GetActorRotation().Yaw);
			const float OffDeg = FMath::Abs(FMath::FindDeltaAngleDegrees(FinalYaw, TravelYaw));
			Check(State, OffDeg <= 15.f,
				FString::Printf(TEXT("Part B SetFacingPoint cleared: arrival holds travel yaw +-15 deg (got %.1f, travel %.1f, off %.1f)"),
					FinalYaw, TravelYaw, OffDeg));

			// Part B check 2: ManualLookDirection clears on sprint and arrival holds reverse travel yaw.
			Leader->SetManualLookDirection(FRotator::NormalizeAxis(TravelYaw + 120.f), 0.f);
			State.SprintGoal = SmokeUtils::FreeSpot(World, State.ArenaOrigin, Leader);
			Check(State, Leader->OrderMoveTo(State.SprintGoal, true) == EOperativeOrderResult::Accepted, TEXT("Part B sprint clearing ManualLookDirection accepted"));
			Check(State, !Leader->HasManualLookPoint(), TEXT("Part B sprint clears ManualLookPoint immediately"));
			Next(State, EStage::SprintClearLook);
			return true;
		}
		case EStage::SprintClearLook:
		{
			if (!Leader)
			{
				return Finish(State, false);
			}
			const bool bArrived = FVector::Dist2D(Leader->GetActorLocation(), State.SprintGoal) < 120.f && Leader->GetVelocity().Size2D() < 10.f;
			if (!bArrived && State.StageTime < 8.f)
			{
				return true;
			}
			Check(State, bArrived, FString::Printf(TEXT("Part B sprint (after ManualLook) arrived (%.1f s)"), State.StageTime));
			Next(State, EStage::SettleLook);
			return true;
		}
		case EStage::SettleLook:
		{
			if (!Leader)
			{
				return Finish(State, false);
			}
			if (State.StageTime < 1.0f)
			{
				return true;
			}
			const float ReturnTravelYaw = static_cast<float>(FRotator::NormalizeAxis(State.ArenaForward.Rotation().Yaw + 180.f));
			const float FinalYaw = static_cast<float>(Leader->GetActorRotation().Yaw);
			const float OffDeg = FMath::Abs(FMath::FindDeltaAngleDegrees(FinalYaw, ReturnTravelYaw));
			Check(State, OffDeg <= 15.f,
				FString::Printf(TEXT("Part B ManualLook cleared: arrival holds travel yaw +-15 deg (got %.1f, travel %.1f, off %.1f)"),
					FinalYaw, ReturnTravelYaw, OffDeg));
			return Finish(State, true);
		}
		default:
			return false;
		}
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.LookDirectionSmoke"),
		TEXT("Task 145: shared Shift+click world yaw on L-path, roof pitch-down AO bone check, and Part B sprint arrival facing on X6."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>&, UWorld* World)
		{
			if (!World)
			{
				UE_LOG(LogCodexTactics, Error, TEXT("LookDirectionSmoke: no UWorld context."));
				return;
			}
			TSharedRef<FState> State = MakeShared<FState>();
			State->World = World;
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Weak = TWeakObjectPtr<UWorld>(World), State](float)
			{
				return Step(Weak, State);
			}), StepSeconds);
		}));
}

#endif // !UE_BUILD_SHIPPING
