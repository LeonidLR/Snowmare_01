// Dev-only headless check of the game designer's shotgun play-test 2026-11-09 (item 3): "when the shotgun operative presses against a wall /
// is in cover, he must use the same locomotion the others have - the one he had with the M16". The D-1 Engineer goes through the cover
// mechanics on L_MovementTest (a 3 m wall spawned 12 m ahead of him; nothing is saved) and in every state the native weapon layer must change
// no bone of the ABP's pose (the probe bWeaponLayerProbe: the layer's largest bone change < 0.01, its weights ~0):
//   1. the approach: a cover order walked from 12 m - the layer drives him far from the slot (sanity) and is off over the last 4.5 m (the run-
//      and-peek's pre-turn zone), blended out without a pop (its weights change < 0.15 a frame);
//   2. pressed to the wall standing; 3. sliding along it (a shimmy); 4. low (crouched in cover); 5. the corner peek and its fire pose;
//   6. the open shot off the wall (a frozen hound out in front); 7. leaving the wall: a walk away - the layer comes back, blended (< 0.15 a frame).
//   powershell -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -Commands ShotgunCoverSmoke -ExtraArgs "-Multiprocess"
// Godot reference: none (UE-only weapon layer).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeAnimInstance.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Tactics/CoverTraceRules.h"
#include "Data/WeaponDataAsset.h"
#include "Debug/SmokeUtils.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CoreDelegates.h"
#include "TimerManager.h"

namespace ShotgunCoverSmoke
{
	constexpr float StepSeconds = 0.1f;
	/** A layer bone change below this is "no change" (the probe's lower / upper maxima). */
	constexpr float NoChange = 0.01f;
	/** The approach: the layer must be off over this last stretch to the slot, cm. */
	constexpr float OffWithinCm = 450.f;
	/** The blends: the layer's weights change at most this much in one frame (blend_seconds 0.2 = ~0.08 at 60 fps). */
	constexpr float MaxWeightStep = 0.15f;

	/** One cover state's maxima over its frames. */
	struct FPhase
	{
		FString Name;
		float MaxWeight = 0.f;
		float MaxLower = 0.f;
		float MaxUpper = 0.f;
		int32 Frames = 0;
	};

