// PMC squads as a normal encounter (user request 2026-11-03; docs/systems/pmc-squad-ai.md 7). UE-only, no Godot reference.

#include "AI/PmcEncounterSubsystem.h"

#include "AI/PmcSquadSubsystem.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/CombatPressureSubsystem.h"
#include "Combat/HealthComponent.h"
#include "Combat/SightRules.h"
#include "Combat/TacticalSightSubsystem.h"
#include "Core/CodexTacticsGameMode.h"
#include "Data/WaveConfigTypes.h"
#include "Engine/World.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "GameFramework/PlayerStart.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Math/RandomStream.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"

namespace PmcEncounterLocal
{
	bool IsLive(const AOperativeCharacter* Operative)
	{
		return IsValid(Operative) && !Operative->IsKilledInAction() && Operative->HealthComponent && Operative->HealthComponent->IsAlive();
	}

	/** The player's living men (the squad), else nothing. */
	TArray<AOperativeCharacter*> PlayerMen(const UWorld& World)
	{
		TArray<AOperativeCharacter*> Out;
		if (const USquadSubsystem* Squad = World.GetSubsystem<USquadSubsystem>())
		{
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				if (IsLive(Member) && Member->IsPlayerTeam())
				{
					Out.Add(Member);
				}
			}
		}
		return Out;
	}
}

bool UPmcEncounterSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UPmcEncounterSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UPmcEncounterSubsystem, STATGROUP_Tickables);
}

UPmcEncounterSubsystem* UPmcEncounterSubsystem::Get(const UWorld* World)
{
	return World ? World->GetSubsystem<UPmcEncounterSubsystem>() : nullptr;
}

void UPmcEncounterSubsystem::ReadConfig()
{
	const UWorld* World = GetWorld();
	const ACodexTacticsGameMode* GameMode = World ? World->GetAuthGameMode<ACodexTacticsGameMode>() : nullptr;
	const ULevelConfigAsset* Level = GameMode ? GameMode->GetActiveLevelConfig() : nullptr;
	if (!Level)
	{
		return; // the game mode's StartPlay has not applied the level data yet
	}
	bConfigRead = true;
	Clock = 0.f;
	const TArray<FPmcEncounterSpec>& Squads = Level->Config.PmcSquads;
	for (int32 Index = 0; Index < Squads.Num(); ++Index)
	{
		Pending.Add({ Squads[Index], Index, Squads[Index].DelaySeconds, 0 });
	}
	if (!Squads.IsEmpty())
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[PmcEncounter] level %s: %d PMC squad(s) scheduled"), *Level->Config.LevelId, Squads.Num());
	}
}

void UPmcEncounterSubsystem::Tick(float DeltaTime)
{
	UWorld* World = GetWorld();
	if (!World || !World->HasBegunPlay())
	{
		return;
	}
	if (!bConfigRead)
	{
		ReadConfig();
		if (!bConfigRead)
		{
			return;
		}
	}
	if (Pending.IsEmpty())
	{
		return;
	}
	if (const UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		Flow && (Flow->GetCombatMode() == ECodexCombatMode::TurnBased || Flow->GetPhase() == ECodexGamePhase::GameOver))
	{
		return; // no squads into the grid fight / after the end
	}
	Clock += DeltaTime;
	for (int32 Index = 0; Index < Pending.Num(); ++Index)
	{
		FPending& Entry = Pending[Index];
		if (Clock < Entry.DueAt)
		{
			continue;
		}
		const int32 Seed = Entry.Spec.Seed != 0 ? Entry.Spec.Seed : 1000 + Entry.Index * 101 + Entry.Retries;
		FString Why;
		if (SpawnEncounter(Entry.Spec, Seed, &Why) == INDEX_NONE && ++Entry.Retries <= RetryLimit)
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("[PmcEncounter] squad %d: %s - retry in %.0f s"), Entry.Index, *Why, RetrySeconds);
			Entry.DueAt = Clock + RetrySeconds;
			continue;
		}
		Pending.RemoveAt(Index--);
	}
}

