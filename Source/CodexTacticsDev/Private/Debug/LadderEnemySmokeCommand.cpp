// Dev-only check of the climbing enemies (docs/design/LADDERS.md 7.5, user decision 2026-10-22: the non-mutant humans climb - the zombified
// PMC, the Military Zombie, the Marksman; mutants never), on campaign level 0.1 with its two test ladders (Scripts/Editor/place_factory_test_ladders.py):
//   Scripts/smoke_parallel.ps1 -Commands LadderEnemySmoke   (map /Game/Maps/L_Demo01_B_FrozenFactory, Scripts/test_map.json)
//  1. class flags: BP_Enemy_Marksman / BP_Enemy_FrozenPMC_Fighter / BP_Enemy_MilitaryZombie climb (CanClimbLadders, nav filter
//     UNavFilter_NoVaultLadder), BP_Enemy_Hound / BP_Enemy_Frostbitten / BP_Enemy_Brute do not (UNavFilter_NoVault);
//  2. each climber, spawned 3.5 m out from a ladder with its AI tick off (the smoke gives the orders through its AI controller): a move onto
//     the roof paths through the ladder's link, AlignBottom -> ClimbUp -> ExitTop -> standing on the roof; a move back down:
//     AlignTop -> EnterTop -> ClimbDown -> ExitBottom -> on the ground. On the ladder: the climb clip owns its full-body slot, the pelvis
//     stays over the feet, the hands near their rungs (no rung IK on the enemy bodies: the clips' own hands);
//  3. the Military Zombie knocked down on the way down falls to the bottom with the fall damage and lies knocked down; the PMC killed on
//     the way down falls (its capsule has no collision once dead: the fall by code) and lies on the floor;
//  4. the three mutants ordered onto the roof never climb: their path does not use the link and they stay on the ground.
// Godot: none (UE-only system).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AIController.h"
#include "Characters/EnemyAIController.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/EnemyRegistrySubsystem.h"
#include "Combat/HealthComponent.h"
#include "Combat/KnockdownComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "GameFlow/LevelExitSubsystem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Animation/AnimInstance.h"
#include "Ladders/LadderActor.h"
#include "Ladders/LadderClimbComponent.h"
#include "Ladders/LadderNavigation.h"
#include "Ladders/LadderRules.h"
#include "Ladders/LadderSubsystem.h"
#include "NavLinkCustomComponent.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Navigation/PathFollowingComponent.h"
#include "SmokeUtils.h"

namespace LadderEnemySmoke
{
	constexpr float StepSeconds = 0.05f;

	const TCHAR* const MarksmanPath = TEXT("/Game/Characters/Enemies/Marksman/BP_Enemy_Marksman.BP_Enemy_Marksman_C");
	const TCHAR* const PmcPath = TEXT("/Game/Characters/Enemies/PMC/FrozenPMC_Fighter/BP_Enemy_FrozenPMC_Fighter.BP_Enemy_FrozenPMC_Fighter_C");
	const TCHAR* const ZombiePath = TEXT("/Game/Characters/Enemies/MilitaryZombie/BP_Enemy_MilitaryZombie.BP_Enemy_MilitaryZombie_C");
	const TCHAR* const HoundPath = TEXT("/Game/Characters/Enemies/Hound/BP_Enemy_Hound.BP_Enemy_Hound_C");
	const TCHAR* const FrostbittenPath = TEXT("/Game/Characters/Enemies/Frostbitten/BP_Enemy_Frostbitten.BP_Enemy_Frostbitten_C");
	const TCHAR* const BrutePath = TEXT("/Game/Characters/Enemies/Brute/BP_Enemy_Brute.BP_Enemy_Brute_C");

	enum class EEnding : uint8 { Down, Knockdown, Death };

	struct FRun
	{
		const TCHAR* ClassPath = nullptr;
		const TCHAR* Name = TEXT("");
		bool bClimber = true;
		bool bWall = true;
		EEnding Ending = EEnding::Down;
		/** Gripping hands: the largest / mean distance to the rung grip, cm (the Marksman's short torso cannot reach it fully). */
		float HandMax = 3.f;
		float HandMean = 3.f;
	};

