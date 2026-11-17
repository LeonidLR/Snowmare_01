// Dev-only console command (F-4 «а», designer 2026-11-17): the level generator's goals as mission objectives, on L_Gen_Test_X2:
//   Scripts/smoke_parallel.ps1 -NoRetry -Commands MissionGoalsSmoke
// The map's Content/Data/LevelGen/objectives/L_Gen_Test_X2.objectives.json is loaded (UMissionGoalsSubsystem), one hologram marker and
// one minimap objective icon per goal, the HUD line "GOALS: 0/N". The squad is teleported to each goal in turn: each is counted
// (marker hidden, the icon gone, MissionGoalReached on the event bus), no victory before the last one; after the last the EXISTING level
// end runs (ULevelExitSubsystem::CompleteLevel -> mission complete + the fade). [map=<L_Gen_...>] for another generated map.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Components/CapsuleComponent.h"
#include "Containers/Ticker.h"
#include "Core/MissionSubsystem.h"
#include "Core/PlayerCommands.h"
#include "Debug/EventBusProbe.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "GameFlow/LevelExitSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "LevelGen/MissionGoalMarkerActor.h"
#include "Components/TextRenderComponent.h"
#include "LevelGen/MissionGoalsSubsystem.h"
#include "Misc/Paths.h"
#include "Subsystems/CodexEventBus.h"
#include "TimerManager.h"
#include "UI/MinimapSubsystem.h"
#include "UObject/StrongObjectPtr.h"

namespace MissionGoalsSmoke
{
	constexpr float WarmupSeconds = 2.f;
	/** Game seconds a goal may take to count after the teleport (the check runs every 0.2 s). */
	constexpr float CountWithinSeconds = 3.f;
	/** Game seconds the squad stands on a goal; the victory must not start before the last. */
	constexpr float SettleSeconds = 1.f;
	constexpr float TimeoutSeconds = 120.f;

	enum class EStage : uint8
	{
		Begin,
		Teleport,
		WaitCount,
		Settle,
		Victory,
		Done
	};

