#include "LevelGen/MissionGoalsSubsystem.h"

#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/EnemyRegistrySubsystem.h"
#include "Combat/HealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "Core/MissionSubsystem.h"
#include "Core/MissionSessionSubsystem.h"
#include "Core/SaveGameSubsystem.h"
#include "Engine/GameInstance.h"
#include "Core/PlayerCommands.h"
#include "Engine/World.h"
#include "GameFlow/LevelExitSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "LevelGen/MissionGoalMarkerActor.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UI/GameMessageSubsystem.h"

#define LOCTEXT_NAMESPACE "MissionGoalsSubsystem"

namespace MissionGoalsSubsystemLocal
{
	static TAutoConsoleVariable<int32> CVarEnabled(
		TEXT("Codex.MissionGoals.Enabled"),
		1,
		TEXT("F-4: the level generator's goals as mission objectives (Content/Data/LevelGen/objectives/<Map>.objectives.json): 1 (default) on, 0 off."),
		ECVF_Default);

	static FAutoConsoleCommandWithWorld CmdDump(
		TEXT("Codex.MissionGoals.Dump"),
		TEXT("Logs the mission goals of this map and which are reached."),
		FConsoleCommandWithWorldDelegate::CreateStatic([](UWorld* World)
		{
			const UMissionGoalsSubsystem* Goals = World ? World->GetSubsystem<UMissionGoalsSubsystem>() : nullptr;
			if (!Goals || !Goals->IsActive())
			{
				UE_LOG(LogCodexTactics, Display, TEXT("[MissionGoals] no goals on this map"));
				return;
			}
			UE_LOG(LogCodexTactics, Display, TEXT("[MissionGoals] %s (%s), %s"), *Goals->GetLoadedPath(),
				Goals->GetFile().Order == EMissionGoalOrder::Sequence ? TEXT("sequence") : TEXT("free order"), *Goals->GetHudLine());
			for (int32 Index = 0; Index < Goals->GetGoalCount(); ++Index)
			{
				const FMissionGoal& Goal = Goals->GetGoals()[Index];
				UE_LOG(LogCodexTactics, Display, TEXT("  %s %s at %s r %.0f%s: %s%s"), *Goal.Id, MissionGoalsRules::KindToString(Goal.Kind),
					*Goal.Location.ToCompactString(), Goal.RadiusCm, Goal.Area.Num() >= 3 ? TEXT(" (room area)") : TEXT(""),
					Goals->IsGoalReached(Index) ? TEXT("REACHED") : TEXT("open"),
					Goals->IsGoalBlocked(Index) ? TEXT(", BLOCKED (enemy in the zone)") : (Goals->IsGoalContested(Index) ? TEXT(", contested") : TEXT("")));
			}
		}));

	bool IsLivingGoalWalker(const AOperativeCharacter* Operative)
	{
		return Operative && !Operative->IsKilledInAction() && (!Operative->HealthComponent || Operative->HealthComponent->GetCurrentHealth() > 0.f);
	}

	FVector GoalWalkerFeet(const AOperativeCharacter* Operative)
	{
		const UCapsuleComponent* Capsule = Operative->GetCapsuleComponent();
		return Operative->GetActorLocation() - FVector(0.f, 0.f, Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 0.f);
	}

	/** One "Enemy"-tagged actor for the zone-clear rule: monsters (AEnemyCharacter), hostile PMC fighters, any other tagged actor. */
	FMissionGoalHostile MakeHostile(const AActor& Actor)
	{
		FMissionGoalHostile Hostile;
		Hostile.Feet = Actor.GetActorLocation() - FVector(0.f, 0.f, Actor.GetSimpleCollisionHalfHeight());
		const UHealthComponent* Health = nullptr;
		if (const AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(&Actor))
		{
			Health = Enemy->GetHealthComponent();
			Hostile.bDying = Enemy->IsDying();
		}
		else if (const AOperativeCharacter* Fighter = Cast<AOperativeCharacter>(&Actor))
		{
			Health = Fighter->HealthComponent;
			Hostile.bDying = Fighter->IsKilledInAction(); // a fallen PMC fighter keeps his "Enemy" tag
		}
		else
		{
			Health = Actor.FindComponentByClass<UHealthComponent>();
		}
		Hostile.bAlive = !Health || Health->IsAlive();
		return Hostile;
	}
}

