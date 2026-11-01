// Dev console commands of the combat lab (game designer decisions 2026-11-01, docs/design/COMBAT_LAB.md) - the designer's switches on
// /Game/Maps/L_FormationLabCombat besides the console variables Codex.Cheat.Immortal / Codex.Cheat.InfiniteAmmo / Codex.Pressure.*:
//   Codex.Pressure.Spawn [patrol|ambush|coordinated] [count]   a spawn event now (the seeded pick without arguments; ignores the alive caps;
//                                                  "coordinated" = a multi-direction ambush plan of 4-6 round the squad, 2026-11-01)
//   Codex.Pressure.Clear                           removes every pressure enemy
//   Codex.Pressure.Status                          the pressure's state in the log (alive, next event, ambushers' states)
// Godot reference: none (UE-only).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "CodexTactics.h"
#include "Combat/CombatCheats.h"
#include "Combat/CombatPressureSubsystem.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

namespace CombatLabCommands
{
	UCombatPressureSubsystem* PressureOf(UWorld* World)
	{
		UCombatPressureSubsystem* Pressure = World ? World->GetSubsystem<UCombatPressureSubsystem>() : nullptr;
		if (!Pressure)
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("[Pressure] no pressure subsystem in this world (a game / PIE world only)"));
		}
		return Pressure;
	}

	void Spawn(const TArray<FString>& Args, UWorld* World)
	{
		UCombatPressureSubsystem* Pressure = PressureOf(World);
		if (!Pressure)
		{
			return;
		}
		TOptional<EPressureSpawnKind> Kind;
		int32 Count = 0;
		for (const FString& Arg : Args)
		{
			if (Arg.Equals(TEXT("patrol"), ESearchCase::IgnoreCase))
			{
				Kind = EPressureSpawnKind::Patrol;
			}
			else if (Arg.Equals(TEXT("ambush"), ESearchCase::IgnoreCase))
			{
				Kind = EPressureSpawnKind::Ambush;
			}
			else if (Arg.Equals(TEXT("coordinated"), ESearchCase::IgnoreCase) || Arg.Equals(TEXT("plan"), ESearchCase::IgnoreCase))
			{
				Kind = EPressureSpawnKind::Coordinated;
			}
			else if (Arg.IsNumeric())
			{
				Count = FCString::Atoi(*Arg);
			}
		}
		const TArray<AEnemyCharacter*> Spawned = Pressure->TriggerEvent(Kind, Count, true);
		UE_LOG(LogCodexTactics, Display, TEXT("[Pressure] console spawn: %d enemies"), Spawned.Num());
	}

	void Clear(const TArray<FString>&, UWorld* World)
	{
		if (UCombatPressureSubsystem* Pressure = PressureOf(World))
		{
			Pressure->DespawnAll();
		}
	}

	void Status(const TArray<FString>&, UWorld* World)
	{
		UCombatPressureSubsystem* Pressure = PressureOf(World);
		if (!Pressure)
		{
			return;
		}
		UE_LOG(LogCodexTactics, Display, TEXT("[Pressure] active %d, auto %d, alive %d / %d (plans %d), next event in %.0f s, events %d (waited %d), sprung by watch %d, immortal %d, infinite ammo %d"),
			Pressure->IsActive() ? 1 : 0, Pressure->IsAutoSpawn() ? 1 : 0, Pressure->GetAliveCount(), Pressure->GetConfig().MaxAlive, Pressure->GetConfig().Coordinated.MaxAlive,
			Pressure->GetSecondsToNextEvent(), Pressure->GetEventsFired(), Pressure->GetEventsDelayed(), Pressure->GetSprungByWatchCount(),
			CombatCheats::IsImmortal(World) ? 1 : 0, CombatCheats::IsInfiniteAmmo(World) ? 1 : 0);
		for (const FPressureSpawnRecord& Record : Pressure->GetSpawnRecords())
		{
			const AEnemyCharacter* Enemy = Record.Enemy.Get();
			EPressureAmbushState State = EPressureAmbushState::Hidden;
			float Hold = 0.f;
			const bool bAmbusher = Enemy && Pressure->GetAmbushState(Enemy, State, Hold);
			UE_LOG(LogCodexTactics, Display, TEXT("[Pressure]   group %d %s %s%s: %s%s"), Record.Group,
				Record.Kind == EPressureSpawnKind::Patrol ? TEXT("patrol") : TEXT("ambusher"), *UEnum::GetValueAsString(Record.Type),
				Record.Plan > 0 ? *FString::Printf(TEXT(" (plan %d %s)"), Record.Plan, AmbushPlanRules::PartyName(Record.Party))
					: Record.SpotKind != EAmbushSpotKind::None ? *FString::Printf(TEXT(" (%s spot)"), AmbushSpotRules::KindName(Record.SpotKind)) : TEXT(""),
				Enemy ? (Enemy->IsOnPatrol() ? TEXT("unengaged") : TEXT("engaged")) : TEXT("gone"),
				bAmbusher ? *FString::Printf(TEXT(", %s (held %.1f s)"), State == EPressureAmbushState::Hidden ? TEXT("hidden")
					: State == EPressureAmbushState::Holding ? TEXT("holding") : TEXT("sprung"), Hold) : TEXT(""));
		}
		for (int32 Plan = 1; Plan <= Pressure->GetLastPlanId(); ++Plan)
		{
			FPressurePlanInfo Info;
			if (Pressure->GetPlanInfo(Plan, Info))
			{
				UE_LOG(LogCodexTactics, Display, TEXT("[Pressure]   plan %d: %s%s, flanks at %.1f s, assault at %.1f s, held %.1f s, relocated %d"), Plan,
					AmbushPlanRules::PhaseName(Info.Phase), Info.bDissolved ? TEXT(" (dissolved)") : TEXT(""), Info.FlankAtClock, Info.AssaultAtClock,
					Info.HoldSeconds, Info.Relocations);
			}
		}
		for (const FPressureRelocationRecord& Move : Pressure->GetRelocations())
		{
			UE_LOG(LogCodexTactics, Display, TEXT("[Pressure]   relocation at %.0f s: group %d %s -> %s (%s spot, ahead %.2f, seen %d)"), Move.Clock, Move.Group,
				*Move.From.ToCompactString(), *Move.To.ToCompactString(), AmbushSpotRules::KindName(Move.Kind), Move.AheadCos, Move.bSeenAtMove ? 1 : 0);
		}
	}

	static FAutoConsoleCommandWithWorldAndArgs SpawnCommand(TEXT("Codex.Pressure.Spawn"),
		TEXT("Combat lab: a spawn event now - [patrol|ambush|coordinated] [count] (the seeded pick without arguments; ignores the alive caps)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Spawn));
	static FAutoConsoleCommandWithWorldAndArgs ClearCommand(TEXT("Codex.Pressure.Clear"),
		TEXT("Combat lab: removes every pressure enemy (patrols, ambushers)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Clear));
	static FAutoConsoleCommandWithWorldAndArgs StatusCommand(TEXT("Codex.Pressure.Status"),
		TEXT("Combat lab: the enemy pressure's state in the log (alive, next event, ambushers, cheats)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Status));
}

#endif // !UE_BUILD_SHIPPING
