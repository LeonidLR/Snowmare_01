#include "Environment/CharacterSnowSubsystem.h"

#include "AI/WorldAIPauseSubsystem.h"
#include "Camera/CameraZoneVolume.h"
#include "Camera/PlayerCameraManager.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/EnemyRegistrySubsystem.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Environment/EnvironmentTrace.h"
#include "Environment/CharacterSnowRules.h"
#include "Environment/EnvironmentRules.h"
#include "Environment/EnvironmentSubsystem.h"
#include "Environment/WeatherPresenterActor.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/HeatSourceComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Misc/ScopeExit.h"

// UE-only (no Godot reference). Design: docs/systems/environment-weather.md section 15 (user decisions 2026-10-23).

namespace CharacterSnowLocal
{
	TAutoConsoleVariable<int32> CVarCharacterSnow(
		TEXT("Codex.Env.CharacterSnow"), 1,
		TEXT("Snow on characters (docs/systems/environment-weather.md section 15): 1 = operatives / enemies get covered while it snows (body Custom ")
		TEXT("Primitive Data 8..12, the M_CharacterMaster* snow layer), 0 = off (every body back to 0)."),
		ECVF_Default);

	TAutoConsoleVariable<int32> CVarCharacterSnowMax(
		TEXT("Codex.Env.CharacterSnowMax"), 40,
		TEXT("Snow on characters: at most this many characters nearest the camera are processed; the others keep their snow frozen."),
		ECVF_Scalability);

	/** Map-placed camera zones are gathered this often (never per character). */
	constexpr float ShelterRefreshSeconds = 10.f;
}

bool UCharacterSnowSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	// Purely visual: nothing to do without a renderer (every -nullrhi smoke / bot run is untouched) or on a dedicated server.
	return Super::ShouldCreateSubsystem(Outer) && FApp::CanEverRender() && !IsRunningDedicatedServer();
}

bool UCharacterSnowSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UCharacterSnowSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	RefreshShelters();
}

TStatId UCharacterSnowSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UCharacterSnowSubsystem, STATGROUP_Tickables);
}

UCharacterSnowSubsystem* UCharacterSnowSubsystem::Get(const UObject* Context)
{
	const UWorld* World = Context ? Context->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UCharacterSnowSubsystem>() : nullptr;
}

bool UCharacterSnowSubsystem::IsEnabled()
{
	return CharacterSnowLocal::CVarCharacterSnow.GetValueOnGameThread() != 0;
}

bool UCharacterSnowSubsystem::GetPockets(const ACharacter* Character, FDirectionalSnow& OutPockets) const
{
	const FEntry* Entry = Entries.Find(TWeakObjectPtr<ACharacter>(const_cast<ACharacter*>(Character)));
	if (!Entry)
	{
		return false;
	}
	OutPockets = Entry->Pockets;
	return true;
}

void UCharacterSnowSubsystem::SetPockets(ACharacter* Character, const FDirectionalSnow& Pockets)
{
	if (!Character)
	{
		return;
	}
	FEntry& Entry = Entries.FindOrAdd(TWeakObjectPtr<ACharacter>(Character));
	Entry.Character = Character;
	Entry.Pockets = Pockets.Clamped();
	Entry.SincePush = 1000.f;
}

void UCharacterSnowSubsystem::ClearAll()
{
	for (TPair<TWeakObjectPtr<ACharacter>, FEntry>& Pair : Entries)
	{
		Pair.Value.Pockets = FDirectionalSnow();
		Pair.Value.SincePush = 1000.f;
	}
}

AWeatherPresenterActor* UCharacterSnowSubsystem::FindPresenter()
{
	AWeatherPresenterActor* Found = Presenter.Get();
	UWorld* World = GetWorld();
	if (!Found && World && World->GetTimeSeconds() >= NextPresenterSearch)
	{
		Found = AWeatherPresenterActor::Find(World);
		Presenter = Found;
		NextPresenterSearch = World->GetTimeSeconds() + 1.0;
	}
	return Found;
}

void UCharacterSnowSubsystem::RefreshShelters()
{
	Shelters.Reset();
	SinceShelterRefresh = 0.f;
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	// Map-placed camera zones (no runtime spawns): gathered at begin play and every few seconds, never per character.
	for (TActorIterator<ACameraZoneVolume> It(World); It; ++It)
	{
		if (It->Environment == ECameraZoneEnvironment::Closed || It->Environment == ECameraZoneEnvironment::Shelter)
		{
			Shelters.Add(*It);
		}
	}
}