bool UMissionGoalsSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UMissionGoalsSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMissionGoalsSubsystem, STATGROUP_Tickables);
}

bool UMissionGoalsSubsystem::IsEnabled()
{
	return MissionGoalsSubsystemLocal::CVarEnabled.GetValueOnGameThread() != 0;
}

void UMissionGoalsSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (!IsEnabled())
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[MissionGoals] off (Codex.MissionGoals.Enabled 0) on %s"), *InWorld.GetMapName());
		return;
	}
	const FString MapName = UWorld::RemovePIEPrefix(InWorld.GetMapName());
	const FString Path = MissionGoalsRules::GetDefaultPathForMap(MapName);
	if (MapName.IsEmpty() || !FPaths::FileExists(Path))
	{
		return; // no goals file: no goals, nothing happens
	}
	LoadFromFile(Path);
}

void UMissionGoalsSubsystem::Deinitialize()
{
	ClearGoals();
	Super::Deinitialize();
}

bool UMissionGoalsSubsystem::LoadFromFile(const FString& Path)
{
	ClearGoals();
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *Path))
	{
		UE_LOG(LogCodexTactics, Warning, TEXT("[MissionGoals] cannot read %s"), *Path);
		return false;
	}
	TArray<FString> Errors;
	FMissionGoalsFile Parsed;
	const bool bOk = MissionGoalsRules::Parse(Json, Parsed, Errors);
	for (const FString& Error : Errors)
	{
		UE_LOG(LogCodexTactics, Warning, TEXT("[MissionGoals] %s: %s"), *Path, *Error);
	}
	if (!bOk)
	{
		return false;
	}
	File = MoveTemp(Parsed);
	Reached.Init(false, File.Goals.Num());
	Contested.Init(false, File.Goals.Num());
	Blocked.Init(false, File.Goals.Num());
	BlockAnnounced.Init(false, File.Goals.Num());
	LoadedPath = Path;
	bActive = true;
	bCompleted = false;
	EvaluateIn = 0.f;
	SpawnMarkers();
	UE_LOG(LogCodexTactics, Display, TEXT("[MissionGoals] %d goals (%s) from %s"), File.Goals.Num(),
		File.Order == EMissionGoalOrder::Sequence ? TEXT("sequence") : TEXT("free order"), *Path);
	return true;
}

void UMissionGoalsSubsystem::RestoreReachedGoals(const TArray<FString>& GoalIds)
{
	Reached.Init(false, File.Goals.Num());
	for (int32 Index = 0; Index < File.Goals.Num(); ++Index)
	{
		Reached[Index] = GoalIds.Contains(File.Goals[Index].Id);
		if (AMissionGoalMarkerActor* Marker = GetMarker(Index))
		{
			Marker->SetGoalShown(!Reached[Index]);
		}
	}
	Blocked.Init(false, File.Goals.Num()); // the next check finds the zone state again
	// Loading state is not reaching a goal: no events, messages or CompleteLevel call.
	bCompleted = bActive && MissionGoalsRules::AreAllReached(Reached);
	EvaluateIn = 0.f;
}

void UMissionGoalsSubsystem::ClearGoals()
{
	for (AMissionGoalMarkerActor* Marker : Markers)
	{
		if (IsValid(Marker))
		{
			Marker->Destroy();
		}
	}
	Markers.Reset();
	File = FMissionGoalsFile();
	Reached.Reset();
	Contested.Reset();
	Blocked.Reset();
	BlockAnnounced.Reset();
	LoadedPath.Reset();
	bActive = false;
	bCompleted = false;
}

