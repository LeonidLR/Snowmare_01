// Dev-only headless check of horde #12 (game designer 2026-11-02 / 11-03 / 11-08; UFirefightSubsystem, UHordeSubsystem::RequestFirefightHorde,
// UHordeIntroSubsystem::BeginExternalHorde) on L_Gen_Depot_1 (the level JSON as shipped: since 2026-11-09 the horde is ON there after 10
// firefights - checked first; the smoke then sets the threshold to 2 by the console, the pressure off, the squad tough and holding fire):
//   Scripts/smoke_parallel.ps1 -Commands HordeFirefightsSmoke   (map /Game/Maps/Generated/L_Gen_Depot_1, Scripts/test_map.json)
// 1. Firefight 1: a pressure patrol (2 frostbitten) 12 m ahead, the squad's shot hits one -> it notices (contact + shots) = ONE firefight
//    (more hits in the same encounter do not add one); the HQ hint "one more firefight"; both killed -> the encounter is over (all down).
// 2. Slip-past: a patrol with muted senses comes within its detection reach (10 m) and is left behind (30 m) unseen -> the streak resets.
// 3. Firefight 2: 1 of 2 again (no horde). Firefight 3: 2 of 2 -> the HQ warning, the patrol is killed at once (exploration again), the
//    countdown runs out -> the fight starts with the horde (6: 4 hounds + 2 MILITARY ZOMBIES via the variant Blueprint), the horde intro
//    gives its warning (alert, flyover) for the external horde; the horde never counts as a firefight (the streak stays 0).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AI/PerceptionRules.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/CombatPressureRules.h"
#include "Combat/CombatPressureSubsystem.h"
#include "Combat/FirefightSubsystem.h"
#include "Combat/HealthComponent.h"
#include "Combat/HordeIntroSubsystem.h"
#include "Combat/HordeSubsystem.h"
#include "Combat/WaveSubsystem.h"
#include "Containers/Ticker.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "UI/GameMessageSubsystem.h"

namespace HordeFirefightsSmoke
{
	constexpr float StepSeconds = 0.1f;

	struct FState
	{
		int32 Stage = 0;
		float StageTime = 0.f;
		float Total = 0.f;
		int32 Failures = 0;
		TArray<TWeakObjectPtr<AEnemyCharacter>> Group;
		int32 FeedMark = 0;
		/** Which firefight the running stage plays (1..3). */
		int32 Firefight = 0;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	void SetCVar(const TCHAR* Name, float Value)
	{
		if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			Variable->Set(Value, ECVF_SetByConsole);
		}
	}

