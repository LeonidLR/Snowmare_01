// Dev-only headless check of the playtest bot's stealth on a patrol map (user plan 2026-10-07; the map is not saved):
//   Scripts/smoke.ps1 -Command CodexTactics.BotStealthSmoke -Map /Game/Maps/L_PatrolTest -Log Smoke-BotStealth.log
// The bot starts without the loot walk (seed 3). On a map without patrols the bot's runtime patrols are spawned first.
// Checks: the level is an ambush level and the bot sneaks (stealth stage, Passive posture — no auto-fire gives the squad
// away); it makes stealth decisions (stance / hide / hold / ambush); it never presses "Start combat"; within 150 s the
// fight starts by its own strike or by a detection (a [Stealth] outcome) — or it is still sneaking undetected — and once
// the fight is on the posture is Aggressive.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Bot/PlaytestBotSubsystem.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Engine/World.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "GameFlow/LevelEncounterSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

namespace BotStealthSmoke
{
	struct FState
	{
		float Time = 0.f;
		bool bStarted = false;
		bool bPassiveSeen = false;
		int32 Failures = 0;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("BotStealthSmoke"));
		return false;
	}

	bool Step(UWorld* World, FState& State)
	{
		State.Time += 0.5f;
		UPlaytestBotSubsystem* Bot = World->GetSubsystem<UPlaytestBotSubsystem>();
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		ULevelEncounterSubsystem* Encounter = World->GetSubsystem<ULevelEncounterSubsystem>();
		const UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		if (!Bot || !Squad || !Encounter || !Flow || !Squad->GetLeader())
		{
			Check(State, false, TEXT("bot, squad and encounter subsystems present"));
			return Finish(State);
		}
		if (!State.bStarted)
		{
			State.bStarted = true;
			if (!Encounter->IsAmbushCombatStart())
			{
				const int32 Spawned = Bot->SpawnRuntimePatrols();
				Check(State, Spawned > 0, FString::Printf(TEXT("no patrols on the map: %d runtime patrol enemies spawned"), Spawned));
			}
			Check(State, Encounter->IsAmbushCombatStart() && Flow->GetPhase() == ECodexGamePhase::Exploration,
				TEXT("ambush level in exploration (no \"Start combat\")"));
			Bot->StartBot(EBotProfile::Veteran, false, false, 3);
			return true;
		}
		if (Bot->GetStage() == UPlaytestBotSubsystem::EBotStage::Stealth && Squad->GetSquadPosture() == ESquadFirePosture::Passive)
		{
			State.bPassiveSeen = true;
		}
		const bool bFight = Flow->IsCombatUnlocked() || Flow->GetPhase() != ECodexGamePhase::Exploration;
		if (bFight && Bot->GetStage() == UPlaytestBotSubsystem::EBotStage::Stealth)
		{
			return true; // the bot notices on its next tick
		}
		if (!bFight && State.Time < 150.f)
		{
			if (Bot->HasPressedCombatStart())
			{
				Check(State, false, TEXT("the bot pressed \"Start combat\" on an ambush level"));
				return Finish(State);
			}
			return true;
		}
		Check(State, Bot->IsStealthLevel(), TEXT("the bot treats the level as a stealth level"));
		Check(State, State.bPassiveSeen, TEXT("sneaking with the Passive fire posture"));
		Check(State, Bot->GetStealthDecisions() > 0, FString::Printf(TEXT("stealth decisions made (%d, last %s, stance %s)"), Bot->GetStealthDecisions(),
			BotStealthRules::ActionName(Bot->GetStealthAction()), BotStealthRules::StanceName(Bot->GetStealthStance())));
		Check(State, !Bot->HasPressedCombatStart(), TEXT("never pressed \"Start combat\""));
		if (bFight)
		{
			Check(State, !Bot->GetStealthOutcome().IsEmpty() && Encounter->GetAmbushStarts() == 1,
				FString::Printf(TEXT("the fight started once by ambush / detection: outcome %s at %.1f s"), *Bot->GetStealthOutcome(), State.Time));
			Check(State, Squad->GetSquadPosture() == ESquadFirePosture::Aggressive, TEXT("the fight runs with the Aggressive posture"));
		}
		else
		{
			Check(State, Bot->GetStealthOutcome().IsEmpty(), FString::Printf(TEXT("still sneaking undetected after %.0f s"), State.Time));
		}
		return Finish(State);
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		TSharedRef<FState> State = MakeShared<FState>();
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FTimerHandle> Handle = MakeShared<FTimerHandle>();
		FTimerHandle StartHandle;
		World->GetTimerManager().SetTimer(StartHandle, FTimerDelegate::CreateLambda([WeakWorld, State, Handle]()
		{
			if (UWorld* W = WeakWorld.Get())
			{
				W->GetTimerManager().SetTimer(*Handle, FTimerDelegate::CreateLambda([WeakWorld, State, Handle]()
				{
					UWorld* W2 = WeakWorld.Get();
					if (W2 && !Step(W2, *State))
					{
						W2->GetTimerManager().ClearTimer(*Handle);
					}
				}), 0.5f, true);
			}
		}), 3.f, false);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(TEXT("CodexTactics.BotStealthSmoke"),
		TEXT("Dev check: the playtest bot sneaks on a patrol map (stealth decisions, no start button, ambush or detection); logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
