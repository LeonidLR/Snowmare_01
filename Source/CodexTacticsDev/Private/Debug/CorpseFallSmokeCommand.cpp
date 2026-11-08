// Dev-only check of fix/corpse-falls-to-ground (game designer PIE 2026-11-08, L_Gen_Depot_1: a killed PMC fighter HUNG IN THE AIR against a
// building wall next to a ladder, lying horizontally in the death pose ~2 m up - killed while climbing; a second body lay against the wall face):
//   Scripts/smoke_parallel.ps1 -Commands CorpseFallSmoke   (map /Game/Maps/L_Demo01_B_FrozenFactory, Scripts/test_map.json)
// On the factory's wall ladder (LadderClimbSmoke's): a squad member (not the commander) is ordered onto the roof and killed ~2 m up the
// ladder; then a PMC fighter the same. Each: the body falls (FallRules::ShouldCorpseFall), lands within ~1.5 s, its feet at the floor
// (+- 30 cm) and its pelvis on the ground (not floating, not inside it), off the wall face (out of the rung plane), the movement stopped,
// the death pose played after the landing and still held 3 s later. Godot: none (UE-only).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AI/PmcSquadSubsystem.h"
#include "AI/SquadOrderCatalog.h"
#include "Characters/OperativeAnimInstance.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/LevelExitSubsystem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "Ladders/LadderActor.h"
#include "Ladders/LadderClimbComponent.h"
#include "Ladders/LadderSubsystem.h"
#include "SmokeUtils.h"

namespace CorpseFallSmoke
{
	constexpr float StepSeconds = 0.05f;