void UMissionGoalsSubsystem::SpawnMarkers()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	for (int32 Index = 0; Index < File.Goals.Num(); ++Index)
	{
		const FMissionGoal& Goal = File.Goals[Index];
		AMissionGoalMarkerActor* Marker = World->SpawnActorDeferred<AMissionGoalMarkerActor>(AMissionGoalMarkerActor::StaticClass(),
			FTransform(Goal.Location), nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Marker)
		{
			Marker->GoalId = Goal.Id;
			// A room goal: the ring inside the room (a radius as wide as the room's smaller half-side, at most the goal radius).
			float Ring = Goal.RadiusCm;
			if (Goal.Area.Num() >= 3)
			{
				const FBox2D Box(Goal.Area);
				Ring = FMath::Min(Ring, static_cast<float>(FMath::Min(Box.GetSize().X, Box.GetSize().Y) * 0.5 - 30.0));
			}
			Marker->RingRadiusCm = FMath::Max(60.f, Ring);
			Marker->SetGoalNumber(Index + 1, File);
			Marker->FinishSpawning(FTransform(Goal.Location));
			Marker->SetGoalShown(true);
		}
		Markers.Add(Marker);
	}
}

AMissionGoalMarkerActor* UMissionGoalsSubsystem::GetMarker(int32 Index) const
{
	return Markers.IsValidIndex(Index) ? Markers[Index].Get() : nullptr;
}

FString UMissionGoalsSubsystem::GetHudLine() const
{
	return bActive ? MissionGoalsRules::FormatHudLine(GetReachedCount(), GetGoalCount(), IsAnyGoalBlocked()) : FString();
}

void UMissionGoalsSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!bActive || bCompleted)
	{
		return;
	}
	EvaluateIn -= DeltaTime;
	if (EvaluateIn > 0.f)
	{
		return;
	}
	EvaluateIn = EvaluateIntervalSeconds;
	EvaluateNow();
}

void UMissionGoalsSubsystem::EvaluateNow()
{
	using namespace MissionGoalsSubsystemLocal;
	UWorld* World = GetWorld();
	if (!bActive || bCompleted || !World || !World->HasBegunPlay())
	{
		return;
	}
	const UGameInstance* GameInstance = World->GetGameInstance();
	const UMissionSessionSubsystem* Session = GameInstance ? GameInstance->GetSubsystem<UMissionSessionSubsystem>() : nullptr;
	const USaveGameSubsystem* Saves = World->GetSubsystem<USaveGameSubsystem>();
	if ((Session && Session->HasPendingLoad()) || (Saves && Saves->IsApplyingSave()))
	{
		return; // reopened worlds must not reach goals from their default spawn before the save is applied
	}
	const UMissionSubsystem* Mission = World->GetSubsystem<UMissionSubsystem>();
	if (Mission && (Mission->IsMissionFailed() || Mission->IsMissionComplete()))
	{
		return;
	}
	const USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
	TArray<AOperativeCharacter*> Living;
	TArray<FVector> Feet;
	for (AOperativeCharacter* Operative : Squad ? Squad->GetMembers() : TArray<AOperativeCharacter*>())
	{
		if (IsLivingGoalWalker(Operative))
		{
			Living.Add(Operative);
			Feet.Add(GoalWalkerFeet(Operative));
		}
	}
	TArray<FMissionGoalHostile> Hostiles;
	GatherHostiles(Hostiles);
	const TArray<FVector> Resisting = MissionGoalsRules::ResistingFeet(Hostiles);
	if (Living.IsEmpty())
	{
		UpdateZoneState(Resisting, {});
		return;
	}
	TArray<int32> BlockedGoals;
	const TArray<FMissionGoalHit> Hits = MissionGoalsRules::UpdateReached(File, Reached, Feet, Resisting, &BlockedGoals);
	UpdateZoneState(Resisting, BlockedGoals);
	AOperativeCharacter* Last = nullptr;
	for (const FMissionGoalHit& Hit : Hits)
	{
		Last = Living[Hit.OperativeIndex];
		HandleReached(Hit.GoalIndex, Last);
	}
	if (!Hits.IsEmpty() && MissionGoalsRules::AreAllReached(Reached))
	{
		HandleAllReached(Last);
	}
}

void UMissionGoalsSubsystem::GatherHostiles(TArray<FMissionGoalHostile>& OutHostiles) const
{
	OutHostiles.Reset();
	for (const AActor* Actor : UEnemyRegistrySubsystem::GetTaggedEnemies(GetWorld()))
	{
		if (IsValid(Actor))
		{
			OutHostiles.Add(MissionGoalsSubsystemLocal::MakeHostile(*Actor));
		}
	}
}

