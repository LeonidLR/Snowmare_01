// Dev-only headless check of the Sprint 09 tripwire mine on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.TripwireSmoke -Log Smoke-Tripwire.log
// Preparation, the squad carries exactly 2 grenades (commander 1, medic 1). 1. The commander marks a 3 m wire in two
// clicks: the medic-sapper rigs it, the squad's grenades go to 0. 2. An operative crawls (prone) right across it: no
// trip. 3. The medic disarms it in 3 s: wire gone, grenades back. 4. A frozen hound on an armed wire: «ЩЁЛК», 0.25 s,
// blast — it loses health and staggers. 5. A standing operative on an armed wire trips it too (friendly fire).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Containers/Ticker.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/DeployableRules.h"
#include "Interactables/RelocationSubsystem.h"
#include "Interactables/TripwireActor.h"

namespace TripwireSmoke
{
	struct FState
	{
		float Time = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		FVector Ground = FVector::ZeroVector;
		FVector A = FVector::ZeroVector;
		FVector B = FVector::ZeroVector;
		TWeakObjectPtr<AOperativeCharacter> Commander;
		TWeakObjectPtr<AOperativeCharacter> Medic;
		TWeakObjectPtr<AOperativeCharacter> Crawler;
		TWeakObjectPtr<ATripwireActor> Wire;
		TWeakObjectPtr<AEnemyCharacter> Hound;
		float HoundHealth = 0.f;
		int32 MedicGrenades = 0;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("TripwireSmoke"));
		return false;
	}

	ATripwireActor* FindWire(UWorld* World)
	{
		for (TActorIterator<ATripwireActor> It(World); It; ++It)
		{
			if (!It->IsHidden() && It->GetActorEnableCollision())
			{
				return *It;
			}
		}
		return nullptr;
	}

	ATripwireActor* LayWire(UWorld* World, const FState& State)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ATripwireActor* Wire = World->SpawnActor<ATripwireActor>((State.A + State.B) * 0.5f, FRotator::ZeroRotator, Params);
		if (Wire)
		{
			Wire->Setup(State.A, State.B, false, false, false);
		}
		return Wire;
	}

	void Place(AActor* Actor, const FVector& Ground)
	{
		const ACharacter* Character = Cast<ACharacter>(Actor);
		Actor->SetActorLocation(Ground + FVector(0.f, 0.f, (Character ? Character->GetSimpleCollisionHalfHeight() : 50.f) + 2.f), false, nullptr,
			ETeleportType::TeleportPhysics);
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
		URelocationSubsystem* Relocation = World->GetSubsystem<URelocationSubsystem>();
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			Member->ColdLevel = 0.f;
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
			State.Commander = Squad->GetLeader();
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Member->HealthComponent->SetMaxHealth(100000.f);
				Member->GrenadesCount = 0;
				if (Member->SquadRole == EOperativeRole::MedicSapper)
				{
					State.Medic = Member;
				}
				else if (Member != State.Commander.Get() && !State.Crawler.IsValid())
				{
					State.Crawler = Member;
				}
			}
			AOperativeCharacter* Commander = State.Commander.Get();
			if (Flow->GetPhase() != ECodexGamePhase::Preparation || !Commander || !State.Medic.IsValid() || !State.Crawler.IsValid())
			{
				Check(State, false, TEXT("preparation, commander, medic-sapper and a third operative"));
				return Finish(State);
			}
			Commander->GrenadesCount = 1;
			State.Medic->GrenadesCount = 1;
			State.Ground = Commander->GetActorLocation() - FVector(0.f, 0.f, Commander->GetSimpleCollisionHalfHeight());
			const FVector Forward = Commander->GetActorForwardVector().GetSafeNormal2D();
			const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward);
			State.A = SmokeUtils::ClearPoint(World, State.Ground, State.Ground + Forward * 500.f);
			State.A.Z = State.Ground.Z;
			State.B = State.A + Right * 300.f;
			Check(State, Relocation->StartTripwirePlacement(Commander) && Relocation->IsPlacingTripwire(), TEXT("placement started with 2 squad grenades"));
			Relocation->UpdatePreview(State.A);
			Relocation->ConfirmPlacement(State.A);
			Relocation->UpdatePreview(State.B);
			Relocation->ConfirmPlacement(State.B);
			Check(State, Relocation->GetTripwireTaskCount() == 1 && !Relocation->IsPlacingTripwire(), TEXT("two clicks: one rigging task"));
			State.Stage = 1;
			State.Time = 0.f;
			return true;
		}
		case 1:
		{
			ATripwireActor* Wire = FindWire(World);
			if (!Wire && State.Time < 25.f)
			{
				return true;
			}
			State.Wire = Wire;
			Check(State, Wire != nullptr && Relocation->GetSquadGrenades() == 0,
				FString::Printf(TEXT("wire rigged (%.1f s), squad grenades %d -> 0"), State.Time, Relocation->GetSquadGrenades()));
			if (!Wire)
			{
				return Finish(State);
			}
			// The crawler lies down right on the wire's middle once it is armed.
			AOperativeCharacter* Crawler = State.Crawler.Get();
			Crawler->StopOperative();
			Crawler->SetStance(EOperativeStance::Prone);
			State.Stage = 2;
			State.Time = 0.f;
			return true;
		}
		case 2:
			if (State.Time < 2.f)
			{
				return true;
			}
			Place(State.Crawler.Get(), (State.A + State.B) * 0.5f);
			State.Stage = 3;
			State.Time = 0.f;
			return true;
		case 3:
		{
			if (State.Time < 1.f)
			{
				return true;
			}
			ATripwireActor* Wire = State.Wire.Get();
			Check(State, Wire && Wire->IsArmed(), TEXT("a prone operative crawls under the armed wire: no trip"));
			Place(State.Crawler.Get(), (State.A + State.B) * 0.5f + FVector(0.f, 0.f, 0.f) + (State.A - State.Ground).GetSafeNormal2D() * 400.f);
			State.MedicGrenades = State.Medic->GrenadesCount;
			if (Wire)
			{
				Wire->ExecuteAction(State.Medic.Get());
			}
			State.Stage = 4;
			State.Time = 0.f;
			return true;
		}
		case 4:
			if (State.Time < 3.5f)
			{
				return true;
			}
			Check(State, !State.Wire.IsValid() && State.Medic->GrenadesCount >= State.MedicGrenades + 1,
				FString::Printf(TEXT("the medic-sapper disarmed it in 3 s: +%d grenades"), State.Medic->GrenadesCount - State.MedicGrenades));
			// Everybody well away; an armed wire and a frozen hound put on it.
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Member->StopOperative();
				Place(Member, State.Ground - (State.A - State.Ground).GetSafeNormal2D() * 300.f);
			}
			State.Wire = LayWire(World, State);
			State.Stage = 5;
			State.Time = 0.f;
			return true;
		case 5:
		{
			if (State.Time < 2.f)
			{
				return true;
			}
			State.Hound = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::FrostHound, (State.A + State.B) * 0.5f + FVector(0.f, 0.f, 100.f));
			AEnemyCharacter* Hound = State.Hound.Get();
			if (!Hound)
			{
				Check(State, false, TEXT("hound spawned"));
				return Finish(State);
			}
			Hound->CustomTimeDilation = 0.f;
			Hound->GetHealthComponent()->SetMaxHealth(1000.f);
			State.HoundHealth = Hound->GetHealthComponent()->GetCurrentHealth();
			Place(Hound, (State.A + State.B) * 0.5f);
			State.Stage = 6;
			State.Time = 0.f;
			return true;
		}
		case 6:
		{
			if (State.Time < 1.f)
			{
				return true;
			}
			AEnemyCharacter* Hound = State.Hound.Get();
			const float Lost = Hound ? State.HoundHealth - Hound->GetHealthComponent()->GetCurrentHealth() : 0.f;
			Check(State, !State.Wire.IsValid() && Lost >= 100.f && Hound && Hound->GetHealthComponent()->HasStatusEffect(EStatusEffect::Stagger),
				FString::Printf(TEXT("a hound on the wire: pin, blast, -%.0f HP, staggered"), Lost));
			if (Hound)
			{
				Hound->Destroy();
			}
			State.Wire = LayWire(World, State);
			State.Stage = 7;
			State.Time = 0.f;
			return true;
		}
		case 7:
		{
			if (State.Time < 2.f)
			{
				return true;
			}
			AOperativeCharacter* Walker = State.Crawler.Get();
			Walker->SetStance(EOperativeStance::Standing);
			Place(Walker, (State.A + State.B) * 0.5f);
			State.Stage = 8;
			State.Time = 0.f;
			return true;
		}
		default:
			if (State.Time < 1.f)
			{
				return true;
			}
			Check(State, !State.Wire.IsValid(), TEXT("a standing operative on the wire trips it too (friendly fire)"));
			return Finish(State);
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
		TEXT("CodexTactics.TripwireSmoke"),
		TEXT("Dev check of the Sprint 09 tripwire: two-click rig by the medic, crawl under, disarm, hound trip + blast, friendly fire."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