	struct FState
	{
		TWeakObjectPtr<UWorld> World;
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		TWeakObjectPtr<ALadderActor> Ladder;
		TWeakObjectPtr<AOperativeCharacter> Victim;
		/** 0 = the squad member, 1 = the PMC fighter. */
		int32 Part = 0;
		float KillFeetOverFloor = -1.f;
		float KilledAt = -1.f;
		float LandedAt = -1.f;
		/** Game time (the death cinematic slows the game down: the ticker's seconds are not the body's). */
		double KilledGameTime = -1.0;
		bool bPoseBeforeLanding = false;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("CorpseFallSmoke"));
		return false;
	}

	void Next(FState& State, int32 Stage)
	{
		State.Stage = Stage;
		State.StageTime = 0.f;
	}

	float FeetOverFloor(const AOperativeCharacter& Body, const ALadderActor& Ladder)
	{
		return static_cast<float>(Body.GetActorLocation().Z) - Body.GetSimpleCollisionHalfHeight() - Ladder.GetFloorZ();
	}

	/** The pelvis bone's height over the ground right under it (the body, pawns ignored); -1e6 with no ground. */
	float PelvisOverGround(UWorld* World, const AOperativeCharacter& Body)
	{
		const USkeletalMeshComponent* Mesh = Body.GetMesh();
		if (!Mesh)
		{
			return -1.e6f;
		}
		const FVector Pelvis = Mesh->GetBoneLocation(TEXT("pelvis"));
		FCollisionQueryParams Params(SCENE_QUERY_STAT(CorpseFallSmoke), false, &Body);
		for (TActorIterator<APawn> It(World); It; ++It)
		{
			Params.AddIgnoredActor(*It);
		}
		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit, Pelvis + FVector(0.f, 0.f, 150.f), Pelvis - FVector(0.f, 0.f, 600.f), ECC_WorldStatic, Params))
		{
			return -1.e6f;
		}
		return static_cast<float>(Pelvis.Z - Hit.ImpactPoint.Z);
	}

	const UOperativeAnimInstance* AnimOf(const AOperativeCharacter& Body)
	{
		return Body.GetMesh() ? Cast<UOperativeAnimInstance>(Body.GetMesh()->GetAnimInstance()) : nullptr;
	}

	bool DeathPoseHeld(const AOperativeCharacter& Body)
	{
		const UOperativeAnimInstance* Anim = AnimOf(Body);
		return Anim && Anim->bIsDead && Anim->HasDeathPlayed() && Anim->GetSlotMontageGlobalWeight(TEXT("FullBody")) > 0.5f;
	}

	bool OrderOntoRoof(AOperativeCharacter* Who, const ALadderActor& Ladder)
	{
		FSquadOrderContext Context;
		Context.Operative = Who;
		Context.Location = Ladder.GetTopLinkWorld() - Ladder.GetOutward() * 100.f;
		return SquadOrderCatalog::Execute(ESquadOrder::Move, Context);
	}

	/** One climber: up until ~2 m, killed, then the checks. Returns true while it runs. */
	bool StepClimber(FState& State, UWorld* World, const TCHAR* Who)
	{
		AOperativeCharacter* Victim = State.Victim.Get();
		ALadderActor* Ladder = State.Ladder.Get();
		ULadderClimbComponent* Climb = Victim ? Victim->LadderClimb.Get() : nullptr;
		if (!Victim || !Ladder || !Climb)
		{
			Check(State, false, FString::Printf(TEXT("%s: the climber, the ladder and his climb component"), Who));
			return false;
		}
		if (State.KilledAt < 0.f)
		{
			const float Feet = FeetOverFloor(*Victim, *Ladder);
			if (Climb->IsClimbing() && Climb->GetState() == ELadderClimbState::ClimbUp && Feet >= 180.f)
			{
				State.KillFeetOverFloor = Feet;
				State.KilledAt = State.StageTime;
				State.KilledGameTime = World->GetTimeSeconds();
				Victim->TakeHit(10000000.f, TEXT("Smoke"), false, true);
				Check(State, Victim->HealthComponent && !Victim->HealthComponent->IsAlive(), FString::Printf(TEXT("%s killed %.0f cm up the ladder (climbing up)"), Who, Feet));
				Check(State, Victim->IsCorpseFalling(), FString::Printf(TEXT("%s: the body falls (it does not freeze on the ladder)"), Who));
				return true;
			}
			if (State.StageTime > 45.f)
			{
				Check(State, false, FString::Printf(TEXT("%s climbed 1.8 m up the ladder (state %d, feet %.0f cm over the floor)"), Who, static_cast<int32>(Climb->GetState()), Feet));
				return false;
			}
			return true;
		}
		const float Since = State.StageTime - State.KilledAt;
		if (State.LandedAt < 0.f)
		{
			const UOperativeAnimInstance* Anim = AnimOf(*Victim);
			State.bPoseBeforeLanding |= Victim->IsCorpseFalling() && Anim && Anim->HasDeathPlayed(); // the death clip started in mid-air
			if (!Victim->IsCorpseFalling())
			{
				State.LandedAt = static_cast<float>(World->GetTimeSeconds() - State.KilledGameTime);
			}
			else if (Since < 15.f)
			{
				return true;
			}
		}
		if (World->GetTimeSeconds() - State.KilledGameTime < State.LandedAt + 3.f && Since < 30.f)
		{
			return true; // the hold check below runs 3 s (game time) after the landing
		}
		const float Feet = FeetOverFloor(*Victim, *Ladder);
		const float Pelvis = PelvisOverGround(World, *Victim);
		const float Out = static_cast<float>(FVector::DotProduct(Victim->GetActorLocation() - Ladder->GetActorLocation(), Ladder->GetOutward()));
		const UCharacterMovementComponent* Movement = Victim->GetCharacterMovement();
		UE_LOG(LogCodexTactics, Display, TEXT("[CorpseFall] %s: killed %.0f cm up, landed after %.2f s (a %.0f cm fall); feet %.0f cm over the floor, pelvis %.0f cm over the ground, %.0f cm out of the rung plane, movement %d"),
			Who, State.KillFeetOverFloor, State.LandedAt, Victim->GetLastCorpseFallCm(), Feet, Pelvis, Out, Movement ? static_cast<int32>(Movement->MovementMode.GetValue()) : -1);
		Check(State, State.LandedAt >= 0.f && State.LandedAt <= 1.5f, FString::Printf(TEXT("%s: the body landed within 1.5 s of game time (%.2f s)"), Who, State.LandedAt));
		Check(State, FMath::Abs(Feet) <= 30.f, FString::Printf(TEXT("%s: the corpse's feet at the floor (%.0f cm, +- 30)"), Who, Feet));
		Check(State, Pelvis > -10.f && Pelvis < 45.f, FString::Printf(TEXT("%s: lying on the ground - pelvis %.0f cm over it (not floating, not inside)"), Who, Pelvis));
		Check(State, Out > 15.f, FString::Printf(TEXT("%s: off the wall face (%.0f cm out of the rung plane)"), Who, Out));
		Check(State, Movement && Movement->MovementMode == MOVE_None && !Climb->IsOnLadder(), FString::Printf(TEXT("%s: off the ladder, the body stopped"), Who));
		Check(State, !State.bPoseBeforeLanding, FString::Printf(TEXT("%s: no death pose in mid-air"), Who));
		Check(State, DeathPoseHeld(*Victim), FString::Printf(TEXT("%s: the death pose played after the landing and held 3 s later"), Who));
		return false;
	}

	bool Step(TSharedRef<FState> State)
	{
		State->Time += StepSeconds;
		State->StageTime += StepSeconds;
		UWorld* World = State->World.Get();
		if (!World)
		{
			return false;
		}
		if (State->Time > 240.f)
		{
			Check(*State, false, FString::Printf(TEXT("time out in stage %d"), State->Stage));
			return Finish(*State, false);
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		switch (State->Stage)
		{
		case 0:
		{
			const ULevelExitSubsystem* Exit = World->GetSubsystem<ULevelExitSubsystem>();
			if (!Squad || !Squad->GetLeader() || State->StageTime < 3.5f || (Exit && Exit->IsStartFadeResolved() && Exit->GetStartFadeAlpha() > 0.f && State->StageTime < 15.f))
			{
				return true;
			}
			Squad->EnterSoloMode();
			Squad->SetFollowersHolding(true);
			Squad->SetSquadPosture(ESquadFirePosture::Passive);
			const ULadderSubsystem* Ladders = World->GetSubsystem<ULadderSubsystem>();
			for (ALadderActor* Each : Ladders ? Ladders->GetLadders() : TArray<ALadderActor*>())
			{
				if (Each->HasWall())
				{
					State->Ladder = Each;
				}
			}
			// A member who is not the commander (his death would end the mission).
			AOperativeCharacter* Victim = nullptr;
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				if (Member && Member->SquadRole != EOperativeRole::Commander)
				{
					Victim = Member;
					break;
				}
			}
			Check(*State, State->Ladder.IsValid() && Victim, TEXT("the factory's wall ladder and a squad member who is not the commander"));
			if (!State->Ladder.IsValid() || !Victim)
			{
				return Finish(*State, false);
			}
			Victim->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			State->Victim = Victim;
			Check(*State, OrderOntoRoof(Victim, *State->Ladder), FString::Printf(TEXT("%s ordered onto the wall ladder's roof"), *Victim->DisplayName.ToString()));
			Next(*State, 1);
			return true;
		}
		case 1:
			if (StepClimber(*State, World, TEXT("the squad member")))
			{
				return true;
			}
			Next(*State, 2);
			return true;
		case 2:
		{
			// The PMC fighter: spawned at the ladder's foot, ordered up, killed the same way.
			UPmcSquadSubsystem* Pmc = UPmcSquadSubsystem::Get(World);
			ALadderActor* Ladder = State->Ladder.Get();
			const FVector Foot = Ladder ? Ladder->GetBottomLinkWorld() + Ladder->GetOutward() * 300.f : FVector::ZeroVector;
			const TArray<AOperativeCharacter*> Spawned = Pmc && Ladder ? Pmc->SpawnSquad(1, SmokeUtils::FreeSpot(World, Foot + FVector(0.f, 0.f, 100.f), nullptr),
				(-Ladder->GetOutward()).Rotation(), 1, 41) : TArray<AOperativeCharacter*>();
			AOperativeCharacter* Fighter = Spawned.IsEmpty() ? nullptr : Spawned[0];
			Check(*State, Fighter != nullptr, TEXT("a PMC fighter at the ladder's foot"));
			if (!Fighter)
			{
				return Finish(*State, false);
			}
			Pmc->SetTeamCommanded(1, false);
			Fighter->bTacticalCeaseFire = true;
			Fighter->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			State->Victim = Fighter;
			State->Part = 1;
			State->KillFeetOverFloor = State->KilledAt = State->LandedAt = -1.f;
			State->bPoseBeforeLanding = false;
			Next(*State, 3);
			return true;
		}
		case 3:
			if (State->StageTime < 1.f)
			{
				return true;
			}
			Check(*State, OrderOntoRoof(State->Victim.Get(), *State->Ladder), TEXT("the PMC fighter ordered onto the roof"));
			Next(*State, 4);
			return true;
		case 4:
			if (StepClimber(*State, World, TEXT("the PMC fighter")))
			{
				return true;
			}
			return Finish(*State, true);
		default:
			return Finish(*State, false);
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
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float) { return Step(State); }), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.CorpseFallSmoke"),
		TEXT("Dev check (game designer 2026-11-08, fix/corpse-falls-to-ground): a squad member and a PMC fighter killed ~2 m up the factory's wall ladder fall to the ground, land within 1.5 s (feet at the floor, pelvis on the ground, off the wall face) and only then hold the death pose; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