void UMissionGoalsSubsystem::UpdateZoneState(TConstArrayView<FVector> ResistingFeet, TConstArrayView<int32> BlockedGoals)
{
	Contested.SetNum(File.Goals.Num());
	Blocked.SetNum(File.Goals.Num());
	BlockAnnounced.SetNum(File.Goals.Num());
	for (int32 Index = 0; Index < File.Goals.Num(); ++Index)
	{
		const FMissionGoal& Goal = File.Goals[Index];
		Contested[Index] = !Reached[Index] && MissionGoalsRules::IsGoalContested(Goal, ResistingFeet, File.FloorToleranceCm);
		BlockAnnounced[Index] = BlockAnnounced[Index] && Contested[Index]; // one radio line per contested spell, not per step in / out
		const bool bNowBlocked = BlockedGoals.Contains(Index);
		if (bNowBlocked && !Blocked[Index] && !BlockAnnounced[Index])
		{
			BlockAnnounced[Index] = true;
			UE_LOG(LogCodexTactics, Display, TEXT("[MissionGoals] %s blocked: the squad is in the zone, a resisting enemy too"), *Goal.Id);
			if (UGameMessageSubsystem* Messages = GetWorld() ? GetWorld()->GetSubsystem<UGameMessageSubsystem>() : nullptr)
			{
				Messages->PostMessage(LOCTEXT("HQ", "HQ"), LOCTEXT("GoalZoneNotClear", "Objective zone not secure: clear it of the enemy."));
			}
		}
		Blocked[Index] = bNowBlocked;
	}
}

void UMissionGoalsSubsystem::HandleReached(int32 GoalIndex, AOperativeCharacter* Operative)
{
	const FMissionGoal& Goal = File.Goals[GoalIndex];
	const int32 Count = GetReachedCount();
	if (AMissionGoalMarkerActor* Marker = GetMarker(GoalIndex))
	{
		Marker->SetGoalShown(false);
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[MissionGoals] %s (%s) reached by %s: %d/%d"), *Goal.Id, MissionGoalsRules::KindToString(Goal.Kind),
		Operative ? *Operative->DisplayName.ToString() : TEXT("?"), Count, GetGoalCount());
	if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
	{
		Messages->PostMessage(LOCTEXT("HQ", "HQ"), FText::Format(LOCTEXT("GoalReached", "Objective point reached ({0}/{1})."), Count, GetGoalCount()));
	}
	FGameplayEventContext Context;
	Context.Subject = Operative;
	Context.TargetId = FName(*Goal.Id);
	Context.Kind = FName(MissionGoalsRules::KindToString(Goal.Kind));
	Context.Location = Goal.Location;
	Context.Radius = Goal.RadiusCm;
	GameplayEvents::RaiseWithContext(this, FName(*(GameplayEvents::MissionGoalReached.ToString() + TEXT(".") + Goal.Id)), Context);
	GameplayEvents::RaiseWithContext(this, GameplayEvents::MissionGoalReached, Context);
	OnGoalReached.Broadcast(Goal.Id, Operative, Count);
}

void UMissionGoalsSubsystem::HandleAllReached(AOperativeCharacter* LastOperative)
{
	if (bCompleted)
	{
		return;
	}
	bCompleted = true;
	UE_LOG(LogCodexTactics, Display, TEXT("[MissionGoals] all %d goals reached: the level is won"), GetGoalCount());
	FGameplayEventContext Context;
	Context.Subject = LastOperative;
	GameplayEvents::RaiseWithContext(this, GameplayEvents::MissionGoalsComplete, Context);
	OnGoalsComplete.Broadcast();
	if (ULevelExitSubsystem* Exit = GetWorld()->GetSubsystem<ULevelExitSubsystem>())
	{
		Exit->CompleteLevel(FString::Printf(TEXT("all %d mission goals reached (%s)"), GetGoalCount(), *FPaths::GetCleanFilename(LoadedPath)));
	}
	else if (UMissionSubsystem* Mission = GetWorld()->GetSubsystem<UMissionSubsystem>())
	{
		Mission->CompleteMission();
	}
}

#undef LOCTEXT_NAMESPACE