	bool Finish(const FState& State, bool bComplete)
	{
		SetCVar(TEXT("Codex.Horde.Enabled"), -1.f);
		SetCVar(TEXT("Codex.Horde.AfterFirefights"), -1.f);
		SetCVar(TEXT("Codex.Horde.FirefightWarningSeconds"), -1.f);
		SetCVar(TEXT("Codex.Pressure.Enabled"), -1.f);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("HordeFirefightsSmoke"));
		return false;
	}

	void Next(FState& State)
	{
		++State.Stage;
		State.StageTime = 0.f;
	}

	/** Feed lines posted since the mark, joined. */
	FString FeedSince(UWorld* World, int32 Mark)
	{
		FString Out;
		if (const UGameMessageSubsystem* Messages = World->GetSubsystem<UGameMessageSubsystem>())
		{
			for (int32 Index = FMath::Max(Mark, 0); Index < Messages->GetHistory().Num(); ++Index)
			{
				Out += Messages->GetHistory()[Index].Text.ToString() + TEXT(" | ");
			}
		}
		return Out;
	}

	int32 FeedCount(UWorld* World)
	{
		const UGameMessageSubsystem* Messages = World->GetSubsystem<UGameMessageSubsystem>();
		return Messages ? Messages->GetHistory().Num() : 0;
	}

	/** A navmesh spot Distance cm ahead of the leader (turned until a clear walk reaches it). */
	FVector SpotAhead(UWorld* World, const AOperativeCharacter& Leader, float Distance, float TurnDeg = 0.f)
	{
		const FVector From = Leader.GetActorLocation();
		const FVector Ahead = Leader.GetActorForwardVector().GetSafeNormal2D().RotateAngleAxis(TurnDeg, FVector::UpVector);
		return SmokeUtils::ClearPoint(World, From, From + Ahead * Distance);
	}

	void SpawnGroup(UWorld* World, FState& State, const AOperativeCharacter& Leader, float Distance, float TurnDeg)
	{
		State.Group.Reset();
		UCombatPressureSubsystem* Pressure = World->GetSubsystem<UCombatPressureSubsystem>();
		if (!Pressure)
		{
			return;
		}
		const FVector Feet = SpotAhead(World, Leader, Distance, TurnDeg);
		for (AEnemyCharacter* Enemy : Pressure->SpawnPatrolGroupAt(Feet, { FPressureEnemyType(EEnemyArchetype::Frostbitten), FPressureEnemyType(EEnemyArchetype::Frostbitten) }))
		{
			State.Group.Add(Enemy);
		}
	}

	void Hit(AEnemyCharacter* Enemy, const AOperativeCharacter& Shooter, float Amount)
	{
		if (Enemy && Enemy->GetHealthComponent())
		{
			FDamageSpec Spec;
			Spec.Amount = Amount;
			Spec.ArmorPenetration = 1.f;
			Spec.AttackerSource = Shooter.DisplayName.ToString();
			Enemy->GetHealthComponent()->TakeDamage(Spec);
		}
	}

	bool GroupDown(const FState& State)
	{
		for (const TWeakObjectPtr<AEnemyCharacter>& Member : State.Group)
		{
			if (Member.IsValid() && !Member->IsDying())
			{
				return false;
			}
		}
		return true;
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.StageTime += StepSeconds;
		State.Total += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Total > 240.f)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke stopped in stage %d"), State.Stage);
			return Finish(State, false);
		}
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UHordeSubsystem* Horde = World->GetSubsystem<UHordeSubsystem>();
		UFirefightSubsystem* Fire = World->GetSubsystem<UFirefightSubsystem>();
		UHordeIntroSubsystem* Intro = World->GetSubsystem<UHordeIntroSubsystem>();
		UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>();
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		if (!Flow || !Squad || !Horde || !Fire || !Intro || !Waves || !Leader)
		{
			return State.Total < 10.f || Finish(State, false);
		}
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			Member->ColdLevel = 0.f;
			Member->bTacticalCeaseFire = true; // only the smoke's own hits count as the squad's shots
			Member->HealthComponent->SetMaxHealth(100000.f, false);
			Member->HealthComponent->Heal(100000.f);
		}
		switch (State.Stage)
		{
		case 0:
		{
			if (State.StageTime < 3.f)
			{
				return true;
			}
			// The Depot as shipped (game designer 2026-11-09): the horde on, after 10 firefights in a row; no real-time-fight clock.
			Horde->RefreshConfig();
			Check(State, Horde->GetConfig().bEnabled && Horde->GetConfig().AfterFirefights == 10 && Fire->GetThreshold() == 10,
				FString::Printf(TEXT("the Depot's level JSON: horde %s after %d firefights in a row"), Horde->GetConfig().bEnabled ? TEXT("on") : TEXT("OFF"),
					Horde->GetConfig().AfterFirefights));
			Check(State, IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Horde.TriggerSeconds")) == nullptr, TEXT("... no real-time-fight horde clock"));
			SetCVar(TEXT("Codex.Pressure.Enabled"), 0.f);
			SetCVar(TEXT("Codex.Horde.Enabled"), 1.f);
			SetCVar(TEXT("Codex.Horde.AfterFirefights"), 2.f);
			SetCVar(TEXT("Codex.Horde.FirefightWarningSeconds"), 4.f);
			Horde->SetDevOverrideJson(TEXT("{\"count\": 6, \"composition\": {\"FROST_HOUND\": 2, \"MILITARY_ZOMBIE\": 1}}"));
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				It->Destroy();
			}
			Fire->ResetForTest();
			Check(State, Flow->GetPhase() == ECodexGamePhase::Exploration, TEXT("exploring the Depot"));
			Check(State, Fire->GetThreshold() == 2 && Horde->GetConfig().bEnabled,
				FString::Printf(TEXT("the horde after %d firefights in a row (level horde_after_firefights 10 -> console 2), horde on"), Fire->GetThreshold()));
			Next(State);
			return true;
		}
		case 1: case 5: case 7:
		{
			// A firefight: the patrol appears 12 m ahead, the squad's hit makes it notice (a fresh encounter: the last one is over).
			if (State.StageTime < 0.5f || ((Fire->IsEncounterActive() || Flow->GetPhase() != ECodexGamePhase::Exploration) && State.StageTime < 6.f))
			{
				return true;
			}
			State.Firefight = State.Stage == 1 ? 1 : State.Stage == 5 ? 2 : 3;
			State.FeedMark = FeedCount(World);
			SpawnGroup(World, State, *Leader, 1200.f, State.Firefight * 25.f);
			Check(State, State.Group.Num() == 2, FString::Printf(TEXT("firefight %d: a patrol of %d, 12 m ahead"), State.Firefight, State.Group.Num()));
			Hit(State.Group.Num() > 0 ? State.Group[0].Get() : nullptr, *Leader, 10.f);
			Next(State);
			return true;
		}
		case 2: case 6: case 8:
		{
			if (!Fire->IsFirefightActive() && State.StageTime < 5.f)
			{
				return true;
			}
			const int32 ExpectedStreak = State.Firefight == 3 ? 0 : 1; // the 2nd of 2 (firefight 3) brings the horde and starts again
			Check(State, Fire->IsFirefightActive(), FString::Printf(TEXT("firefight %d: contact + shots exchanged = a firefight (%s)"), State.Firefight,
				*FeedSince(World, State.FeedMark)));
			Check(State, Flow->GetPhase() == ECodexGamePhase::WaveCombat, TEXT("... the contact started the skirmish"));
			const int32 Before = Fire->GetFirefightCount();
			Hit(State.Group.Num() > 1 ? State.Group[1].Get() : nullptr, *Leader, 10.f); // more shots, the same encounter
			Hit(State.Group.Num() > 0 ? State.Group[0].Get() : nullptr, *Leader, 10.f);
			Check(State, Fire->GetFirefightCount() == Before && Fire->GetFirefightCount() == State.Firefight,
				FString::Printf(TEXT("... one per encounter, not per shot (%d firefights on the level)"), Fire->GetFirefightCount()));
			Check(State, Fire->GetStreak() == ExpectedStreak, FString::Printf(TEXT("... %d in a row now"), Fire->GetStreak()));
			const FString Feed = FeedSince(World, State.FeedMark);
			if (State.Firefight == 3)
			{
				Check(State, Horde->IsFirefightHordePending() && Feed.Contains(TEXT("HORDE is closing")),
					FString::Printf(TEXT("2 in a row: the HQ warning before the horde (%.1f s to go)"), Horde->GetFirefightHordeSecondsLeft()));
			}
			else
			{
				Check(State, !Horde->IsFirefightHordePending() && Feed.Contains(TEXT("One more firefight")), TEXT("... no horde yet, the HQ hint \"one more firefight\""));
			}
			for (const TWeakObjectPtr<AEnemyCharacter>& Member : State.Group)
			{
				Hit(Member.Get(), *Leader, 100000.f);
			}
			Next(State);
			return true;
		}
		case 3: case 9:
		{
			// The patrol is down: the encounter is over, the skirmish too.
			if ((Fire->IsEncounterActive() || Flow->GetPhase() != ECodexGamePhase::Exploration) && State.StageTime < 5.f)
			{
				return true;
			}
			Check(State, GroupDown(State) && !Fire->IsEncounterActive() && Fire->GetLastEnd() == EFirefightEnd::AllDown,
				TEXT("every engaged enemy down: the encounter is over"));
			Check(State, Flow->GetPhase() == ECodexGamePhase::Exploration, TEXT("... back to exploration"));
			if (State.Stage == 3)
			{
				// 2. The slip-past: muted senses (it can never spot the squad), 10 m away - inside its detection reach.
				State.FeedMark = FeedCount(World);
				SpawnGroup(World, State, *Leader, 1000.f, -60.f);
				for (const TWeakObjectPtr<AEnemyCharacter>& Member : State.Group)
				{
					if (AEnemyCharacter* Enemy = Member.Get())
					{
						Enemy->PerceptionOverride = PerceptionRules::Scaled(Enemy->GetPerception(), 0.f);
						Enemy->PerceptionOverride.SmellDetectCm = 0.f;
						Enemy->bOverridePerception = true;
						Enemy->CustomTimeDilation = 0.f;
						Enemy->SetActorTickEnabled(false);
					}
				}
				Check(State, State.Group.Num() == 2 && Fire->GetStreak() == 1, FString::Printf(TEXT("slip-past: a blind patrol 10 m away, the streak at %d"),
					Fire->GetStreak()));
			}
			Next(State);
			return true;
		}
		case 4:
		{
			if (State.StageTime < 1.f)
			{
				return true; // a group pass sees it within the reach
			}
			if (State.StageTime < 1.2f)
			{
				// The squad leaves it behind: 30 m (the reach 15 m x 1.25 = 18.75 m) - moved, never woken.
				const FVector Away = (State.Group.Num() > 0 && State.Group[0].IsValid() ? State.Group[0]->GetActorLocation() - Leader->GetActorLocation()
					: FVector(1.f, 0.f, 0.f)).GetSafeNormal2D();
				for (const TWeakObjectPtr<AEnemyCharacter>& Member : State.Group)
				{
					if (AEnemyCharacter* Enemy = Member.Get())
					{
						Enemy->SetActorLocation(Enemy->GetActorLocation() + Away * 2000.f, false, nullptr, ETeleportType::TeleportPhysics);
					}
				}
				return true;
			}
			if (Fire->GetSlipPastCount() == 0 && State.StageTime < 4.f)
			{
				return true;
			}
			Check(State, Fire->GetSlipPastCount() == 1 && Fire->GetStreak() == 0,
				FString::Printf(TEXT("slipped past unseen: the streak resets (%d -> %d, %d slip-past)"), 1, Fire->GetStreak(), Fire->GetSlipPastCount()));
			Check(State, FeedSince(World, State.FeedMark).Contains(TEXT("never saw you")), TEXT("... the HQ line \"they never saw you\""));
			Check(State, Fire->GetFirefightCount() == 1 && !Fire->IsEncounterActive(), TEXT("... no contact, no firefight"));
			for (const TWeakObjectPtr<AEnemyCharacter>& Member : State.Group)
			{
				if (Member.IsValid())
				{
					Member->Destroy();
				}
			}
			State.Group.Reset();
			Next(State);
			return true;
		}
		case 10:
		{
			// The countdown runs in exploration (the patrol is dead): the horde starts a fight of its own.
			if (Horde->GetFirefightHordesReleased() == 0 && State.StageTime < 12.f)
			{
				return true;
			}
			Check(State, Horde->GetFirefightHordesReleased() == 1 && Flow->GetPhase() == ECodexGamePhase::WaveCombat,
				FString::Printf(TEXT("the horde came %.1f s after the warning and the fight is on"), State.StageTime));
			int32 Hounds = 0;
			int32 Zombies = 0;
			bool bHunters = true;
			for (const TWeakObjectPtr<AEnemyCharacter>& Member : Horde->GetLastHorde())
			{
				const AEnemyCharacter* Enemy = Member.Get();
				if (!Enemy)
				{
					continue;
				}
				Hounds += Enemy->GetArchetype() == EEnemyArchetype::FrostHound ? 1 : 0;
				Zombies += Enemy->GetArchetype() == EEnemyArchetype::Frostbitten && GetNameSafe(Enemy->GetClass()).Contains(TEXT("MilitaryZombie")) ? 1 : 0;
				bHunters &= Enemy->bKnowsSquadPosition;
			}
			Check(State, Hounds == 4 && Zombies == 2, FString::Printf(TEXT("6 of the horde: %d hounds, %d Military Zombies (variant Blueprint)"), Hounds, Zombies));
			Check(State, bHunters && Waves->GetAliveEnemyCount() >= 6, FString::Printf(TEXT("... they know where the squad is, in the fight (%d alive)"),
				Waves->GetAliveEnemyCount()));
			Check(State, Intro->GetExternalIntroCount() == 1, TEXT("the horde intro takes the external horde"));
			Next(State);
			return true;
		}
		case 11:
		{
			if (Intro->GetWarningCount() == 0 && State.StageTime < 15.f)
			{
				return true;
			}
			Check(State, Intro->GetWarningCount() == 1, FString::Printf(TEXT("the horde intro's warning: alert + HQ line (%s)"), Intro->IsFlyoverActive()
				? TEXT("the flyover runs - the level's first horde") : TEXT("no flyover")));
			Check(State, Intro->GetStartedCount() == 1, TEXT("... the level's first horde gets its flyover"));
			Intro->DismissIntro();
			Next(State);
			return true;
		}
		case 12:
		{
			if (State.StageTime < 3.f)
			{
				return true;
			}
			Check(State, Fire->GetStreak() == 0 && Fire->GetFirefightCount() == 3,
				FString::Printf(TEXT("the horde's own fight is no firefight (streak %d, %d on the level)"), Fire->GetStreak(), Fire->GetFirefightCount()));
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
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.HordeFirefightsSmoke"),
		TEXT("Dev check of horde #12 on L_Gen_Depot_1: firefights in a row, the slip-past reset, the warning and the horde; PASS / FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
