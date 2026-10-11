// Dev-only diagnostic of how a map's fight starts (user bug 2026-10-11, L_PatrolTest): logs once a second the game-flow
// phase / combat mode, the encounter mode, every enemy (archetype state, patrol, target, health, placed in the map) and
// every operative (fire posture, combat target, health) for N seconds, then (with "exit") quits.
//   UnrealEditor-Cmd <uproject> /Game/Maps/L_PatrolTest -game -nullrhi "-ExecCmds=CodexTactics.EncounterProbe 30 exit"
// Lines start with "[Probe]".

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AI/PatrolRouteActor.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/FirePostureRules.h"
#include "Characters/MarksmanEnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "GameFlow/LevelEncounterRules.h"
#include "GameFlow/LevelEncounterSubsystem.h"
#include "HAL/IConsoleManager.h"

namespace EncounterProbe
{
	void LogSnapshot(UWorld& World, float Time)
	{
		const UGameFlowSubsystem* Flow = World.GetSubsystem<UGameFlowSubsystem>();
		const ULevelEncounterSubsystem* Encounter = World.GetSubsystem<ULevelEncounterSubsystem>();
		UE_LOG(LogCodexTactics, Display, TEXT("[Probe] t=%.0f s phase %s mode %s ambush-fight %d unlocked %d | combat start %s, ambush level %d, patrols %d, placed enemies %d, ambush starts %d"),
			Time, Flow ? *UEnum::GetValueAsString(Flow->GetPhase()) : TEXT("-"), Flow ? *UEnum::GetValueAsString(Flow->GetCombatMode()) : TEXT("-"),
			Flow && Flow->IsAmbushFight() ? 1 : 0, Flow && Flow->IsCombatUnlocked() ? 1 : 0,
			Encounter ? *LevelEncounterRules::CombatStartName(Encounter->GetCombatStartMode()) : TEXT("-"),
			Encounter && Encounter->IsAmbushCombatStart() ? 1 : 0, Encounter && Encounter->LevelHasPatrols() ? 1 : 0,
			Encounter ? Encounter->GetNumLevelEnemies() : -1, Encounter ? Encounter->GetAmbushStarts() : -1);
		for (TActorIterator<AEnemyCharacter> It(&World); It; ++It)
		{
			const AEnemyCharacter* Enemy = *It;
			const AMarksmanEnemyCharacter* Marksman = Cast<AMarksmanEnemyCharacter>(Enemy);
			const UHealthComponent* Health = Enemy->GetHealthComponent();
			UE_LOG(LogCodexTactics, Display, TEXT("[Probe]   enemy %s (%s%s) hp %.0f patrol %d route %s escort-of %s target %s placed %d%s"),
				*Enemy->GetName(), *UEnum::GetValueAsString(Enemy->GetArchetype()),
				Marksman ? *FString::Printf(TEXT(", %s, shots %d"), *UEnum::GetValueAsString(Marksman->GetAIState()), Marksman->GetShotsFired()) : TEXT(""),
				Health ? Health->GetCurrentHealth() : -1.f, Enemy->IsOnPatrol() ? 1 : 0, *GetNameSafe(Enemy->AssignedPatrolRoute.Get()),
				*GetNameSafe(Enemy->GetEscortLeader()), *GetNameSafe(Enemy->GetCurrentTarget()),
				Encounter && Encounter->IsLevelEnemy(Enemy) ? 1 : 0, Enemy->IsDying() ? TEXT(" DYING") : TEXT(""));
		}
		if (const USquadSubsystem* Squad = World.GetSubsystem<USquadSubsystem>())
		{
			for (const AOperativeCharacter* Member : Squad->GetMembers())
			{
				const UHealthComponent* Health = Member ? Member->HealthComponent : nullptr;
				UE_LOG(LogCodexTactics, Display, TEXT("[Probe]   operative %s posture %s hp %.0f target %s"), *Member->DisplayName.ToString(),
					*UEnum::GetValueAsString(Member->GetFirePosture()), Health ? Health->GetCurrentHealth() : -1.f,
					*GetNameSafe(Member->GetCurrentCombatTarget()));
			}
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		const float Seconds = Args.Num() > 0 ? FCString::Atof(*Args[0]) : 30.f;
		const bool bExit = Args.ContainsByPredicate([](const FString& Arg) { return Arg.Equals(TEXT("exit"), ESearchCase::IgnoreCase); });
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<float> Time = MakeShared<float>(0.f);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, Time, Seconds, bExit](float)
		{
			UWorld* ProbeWorld = WeakWorld.Get();
			if (!ProbeWorld)
			{
				return false;
			}
			LogSnapshot(*ProbeWorld, *Time);
			*Time += 1.f;
			if (*Time > Seconds)
			{
				if (bExit)
				{
					FPlatformMisc::RequestExit(false, TEXT("EncounterProbe"));
				}
				return false;
			}
			return true;
		}), 1.f);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.EncounterProbe"),
		TEXT("Logs the fight start state ([Probe]: phase, encounter mode, enemies, operatives) once a second: [seconds=30] [exit]."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