int32 UPmcEncounterSubsystem::SpawnEncounter(const FPmcEncounterSpec& Spec, int32 Seed, FString* OutWhy)
{
	auto Fail = [OutWhy](const FString& Why)
	{
		if (OutWhy)
		{
			*OutWhy = Why;
		}
		return INDEX_NONE;
	};
	UWorld* World = GetWorld();
	UPmcSquadSubsystem* Pmc = UPmcSquadSubsystem::Get(World);
	UNavigationSystemV1* Nav = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
	if (!World || !Pmc || !Nav)
	{
		return Fail(TEXT("no world / PMC subsystem / navigation"));
	}
	if (Seed == 0)
	{
		Seed = 5000 + (++SeedCounter) * 37;
	}
	// The anchor: the player's squad (its centre; the leader for the path test), else the player start.
	const AActor* StartActor = UGameplayStatics::GetActorOfClass(World, APlayerStart::StaticClass());
	const TArray<AOperativeCharacter*> Men = PmcEncounterLocal::PlayerMen(*World);
	FVector PlayerStart = StartActor ? StartActor->GetActorLocation() : FVector::ZeroVector;
	FVector Anchor = PlayerStart;
	FVector PathFrom = PlayerStart;
	if (!Men.IsEmpty())
	{
		Anchor = FVector::ZeroVector;
		for (const AOperativeCharacter* Man : Men)
		{
			Anchor += Man->GetActorLocation();
		}
		Anchor /= Men.Num();
		const USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		const AOperativeCharacter* Leader = Squad && PmcEncounterLocal::IsLive(Squad->GetLeader()) ? Squad->GetLeader() : Men[0];
		PathFrom = Leader->GetActorLocation();
	}
	else if (!StartActor)
	{
		return Fail(TEXT("no player squad / player start to place the squad against"));
	}
	float GroundZ = static_cast<float>(PathFrom.Z) - 92.f;
	FNavLocation FromNav;
	if (Nav->ProjectPointToNavigation(PathFrom, FromNav, FVector(200.f, 200.f, 300.f)))
	{
		GroundZ = static_cast<float>(FromNav.Location.Z);
		PathFrom = FromNav.Location;
	}
	// Out of the squad's view BOTH ways: no low line (the pressure's ambush test, world static) and no eye line from any man's eyes to
	// a standing / crouching fighter's head there (the sight trace, pawns ignored) - the first try only checked the low line and the
	// fighters spotted the squad over a low wall the moment they came in.
	const UCombatPressureSubsystem* Pressure = World->GetSubsystem<UCombatPressureSubsystem>();
	const UTacticalSightSubsystem* SightCheck = World->GetSubsystem<UTacticalSightSubsystem>();
	auto IsSeen = [Pressure, SightCheck, &Men](const FVector& Feet)
	{
		if (Pressure && Pressure->IsVisibleToSquad(Feet))
		{
			return true;
		}
		for (const AOperativeCharacter* Man : Men)
		{
			if (SightRules::IsWithinTouchReveal(static_cast<float>(FVector::Dist2D(Man->GetActorLocation(), Feet))))
			{
				return true;
			}
			if (SightCheck)
			{
				const FVector Eye = UTacticalSightSubsystem::EyePoint(*Man);
				if (SightCheck->HasClearSight(Eye, Feet + FVector(0.f, 0.f, 165.f)) || SightCheck->HasClearSight(Eye, Feet + FVector(0.f, 0.f, 110.f)))
				{
					return true;
				}
			}
		}
		return false;
	};
	auto HasPath = [World, Nav](const FVector& From, const FVector& To)
	{
		const UNavigationPath* Path = Nav->FindPathToLocationSynchronously(const_cast<UWorld*>(World), From, To);
		return Path && Path->IsValid() && !Path->IsPartial();
	};

	const float EyeCm = SightRules::EyeHeight(EOperativeStance::Standing);
	FVector Spot = FVector::ZeroVector;
	int32 SpotLevel = 0;
	if (Spec.bRandomSpawn)
	{
		// Up to MaxCandidates navmesh spots in the ring round the squad on a random level (ground, +3 / +6 / +9 m: roofs, walkways).
		FRandomStream Stream(Seed * 7919 + 211);
		TArray<FPmcSpawnCandidate> Candidates;
		for (int32 Try = 0; Try < 200 && Candidates.Num() < MaxCandidates; ++Try)
		{
			const float Angle = Stream.FRandRange(0.f, 2.f * PI);
			const float Radius = Stream.FRandRange(Spec.MinCm, Spec.MaxCm);
			const int32 Level = Stream.RandRange(0, MaxLevels);
			const FVector Probe(Anchor.X + FMath::Cos(Angle) * Radius, Anchor.Y + FMath::Sin(Angle) * Radius, GroundZ + Level * 300.f + 100.f);
			FNavLocation OnNav;
			if (Nav->ProjectPointToNavigation(Probe, OnNav, FVector(300.f, 300.f, 200.f)))
			{
				Candidates.Add({ OnNav.Location, SightRules::HeightLevel(static_cast<float>(OnNav.Location.Z) + EyeCm - GroundZ) });
			}
		}
		const int32 Picked = PmcEncounterRules::PickEncounterSpot(Candidates, Anchor, Spec.MinCm, Spec.MaxCm, Seed,
			[&](int32 Index)
			{
				const FVector& Feet = Candidates[Index].Location;
				return !IsSeen(Feet) && HasPath(PathFrom, Feet) && HasPath(Feet, PathFrom);
			});
		if (Picked == INDEX_NONE)
		{
			return Fail(FString::Printf(TEXT("no hidden, reachable spot %.0f-%.0f m from the squad (%d candidates)"), Spec.MinCm / 100.f, Spec.MaxCm / 100.f,
				Candidates.Num()));
		}
		Spot = Candidates[Picked].Location;
		SpotLevel = Candidates[Picked].Level;
	}
	else
	{
		FNavLocation OnNav;
		const FVector Probe(Spec.FixedSpot.X, Spec.FixedSpot.Y, Spec.FixedSpot.Z != 0.0 ? Spec.FixedSpot.Z : GroundZ + 100.f);
		if (!Nav->ProjectPointToNavigation(Probe, OnNav, FVector(300.f, 300.f, Spec.FixedSpot.Z != 0.0 ? 200.f : 1200.f)))
		{
			return Fail(FString::Printf(TEXT("the fixed spot %s is off the navmesh"), *Spec.FixedSpot.ToCompactString()));
		}
		Spot = OnNav.Location;
		SpotLevel = SightRules::HeightLevel(static_cast<float>(Spot.Z) + EyeCm - GroundZ);
	}

	// A new hostile team.
	int32 TeamId = 1;
	for (const int32 Existing : Pmc->GetTeamIds())
	{
		TeamId = FMath::Max(TeamId, Existing + 1);
	}
	FPmcWeights Weights;
	const bool bWeights = PmcCommanderRules::LoadWeightsFile(Spec.Weights, Weights);
	const FVector Towards = (Anchor - Spot).GetSafeNormal2D();
	const FRotator Facing = Towards.IsNearlyZero() ? FRotator::ZeroRotator : Towards.Rotation();
	const FVector Right(-Towards.Y, Towards.X, 0.f);
	int32 Count = 0;
	TArray<FVector> Used;
	for (int32 Index = 0; Index < Spec.Count; ++Index)
	{
		// A line across the squad's direction, a second / third rank behind, then a ring round the spot: on the navmesh at the spot's
		// level, out of the squad's view, not on a mate's place.
		TArray<FVector> Wants;
		for (int32 Rank = 0; Rank < 3; ++Rank)
		{
			Wants.Add(Spot + Right * ((Index - (Spec.Count - 1) * 0.5f) * 150.f) - Towards * (Rank * 150.f));
		}
		for (int32 Bearing = 0; Bearing < 8; ++Bearing)
		{
			const float Angle = (Bearing + Index * 0.5f) * (PI / 4.f);
			Wants.Add(Spot + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * 140.f);
		}
		FVector Place = Spot + FVector(0.f, 0.f, Index * 10.f);
		for (const FVector& Want : Wants)
		{
			FNavLocation OnNav;
			const bool bFree = !Used.ContainsByPredicate([&Want](const FVector& Taken) { return FVector::Dist2D(Taken, Want) < 90.f; });
			if (bFree && Nav->ProjectPointToNavigation(Want, OnNav, FVector(100.f, 100.f, 150.f)) && FMath::Abs(OnNav.Location.Z - Spot.Z) <= 150.f
				&& !Used.ContainsByPredicate([&OnNav](const FVector& Taken) { return FVector::Dist2D(Taken, OnNav.Location) < 90.f; })
				&& !IsSeen(OnNav.Location))
			{
				Place = OnNav.Location;
				break;
			}
		}
		Used.Add(Place);
		if (Pmc->SpawnFighter(TeamId, Place + FVector(0.f, 0.f, 95.f), Facing, Seed, Index))
		{
			++Count;
		}
	}
	if (Count == 0)
	{
		return Fail(TEXT("no fighter could spawn"));
	}
	Pmc->SetTeamPosture(TeamId, Spec.Posture, Spot, PlayerStart, Seed, Weights);
	// Hidden at once: the team sight first (who sees whom), then the squad's sight hides what it does not see.
	Pmc->RefreshSight();
	if (UTacticalSightSubsystem* Sight = World->GetSubsystem<UTacticalSightSubsystem>())
	{
		Sight->Refresh();
	}
	FPmcEncounterSpawn& Record = Spawned.AddDefaulted_GetRef();
	Record.TeamId = TeamId;
	Record.Spec = Spec;
	Record.Spot = Spot;
	Record.Level = SpotLevel;
	Record.Seed = Seed;
	Record.Time = World->GetTimeSeconds();
	UE_LOG(LogCodexTactics, Display, TEXT("[PmcEncounter] team %d: %d fighter(s), posture %s, weights %s%s, at %s level %d, %.0f m from the squad (seed %d)"),
		TeamId, Count, PmcEncounterRules::PostureName(Spec.Posture), *Spec.Weights, bWeights ? TEXT("") : TEXT(" (missing: defaults)"),
		*Spot.ToCompactString(), SpotLevel, FVector::Dist2D(Spot, Anchor) / 100.f, Seed);
	return TeamId;
}