	struct FState
	{
		EStage Stage = EStage::Begin;
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Goal = 0;
		int32 Failures = 0;
		FString MapName;
		SmokeUtils::FPreloadGate PreloadGate;
		TStrongObjectPtr<UEventBusProbe> Probe;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State, bool bComplete)
	{
		State.Stage = EStage::Done;
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("MissionGoalsSmoke"));
		return false;
	}

	/** The goal markers the minimap draws as objective icons now (-1 when the minimap is not ready: no capture headless). */
	int32 MinimapGoalIcons(const UWorld* World)
	{
		const UMinimapSubsystem* Minimap = World->GetSubsystem<UMinimapSubsystem>();
		if (!Minimap || !Minimap->IsReady())
		{
			return -1;
		}
		TArray<FMinimapIcon> Icons;
		Minimap->BuildIcons(Icons);
		int32 Count = 0;
		for (const FMinimapIcon& Icon : Icons)
		{
			Count += Icon.Kind == EMinimapIconKind::Objective && Icon.Actor.IsValid() && Icon.Actor->IsA<AMissionGoalMarkerActor>() ? 1 : 0;
		}
		return Count;
	}

	int32 ShownMarkers(const UMissionGoalsSubsystem* Goals)
	{
		int32 Count = 0;
		for (int32 Index = 0; Index < Goals->GetGoalCount(); ++Index)
		{
			const AMissionGoalMarkerActor* Marker = Goals->GetMarker(Index);
			Count += Marker && Marker->IsMarkerShown() && !Marker->IsHidden() ? 1 : 0;
		}
		return Count;
	}

	void CheckGoalLabels(FState& State, const UMissionGoalsSubsystem* Goals)
	{
		for (int32 Index = 0; Index < Goals->GetGoalCount(); ++Index)
		{
			const AMissionGoalMarkerActor* Marker = Goals->GetMarker(Index);
			const UTextRenderComponent* Text = Marker ? Marker->GetGoalText() : nullptr;
			const FString Expected = FString::Printf(TEXT("GOAL %d"), Index + 1);
			const bool bReached = Goals->IsGoalReached(Index);
			const bool bVisibility = Text && (bReached ? !Text->IsVisible() : Text->IsVisible() && Marker->IsMarkerShown() && !Marker->IsHidden());
			Check(State, Text && Text->Text.ToString() == Expected && bVisibility,
				FString::Printf(TEXT("%s label has its text and is %s"), *Expected, bReached ? TEXT("hidden after reaching") : TEXT("visible while unreached")));
		}
	}

	bool IsVictory(const UWorld* World)
	{
		const UMissionSubsystem* Mission = World->GetSubsystem<UMissionSubsystem>();
		const ULevelExitSubsystem* Exit = World->GetSubsystem<ULevelExitSubsystem>();
		return (Mission && Mission->IsMissionComplete()) || (Exit && Exit->IsExiting());
	}

	/** The squad at the goal: the leader on its point, the others 80 cm round him (inside the 300 cm radius / the room). */
	void TeleportSquad(UWorld* World, const FMissionGoal& Goal)
	{
		const USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		int32 Slot = 0;
		for (AOperativeCharacter* Member : Squad ? Squad->GetMembers() : TArray<AOperativeCharacter*>())
		{
			const float Angle = Slot * 2.f * PI / 3.f;
			const FVector Offset = Slot == 0 ? FVector::ZeroVector : FVector(FMath::Cos(Angle) * 80.f, FMath::Sin(Angle) * 80.f, 0.f);
			const float HalfHeight = Member->GetCapsuleComponent() ? Member->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.f;
			Member->TeleportTo(Goal.Location + Offset + FVector(0.f, 0.f, HalfHeight + 5.f), Member->GetActorRotation(), false, true);
			++Slot;
		}
	}

	bool Step(UWorld* World, FState& State, float DeltaSeconds)
	{
		switch (State.PreloadGate.Poll(World))
		{
		case SmokeUtils::EPreloadGate::Waiting:
			return true;
		case SmokeUtils::EPreloadGate::Failed:
			Check(State, false, State.PreloadGate.FailReason);
			return Finish(State, false);
		default:
			break;
		}
		State.Time += DeltaSeconds;
		State.StageTime += DeltaSeconds;
		UMissionGoalsSubsystem* Goals = World->GetSubsystem<UMissionGoalsSubsystem>();
		const USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		if (!Goals || !Squad || State.Time > TimeoutSeconds)
		{
			Check(State, false, FString::Printf(TEXT("finished in time (stage %d, goal %d, %.1f s)"), static_cast<int32>(State.Stage), State.Goal, State.Time));
			return Finish(State, false);
		}
		const int32 Total = Goals->GetGoalCount();
		auto SetStage = [&State](EStage Stage) { State.Stage = Stage; State.StageTime = 0.f; };
		switch (State.Stage)
		{
		case EStage::Begin:
		{
			if (State.StageTime < WarmupSeconds || Squad->GetMembers().IsEmpty())
			{
				return true;
			}
			const FString Expected = MissionGoalsRules::GetDefaultPathForMap(State.MapName);
			Check(State, Goals->IsActive(), FString::Printf(TEXT("the map's goals file is loaded (%s)"), *FPaths::GetCleanFilename(Goals->GetLoadedPath())));
			Check(State, FPaths::IsSamePath(Goals->GetLoadedPath(), Expected), FString::Printf(TEXT("it is %s"), *FPaths::GetCleanFilename(Expected)));
			Check(State, Total >= 2, FString::Printf(TEXT("%d goals (>= 2)"), Total));
			if (!Goals->IsActive() || Total < 1)
			{
				return Finish(State, false);
			}
			Check(State, Goals->GetReachedCount() == 0, TEXT("none reached at the start"));
			CheckGoalLabels(State, Goals);
			Check(State, ShownMarkers(Goals) == Total, FString::Printf(TEXT("a world marker shown per goal (%d/%d)"), ShownMarkers(Goals), Total));
			Check(State, Goals->GetHudLine() == MissionGoalsRules::FormatHudLine(0, Total), FString::Printf(TEXT("HUD line '%s'"), *Goals->GetHudLine()));
			const int32 Icons = MinimapGoalIcons(World);
			if (Icons >= 0)
			{
				Check(State, Icons == Total, FString::Printf(TEXT("a minimap objective icon per goal (%d/%d)"), Icons, Total));
			}
			else
			{
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke note: the minimap is not ready (no capture) - its icon check is skipped"));
			}
			Check(State, !IsVictory(World), TEXT("no victory at the start"));
			State.Probe.Reset(NewObject<UEventBusProbe>());
			if (UCodexEventBus* Bus = UCodexEventBus::Get(World))
			{
				State.Probe->ListenGameplayEvents(Bus);
			}
			SetStage(EStage::Teleport);
			return true;
		}
		case EStage::Teleport:
		{
			const FMissionGoal& Goal = Goals->GetGoals()[State.Goal];
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: the squad to %s (%s) at %s"), *Goal.Id, MissionGoalsRules::KindToString(Goal.Kind), *Goal.Location.ToCompactString());
			TeleportSquad(World, Goal);
			SetStage(EStage::WaitCount);
			return true;
		}
		case EStage::WaitCount:
		{
			const bool bCounted = Goals->IsGoalReached(State.Goal);
			if (!bCounted && State.StageTime < CountWithinSeconds)
			{
				return true;
			}
			const FMissionGoal& Goal = Goals->GetGoals()[State.Goal];
			Check(State, bCounted, FString::Printf(TEXT("%s counted (%.1f s after the teleport)"), *Goal.Id, State.StageTime));
			Check(State, Goals->GetReachedCount() == State.Goal + 1, FString::Printf(TEXT("%d/%d reached"), Goals->GetReachedCount(), Total));
			Check(State, Goals->GetHudLine() == MissionGoalsRules::FormatHudLine(State.Goal + 1, Total), FString::Printf(TEXT("HUD line '%s'"), *Goals->GetHudLine()));
			const AMissionGoalMarkerActor* Marker = Goals->GetMarker(State.Goal);
			Check(State, Marker && !Marker->IsMarkerShown(), FString::Printf(TEXT("%s marker hidden"), *Goal.Id));
			CheckGoalLabels(State, Goals);
			Check(State, ShownMarkers(Goals) == Total - State.Goal - 1, FString::Printf(TEXT("%d markers left"), ShownMarkers(Goals)));
			const int32 Heard = State.Probe ? State.Probe->CountEvent(GameplayEvents::MissionGoalReached) : 0;
			Check(State, Heard == State.Goal + 1, FString::Printf(TEXT("MissionGoalReached on the event bus (%d)"), Heard));
			if (!bCounted)
			{
				return Finish(State, false);
			}
			SetStage(EStage::Settle);
			return true;
		}
		case EStage::Settle:
		{
			const bool bLast = State.Goal == Total - 1;
			if (!bLast && IsVictory(World))
			{
				Check(State, false, FString::Printf(TEXT("no victory before the last goal (after %s)"), *Goals->GetGoals()[State.Goal].Id));
				return Finish(State, false);
			}
			if (State.StageTime < SettleSeconds && !bLast)
			{
				return true;
			}
			if (!bLast)
			{
				const int32 Icons = MinimapGoalIcons(World);
				if (Icons >= 0)
				{
					Check(State, Icons == Total - State.Goal - 1, FString::Printf(TEXT("the minimap icon of %s gone (%d left)"), *Goals->GetGoals()[State.Goal].Id, Icons));
				}
				Check(State, !IsVictory(World), FString::Printf(TEXT("no victory with %d/%d goals"), State.Goal + 1, Total));
				++State.Goal;
				SetStage(EStage::Teleport);
				return true;
			}
			SetStage(EStage::Victory);
			return true;
		}
		case EStage::Victory:
		{
			if (!IsVictory(World) && State.StageTime < CountWithinSeconds)
			{
				return true;
			}
			const UMissionSubsystem* Mission = World->GetSubsystem<UMissionSubsystem>();
			const ULevelExitSubsystem* Exit = World->GetSubsystem<ULevelExitSubsystem>();
			Check(State, Goals->AreAllReached(), TEXT("every goal reached"));
			Check(State, Mission && Mission->IsMissionComplete(), TEXT("the mission is complete (UMissionSubsystem::CompleteMission)"));
			Check(State, Exit && Exit->IsExiting(), TEXT("the level end runs (ULevelExitSubsystem: the fade to black)"));
			Check(State, Mission && !Mission->IsMissionFailed(), TEXT("not failed"));
			const int32 Complete = State.Probe ? State.Probe->CountEvent(GameplayEvents::MissionGoalsComplete) : 0;
			Check(State, Complete == 1, FString::Printf(TEXT("MissionGoalsComplete on the event bus once (%d)"), Complete));
			return Finish(State, true);
		}
		default:
			return false;
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		TSharedRef<FState> State = MakeShared<FState>();
		State->MapName = UWorld::RemovePIEPrefix(World->GetMapName());
		for (const FString& Arg : Args)
		{
			if (Arg.StartsWith(TEXT("map=")))
			{
				State->MapName = FPaths::GetBaseFilename(Arg.Mid(4));
			}
		}
		// A goal check, not a fight: the pressure director and the level's PMC encounters stay off.
		for (const TCHAR* Name : { TEXT("Codex.Pressure.Enabled"), TEXT("Codex.Pmc.Encounters"), TEXT("Codex.Combat.UnderFireManoeuvre") })
		{
			if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
			{
				Variable->Set(0, ECVF_SetByConsole);
			}
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke: MissionGoalsSmoke on %s"), *State->MapName);
		TWeakObjectPtr<UWorld> WeakWorld(World);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			UWorld* W = WeakWorld.Get();
			if (!W)
			{
				return Finish(*State, false);
			}
			return State->Stage != EStage::Done && Step(W, *State, W->IsPaused() ? 0.f : W->GetDeltaSeconds());
		}), 0.f);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.MissionGoalsSmoke"),
		TEXT("Dev check (F-4, designer 2026-11-17): the generated map's mission goals - loaded with markers / minimap icons / HUD line, the squad ")
		TEXT("teleported to each goal in turn is counted, no victory before the last, the level end after it. [map=<L_Gen_...>]. PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
