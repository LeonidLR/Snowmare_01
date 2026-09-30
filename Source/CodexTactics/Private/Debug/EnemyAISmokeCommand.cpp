// Dev-only console command for a headless enemy AI check on L_MovementTest (exploration, so the squad holds fire):
//   Scripts/smoke.ps1 -Command CodexTactics.EnemyAISmoke
// Godot enemy_base.gd / enemy_frost_*.gd: per-type affinities / armor (brute kinetic 0.25, armor 0.75); frost halves
// the speed; a hound near a burning barrel flees («СТРАХ ОГНЯ»); a hound picks a close turret over the squad; a
// barricade in the way gets smashed; a spitter with a clear line shoots the commander.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Materials/Material.h"
#include "Components/MeshComponent.h"
#include "Combat/CombatFeedbackSubsystem.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Containers/Ticker.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/BarrelActor.h"
#include "Interactables/BarricadeActor.h"
#include "Interactables/TurretActor.h"
#include "TimerManager.h"
#include "UI/FloatingTextSubsystem.h"

namespace EnemyAISmoke
{
	constexpr float StepSeconds = 0.25f;

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		float StartDistance = 0.f;
		float BarricadeHealth = 0.f;
		float LeaderHealth = 0.f;
		TWeakObjectPtr<AEnemyCharacter> Hound;
		TWeakObjectPtr<ABarrelActor> Barrel;
		TWeakObjectPtr<ABarricadeActor> Barricade;
		TWeakObjectPtr<AEnemyCharacter> Spitter;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("EnemyAISmoke"));
		return false;
	}

	AEnemyCharacter* Spawn(UWorld* World, EEnemyArchetype Type, const FVector& DesignPoint)
	{
		return World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(Type, SmokeUtils::LevelPoint(World, DesignPoint), SmokeUtils::LayoutTransform(World).Rotator());
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time < 3.f)
		{
			return World != nullptr;
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Leader = Squad->GetLeader();
		UFloatingTextSubsystem* Floating = World->GetSubsystem<UFloatingTextSubsystem>();
		if (!Leader || State.Time > 40.f)
		{
			Check(State, false, FString::Printf(TEXT("leader / timeout (stage %d)"), State.Stage));
			return Finish(State, false);
		}
		auto Next = [&State]() { ++State.Stage; State.StageTime = 0.f; };
		switch (State.Stage)
		{
		case 0:
		{
			// Affinities, armor, frost.
			AEnemyCharacter* Brute = Spawn(World, EEnemyArchetype::Brute, FVector(-1500.f, 1500.f, 100.f));
			Check(State, Brute && FMath::IsNearlyEqual(Brute->GetHealthComponent()->ElementalAffinities.Kinetic, 0.25f)
				&& FMath::IsNearlyEqual(Brute->GetHealthComponent()->ElementalAffinities.Energy, 2.f), TEXT("brute affinities: kinetic 0.25, energy 2"));
			if (Brute)
			{
				const float Speed = Brute->GetCharacterMovement()->MaxWalkSpeed;
				FDamageSpec Frost;
				Frost.Amount = 1.f;
				Frost.DamageType = EDamageType::Fire; // any damage carrying the frozen status
				Frost.StatusEffect = EStatusEffect::Frozen;
				Frost.StatusDuration = 5.f;
				Brute->GetHealthComponent()->TakeDamage(Frost);
				Brute->Tick(0.01f);
				Check(State, FMath::IsNearlyEqual(Brute->GetCharacterMovement()->MaxWalkSpeed, Speed * 0.5f, 1.f),
					FString::Printf(TEXT("frozen: speed %.0f -> %.0f"), Speed, Brute->GetCharacterMovement()->MaxWalkSpeed));
				Brute->Destroy();
			}

			// Fire fear: a hound 3 m from a burning barrel.
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
			State.Barrel = World->SpawnActor<ABarrelActor>(SmokeUtils::LevelPoint(World, FVector(-1500.f, 0.f, 70.f)), FRotator::ZeroRotator, Params);
			State.Hound = Spawn(World, EEnemyArchetype::FrostHound, FVector(-1200.f, 0.f, 100.f));
			Leader->MatchesCount = FMath::Max(Leader->MatchesCount, 1);
			Check(State, State.Barrel.IsValid() && State.Hound.IsValid() && State.Barrel->Ignite(Leader), TEXT("barrel lit, hound nearby"));
			State.StartDistance = State.Hound.IsValid() && State.Barrel.IsValid() ? FVector::Dist2D(State.Hound->GetActorLocation(), State.Barrel->GetActorLocation()) : 0.f;
			Next();
			return true;
		}
		case 1:
		{
			if (State.StageTime < 1.5f)
			{
				return true;
			}
			const float Distance = FVector::Dist2D(State.Hound->GetActorLocation(), State.Barrel->GetActorLocation());
			Check(State, Distance > State.StartDistance + 150.f && Floating->HasShown(TEXT("СТРАХ ОГНЯ")),
				FString::Printf(TEXT("hound flees the fire (%.0f -> %.0f cm)"), State.StartDistance, Distance));
			State.Barrel->Extinguish();
			State.Hound->Destroy();

			// A close turret pulls a hound off the squad.
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			ATurretActor* Turret = World->SpawnActor<ATurretActor>(SmokeUtils::LevelPoint(World, FVector(1500.f, -1500.f, 60.f)), FRotator::ZeroRotator, Params);
			AEnemyCharacter* Hound = Spawn(World, EEnemyArchetype::FrostHound, FVector(1500.f, -900.f, 100.f));
			// The level generator outranks everything for small enemies (0.4 x distance): take it out of the picture.
			TArray<AInteractableActor*> Generators;
			for (TActorIterator<AInteractableActor> It(World); It; ++It)
			{
				if (It->ObjectType == EInteractableType::Generator && !It->bGeneratorBroken)
				{
					It->bGeneratorBroken = true;
					Generators.Add(*It);
				}
			}
			const AActor* Picked = Hound ? Hound->FindTarget() : nullptr;
			for (AInteractableActor* Generator : Generators)
			{
				Generator->bGeneratorBroken = false;
			}
			Check(State, Turret && Hound && Picked == Turret, FString::Printf(TEXT("hound goes for a close turret (picked %s, turret broken %d)"),
				*GetNameSafe(Picked), Turret && Turret->IsBroken() ? 1 : 0));
			if (Hound)
			{
				Hound->Destroy();
			}
			if (Turret)
			{
				Turret->Destroy();
			}

			// A barricade between a brute and the commander gets smashed.
			AEnemyCharacter* Brute = Spawn(World, EEnemyArchetype::Brute, FVector(800.f, 0.f, 100.f));
			State.Barricade = World->SpawnActor<ABarricadeActor>(SmokeUtils::LevelPoint(World, FVector(620.f, 0.f, 50.f)),
				SmokeUtils::LayoutTransform(World).Rotator() + FRotator(0.f, 90.f, 0.f), Params);
			State.BarricadeHealth = State.Barricade.IsValid() ? State.Barricade->FindComponentByClass<UHealthComponent>()->GetCurrentHealth() : 0.f;
			State.Hound = Brute;
			Next();
			return true;
		}
		case 2:
		{
			if (State.StageTime < 3.f)
			{
				return true;
			}
			const float Health = State.Barricade.IsValid() ? State.Barricade->FindComponentByClass<UHealthComponent>()->GetCurrentHealth() : 0.f;
			Check(State, Health < State.BarricadeHealth, FString::Printf(TEXT("brute smashes the barricade in its way (%.0f -> %.0f)"), State.BarricadeHealth, Health));
			if (State.Hound.IsValid())
			{
				State.Hound->Destroy();
			}
			if (State.Barricade.IsValid())
			{
				State.Barricade->Destroy();
			}
			// A spitter 10 m out with a clear line shoots.
			Leader->bForceHitForTesting = false;
			State.LeaderHealth = Leader->HealthComponent->GetCurrentHealth();
			State.Spitter = Spawn(World, EEnemyArchetype::Spitter, FVector(1000.f, 0.f, 100.f));
			Next();
			return true;
		}
		case 3:
		{
			const float Health = Leader->HealthComponent->GetCurrentHealth();
			if (Health >= State.LeaderHealth && State.StageTime < 8.f)
			{
				return true;
			}
			bool bAnyHit = Health < State.LeaderHealth;
			for (const AOperativeCharacter* Member : Squad->GetMembers())
			{
				bAnyHit |= Member->HealthComponent->GetCurrentHealth() < Member->HealthComponent->GetMaxHealth();
			}
			Check(State, bAnyHit || Floating->HasShown(TEXT("УКЛОНЕНИЕ")), TEXT("spitter with a clear line shoots the squad"));

			// Frostbitten blow: the leader is shoved away and flashes red; a hit on the enemy flashes it.
			UCombatFeedbackSubsystem* Feedback = World->GetSubsystem<UCombatFeedbackSubsystem>();
			AEnemyCharacter* Frostbitten = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::Frostbitten,
				Leader->GetActorLocation() + Leader->GetActorForwardVector() * 120.f, FRotator::ZeroRotator);
			if (Frostbitten && Feedback)
			{
				const int32 Flashes = Feedback->GetDamageFlashCount();
				Leader->ForcedDodgeRollForTesting = 0.f; // no dodge
				Frostbitten->AttackTarget(Leader);
				Check(State, Leader->GetCharacterMovement()->PendingLaunchVelocity.Size2D() > 200.f,
					FString::Printf(TEXT("frostbitten shoves the leader (%.0f cm/s)"), Leader->GetCharacterMovement()->PendingLaunchVelocity.Size2D()));
				FDamageSpec Spec;
				Spec.Amount = 5.f;
				Frostbitten->GetHealthComponent()->TakeDamage(Spec);
				Check(State, Feedback->GetDamageFlashCount() >= Flashes + 2, TEXT("damage flashes: operative light and enemy glow"));
				// Godot _apply_enemy_target_fresnel: an enemy picked as a target gets the see-through red fresnel edge.
				Feedback->HighlightTarget(Frostbitten);
				bool bFresnel = false;
				TInlineComponentArray<UMeshComponent*> TargetMeshes(Frostbitten);
				for (const UMeshComponent* TargetMesh : TargetMeshes)
				{
					const UMaterialInterface* Overlay = TargetMesh->GetOverlayMaterial();
					bFresnel |= Overlay && Overlay->GetMaterial() && Overlay->GetMaterial()->GetName() == TEXT("M_TargetFresnel");
				}
				Check(State, bFresnel, TEXT("target highlight: M_TargetFresnel overlay on the enemy"));
			}
			else
			{
				Check(State, false, TEXT("frostbitten spawned"));
			}
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
		TEXT("CodexTactics.EnemyAISmoke"),
		TEXT("Dev check: enemy affinities, frost slow, fire fear, turret priority, barricade smashing, spitter fire; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