bool UCharacterSnowSubsystem::IsSheltered(const FVector& Location) const
{
	for (const TWeakObjectPtr<ACameraZoneVolume>& Zone : Shelters)
	{
		const ACameraZoneVolume* Volume = Zone.Get();
		if (Volume && Volume->ContainsLocation(Location))
		{
			return true;
		}
	}
	return false;
}

bool UCharacterSnowSubsystem::IsNearHeat(const FVector& Location) const
{
	const UWorld* World = GetWorld();
	for (const TWeakObjectPtr<UHeatSourceComponent>& Source : UHeatSourceComponent::GetAllSources())
	{
		if (Source.IsValid() && Source->GetWorld() == World && Source->IsLocationWarm(Location))
		{
			return true;
		}
	}
	return false;
}

void UCharacterSnowSubsystem::Push(FEntry& Entry, const FDirectionalSnow& Values)
{
	ACharacter* Character = Entry.Character.Get();
	USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
	if (!Mesh)
	{
		return;
	}
	const float Data[FDirectionalSnow::PocketCount] = { Values.Top, Values.PX, Values.NX, Values.PY, Values.NY };
	Mesh->SetCustomPrimitiveDataFloatArray(CharacterSnowRules::FirstCustomDataIndex, TConstArrayView<float>(Data, FDirectionalSnow::PocketCount));
	Entry.Pushed = Values;
	Entry.SincePush = 0.f;
	Entry.bEverPushed = true;
	++Metrics.Pushes;
	CODEX_ENV_TRACE(Verbose, this, TEXT("character snow push %s: top %.2f"), *GetNameSafe(Character), Values.Top);
}

void UCharacterSnowSubsystem::ZeroAll()
{
	for (TPair<TWeakObjectPtr<ACharacter>, FEntry>& Pair : Entries)
	{
		FEntry& Entry = Pair.Value;
		if (Entry.bEverPushed && !Entry.Pushed.IsZero())
		{
			Push(Entry, FDirectionalSnow());
		}
	}
	bZeroed = true;
}

void UCharacterSnowSubsystem::Tick(float DeltaTime)
{
	const double Start = FPlatformTime::Seconds();
	++Metrics.Ticks;
	ON_SCOPE_EXIT
	{
		Metrics.CpuSeconds += FPlatformTime::Seconds() - Start;
	};

	const AWeatherPresenterActor* Weather = FindPresenter();
	const bool bActive = IsEnabled() && Weather && Weather->IsApplying() && AWeatherPresenterActor::AreVisualsEnabled();
	if (!bActive)
	{
		if (!bZeroed)
		{
			ZeroAll();
		}
		SinceStep = 0.f;
		return;
	}
	SinceShelterRefresh += DeltaTime;
	if (SinceShelterRefresh >= CharacterSnowLocal::ShelterRefreshSeconds)
	{
		RefreshShelters();
	}
	SinceStep += DeltaTime;
	if (SinceStep < CharacterSnowRules::PushIntervalSeconds && !bStepRequested && !bZeroed)
	{
		return;
	}
	const float Delta = SinceStep;
	SinceStep = 0.f;
	bStepRequested = false;
	Step(Delta);
}

