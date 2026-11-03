// Dev-only check of the PMC commander (user request 2026-11-03, docs/systems/pmc-squad-ai.md "Commander") on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.PmcCommanderSmoke
// Three PMC fighters 20 m from the (immortal, Passive) squad on an empty arena, facing AWAY: no contact -> every fighter holds in ambush
// (crouched). Turned round -> contact: within 25 s one fighter gets a FLANK order whose point lies >= 60 deg off the frontal line through a
// mate, while a mate takes a frontal position (suppress / cover / advance / hold); the flanker reaches his point. Orders go only through
// the shared catalog; nobody fires at what his team does not see.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AI/PmcCommanderRules.h"
#include "AI/PmcSquadSubsystem.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Containers/Ticker.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

namespace PmcCommanderSmoke
{
	constexpr float StepSeconds = 0.1f;

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		FVector Origin = FVector::ZeroVector;
		FVector F = FVector::ForwardVector;
		FVector R = FVector::RightVector;
		TArray<TWeakObjectPtr<AOperativeCharacter>> Fighters;
		TWeakObjectPtr<AOperativeCharacter> Flanker;
		FVector FlankGoal = FVector::ZeroVector;
		bool bFlankSeen = false;
		bool bFrontalSeen = false;
		float FlankAngle = 0.f;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("PmcCommanderSmoke"));
		return false;
	}

	int32 CountOrder(const FState& State, const UPmcSquadSubsystem& Pmc, EPmcOrder Order)
	{
		int32 Count = 0;
		for (const TWeakObjectPtr<AOperativeCharacter>& Fighter : State.Fighters)
		{
			Count += Fighter.IsValid() && Pmc.GetFighterOrder(Fighter.Get()) == Order ? 1 : 0;
		}
		return Count;
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time > 120.f)
		{
			return World ? Finish(State, false) : false;
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UPmcSquadSubsystem* Pmc = UPmcSquadSubsystem::Get(World);
		switch (State.Stage)
		{
		case 0:
		{
			if (State.StageTime < 2.f)
			{
				return true;
			}
			if (!Squad || !Pmc || Squad->GetMembers().Num() < 3)
			{
				Check(State, false, TEXT("squad of 3 and UPmcSquadSubsystem present"));
				return Finish(State, false);
			}
			const TArray<AOperativeCharacter*> Members = Squad->GetMembers();
			TArray<const AActor*> Ignore;
			for (const AOperativeCharacter* Member : Members)
			{
				Ignore.Add(Member);
			}
			const SmokeUtils::FClearArena Arena = SmokeUtils::FindClearArena(World, Members[0]->GetActorLocation(), Members[0]->GetActorForwardVector(),
				FBox2D(FVector2D(-500.f, -1600.f), FVector2D(3200.f, 1600.f)), Ignore);
			Check(State, Arena.bFound, FString::Printf(TEXT("an empty arena found (tried %d)"), Arena.Tried));
			if (!Arena.bFound)
			{
				return Finish(State, false);
			}
			State.Origin = Arena.Origin;
			State.F = Arena.Forward;
			State.R = Arena.Right;
			// The squad: immortal, Passive, holding - a target that does not shoot back (the commander's decisions are what is checked).
			if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Cheat.Immortal")))
			{
				Variable->Set(1, ECVF_SetByConsole);
			}
			Squad->SetSquadPosture(ESquadFirePosture::Passive);
			Squad->SetFollowersHolding(true);
			for (int32 Index = 0; Index < Members.Num(); ++Index)
			{
				Members[Index]->StopOperative();
				const FVector Spot = SmokeUtils::FreeSpot(World, State.Origin + State.R * ((Index - 1) * 200.f), Members[Index]);
				Members[Index]->TeleportTo(Spot, State.F.Rotation(), false, true);
			}
			// Three fighters 20 m ahead facing away (their FOV never meets the squad): no contact.
			for (AOperativeCharacter* Fighter : Pmc->SpawnSquad(1, State.Origin + State.F * 2000.f, State.F.Rotation(), 3, 11))
			{
				State.Fighters.Add(Fighter);
			}
			Check(State, State.Fighters.Num() == 3, TEXT("three PMC fighters spawned 20 m ahead, facing away"));
			FPmcWeights Weights;
			PmcCommanderRules::LoadWeightsFile(FString(), Weights);
			Pmc->SetTeamCommanded(1, true, &Weights);
			State.Stage = 1;
			State.StageTime = 0.f;
			return true;
		}
		case 1:
		{
			if (State.StageTime < 2.f)
			{
				return true;
			}
			Pmc->RunDecisions(1);
			Check(State, !Pmc->HasTeamContact(1), TEXT("no contact while they face away (FOV)"));
			Check(State, CountOrder(State, *Pmc, EPmcOrder::Ambush) == 3, FString::Printf(TEXT("all three hold in AMBUSH (%d)"), CountOrder(State, *Pmc, EPmcOrder::Ambush)));
			int32 Crouched = 0;
			for (const TWeakObjectPtr<AOperativeCharacter>& Fighter : State.Fighters)
			{
				Crouched += Fighter.IsValid() && Fighter->GetStance() == EOperativeStance::Crouching ? 1 : 0;
			}
			Check(State, Crouched == 3, FString::Printf(TEXT("... crouched (%d)"), Crouched));
			// Turn them round: contact.
			for (const TWeakObjectPtr<AOperativeCharacter>& Fighter : State.Fighters)
			{
				if (Fighter.IsValid())
				{
					Fighter->SetActorRotation((-State.F).Rotation());
				}
			}
			State.Stage = 2;
			State.StageTime = 0.f;
			return true;
		}
		case 2:
		{
			// Watch the orders for up to 25 s: a flank with a frontal mate.
			for (const TWeakObjectPtr<AOperativeCharacter>& Fighter : State.Fighters)
			{
				AOperativeCharacter* Operative = Fighter.Get();
				if (!Operative)
				{
					continue;
				}
				const EPmcOrder Order = Pmc->GetFighterOrder(Operative);
				if (Order == EPmcOrder::Flank && !State.bFlankSeen)
				{
					State.bFlankSeen = true;
					State.Flanker = Operative;
					State.FlankGoal = Pmc->GetFighterGoal(Operative);
					// The frontal line: the squad's centre -> a mate's position.
					for (const TWeakObjectPtr<AOperativeCharacter>& Other : State.Fighters)
					{
						if (Other.IsValid() && Other.Get() != Operative)
						{
							State.FlankAngle = FMath::Max(State.FlankAngle, PmcCommanderRules::ApproachAngleDeg(State.Origin, State.FlankGoal, Other->GetActorLocation()));
						}
					}
				}
				else if (Order == EPmcOrder::Suppress || Order == EPmcOrder::TakeCover || Order == EPmcOrder::Advance || Order == EPmcOrder::Hold || Order == EPmcOrder::Crouch)
				{
					State.bFrontalSeen |= State.bFlankSeen || Pmc->HasTeamContact(1);
				}
			}
			if (State.StageTime < 25.f && !(State.bFlankSeen && State.bFrontalSeen))
			{
				return true;
			}
			const FPmcTeamStats* Stats = Pmc->GetTeamStats(1);
			Check(State, Pmc->HasTeamContact(1), TEXT("contact once they face the squad"));
			Check(State, Stats && Stats->Ambushes >= 1, FString::Printf(TEXT("the ambush sprang (%d)"), Stats ? Stats->Ambushes : -1));
			Check(State, State.bFlankSeen && Stats && Stats->FlanksAttempted >= 1, FString::Printf(TEXT("a FLANK order was given (%d attempted)"), Stats ? Stats->FlanksAttempted : -1));
			Check(State, State.bFrontalSeen, TEXT("a mate took a frontal position meanwhile"));
			Check(State, State.FlankAngle >= 60.f, FString::Printf(TEXT("the flank point lies %.0f deg off the frontal line (>= 60)"), State.FlankAngle));
			State.Stage = 3;
			State.StageTime = 0.f;
			return true;
		}
		case 3:
		{
			AOperativeCharacter* Flanker = State.Flanker.Get();
			const FPmcTeamStats* Stats = Pmc->GetTeamStats(1);
			const bool bReached = Stats && Stats->FlanksSucceeded >= 1;
			if (State.StageTime < 40.f && !bReached && Flanker)
			{
				return true;
			}
			Check(State, bReached, FString::Printf(TEXT("the flanker reached his point (%d succeeded, %.0f cm off it)"), Stats ? Stats->FlanksSucceeded : -1,
				Flanker ? FVector::Dist2D(Flanker->GetActorLocation(), State.FlankGoal) : -1.f));
			Check(State, Stats && Stats->OrdersIssued >= 4, FString::Printf(TEXT("orders went through the catalog (%d issued)"), Stats ? Stats->OrdersIssued : -1));
			Check(State, Stats && Stats->FriendlyFireHits == 0, TEXT("no friendly fire"));
			Pmc->SetTeamCommanded(1, false);
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
		{
			FTimerHandle PlaceHandle;
			TWeakObjectPtr<UWorld> PlaceWorld(World);
			World->GetTimerManager().SetTimer(PlaceHandle, FTimerDelegate::CreateLambda([PlaceWorld]()
			{
				SmokeUtils::PlaceSquadAtTestStart(PlaceWorld.Get());
			}), 0.5f, false);
		}
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.PmcCommanderSmoke"),
		TEXT("Dev check: three PMC fighters hold in ambush without contact, then on contact one flanks (>= 60 deg off the frontal line) while a mate holds the front; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