namespace PmcEncounterConsole
{
	/** CodexTactics.PmcSpawn [count] [weights] [posture] [min] [max] - positional or key=value (count= weights= posture= min= max= seed=). */
	static void Run(const TArray<FString>& Args, UWorld* World)
	{
		UPmcEncounterSubsystem* Encounters = UPmcEncounterSubsystem::Get(World);
		if (!Encounters)
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("[PmcEncounter] PmcSpawn: no game world"));
			return;
		}
		FPmcEncounterSpec Spec;
		Spec.DelaySeconds = 0.f;
		int32 Seed = 0;
		int32 Positional = 0;
		for (const FString& Raw : Args)
		{
			FString Key;
			FString Value = Raw;
			if (Raw.Split(TEXT("="), &Key, &Value))
			{
				Key = Key.ToLower();
			}
			else
			{
				static const TCHAR* Order[] = { TEXT("count"), TEXT("weights"), TEXT("posture"), TEXT("min"), TEXT("max") };
				Key = Positional < UE_ARRAY_COUNT(Order) ? Order[Positional] : TEXT("");
				++Positional;
			}
			if (Key == TEXT("count"))
			{
				Spec.Count = FMath::Clamp(FCString::Atoi(*Value), 1, 6);
			}
			else if (Key == TEXT("weights"))
			{
				Spec.Weights = Value;
			}
			else if (Key == TEXT("posture") && !PmcEncounterRules::ParsePosture(Value, Spec.Posture))
			{
				UE_LOG(LogCodexTactics, Warning, TEXT("[PmcEncounter] PmcSpawn: posture \"%s\" unknown (defend, hunt, patrol) - hunt"), *Value);
			}
			else if (Key == TEXT("min"))
			{
				Spec.MinCm = FMath::Max(5.f, FCString::Atof(*Value)) * 100.f;
			}
			else if (Key == TEXT("max"))
			{
				Spec.MaxCm = FCString::Atof(*Value) * 100.f;
			}
			else if (Key == TEXT("seed"))
			{
				Seed = FCString::Atoi(*Value);
			}
		}
		Spec.MaxCm = FMath::Max(Spec.MaxCm, Spec.MinCm + 500.f);
		FString Why;
		const int32 TeamId = Encounters->SpawnEncounter(Spec, Seed, &Why);
		if (TeamId == INDEX_NONE)
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("[PmcEncounter] PmcSpawn failed: %s"), *Why);
		}
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(TEXT("CodexTactics.PmcSpawn"),
		TEXT("Brings a hostile PMC squad into the running game (2026-11-03, docs/systems/pmc-squad-ai.md 7): ")
		TEXT("CodexTactics.PmcSpawn [count=3] [weights=pmc_commander.json] [posture=hunt|defend|patrol] [min=40] [max=90] [seed=N] ")
		TEXT("- a hidden, reachable navmesh spot min..max m from the squad; positional values in that order work too."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}