	struct FState
	{
		int32 Stage = 0;
		float StageTime = 0.f;
		float Time = 0.f;
		int32 Failures = 0;
		TWeakObjectPtr<AOperativeCharacter> Engineer;
		TWeakObjectPtr<AStaticMeshActor> Wall;
		TWeakObjectPtr<AEnemyCharacter> Hound;
		FVector P = FVector::ZeroVector;
		FVector F = FVector::ForwardVector;
		FVector R = FVector::RightVector;
		float GroundZ = 0.f;
		FCoverSlot Slot;
		/** The phase being sampled (index into Phases; -1 = none). */
		int32 Phase = -1;
		TArray<FPhase> Phases;
		// The approach: the layer far out, the last stretch, the blend.
		float FarMaxWeight = 0.f;
		bool bLastWeightValid = false;
		float LastUpper = 0.f;
		float LastLower = 0.f;
		float MaxStep = 0.f;
		bool bTrackSteps = false;
		float PeekSeconds = 0.f;
		FDelegateHandle FrameHandle;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State, bool bComplete)
	{
		FCoreDelegates::OnEndFrame.Remove(State.FrameHandle);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("ShotgunCoverSmoke"));
		return false;
	}

	void GoTo(FState& State, int32 Stage)
	{
		State.Stage = Stage;
		State.StageTime = 0.f;
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke stage %d at %.1f s"), Stage, State.Time);
	}

	UOperativeAnimInstance* AnimOf(const AOperativeCharacter* Operative)
	{
		return Operative && Operative->GetMesh() ? Cast<UOperativeAnimInstance>(Operative->GetMesh()->GetAnimInstance()) : nullptr;
	}

	void BeginPhase(FState& State, const TCHAR* Name)
	{
		FPhase& Phase = State.Phases.AddDefaulted_GetRef();
		Phase.Name = Name;
		State.Phase = State.Phases.Num() - 1;
	}

	/** Ends the phase being sampled and checks it: the layer changed no bone of the ABP's pose. */
	void EndPhase(FState& State, int32 MinFrames = 10)
	{
		if (!State.Phases.IsValidIndex(State.Phase))
		{
			return;
		}
		const FPhase& Phase = State.Phases[State.Phase];
		State.Phase = -1;
		Check(State, Phase.Frames >= MinFrames && Phase.MaxLower < NoChange && Phase.MaxUpper < NoChange && Phase.MaxWeight < NoChange,
			FString::Printf(TEXT("%s: the ABP's pose, the weapon layer off (largest bone change lower %.3f / upper %.3f, weight %.3f over %d frames)"),
				*Phase.Name, Phase.MaxLower, Phase.MaxUpper, Phase.MaxWeight, Phase.Frames));
	}

	void SampleFrame(FState& State)
	{
		const AOperativeCharacter* Engineer = State.Engineer.Get();
		const UOperativeAnimInstance* Anim = AnimOf(Engineer);
		if (!Anim)
		{
			return;
		}
		const float Upper = Anim->GetShotgunUpperWeight();
		const float Lower = Anim->GetShotgunLowerWeight();
		if (State.bTrackSteps && State.bLastWeightValid)
		{
			State.MaxStep = FMath::Max3(State.MaxStep, FMath::Abs(Upper - State.LastUpper), FMath::Abs(Lower - State.LastLower));
		}
		State.LastUpper = Upper;
		State.LastLower = Lower;
		State.bLastWeightValid = true;
		if (State.Stage == 2 && Engineer->HasPendingCover())
		{
			// The approach: by the distance to the slot.
			const float ToSlot = static_cast<float>(FVector::Dist2D(Engineer->GetActorLocation(), State.Slot.WorldLocation));
			if (ToSlot > 900.f)
			{
				State.FarMaxWeight = FMath::Max(State.FarMaxWeight, FMath::Max(Upper, Lower));
			}
			if (ToSlot < OffWithinCm && State.Phase < 0)
			{
				BeginPhase(State, TEXT("1: the approach's last 4.5 m"));
			}
		}
		if (State.Phases.IsValidIndex(State.Phase))
		{
			FPhase& Phase = State.Phases[State.Phase];
			Phase.MaxWeight = FMath::Max3(Phase.MaxWeight, Upper, Lower);
			Phase.MaxLower = FMath::Max(Phase.MaxLower, Anim->GetWeaponLayerLowerDeviation());
			Phase.MaxUpper = FMath::Max(Phase.MaxUpper, Anim->GetWeaponLayerUpperDeviation());
			++Phase.Frames;
		}
	}

	AStaticMeshActor* SpawnBlock(UWorld* World, const FVector& Centre, const FRotator& Rotation, const FVector& Scale)
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

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, TSharedRef<FState> StateRef)
	{
		UWorld* World = WeakWorld.Get();
		FState& State = *StateRef;
		if (!World)
		{
			return false;
		}
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		if (State.Time > 120.f)
		{
			Check(State, false, FString::Printf(TEXT("finished in time (stage %d)"), State.Stage));
			return Finish(State, false);
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Engineer = State.Engineer.Get();
		if (Squad)
		{
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Member->ColdLevel = 0.f;
			}
		}
		switch (State.Stage)
		{
		case 0: // the squad, the Engineer leads; the wall 12 m ahead
		{
			if (State.Time < 3.f || !Squad || Squad->GetMembers().Num() < 3)
			{
				return true;
			}
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Member->HealthComponent->SetMaxHealth(100000.f, true);
				Member->bTacticalCeaseFire = true;
				if (Member->SquadRole == EOperativeRole::Engineer)
				{
					State.Engineer = Member;
				}
			}
			Engineer = State.Engineer.Get();
			UOperativeAnimInstance* Anim = AnimOf(Engineer);
			if (!Engineer || !Anim || !Engineer->CurrentWeapon || Engineer->CurrentWeapon->WeaponId != TEXT("shotgun"))
			{
				Check(State, false, TEXT("the Engineer with the D-1"));
				return Finish(State, false);
			}
			Squad->SetLeader(Engineer);
			Engineer->StopOperative();
			Engineer->bCornerHoldAtEdge = false; // the cover states one by one (the corner hold's step-out is CornerHoldSmoke's)
			Anim->bWeaponLayerProbe = true;
			// Headless meshes are never rendered: evaluate the pose every frame (the probe reads it).
			Engineer->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			State.F = Engineer->GetActorForwardVector().GetSafeNormal2D();
			State.R = FVector::CrossProduct(FVector::UpVector, State.F);
			State.P = Engineer->GetActorLocation();
			State.GroundZ = static_cast<float>(Engineer->GetActorLocation().Z) - Engineer->GetSimpleCollisionHalfHeight();
			int32 Index = 0;
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				if (Member != Engineer)
				{
					Member->StopOperative();
					Member->TeleportTo(SmokeUtils::FreeSpot(World, State.P - State.F * 500.f + State.R * (Index++ % 2 == 0 ? 300.f : -300.f), Member), State.F.Rotation(), false, true);
				}
			}
			const FVector WallCentre = State.P + State.F * 1200.f;
			State.Wall = SpawnBlock(World, FVector(WallCentre.X, WallCentre.Y, State.GroundZ + 150.f), State.F.Rotation(), FVector(0.4f, 6.f, 3.f));
			Check(State, State.Wall.IsValid(), TEXT("a 3 m wall 12 m ahead"));
			GoTo(State, 1);
			return true;
		}
		case 1: // the navmesh rebuilt: a cover order to the slot near the wall's end, walked
		{
			if (State.StageTime < 2.f)
			{
				return true;
			}
			const UOperativeAnimInstance* Anim = AnimOf(Engineer);
			Check(State, Anim && Anim->GetShotgunLayerMask() == EWeaponLayerMask::Full && !Anim->IsWeaponLayerAbpHandoff(),
				FString::Printf(TEXT("0: standing in the open, the D-1 layer drives him (mask %d)"), Anim ? static_cast<int32>(Anim->GetShotgunLayerMask()) : -1));
			const FVector EndPoint = State.P + State.F * 1180.f + State.R * 250.f + FVector(0.f, 0.f, 90.f);
			const bool bFound = CoverTraceRules::FindCoverSlotAt(World, EndPoint, State.F, State.Slot);
			const bool bOrdered = bFound && Engineer->OrderTakeCover(State.Slot, false) == EOperativeOrderResult::Accepted;
			Check(State, bOrdered, FString::Printf(TEXT("a cover order near the wall's end (found %d, high %d)"), bFound ? 1 : 0,
				bFound && State.Slot.Height == ECoverHeight::HighCover ? 1 : 0));
			if (!bOrdered)
			{
				return Finish(State, false);
			}
			State.bTrackSteps = true;
			State.MaxStep = 0.f;
			GoTo(State, 2);
			return true;
		}
		case 2: // the approach (sampled per frame by the distance), then in cover
		{
			if (!Engineer->bInCover && State.StageTime < 15.f)
			{
				return true;
			}
			EndPhase(State, 5);
			Check(State, Engineer->bInCover, FString::Printf(TEXT("1: he enters the cover (%.1f s)"), State.StageTime));
			Check(State, State.FarMaxWeight > 0.9f, FString::Printf(TEXT("1: ... the layer drove him far from the slot (weight %.2f beyond 9 m)"), State.FarMaxWeight));
			Check(State, State.MaxStep < MaxWeightStep, FString::Printf(TEXT("1: ... and blended out on the approach, no pop (largest weight step %.3f a frame; limit %.2f)"),
				State.MaxStep, MaxWeightStep));
			State.bTrackSteps = false;
			GoTo(State, 3);
			return true;
		}
		case 3: // pressed to the wall standing
		{
			if (State.StageTime < 1.f)
			{
				return true;
			}
			if (State.Phase < 0)
			{
				BeginPhase(State, TEXT("2: pressed to the wall standing"));
				return true;
			}
			if (State.StageTime < 2.5f)
			{
				return true;
			}
			EndPhase(State);
			GoTo(State, 6);
			return true;
		}
		case 4:
		{
			if ((Engineer->bShimmying || State.StageTime < 0.5f) && State.StageTime < 6.f)
			{
				return true;
			}
			EndPhase(State, 5);
			Engineer->SetStance(EOperativeStance::Crouching);
			GoTo(State, 5);
			return true;
		}
		case 5: // low: crouched in cover
		{
			if (State.StageTime < 1.5f)
			{
				return true;
			}
			if (State.Phase < 0)
			{
				BeginPhase(State, TEXT("4: low in cover (crouched)"));
				return true;
			}
			if (State.StageTime < 3.f)
			{
				return true;
			}
			EndPhase(State);
			// The open shot from the low wall position: a frozen hound out in front of the wall (the open side).
			const FVector Spot = Engineer->GetActorLocation() - State.F * 600.f;
			if (AEnemyCharacter* Hound = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::FrostHound, Spot + FVector(0.f, 0.f, 60.f), State.F.Rotation()))
			{
				Hound->CustomTimeDilation = 0.f;
				Hound->SetActorTickEnabled(false);
				Hound->GetHealthComponent()->SetMaxHealth(100000.f, true);
				State.Hound = Hound;
			}
			const bool bOpen = State.Hound.IsValid() && Engineer->BeginOpenShotFromCover(State.Hound.Get());
			Check(State, bOpen, TEXT("6: an open shot off the wall at a hound in front"));
			if (bOpen)
			{
				Engineer->bTacticalCeaseFire = false;
				Engineer->CurrentClip = FMath::Max(Engineer->CurrentClip, 2);
				Engineer->ShootAtTarget(State.Hound.Get());
			}
			BeginPhase(State, TEXT("6: the open shot off the wall"));
			GoTo(State, 8);
			return true;
		}
		case 6: // the corner peek round the wall's end (from the slot near it)
		{
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(UGameplayStatics::GetPlayerController(World, 0));
			AStaticMeshActor* Wall = State.Wall.Get();
			const FVector EdgePoint = State.P + State.F * 1180.f + State.R * 285.f + FVector(0.f, 0.f, 150.f);
			const FHitResult EdgeHit(Wall, Wall ? Wall->GetStaticMeshComponent() : nullptr, EdgePoint, -State.F);
			ECoverFacing Side = ECoverFacing::Left;
			float Edge = 0.f;
			const bool bPeek = PC && Engineer->bInCover && PC->FindCornerPeekForHit(Engineer, EdgeHit, Side, Edge)
				&& Engineer->OrderCornerPeek(Side, Edge) == EOperativeOrderResult::Accepted;
			Check(State, bPeek, FString::Printf(TEXT("5: a corner peek ordered (side %d, edge %.0f; peekable cover %d, slot L %d at %.0f / R %d at %.0f)"),
				static_cast<int32>(Side), Edge, Engineer->IsInPeekableCover() ? 1 : 0, Engineer->GetCoverSlot().bLeftEdgeExposed ? 1 : 0,
				Engineer->GetCoverSlot().LeftEdgeDistanceCm, Engineer->GetCoverSlot().bRightEdgeExposed ? 1 : 0, Engineer->GetCoverSlot().RightEdgeDistanceCm));
			BeginPhase(State, TEXT("5: the corner peek and its fire pose"));
			GoTo(State, 7);
			return true;
		}
		case 7:
		{
			// He shimmies to the corner first, then leans out: sampled until he holds the peek for 1.5 s (or 8 s).
			const bool bPeeking = Engineer->IsManualCornerPeekSpot() || Engineer->IsPeekingRoundCorner();
			State.PeekSeconds = bPeeking ? State.PeekSeconds + StepSeconds : 0.f;
			if (State.PeekSeconds < 1.5f && State.StageTime < 8.f)
			{
				return true;
			}
			EndPhase(State);
			Check(State, bPeeking, FString::Printf(TEXT("5: ... he holds the corner peek (%.1f s; manual peek %d)"), State.PeekSeconds, Engineer->IsManualCornerPeek() ? 1 : 0));
			// A shimmy along the wall, 1.5 m towards its middle (it ends the peek).
			FCoverSlot Shimmy;
			const FVector Along = Engineer->GetCoverSlot().WorldLocation - State.R * 150.f;
			const bool bShimmy = CoverTraceRules::FindShimmySlot(World, Engineer->GetCoverSlot(), Along, Shimmy) && Engineer->OrderShimmyTo(Shimmy) == EOperativeOrderResult::Accepted;
			Check(State, bShimmy, TEXT("3: a shimmy along the wall ordered"));
			BeginPhase(State, TEXT("3: sliding along the wall"));
			GoTo(State, 4);
			return true;
		}
		case 8:
		{
			if (State.StageTime < 1.2f)
			{
				return true;
			}
			Check(State, Engineer->IsCoverOpenShotActive() || Engineer->bInCover, TEXT("6: ... off the wall for the shot (or back at it)"));
			EndPhase(State, 5);
			Engineer->bTacticalCeaseFire = true;
			SmokeUtils::ParkEnemyFarAway(State.Hound.Get());
			GoTo(State, 9);
			return true;
		}
		case 9: // back at the wall, then a walk away: the layer comes back, blended
		{
			if (!Engineer->bInCover && State.StageTime < 6.f)
			{
				return true;
			}
			if (State.StageTime < 1.f)
			{
				return true;
			}
			Engineer->OrderMoveTo(SmokeUtils::ClearPoint(World, Engineer->GetActorLocation(), Engineer->GetActorLocation() - State.F * 600.f), false);
			State.bTrackSteps = true;
			State.MaxStep = 0.f;
			GoTo(State, 10);
			return true;
		}
		case 10:
		{
			if (State.StageTime < 3.f)
			{
				return true;
			}
			const UOperativeAnimInstance* Anim = AnimOf(Engineer);
			Check(State, !Engineer->bInCover && Anim && Anim->GetShotgunLayerMask() != EWeaponLayerMask::Off && FMath::Max(Anim->GetShotgunUpperWeight(), Anim->GetShotgunLowerWeight()) > 0.9f,
				FString::Printf(TEXT("7: off the wall the D-1 layer is back (in cover %d, mask %d, weights %.2f / %.2f)"), Engineer->bInCover ? 1 : 0,
					Anim ? static_cast<int32>(Anim->GetShotgunLayerMask()) : -1, Anim ? Anim->GetShotgunUpperWeight() : 0.f, Anim ? Anim->GetShotgunLowerWeight() : 0.f));
			Check(State, State.MaxStep < MaxWeightStep, FString::Printf(TEXT("7: ... blended in, no pop (largest weight step %.3f a frame; limit %.2f)"), State.MaxStep, MaxWeightStep));
			FString Summary;
			for (const FPhase& Phase : State.Phases)
			{
				Summary += FString::Printf(TEXT(" [%s: lower %.3f upper %.3f weight %.3f, %d frames]"), *Phase.Name, Phase.MaxLower, Phase.MaxUpper, Phase.MaxWeight, Phase.Frames);
			}
			UE_LOG(LogCodexTactics, Display, TEXT("ShotgunCoverSmoke: per state%s"), *Summary);
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
		FTimerHandle PlaceHandle;
		TWeakObjectPtr<UWorld> PlaceWorld(World);
		World->GetTimerManager().SetTimer(PlaceHandle, FTimerDelegate::CreateLambda([PlaceWorld]()
		{
			SmokeUtils::PlaceSquadAtTestStart(PlaceWorld.Get());
		}), 0.5f, false);
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		State->FrameHandle = FCoreDelegates::OnEndFrame.AddLambda([State]() { SampleFrame(*State); });
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.ShotgunCoverSmoke"),
		TEXT("Dev check: the D-1 Engineer in every cover state (approach, pressed, shimmy, low, corner peek, open shot, leaving) is the ABP's pose - the weapon layer off, blended; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