void UCharacterSnowSubsystem::Step(float DeltaSeconds)
{
	UWorld* World = GetWorld();
	const UEnvironmentSubsystem* Environment = UEnvironmentSubsystem::Get(World);
	if (!World || !Environment || !Environment->IsInitialised())
	{
		return;
	}
	++Metrics.Steps;
	bZeroed = false;
	const FEnvironmentConfig& Config = Environment->GetConfig();
	const FEnvironmentState& State = Environment->GetStateRef();

	// The environment's clock rules, but not its lock: the locked Demo 01 lake still snows on the squad.
	const UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
	const bool bAdvance = EnvironmentRules::ShouldAdvance(Flow ? Flow->GetCombatMode() : ECodexCombatMode::None, UWorldAIPauseSubsystem::IsPausedIn(World));
	const float SnowSeconds = bAdvance ? DeltaSeconds * Environment->GetClockScale() : 0.f;

	CharacterSnowRules::FCharacterSnowConditions Shared;
	Shared.SnowCoverRate = EnvironmentRules::GetBlendedSnowCoverRate(Config, State);
	Shared.WindStrength = Environment->GetWindStrength();
	Shared.ObjectMeltRate = EnvironmentRules::GetObjectSnowMeltRate(Config, State, Environment->GetSunElevation());
	const FVector WorldWind = Environment->GetWindDirection();
	// Section 17: 0 on a temperate level (its Snowfall / Blizzard rungs are rain / storm); the arctic sum as before.
	const float Snowing = EnvironmentRules::GetSnowingAmount(Config, State);

	// Candidates from the registries (no actor scan), nearest the camera first.
	FVector Eye = FVector::ZeroVector;
	if (const APlayerCameraManager* CameraManager = UGameplayStatics::GetPlayerCameraManager(World, 0))
	{
		Eye = CameraManager->GetCameraLocation();
	}
	struct FCandidate
	{
		ACharacter* Character;
		bool bEnemy;
		double DistanceSq;
	};
	TArray<FCandidate, TInlineAllocator<96>> Candidates;
	if (const USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>())
	{
		for (AOperativeCharacter* Operative : Squad->GetMembers())
		{
			if (IsValid(Operative))
			{
				Candidates.Add({ Operative, false, FVector::DistSquared(Eye, Operative->GetActorLocation()) });
			}
		}
	}
	for (AEnemyCharacter* Enemy : UEnemyRegistrySubsystem::GetEnemyCharacters(World))
	{
		if (IsValid(Enemy))
		{
			Candidates.Add({ Enemy, true, FVector::DistSquared(Eye, Enemy->GetActorLocation()) });
		}
	}
	const int32 Cap = FMath::Max(0, CharacterSnowLocal::CVarCharacterSnowMax.GetValueOnGameThread());
	if (Candidates.Num() > Cap)
	{
		Candidates.Sort([](const FCandidate& A, const FCandidate& B) { return A.DistanceSq < B.DistanceSq; });
		Candidates.SetNum(Cap);
	}

	for (const FCandidate& Candidate : Candidates)
	{
		ACharacter* Character = Candidate.Character;
		const USkeletalMeshComponent* Mesh = Character->GetMesh();
		if (!Mesh)
		{
			continue;
		}
		const FQuat BodyRotation = Mesh->GetComponentQuat();
		FEntry* Entry = Entries.Find(TWeakObjectPtr<ACharacter>(Character));
		if (!Entry)
		{
			Entry = &Entries.Add(TWeakObjectPtr<ACharacter>(Character));
			Entry->Character = Character;
			// Operatives start each level bare; an enemy that appears in a storm comes in already partly white.
			if (Candidate.bEnemy)
			{
				Entry->Pockets = CharacterSnowRules::MakeSpawnCover(Config, State.ObjectSnow, Snowing, BodyRotation);
			}
		}
		Entry->SincePush += DeltaSeconds;
		const FVector Location = Character->GetActorLocation();
		CharacterSnowRules::FCharacterSnowConditions Conditions = Shared;
		Conditions.LocalWind = CharacterSnowRules::ToLocalWind(WorldWind, BodyRotation);
		// Hidden: an operative inside the APC. (Enemies are hidden by the fog of war: they stay out in the snow.)
		Conditions.bSheltered = (!Candidate.bEnemy && Character->IsHidden()) || IsSheltered(Location) || IsNearHeat(Location);
		EnvironmentRules::StepDirectionalSnow(Entry->Pockets, CharacterSnowRules::MakeInputs(Config, Conditions), SnowSeconds);
		const bool bFirstSnow = !Entry->bEverPushed && !Entry->Pockets.IsZero();
		if (bFirstSnow || CharacterSnowRules::ShouldPush(Entry->Pushed, Entry->Pockets, Entry->SincePush))
		{
			Push(*Entry, Entry->Pockets);
		}
	}
	Metrics.Processed = Candidates.Num();

	// Forget the characters that left the world (destroyed bodies take their CPD with them).
	for (auto It = Entries.CreateIterator(); It; ++It)
	{
		if (!It->Value.Character.IsValid())
		{
			It.RemoveCurrent();
		}
	}
	Metrics.Tracked = Entries.Num();
}
