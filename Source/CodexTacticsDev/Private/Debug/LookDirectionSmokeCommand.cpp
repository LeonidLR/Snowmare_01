// Runtime smoke for task 145 on L_Gen_Depot_1_X6:
//  1. Multi-select Shift+click gives identical world yaw; L-shaped walk keeps locked yaw +-10 deg on both legs.
//  2. Standing on an X6 roof looking down via Shift+click on ground below drives AnimInstance AimPitch < -10 deg
//     and bends the skeletal pose/muzzle pitch downward relative to level baseline.
//  3. Part B: stale ManualLook / SetFacingPoint before a plain sprint clears cleanly; arrival stays within +-15 deg of travel yaw.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeAnimInstance.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Containers/Ticker.h"
#include "Debug/SmokeUtils.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
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
		SprintClear,
		SettleAfterSprint,
		Done
	};

	struct FRoofTestSpot
	{
		bool bFound = false;
		FVector RoofStandFeet = FVector::ZeroVector;
		FVector Outward = FVector::ForwardVector;
		FVector GroundBelow = FVector::ZeroVector;
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
		float LevelAimPitch = 0.f;
		float LevelPosePitch = 0.f;
		float LevelMuzzlePitch = 0.f;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), (State.Failures == 0 && bComplete) ? TEXT("PASS") : TEXT("FAIL"));
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
		if (!FFileHelper::LoadFileToString(LevelText, *LevelFile)
			|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(LevelText), Level) || !Level.IsValid()
			|| !Level->HasField(TEXT("levelgen_layout")))
		{
			return Out;
		}
		FString LayoutText;
		TSharedPtr<FJsonObject> Layout;
		if (!FFileHelper::LoadFileToString(LayoutText, *(FPaths::ProjectDir() / Level->GetStringField(TEXT("levelgen_layout"))))
			|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(LayoutText), Layout) || !Layout.IsValid())
		{
			return Out;
		}
		const float GroundZ = Layout->HasField(TEXT("ground_z")) ? static_cast<float>(Layout->GetNumberField(TEXT("ground_z"))) : 0.f;
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
		for (const TSharedPtr<FJsonValue>& BV : Layout->GetArrayField(TEXT("buildings")))
		{
			const TSharedPtr<FJsonObject>& B = BV->AsObject();
			bool bWalkable = false;
			bool bEnterable = false;
			B->TryGetBoolField(TEXT("roof_walkable"), bWalkable);
			B->TryGetBoolField(TEXT("enterable"), bEnterable);
			if (!bWalkable || !bEnterable)
			{
				continue;
			}
			const int32 Floors = static_cast<int32>(B->GetNumberField(TEXT("floors")));
			const float RoofZ = GroundZ + Floors * 300.f;
			const TArray<TSharedPtr<FJsonValue>>& Footprint = B->GetArrayField(TEXT("footprint"));
			const FVector2D Min(Footprint[0]->AsArray()[0]->AsNumber(), Footprint[0]->AsArray()[1]->AsNumber());
			const FVector2D Max(Footprint[1]->AsArray()[0]->AsNumber(), Footprint[1]->AsArray()[1]->AsNumber());
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
				if (!Nav->ProjectPointToNavigation(Edge.Key, RoofNav, FVector(80.f, 80.f, 120.f))
					|| FMath::Abs(static_cast<float>(RoofNav.Location.Z) - RoofZ) > 90.f)
				{
					continue;
				}
				const FVector GroundQuery = RoofNav.Location + Edge.Value * 600.f + FVector(0.f, 0.f, -RoofZ + GroundZ);
				FNavLocation GroundNav;
				if (!Nav->ProjectPointToNavigation(GroundQuery, GroundNav, FVector(200.f, 200.f, 150.f))
					|| FMath::Abs(static_cast<float>(GroundNav.Location.Z) - GroundZ) > 100.f)
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
		if (!Squad)
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
			UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
			SmokeUtils::StartWaveFight(Flow);
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
			const LookHoldRules::FLookDirection SharedDir = LookHoldRules::DirectionFromClick(Leader->GetActorLocation(), ClickPoint);
			Leader->SetManualLookDirection(SharedDir.YawDeg, SharedDir.PitchDeg);
			Mate->SetManualLookDirection(SharedDir.YawDeg, SharedDir.PitchDeg);
			State.LockedYawDeg = SharedDir.YawDeg;

			const float YawMatch = FMath::Abs(FMath::FindDeltaAngleDegrees(Leader->GetManualLookYawDeg(), Mate->GetManualLookYawDeg()));
			Check(State, YawMatch < 0.05f, FString::Printf(TEXT("Multi-select shared direction yaw identical (diff %.2f deg, yaw %.1f)"), YawMatch, SharedDir.YawDeg));

			State.Leg1Goal = SmokeUtils::FreeSpot(World, State.ArenaOrigin + State.ArenaRight * 450.f, Leader);
			Check(State, Leader->OrderMoveTo(State.Leg1Goal, false) == EOperativeOrderResult::Accepted, TEXT("L-path leg 1 plain move accepted"));
			Next(State, EStage::WalkLeg1);
			return true;
		}
		case EStage::WalkLeg1:
		{
			if (!Leader)
			{
				return Finish(State, false);
			}
			if (Leader->GetVelocity().Size2D() > 25.f && State.StageTime > 0.35f)
			{
				++State.Leg1Samples;
				const float Err = FMath::Abs(FMath::FindDeltaAngleDegrees(static_cast<float>(Leader->GetActorRotation().Yaw), State.LockedYawDeg));
				if (Err > 10.f)
				{
					++State.Leg1Bad;
				}
			}
			const bool bArrived = FVector::Dist2D(Leader->GetActorLocation(), State.Leg1Goal) < 100.f && Leader->GetVelocity().Size2D() < 15.f;
			if (!bArrived && State.StageTime < 8.f)
			{
				return true;
			}
			Check(State, State.Leg1Samples >= 3 && State.Leg1Bad == 0,
				FString::Printf(TEXT("L-path leg 1 keeps locked compass yaw +-10 deg (%d bad / %d samples)"), State.Leg1Bad, State.Leg1Samples));

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
			Check(State, State.RoofSpot.bFound, TEXT("Found X6 walkable roof + ground spot below for pitch-down check"));
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

			// Part B: arm stale look + ordered facing, then issue a plain sprint move elsewhere and verify arrival facing matches travel.
			const float HalfH = Leader->GetCapsuleComponent() ? Leader->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 88.f;
			Leader->TeleportTo(State.ArenaOrigin + FVector(0.f, 0.f, HalfH + 4.f), FRotator(0.f, State.ArenaForward.Rotation().Yaw, 0.f), false, true);
			Leader->StopOperative();
			Leader->SetFacingPoint(State.ArenaOrigin - State.ArenaForward * 500.f);
			Leader->SetManualLookDirection(FRotator::NormalizeAxis(static_cast<float>(State.ArenaForward.Rotation().Yaw) + 120.f), 0.f);
			State.SprintGoal = SmokeUtils::FreeSpot(World, State.ArenaOrigin + State.ArenaForward * 600.f, Leader);
			Check(State, Leader->OrderMoveTo(State.SprintGoal, true) == EOperativeOrderResult::Accepted, TEXT("Part B plain sprint after stale look/facing accepted"));
			Check(State, !Leader->HasManualLookPoint(), TEXT("Part B sprint clears ManualLookPoint immediately"));
			Next(State, EStage::SprintClear);
			return true;
		}
		case EStage::SprintClear:
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
			Check(State, bArrived, FString::Printf(TEXT("Part B sprint arrived at goal (%.1f s)"), State.StageTime));
			Next(State, EStage::SettleAfterSprint);
			return true;
		}
		case EStage::SettleAfterSprint:
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
				FString::Printf(TEXT("Part B arrival holds travel yaw +-15 deg after settling (got %.1f, travel %.1f, off %.1f)"), FinalYaw, TravelYaw, OffDeg));
			return Finish(State, true);
		}
		default:
			return false;
		}
	}

	void Run()
	{
		UWorld* World = nullptr;
		if (GEngine)
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE)
				{
					World = Context.World();
					break;
				}
			}
		}
		TSharedRef<FState> State = MakeShared<FState>();
		State->World = World;
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Weak = TWeakObjectPtr<UWorld>(World), State](float)
		{
			return Step(Weak, State);
		}), StepSeconds);
	}

	FAutoConsoleCommand Command(
		TEXT("CodexTactics.LookDirectionSmoke"),
		TEXT("Task 145: shared Shift+click world yaw on L-path, roof pitch-down AO bone check, and Part B sprint arrival facing on X6."),
		FConsoleCommandDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
