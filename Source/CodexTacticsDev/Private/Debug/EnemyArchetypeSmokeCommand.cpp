// Dev-only headless check of the archetype safety net for enemies placed in a level (user bug 2026-10-12: a BP_Enemy_Brute
// put on a patrol route kept the property default FrostHound and played a 45 HP hound) on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.EnemyArchetypeSmoke
// BP_Enemy_Brute / _Frostbitten / _Cutter / _Hound are spawned like level actors (no spawner, the Archetype field untouched):
// each must get its archetype from the game mode's class mapping, with the same max HP and display name as the wave
// spawner's enemy of that type; a BP_Enemy_Brute whose instance is set to Cutter keeps the designer's Cutter.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Containers/Ticker.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"

namespace EnemyArchetypeSmoke
{
	struct FState
	{
		float Time = 0.f;
		int32 Failures = 0;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	FString Name(EEnemyArchetype Archetype)
	{
		return StaticEnum<EEnemyArchetype>()->GetNameStringByValue(static_cast<int64>(Archetype));
	}

	/** A level actor of the Blueprint: no spawner scope, the Archetype field left alone (or set by Override before BeginPlay). */
	AEnemyCharacter* PlaceLikeLevelActor(UWorld* World, UClass* Class, const FVector& Location, TOptional<EEnemyArchetype> Override = {})
	{
		const FTransform Transform(FRotator::ZeroRotator, Location);
		AEnemyCharacter* Enemy = World->SpawnActorDeferred<AEnemyCharacter>(Class, Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Enemy)
		{
			return nullptr;
		}
		if (Override.IsSet())
		{
			Enemy->InitializeArchetype(Override.GetValue()); // the designer's per-instance value
		}
		Enemy->FinishSpawning(Transform);
		Enemy->SetTurnBasedHeld(true);
		return Enemy;
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State, float Delta)
	{
		State.Time += Delta;
		UWorld* World = WeakWorld.Get();
		if (!World)
		{
			return false;
		}
		if (State.Time < 2.f)
		{
			return true;
		}
		UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>();
		struct FCase
		{
			const TCHAR* Path;
			EEnemyArchetype Expected;
		};
		const FCase Cases[] = {
			{ TEXT("/Game/Characters/Enemies/Brute/BP_Enemy_Brute.BP_Enemy_Brute_C"), EEnemyArchetype::Brute },
			{ TEXT("/Game/Characters/Enemies/Frostbitten/BP_Enemy_Frostbitten.BP_Enemy_Frostbitten_C"), EEnemyArchetype::Frostbitten },
			{ TEXT("/Game/Characters/Enemies/Cutter/BP_Enemy_Cutter.BP_Enemy_Cutter_C"), EEnemyArchetype::Cutter },
			{ TEXT("/Game/Characters/Enemies/Hound/BP_Enemy_Hound.BP_Enemy_Hound_C"), EEnemyArchetype::FrostHound },
		};
		int32 Index = 0;
		for (const FCase& Case : Cases)
		{
			UClass* Class = LoadClass<AEnemyCharacter>(nullptr, Case.Path);
			const FVector At = SmokeUtils::LevelPoint(World, FVector(-2500.f + 400.f * Index, 3500.f, 150.f));
			AEnemyCharacter* Placed = Class ? PlaceLikeLevelActor(World, Class, At) : nullptr;
			AEnemyCharacter* Reference = Waves ? Waves->SpawnEnemy(Case.Expected, At + FVector(0.f, 250.f, 0.f), FRotator::ZeroRotator) : nullptr;
			if (Reference)
			{
				Reference->SetTurnBasedHeld(true);
			}
			++Index;
			if (!Placed || !Reference)
			{
				Check(State, false, FString::Printf(TEXT("%s placed and a wave %s for reference"), Case.Path, *Name(Case.Expected)));
				continue;
			}
			Check(State, Placed->WasPlacedInLevel() && !Reference->WasPlacedInLevel(), FString::Printf(TEXT("%s: placed (no spawner) vs the wave's"),
				*Class->GetName()));
			Check(State, Placed->GetArchetype() == Case.Expected, FString::Printf(TEXT("%s placed -> archetype %s (expected %s)"), *Class->GetName(),
				*Name(Placed->GetArchetype()), *Name(Case.Expected)));
			const float PlacedHp = Placed->GetHealthComponent() ? Placed->GetHealthComponent()->GetMaxHealth() : -1.f;
			const float ReferenceHp = Reference->GetHealthComponent() ? Reference->GetHealthComponent()->GetMaxHealth() : -2.f;
			Check(State, FMath::IsNearlyEqual(PlacedHp, ReferenceHp) && Placed->GetEnemyDisplayName() == Reference->GetEnemyDisplayName(),
				FString::Printf(TEXT("%s: %.0f HP \"%s\" like the wave's %s (%.0f HP \"%s\")"), *Class->GetName(), PlacedHp, *Placed->GetEnemyDisplayName(),
					*Name(Case.Expected), ReferenceHp, *Reference->GetEnemyDisplayName()));
		}
		// The designer's per-instance value wins over the class mapping.
		UClass* BruteClass = LoadClass<AEnemyCharacter>(nullptr, Cases[0].Path);
		AEnemyCharacter* Override = BruteClass ? PlaceLikeLevelActor(World, BruteClass, SmokeUtils::LevelPoint(World, FVector(-2500.f, 4200.f, 150.f)),
			EEnemyArchetype::Cutter) : nullptr;
		Check(State, Override && Override->GetArchetype() == EEnemyArchetype::Cutter, TEXT("a BP_Enemy_Brute set to Cutter on the instance stays a Cutter"));
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("EnemyArchetypeSmoke"));
		return false;
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float Delta)
		{
			return Step(WeakWorld, *State, Delta);
		}), 0.f);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.EnemyArchetypeSmoke"),
		TEXT("Dev check: enemies placed in a level take their archetype (stats, name) from their Blueprint class (BP_Enemy_Brute -> Brute); an instance override wins."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