	struct FState
	{
		TWeakObjectPtr<UWorld> World;
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		TArray<FRun> Runs;
		int32 Run = 0;
		/** 0 = up, 1 = down / the ending, 2 = done. */
		int32 Leg = 0;
		TWeakObjectPtr<ALadderActor> Wall;
		TWeakObjectPtr<ALadderActor> Free;
		TWeakObjectPtr<AEnemyCharacter> Enemy;
		FVector Goal = FVector::ZeroVector;
		uint32 SeenStates = 0;
		float DoneAt = -1.f;
		// Body measures on the loop.
		int32 BodySamples = 0;
		float MinSlotWeight = 2.f;
		float MinPelvisOverFeet = 1e6f;
		float MaxPelvisOverFeet = -1e6f;
		float HandErrorSum = 0.f;
		float MaxHandError = 0.f;
		/** Signed: along the outward direction (towards the climber) / up, from the nearest rung centre. */
		FVector2D HandOffsetSum = FVector2D::ZeroVector;
		int32 HandSamples = 0;
		FVector2D FootOffsetSum = FVector2D::ZeroVector;
		float MaxFootError = 0.f;
		int32 FootSamples = 0;
		float MinFeetOverFloor = 1e6f;
		float MaxFeetOverFloor = -1e6f;
		// The climb out on top / the start down (user bug 2026-10-23: legs inside the platform): the deepest leg bone inside it.
		float MaxInside = 0.f;
		const TCHAR* MaxInsideBone = TEXT("-");
		float EnterTopTime = 0.f;
		// Ending.
		float EndFeetZ = -1.f;
		float HealthBefore = 0.f;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("LadderEnemySmoke"));
		return false;
	}

	ALadderActor* RunLadder(const FState& State)
	{
		return State.Runs.IsValidIndex(State.Run) ? (State.Runs[State.Run].bWall ? State.Wall.Get() : State.Free.Get()) : nullptr;
	}

	FVector RoofGoal(const ALadderActor& Ladder)
	{
		return Ladder.GetTopLinkWorld() - Ladder.GetOutward() * 100.f;
	}

	FVector GroundGoal(const ALadderActor& Ladder)
	{
		return Ladder.GetBottomLinkWorld() + Ladder.GetOutward() * 250.f;
	}

	/** Does a navmesh path from From to To, with the enemy controller's filter, run through Ladder's link? Also: full, its end on the roof. */
	void ProbePath(UWorld* World, const AEnemyCharacter& Enemy, const ALadderActor& Ladder, const FVector& To, bool& bOutUsesLink, bool& bOutReachesRoof)
	{
		bOutUsesLink = false;
		bOutReachesRoof = false;
		const AEnemyAIController* Controller = Cast<AEnemyAIController>(Enemy.GetController());
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		if (!Controller || !Nav || !Ladder.GetNavLink())
		{
			return;
		}
		UNavigationPath* Path = Nav->FindPathToLocationSynchronously(World, Enemy.GetActorLocation(), To, const_cast<AEnemyCharacter*>(&Enemy),
			Controller->GetDefaultNavigationFilterClass());
		if (!Path || !Path->GetPath().IsValid())
		{
			return;
		}
		const FNavLinkId LinkId = Ladder.GetNavLink()->GetId();
		for (const FNavPathPoint& Point : Path->GetPath()->GetPathPoints())
		{
			bOutUsesLink |= Point.CustomNavLinkId == LinkId;
		}
		const TArray<FNavPathPoint>& Points = Path->GetPath()->GetPathPoints();
		bOutReachesRoof = !Path->IsPartial() && Points.Num() > 0 && Points.Last().Location.Z > Ladder.GetTopWorldZ() - 60.f;
	}

	void ResetMeasures(FState& State)
	{
		State.SeenStates = 0;
		State.DoneAt = -1.f;
		State.BodySamples = 0;
		State.MinSlotWeight = 2.f;
		State.MinPelvisOverFeet = 1e6f;
		State.MaxPelvisOverFeet = -1e6f;
		State.HandErrorSum = 0.f;
		State.MaxHandError = 0.f;
		State.HandOffsetSum = FVector2D::ZeroVector;
		State.HandSamples = 0;
		State.FootOffsetSum = FVector2D::ZeroVector;
		State.MaxFootError = 0.f;
		State.FootSamples = 0;
		State.MinFeetOverFloor = 1e6f;
		State.MaxFeetOverFloor = -1e6f;
		State.EndFeetZ = -1.f;
		State.MaxInside = 0.f;
		State.MaxInsideBone = TEXT("-");
	}

	/** On ClimbOutTop (both ways): how deep any leg bone is inside the platform (an overlap, its depth to the top / the face in front). */
	void SampleTop(FState& State, const AEnemyCharacter& Enemy, const ALadderActor& Ladder)
	{
		UWorld* World = Enemy.GetWorld();
		const USkeletalMeshComponent* Mesh = Enemy.GetMesh();
		if (!World || !Mesh)
		{
			return;
		}
		FCollisionQueryParams Params(SCENE_QUERY_STAT(LadderEnemyTop), false, &Enemy);
		Params.AddIgnoredActor(&Ladder);
		const FVector Out = Ladder.GetOutward();
		for (const TCHAR* Bone : { TEXT("ball_l"), TEXT("ball_r"), TEXT("foot_l"), TEXT("foot_r"), TEXT("calf_l"), TEXT("calf_r"), TEXT("thigh_l"), TEXT("thigh_r") })
		{
			if (Mesh->GetBoneIndex(FName(Bone)) == INDEX_NONE)
			{
				continue;
			}
			const FVector P = Mesh->GetBoneLocation(FName(Bone));
			// The platform's top part only (60 cm): lower down a knee touching the wall face behind a wall ladder is the loop pose (wall 10 cm
			// behind the rungs), not the climb out.
			if (P.Z < Ladder.GetTopWorldZ() - 60.f
				|| !World->OverlapBlockingTestByChannel(P, FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(0.5f), Params))
			{
				continue;
			}
			FHitResult Above, Face;
			const float Vertical = World->LineTraceSingleByChannel(Above, P + FVector(0.f, 0.f, 150.f), P - FVector(0.f, 0.f, 2.f), ECC_Visibility, Params)
				? static_cast<float>(Above.ImpactPoint.Z - P.Z) : 0.f;
			const float Horizontal = World->LineTraceSingleByChannel(Face, P + Out * 150.f, P - Out * 2.f, ECC_Visibility, Params)
				? static_cast<float>(FVector::DotProduct(Face.ImpactPoint - P, Out)) : 0.f;
			// A knee / hip joint centre sits ~4 cm inside its skin: a knee kneeling on the edge (the clip does, going down) has its centre that deep.
			const bool bJoint = FCString::Strncmp(Bone, TEXT("calf"), 4) == 0 || FCString::Strncmp(Bone, TEXT("thigh"), 5) == 0;
			const float Depth = FMath::Max(0.f, FMath::Min(Vertical, Horizontal) - (bJoint ? 4.f : 0.f));
			if (Depth > State.MaxInside)
			{
				State.MaxInside = Depth;
				State.MaxInsideBone = Bone;
			}
			if (Depth > 3.f)
			{
				UE_LOG(LogCodexTactics, Display, TEXT("[LadderEnemy] %s %.1f cm inside (vertical %.1f, horizontal %.1f): %.1f cm under the top, %.1f cm past the rung plane, climb state %d, push %.1f"), Bone, Depth,
					Vertical, Horizontal, Ladder.GetTopWorldZ() - P.Z, FVector::DotProduct(P - Ladder.GetActorLocation(), -Out), static_cast<int32>(Enemy.LadderClimb->GetState()),
					Enemy.LadderClimb->GetMaxEdgePush());
			}
		}
	}

	void Order(FState& State, const FVector& Goal)
	{
		AEnemyCharacter* Enemy = State.Enemy.Get();
		AAIController* Controller = Enemy ? Cast<AAIController>(Enemy->GetController()) : nullptr;
		State.Goal = Goal;
		const EPathFollowingRequestResult::Type Result = Controller ? Controller->MoveToLocation(Goal, 30.f, false, true) : EPathFollowingRequestResult::Failed;
		UE_LOG(LogCodexTactics, Display, TEXT("[LadderEnemy] %s ordered to %s (result %d)"), *GetNameSafe(Enemy), *Goal.ToCompactString(), static_cast<int32>(Result));
	}

	/** Spawns the run's enemy 3.5 m out from its ladder, its AI off (the smoke orders it), invisible to the squad. */
	bool SpawnRun(FState& State, UWorld* World)
	{
		const FRun& Run = State.Runs[State.Run];
		ALadderActor* Ladder = RunLadder(State);
		UClass* Class = LoadClass<AEnemyCharacter>(nullptr, Run.ClassPath);
		Check(State, Class != nullptr, FString::Printf(TEXT("%s loads"), Run.Name));
		if (!Class || !Ladder)
		{
			return false;
		}
		const FVector Spot = SmokeUtils::FreeSpot(World, Ladder->GetBottomLinkWorld() + Ladder->GetOutward() * 350.f);
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		AEnemyCharacter* Enemy = World->SpawnActor<AEnemyCharacter>(Class, Spot + FVector(0.f, 0.f, 10.f), FRotator(0.f, (-Ladder->GetOutward()).Rotation().Yaw, 0.f), Params);
		Check(State, Enemy != nullptr, FString::Printf(TEXT("%s spawned by the %s ladder"), Run.Name, Run.bWall ? TEXT("wall") : TEXT("free-standing")));
		if (!Enemy)
		{
			return false;
		}
		State.Enemy = Enemy;
		// The smoke drives it: no AI tick (perception, attacks, its own moves), and the squad does not see it (no fire, no fight).
		Enemy->SetActorTickEnabled(false);
		if (UEnemyRegistrySubsystem* Registry = World->GetSubsystem<UEnemyRegistrySubsystem>())
		{
			Registry->UnregisterEnemy(Enemy);
		}
		Enemy->Tags.Remove(FName(TEXT("Enemy")));
		if (USkeletalMeshComponent* Mesh = Enemy->GetMesh())
		{
			Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones; // headless: bones every tick
		}
		const AEnemyAIController* Controller = Cast<AEnemyAIController>(Enemy->GetController());
		const UClass* Filter = Controller ? Controller->GetDefaultNavigationFilterClass().Get() : nullptr;
		const bool bClimbFilter = Filter == UNavFilter_NoVaultLadder::StaticClass();
		Check(State, Enemy->bCanClimbLadders == Run.bClimber && Enemy->CanClimbLadders() == Run.bClimber && bClimbFilter == Run.bClimber
			&& Enemy->LadderClimb && Enemy->LadderClimb->IsComponentTickEnabled() == Run.bClimber,
			FString::Printf(TEXT("%s: bCanClimbLadders %d, can climb %d, nav filter %s, climb ticks %d (a %s)"), Run.Name, Enemy->bCanClimbLadders ? 1 : 0,
				Enemy->CanClimbLadders() ? 1 : 0, *GetNameSafe(Filter), Enemy->LadderClimb && Enemy->LadderClimb->IsComponentTickEnabled() ? 1 : 0,
				Run.bClimber ? TEXT("climber") : TEXT("mutant: never")));
		ResetMeasures(State);
		State.Leg = 0;
		return true;
	}

	/** The climb clip on the body: its slot weight, pelvis over the feet, the hands to the nearest rung (the plane out / up). */
	void SampleBody(FState& State, const AEnemyCharacter& Enemy, const ULadderClimbComponent& Climb, const ALadderActor& Ladder)
	{
		const USkeletalMeshComponent* Mesh = Enemy.GetMesh();
		const UAnimInstance* Anim = Mesh ? Mesh->GetAnimInstance() : nullptr;
		if (!Anim || Climb.GetLoopPos().Time < 0.f)
		{
			return;
		}
		const float FeetZ = static_cast<float>(Enemy.GetActorLocation().Z) - Enemy.GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		State.MinSlotWeight = FMath::Min(State.MinSlotWeight, Anim->GetSlotMontageGlobalWeight(Climb.GetClipSlot()));
		const float Pelvis = static_cast<float>(Mesh->GetBoneLocation(TEXT("pelvis")).Z) - FeetZ;
		State.MinPelvisOverFeet = FMath::Min(State.MinPelvisOverFeet, Pelvis);
		State.MaxPelvisOverFeet = FMath::Max(State.MaxPelvisOverFeet, Pelvis);
		++State.BodySamples;
		// Gripping hands (rung IK alpha 1, the operatives' native IK on the enemy proxy): the wrist to its grip point (lateral excluded).
		const FLadderIKFrame& Frame = Climb.GetIKFrame();
		if (!Frame.bActive)
		{
			return;
		}
		const TCHAR* Bones[] = { TEXT("hand_l"), TEXT("hand_r") };
		for (int32 Limb = 0; Limb < 2; ++Limb)
		{
			if (Frame.Alpha[Limb] < 0.99f)
			{
				continue;
			}
			const FVector Rel = Mesh->GetBoneLocation(Bones[Limb]) - Frame.RungCentre[Limb];
			const FVector2D Error(static_cast<float>(FVector::DotProduct(Rel, Frame.Outward)) - Frame.HandOffset.X, static_cast<float>(Rel.Z) - Frame.HandOffset.Y);
			State.HandErrorSum += Error.Size();
			State.HandOffsetSum += Error;
			State.MaxHandError = FMath::Max(State.MaxHandError, static_cast<float>(Error.Size()));
			++State.HandSamples;
		}
		// Gripping feet: the ball of the foot to its grip point.
		const TCHAR* Balls[] = { TEXT("ball_l"), TEXT("ball_r") };
		for (int32 Side = 0; Side < 2; ++Side)
		{
			const int32 Limb = static_cast<int32>(ELadderLimb::FootL) + Side;
			if (Frame.Alpha[Limb] < 0.99f)
			{
				continue;
			}
			const FVector Rel = Mesh->GetBoneLocation(Balls[Side]) - Frame.RungCentre[Limb];
			const FVector2D Error(static_cast<float>(FVector::DotProduct(Rel, Frame.Outward)) - Frame.FootOffset.X, static_cast<float>(Rel.Z) - Frame.FootOffset.Y);
			State.FootOffsetSum += Error;
			State.MaxFootError = FMath::Max(State.MaxFootError, static_cast<float>(Error.Size()));
			++State.FootSamples;
		}
	}

	/** One leg of the run; true when it is over (checks posted). */
	bool TickLeg(FState& State, UWorld* World)
	{
		const FRun& Run = State.Runs[State.Run];
		AEnemyCharacter* Enemy = State.Enemy.Get();
		ALadderActor* Ladder = RunLadder(State);
		ULadderClimbComponent* Climb = Enemy ? Enemy->LadderClimb.Get() : nullptr;
		if (!Enemy || !Ladder || !Climb)
		{
			Check(State, false, FString::Printf(TEXT("%s: enemy / ladder / climb component"), Run.Name));
			return true;
		}
		const ELadderClimbState ClimbState = Climb->GetState();
		State.SeenStates |= 1u << static_cast<uint32>(ClimbState);
		// The capsule's (the simple collision cylinder of a dead body - capsule off - is the mesh's pose bounds).
		const float HalfHeight = Enemy->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		const float FeetOverFloor = static_cast<float>(Enemy->GetActorLocation().Z) - HalfHeight - Ladder->GetFloorZ();
		State.MinFeetOverFloor = FMath::Min(State.MinFeetOverFloor, FeetOverFloor);
		State.MaxFeetOverFloor = FMath::Max(State.MaxFeetOverFloor, FeetOverFloor);
		const auto Seen = [&State](ELadderClimbState S) { return (State.SeenStates & (1u << static_cast<uint32>(S))) != 0; };
		if (!Run.bClimber)
		{
			// A mutant ordered onto the roof: 8 s, never on the ladder, never off the ground.
			if (State.StageTime < 8.f)
			{
				return false;
			}
			Check(State, State.SeenStates == (1u << static_cast<uint32>(ELadderClimbState::None)) && State.MaxFeetOverFloor < 50.f,
				FString::Printf(TEXT("%s ordered onto the roof never climbs (states 0x%x, highest feet %.0f cm over the ladder's floor, %.0f cm from the ladder's foot)"),
					Run.Name, State.SeenStates, State.MaxFeetOverFloor, FVector::Dist2D(Enemy->GetActorLocation(), Ladder->GetBottomLinkWorld())));
			return true;
		}
		if ((ClimbState == ELadderClimbState::ClimbUp || ClimbState == ELadderClimbState::ClimbDown) && State.StageTime > 0.5f)
		{
			SampleBody(State, *Enemy, *Climb, *Ladder);
		}
		// (Not the first 0.3 s down: the clip blends in from the body's own idle - a hunched / crouched one kneels on the floor.)
		State.EnterTopTime = ClimbState == ELadderClimbState::EnterTop ? State.EnterTopTime + StepSeconds : 0.f;
		if (ClimbState == ELadderClimbState::ExitTop || (ClimbState == ELadderClimbState::EnterTop && State.EnterTopTime > 0.3f))
		{
			SampleTop(State, *Enemy, *Ladder);
		}
		// The endings on the way down: feet > 1.2 m up.
		if (State.Leg == 1 && Run.Ending != EEnding::Down && State.EndFeetZ < 0.f && ClimbState == ELadderClimbState::ClimbDown && FeetOverFloor > 120.f)
		{
			State.EndFeetZ = FeetOverFloor;
			UHealthComponent* Health = Enemy->GetHealthComponent();
			State.HealthBefore = Health ? Health->GetCurrentHealth() : 0.f;
			if (Run.Ending == EEnding::Knockdown)
			{
				const bool bKnocked = Enemy->KnockdownComponent && Enemy->KnockdownComponent->ForceKnockDown(EKnockdownDirection::Back, EKnockdownCause::HeavyMelee);
				Check(State, bKnocked && Climb->IsDropping(), FString::Printf(TEXT("%s knocked down %.0f cm up the ladder: it drops (state %d)"), Run.Name, FeetOverFloor,
					static_cast<int32>(Climb->GetState())));
			}
			else if (Health)
			{
				Health->ApplyDirectHealthLoss(Health->GetCurrentHealth() + 100.f, TEXT("Smoke"));
				Check(State, !Health->IsAlive() && Climb->IsDropping(), FString::Printf(TEXT("%s killed %.0f cm up the ladder: the body drops (state %d)"), Run.Name, FeetOverFloor,
					static_cast<int32>(Climb->GetState())));
			}
		}
		if (State.StageTime > 90.f)
		{
			Check(State, false, FString::Printf(TEXT("%s leg %d timed out in state %d at %s (%.0f cm from the goal)"), Run.Name, State.Leg, static_cast<int32>(ClimbState),
				*Enemy->GetActorLocation().ToCompactString(), FVector::Dist2D(Enemy->GetActorLocation(), State.Goal)));
			return true;
		}
		const bool bEnding = State.Leg == 1 && Run.Ending != EEnding::Down;
		const bool bExitSeen = bEnding ? Seen(ELadderClimbState::Dropping) : State.Leg == 0 ? Seen(ELadderClimbState::ExitTop) : Seen(ELadderClimbState::ExitBottom);
		const AAIController* Controller = Cast<AAIController>(Enemy->GetController());
		const bool bMoving = Controller && Controller->GetMoveStatus() == EPathFollowingStatus::Moving;
		if (!bExitSeen || ClimbState != ELadderClimbState::None || (!bEnding && (Enemy->GetVelocity().Size2D() > 10.f || bMoving)))
		{
			State.DoneAt = -1.f;
			return false;
		}
		if (State.DoneAt < 0.f)
		{
			State.DoneAt = State.StageTime;
		}
		if (State.StageTime - State.DoneAt < (bEnding ? 2.f : 1.f))
		{
			return false; // settled (no falling through the roof / floor)
		}
		if (bEnding)
		{
			Check(State, FMath::Abs(FeetOverFloor) < 15.f, FString::Printf(TEXT("%s fell to the bottom (%.0f cm fall from %.0f cm; feet %.1f cm over the floor)"), Run.Name,
				Climb->GetLastDropHeight(), State.EndFeetZ, FeetOverFloor));
			if (Run.Ending == EEnding::Knockdown)
			{
				const float Expected = LadderRules::FallDamage(Climb->GetLastDropHeight());
				const UHealthComponent* Health = Enemy->GetHealthComponent();
				const float Lost = State.HealthBefore - (Health ? Health->GetCurrentHealth() : 0.f);
				Check(State, Expected > 0.f && FMath::IsNearlyEqual(Lost, Expected, 0.6f), FString::Printf(TEXT("%s: fall damage = clamp((H - 100) * 0.1, 0, 40) = %.1f HP (lost %.1f)"),
					Run.Name, Expected, Lost));
				Check(State, Enemy->IsKnockedDown(), FString::Printf(TEXT("%s lies knocked down after the landing"), Run.Name));
			}
			else
			{
				Check(State, Enemy->IsDying() && !Ladder->GetLeaseHolder(), FString::Printf(TEXT("%s: dead at the foot, the ladder free"), Run.Name));
			}
			return true;
		}
		const bool bUp = State.Leg == 0;
		const bool bStates = bUp ? Seen(ELadderClimbState::AlignBottom) && Seen(ELadderClimbState::ClimbUp) && Seen(ELadderClimbState::ExitTop)
			: Seen(ELadderClimbState::AlignTop) && Seen(ELadderClimbState::EnterTop) && Seen(ELadderClimbState::ClimbDown) && Seen(ELadderClimbState::ExitBottom);
		Check(State, bStates, FString::Printf(TEXT("%s %s the %s ladder: %s (states 0x%x)"), Run.Name, bUp ? TEXT("up") : TEXT("down"), Run.bWall ? TEXT("wall") : TEXT("free-standing"),
			bUp ? TEXT("AlignBottom -> ClimbUp -> ExitTop") : TEXT("AlignTop -> EnterTop -> ClimbDown -> ExitBottom"), State.SeenStates));
		const float LevelZ = bUp ? Ladder->GetTopWorldZ() : Ladder->GetFloorZ();
		const float FeetOverLevel = static_cast<float>(Enemy->GetActorLocation().Z) - HalfHeight - LevelZ;
		Check(State, Enemy->GetCharacterMovement()->IsMovingOnGround() && FMath::Abs(FeetOverLevel) < 15.f && FVector::Dist2D(Enemy->GetActorLocation(), State.Goal) < 150.f,
			FString::Printf(TEXT("%s standing %s at the goal (feet %.0f cm off the level, %.0f cm from the goal)"), Run.Name, bUp ? TEXT("on the roof") : TEXT("on the ground"),
				FeetOverLevel, FVector::Dist2D(Enemy->GetActorLocation(), State.Goal)));
		Check(State, State.MinFeetOverFloor > -10.f, FString::Printf(TEXT("%s never below the ladder's floor (min feet %.1f cm)"), Run.Name, State.MinFeetOverFloor));
		Check(State, State.MaxInside <= 3.f && (!bUp || FMath::Abs(FeetOverLevel) <= 3.f),
			FString::Printf(TEXT("%s: no leg inside the platform on ClimbOutTop (max %.1f cm, %s; pushed off the wall at most %.1f cm)%s"), Run.Name, State.MaxInside,
				State.MaxInsideBone, Climb->GetMaxEdgePush(), bUp ? *FString::Printf(TEXT(", the capsule bottom %.1f cm over the roof"), FeetOverLevel) : TEXT("")));
		const float MeanHand = State.HandSamples > 0 ? State.HandErrorSum / State.HandSamples : 1e6f;
		UE_LOG(LogCodexTactics, Display, TEXT("[LadderEnemy] %s %s body: slot weight min %.2f, pelvis %.0f..%.0f cm over the feet, gripping hands to their grip mean %.1f / max %.1f cm (%d samples; mean error %.1f out, %.1f up); gripping feet max %.1f cm (%d samples; mean error %.1f out, %.1f up)"),
			Run.Name, bUp ? TEXT("up") : TEXT("down"), State.MinSlotWeight, State.MinPelvisOverFeet, State.MaxPelvisOverFeet, MeanHand, State.MaxHandError, State.HandSamples,
			State.HandSamples ? State.HandOffsetSum.X / State.HandSamples : 0.f, State.HandSamples ? State.HandOffsetSum.Y / State.HandSamples : 0.f,
			State.MaxFootError, State.FootSamples, State.FootSamples ? State.FootOffsetSum.X / State.FootSamples : 0.f,
			State.FootSamples ? State.FootOffsetSum.Y / State.FootSamples : 0.f);
		Check(State, State.BodySamples > 10 && State.MinSlotWeight > 0.9f && State.MinPelvisOverFeet > 40.f && State.MaxPelvisOverFeet < 130.f && State.HandSamples > 10
			&& State.MaxHandError <= Run.HandMax && MeanHand <= Run.HandMean,
			FString::Printf(TEXT("%s on the loop: the climb clip owns its slot (min %.2f), pelvis %.0f..%.0f cm over the feet (40..130), gripping hands on their rung grip (rung IK; max %.2f <= %.0f, mean %.2f <= %.0f cm, %d samples)"),
				Run.Name, State.MinSlotWeight, State.MinPelvisOverFeet, State.MaxPelvisOverFeet, State.MaxHandError, Run.HandMax, MeanHand, Run.HandMean, State.HandSamples));
		Check(State, Climb->GetLastRefusal() == ELadderRefusal::None && !Ladder->GetLeaseHolder(), FString::Printf(TEXT("%s: no refusal, the lease released"), Run.Name));
		return true;
	}

	bool Step(TSharedRef<FState> StateRef)
	{
		FState& State = *StateRef;
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		UWorld* World = State.World.Get();
		if (!World)
		{
			return false;
		}
		if (State.Time > 560.f)
		{
			Check(State, false, FString::Printf(TEXT("time out in stage %d"), State.Stage));
			return Finish(State, false);
		}
		switch (State.Stage)
		{
		case 0:
		{
			USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
			const ULevelExitSubsystem* Exit = World->GetSubsystem<ULevelExitSubsystem>();
			if (!Squad || !Squad->GetLeader() || State.StageTime < 3.5f || (Exit && Exit->IsStartFadeResolved() && Exit->GetStartFadeAlpha() > 0.f && State.StageTime < 15.f))
			{
				return true;
			}
			Squad->EnterSoloMode();
			Squad->SetFollowersHolding(true);
			const ULadderSubsystem* Ladders = World->GetSubsystem<ULadderSubsystem>();
			for (ALadderActor* Ladder : Ladders ? Ladders->GetLadders() : TArray<ALadderActor*>())
			{
				(Ladder->HasWall() ? State.Wall : State.Free) = Ladder;
			}
			Check(State, State.Wall.IsValid() && State.Free.IsValid(), TEXT("the map's two test ladders registered"));
			if (!State.Wall.IsValid() || !State.Free.IsValid())
			{
				return Finish(State, false);
			}
			State.Runs = {
				{ MarksmanPath, TEXT("Marksman"), true, true, EEnding::Down, 15.f, 6.f },
				{ PmcPath, TEXT("Frozen PMC Fighter"), true, false, EEnding::Down },
				{ ZombiePath, TEXT("Military Zombie"), true, true, EEnding::Knockdown },
				{ PmcPath, TEXT("Frozen PMC Fighter"), true, true, EEnding::Death },
				{ HoundPath, TEXT("Hound"), false, true, EEnding::Down },
				{ FrostbittenPath, TEXT("Frostbitten"), false, true, EEnding::Down },
				{ BrutePath, TEXT("Brute"), false, false, EEnding::Down },
			};
			State.Run = 0;
			State.Stage = 1;
			State.StageTime = 0.f;
			return true;
		}
		case 1:
		{
			// Spawn the run's enemy, let it settle, probe its path, order it onto the roof.
			if (!State.Enemy.IsValid())
			{
				if (!SpawnRun(State, World))
				{
					State.Stage = 3;
				}
				State.StageTime = 0.f;
				return true;
			}
			if (State.StageTime < 1.f)
			{
				return true;
			}
			const FRun& Run = State.Runs[State.Run];
			ALadderActor* Ladder = RunLadder(State);
			bool bUsesLink = false;
			bool bReachesRoof = false;
			ProbePath(World, *State.Enemy, *Ladder, RoofGoal(*Ladder), bUsesLink, bReachesRoof);
			Check(State, bUsesLink == Run.bClimber && bReachesRoof == Run.bClimber,
				FString::Printf(TEXT("%s: its navmesh path onto the roof %s the ladder's link (uses it %d, reaches the roof %d)"), Run.Name,
					Run.bClimber ? TEXT("runs through") : TEXT("never takes"), bUsesLink ? 1 : 0, bReachesRoof ? 1 : 0));
			Order(State, RoofGoal(*Ladder));
			State.Stage = 2;
			State.StageTime = 0.f;
			return true;
		}
		case 2:
		{
			if (!TickLeg(State, World))
			{
				return true;
			}
			const FRun& Run = State.Runs[State.Run];
			if (Run.bClimber && State.Leg == 0)
			{
				// Back down (or the ending on the way down).
				ResetMeasures(State);
				State.Leg = 1;
				Order(State, GroundGoal(*RunLadder(State)));
				State.StageTime = 0.f;
				return true;
			}
			State.Stage = 3;
			State.StageTime = 0.f;
			return true;
		}
		case 3:
			// Next run: the enemy goes, the ladder is free.
			if (AEnemyCharacter* Enemy = State.Enemy.Get())
			{
				Enemy->Destroy();
			}
			State.Enemy = nullptr;
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			if (!State.Runs.IsValidIndex(++State.Run))
			{
				return Finish(State, true);
			}
			State.Stage = 1;
			State.StageTime = 0.f;
			return true;
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
		TSharedRef<FState> State = MakeShared<FState>();
		State->World = World;
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float)
		{
			return Step(State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.LadderEnemySmoke"),
		TEXT("Dev check (2026-10-23, docs/design/LADDERS.md 7.5): on L_Demo01_B_FrozenFactory the Marksman, the Frozen PMC Fighter and the Military Zombie climb "
			"the test ladders up and down through their nav links (a knockdown and a death on the way down drop them); the Hound, the Frostbitten and the "
			"Brute never take a ladder."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
