#include "Combat/HordeSubsystem.h"

#include "AI/WorldAIPauseSubsystem.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/HordeIntroSubsystem.h"
#include "Combat/TacticalSightSubsystem.h"
#include "Combat/WaveSubsystem.h"
#include "Core/CodexTacticsGameMode.h"
#include "Data/WaveConfigTypes.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Sound/SoundBase.h"
#include "UI/FloatingTextSubsystem.h"
#include "UI/GameMessageSubsystem.h"

namespace HordeTuning
{
	static TAutoConsoleVariable<float> CVarTriggerSeconds(TEXT("Codex.Horde.TriggerSeconds"), -1.f,
		TEXT("Horde: real-time fight seconds before the first horde (-1: horde.json / level JSON; smokes shorten it)"));
	static TAutoConsoleVariable<int32> CVarEnabled(TEXT("Codex.Horde.Enabled"), -1,
		TEXT("Horde: 1 on, 0 off, -1 data (horde.json enabled && level horde_enabled)"));
	static TAutoConsoleVariable<float> CVarRepeatSeconds(TEXT("Codex.Horde.RepeatSeconds"), -1.f,
		TEXT("Horde: > 0 turns repeating hordes on with this period (-1: data)"));
	static TAutoConsoleVariable<int32> CVarAfterFirefights(TEXT("Codex.Horde.AfterFirefights"), -1,
		TEXT("Horde #12: >= 1 = the horde after this many firefights in a row (-1: the level JSON horde_after_firefights, default 6)"));
	static TAutoConsoleVariable<float> CVarFirefightWarningSeconds(TEXT("Codex.Horde.FirefightWarningSeconds"), -1.f,
		TEXT("Horde #12: >= 0 = seconds from the HQ warning to the firefight horde (-1: horde.json firefight_warning_seconds)"));

	/** Seconds between attempts when no spawn point was found. */
	constexpr float RetrySeconds = 2.f;
}

void UHordeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency(UGameFlowSubsystem::StaticClass());
	Collection.InitializeDependency(UWaveSubsystem::StaticClass());
}

void UHordeSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (UGameFlowSubsystem* Flow = InWorld.GetSubsystem<UGameFlowSubsystem>())
	{
		Flow->OnGameFlowChanged.AddDynamic(this, &UHordeSubsystem::HandleGameFlowChanged);
		LastPhase = Flow->GetPhase();
	}
	RefreshConfig();
}

bool UHordeSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE || WorldType == EWorldType::GamePreview;
}

TStatId UHordeSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UHordeSubsystem, STATGROUP_Tickables);
}

void UHordeSubsystem::RefreshConfig()
{
	const FHordeConfig Defaults = HordeRules::LoadDefaults();
	const ACodexTacticsGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<ACodexTacticsGameMode>() : nullptr;
	const ULevelConfigAsset* Level = GameMode ? GameMode->GetActiveLevelConfig() : nullptr;
	Config = Level ? HordeRules::ResolveForLevel(Defaults, Level->Config.bHordeEnabled, Level->Config.HordeOverrideJson, Level->Config.HordeAfterFirefights)
		: Defaults;
	Config.AfterFirefights = FirefightRules::ResolveThreshold(Config.AfterFirefights, HordeTuning::CVarAfterFirefights.GetValueOnGameThread());
	if (const float Lead = HordeTuning::CVarFirefightWarningSeconds.GetValueOnGameThread(); Lead >= 0.f)
	{
		Config.Firefight.WarningLeadSeconds = Lead;
	}
	if (const float Trigger = HordeTuning::CVarTriggerSeconds.GetValueOnGameThread(); Trigger > 0.f)
	{
		Config.TriggerSeconds = Trigger;
	}
	if (const float Repeat = HordeTuning::CVarRepeatSeconds.GetValueOnGameThread(); Repeat > 0.f)
	{
		Config.bRepeats = true;
		Config.RepeatSeconds = Repeat;
	}
	if (!DevOverrideJson.IsEmpty())
	{
		HordeRules::ApplyJsonText(DevOverrideJson, Config);
	}
	if (const int32 Enabled = HordeTuning::CVarEnabled.GetValueOnGameThread(); Enabled >= 0)
	{
		Config.bEnabled = Enabled != 0;
	}
}

void UHordeSubsystem::HandleGameFlowChanged(ECodexGamePhase Phase, ECodexCombatMode CombatMode)
{
	// A new fight (a wave, an ambush fight) starts its own clock; the pause / turn-based switches stay in the fight.
	if (Phase == ECodexGamePhase::WaveCombat && LastPhase != ECodexGamePhase::WaveCombat)
	{
		RefreshConfig();
		Timer.Reset();
		bFightTracked = true;
		LastHorde.Reset();
		UE_LOG(LogCodexTactics, Display, TEXT("[Horde] fight clock started (%s, first horde at %.0f s of real-time fight%s)"),
			Config.bEnabled ? TEXT("on") : TEXT("off"), Config.TriggerSeconds,
			Config.bRepeats ? *FString::Printf(TEXT(", then every %.0f s"), Config.RepeatSeconds) : TEXT(""));
	}
	else if (Phase != ECodexGamePhase::WaveCombat)
	{
		bFightTracked = false;
	}
	LastPhase = Phase;
}

void UHordeSubsystem::Tick(float DeltaTime)
{
	bCountingNow = false;
	UWorld* World = GetWorld();
	const UGameFlowSubsystem* Flow = World ? World->GetSubsystem<UGameFlowSubsystem>() : nullptr;
	TickFirefightHorde(DeltaTime);
	if (!bFightTracked || !Flow)
	{
		return;
	}
	bCountingNow = Config.bEnabled && HordeRules::IsCountingTime(Flow->GetPhase(), Flow->GetCombatMode(), UWorldAIPauseSubsystem::IsPausedIn(World));
	if (RetryCooldown > 0.f)
	{
		RetryCooldown -= DeltaTime;
		Timer.CombatSeconds += bCountingNow ? DeltaTime : 0.f;
		return;
	}
	if (!Timer.Advance(DeltaTime, bCountingNow, Config))
	{
		return;
	}
	if (SpawnHorde(Timer.HordesReleased - 1) == 0)
	{
		--Timer.HordesReleased; // no spawn point now: try again shortly
		RetryCooldown = HordeTuning::RetrySeconds;
	}
}

bool UHordeSubsystem::RequestFirefightHorde()
{
	UWorld* World = GetWorld();
	if (!World || !Config.bEnabled || IsFirefightHordePending())
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Horde] firefight horde not announced (%s)"),
			!Config.bEnabled ? TEXT("the horde is off on this level") : TEXT("one is already on its way"));
		return false;
	}
	FirefightHordeCountdown = FMath::Max(Config.Firefight.WarningLeadSeconds, 0.f);
	FirefightRetryCooldown = 0.f;
	++FirefightHordesAnnounced;
	UE_LOG(LogCodexTactics, Display, TEXT("[Horde] %d firefights in a row: the horde comes in %.0f s (HQ warning now)"), Config.AfterFirefights,
		FirefightHordeCountdown);
	if (UGameMessageSubsystem* Messages = World->GetSubsystem<UGameMessageSubsystem>())
	{
		Messages->PostMessage(FText::FromString(TEXT("HQ")), FText::FromString(FString::Printf(
			TEXT("⚠️ All that shooting has drawn them in - a HORDE is closing on your position! About %.0f seconds - take up defensive positions!"),
			FMath::Max(FirefightHordeCountdown, 1.f))));
	}
	return true;
}

void UHordeSubsystem::TickFirefightHorde(float DeltaTime)
{
	UWorld* World = GetWorld();
	UGameFlowSubsystem* Flow = World ? World->GetSubsystem<UGameFlowSubsystem>() : nullptr;
	if (!IsFirefightHordePending() || !Flow)
	{
		return;
	}
	if (!Config.bEnabled)
	{
		FirefightHordeCountdown = -1.f; // switched off meanwhile (console)
		return;
	}
	const ECodexGamePhase Phase = Flow->GetPhase();
	const bool bRunning = (Phase == ECodexGamePhase::Exploration || (Phase == ECodexGamePhase::WaveCombat && Flow->GetCombatMode() == ECodexCombatMode::RealTime))
		&& !UWorldAIPauseSubsystem::IsPausedIn(World);
	if (!bRunning)
	{
		return; // the tactical pause, turn-based combat, a dialogue, a cutscene / preparation / post-combat: the horde waits
	}
	if (FirefightRetryCooldown > 0.f)
	{
		FirefightRetryCooldown -= DeltaTime;
		return;
	}
	FirefightHordeCountdown = FMath::Max(0.f, FirefightHordeCountdown - DeltaTime);
	if (FirefightHordeCountdown > 0.f)
	{
		return;
	}
	if (ReleaseFirefightHorde())
	{
		FirefightHordeCountdown = -1.f;
		++FirefightHordesReleased;
	}
	else
	{
		FirefightRetryCooldown = HordeTuning::RetrySeconds;
	}
}

bool UHordeSubsystem::ReleaseFirefightHorde()
{
	UWorld* World = GetWorld();
	UGameFlowSubsystem* Flow = World ? World->GetSubsystem<UGameFlowSubsystem>() : nullptr;
	FVector Centre;
	FVector Point;
	bool bHidden = false;
	if (!Flow || !GetSquadCentre(Centre) || !FindSpawnPoint(Centre, Point, bHidden))
	{
		return false;
	}
	if (Flow->GetPhase() == ECodexGamePhase::Exploration)
	{
		// The firefight is over already: the horde starts a fight of its own (a skirmish level ends it when the horde is dead).
		const EGameFlowResult Started = Flow->StartAmbushCombat();
		if (Started != EGameFlowResult::Ok)
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("[Horde] firefight horde: the fight cannot start from exploration (%d), retrying"), static_cast<int32>(Started));
			return false;
		}
	}
	const int32 Spawned = SpawnHorde(0);
	if (Spawned == 0)
	{
		return false;
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[Horde] firefight horde out: %d enemies"), Spawned);
	if (UHordeIntroSubsystem* Intro = World->GetSubsystem<UHordeIntroSubsystem>())
	{
		TArray<AEnemyCharacter*> Members;
		for (const TWeakObjectPtr<AEnemyCharacter>& Member : LastHorde)
		{
			if (AEnemyCharacter* Enemy = Member.Get())
			{
				Members.Add(Enemy);
			}
		}
		Intro->BeginExternalHorde(Members);
	}
	return true;
}

int32 UHordeSubsystem::ReleaseHorde()
{
	const int32 Spawned = SpawnHorde(Timer.HordesReleased);
	Timer.HordesReleased += Spawned > 0 ? 1 : 0;
	return Spawned;
}

bool UHordeSubsystem::GetActiveWarning(FVector& OutLocation, int32& OutCount, float& OutSecondsLeft) const
{
	const UWorld* World = GetWorld();
	if (!World || WarningUntil < 0.0)
	{
		return false;
	}
	OutSecondsLeft = static_cast<float>(WarningUntil - World->GetRealTimeSeconds());
	if (OutSecondsLeft <= 0.f)
	{
		return false;
	}
	OutLocation = LastSpawnPoint;
	OutCount = WarningCount;
	return true;
}

bool UHordeSubsystem::GetSquadCentre(FVector& OutCentre) const
{
	const USquadSubsystem* Squad = GetWorld() ? GetWorld()->GetSubsystem<USquadSubsystem>() : nullptr;
	FVector Sum = FVector::ZeroVector;
	int32 Count = 0;
	for (const AOperativeCharacter* Member : Squad ? Squad->GetMembers() : TArray<AOperativeCharacter*>())
	{
		if (Member && Member->HealthComponent && Member->HealthComponent->IsAlive())
		{
			Sum += Member->GetActorLocation();
			++Count;
		}
	}
	if (Count == 0)
	{
		return false;
	}
	OutCentre = Sum / Count;
	return true;
}

bool UHordeSubsystem::IsVisibleToSquad(const FVector& Point) const
{
	UWorld* World = GetWorld();
	const USquadSubsystem* Squad = World ? World->GetSubsystem<USquadSubsystem>() : nullptr;
	if (!Squad)
	{
		return false;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(HordeSight), false);
	for (TActorIterator<APawn> It(World); It; ++It)
	{
		Params.AddIgnoredActor(*It); // bodies never hide the spot
	}
	for (const AOperativeCharacter* Member : Squad->GetMembers())
	{
		if (!Member || !Member->HealthComponent || !Member->HealthComponent->IsAlive())
		{
			continue;
		}
		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit, UTacticalSightSubsystem::EyePoint(*Member), Point, ECC_Visibility, Params))
		{
			return true;
		}
	}
	return false;
}

bool UHordeSubsystem::FindSpawnPoint(const FVector& Centre, FVector& OutPoint, bool& bOutHidden) const
{
	UWorld* World = GetWorld();
	UNavigationSystemV1* Nav = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
	FNavLocation CentreOnNav;
	const bool bCentreOnNav = Nav && Nav->ProjectPointToNavigation(Centre, CentreOnNav, FVector(300.f, 300.f, 500.f));
	TArray<FHordeSpawnCandidate> Candidates;
	for (int32 Sample = 0; Sample < Config.SpawnSamples; ++Sample)
	{
		const FVector Direction = FVector(1.f, 0.f, 0.f).RotateAngleAxis(FMath::FRandRange(0.f, 360.f), FVector::UpVector);
		const FVector Probe = Centre + Direction * FMath::FRandRange(Config.MinDistanceCm, Config.MaxDistanceCm);
		FHordeSpawnCandidate Candidate;
		if (Nav && bCentreOnNav)
		{
			FNavLocation OnNav;
			if (!Nav->ProjectPointToNavigation(Probe, OnNav, FVector(300.f, 300.f, 1000.f)))
			{
				continue;
			}
			Candidate.Location = OnNav.Location;
			const UNavigationPath* Path = Nav->FindPathToLocationSynchronously(World, OnNav.Location, CentreOnNav.Location);
			Candidate.bReachable = Path && Path->IsValid() && !Path->IsPartial();
		}
		else
		{
			// No navmesh (a bare test world): the probe on the squad's height, reachable by assumption.
			Candidate.Location = FVector(Probe.X, Probe.Y, Centre.Z);
			Candidate.bReachable = true;
		}
		Candidate.DistanceCm = FVector::Dist2D(Candidate.Location, Centre);
		Candidate.bVisibleToSquad = IsVisibleToSquad(Candidate.Location + FVector(0.f, 0.f, 100.f));
		Candidates.Add(Candidate);
	}
	const int32 Picked = HordeRules::PickSpawnPoint(Candidates, Config);
	if (Picked == INDEX_NONE)
	{
		UE_LOG(LogCodexTactics, Warning, TEXT("[Horde] no reachable spawn point %.0f-%.0f m from the squad (%d candidates on the navmesh)"),
			Config.MinDistanceCm / 100.f, Config.MaxDistanceCm / 100.f, Candidates.Num());
		return false;
	}
	OutPoint = Candidates[Picked].Location;
	bOutHidden = !Candidates[Picked].bVisibleToSquad;
	return true;
}

int32 UHordeSubsystem::SpawnHorde(int32 HordeIndex)
{
	UWorld* World = GetWorld();
	UWaveSubsystem* Waves = World ? World->GetSubsystem<UWaveSubsystem>() : nullptr;
	FVector Centre;
	if (!Waves || !GetSquadCentre(Centre))
	{
		return 0;
	}
	FVector Point;
	bool bHidden = false;
	if (!FindSpawnPoint(Centre, Point, bHidden))
	{
		return 0;
	}
	// The level's difficulty: the current wave's modifiers (hp / damage / speed) apply to the horde too.
	const FWaveModifiers* Modifiers = nullptr;
	if (Config.bApplyWaveModifiers)
	{
		const ACodexTacticsGameMode* GameMode = World->GetAuthGameMode<ACodexTacticsGameMode>();
		const ULevelConfigAsset* Level = GameMode ? GameMode->GetActiveLevelConfig() : nullptr;
		const UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		const int32 Wave = Flow ? Flow->GetWaveIndex() : 0;
		if (Level && Level->Config.Waves.IsValidIndex(Wave - 1))
		{
			Modifiers = &Level->Config.Waves[Wave - 1].Modifiers;
		}
	}
	const TArray<FPressureEnemyType> Types = HordeRules::BuildMembers(Config, HordeRules::GetHordeSize(Config, HordeIndex));
	const FRotator Facing(0.f, (Centre - Point).Rotation().Yaw, 0.f);
	LastHorde.Reset();
	for (int32 Index = 0; Index < Types.Num(); ++Index)
	{
		const FVector Spot = Waves->FindFreeSpawnSpot(Point + HordeRules::ClusterOffset(Index, Types.Num(), Config.ClusterRadiusCm));
		AEnemyCharacter* Enemy = Waves->SpawnEnemyVariant(Types[Index].Type, Spot, Facing, Types[Index].Variant); // MILITARY_ZOMBIE: its BP
		if (!Enemy)
		{
			continue;
		}
		Enemy->bKnowsSquadPosition = true;
		if (Modifiers)
		{
			Enemy->ApplyWaveModifiers(Modifiers->EnemyHpMult, Modifiers->EnemyDamageMult, Modifiers->EnemySpeedMult, 0.f);
		}
		LastHorde.Add(Enemy);
	}
	if (LastHorde.IsEmpty())
	{
		return 0;
	}
	LastSpawnPoint = Point;
	LastSquadCentre = Centre;
	bLastSpawnHidden = bHidden;
	WarningCount = LastHorde.Num();
	WarningUntil = World->GetRealTimeSeconds() + Config.WarningSeconds;
	const float Distance = FVector::Dist2D(Point, Centre) / 100.f;
	UE_LOG(LogCodexTactics, Display, TEXT("[Horde] horde %d: %d enemies at %.0f m from the squad (%s) after %.0f s of real-time fight"),
		HordeIndex + 1, LastHorde.Num(), Distance, bHidden ? TEXT("out of sight") : TEXT("in sight"), Timer.CombatSeconds);
	if (UGameMessageSubsystem* Messages = World->GetSubsystem<UGameMessageSubsystem>())
	{
		Messages->PostMessage(FText::FromString(TEXT("HQ")), FText::FromString(FString::Printf(
			TEXT("⚠️ HORDE! Creatures heading straight for the squad - %.0f m! Take up defensive positions!"), Distance))); // no count (bug 08)
	}
	if (AEnemyCharacter* First = LastHorde[0].Get())
	{
		UFloatingTextSubsystem::SpawnAboveEnemy(First, TEXT("⚠️ HORDE!"), FLinearColor(1.f, 0.2f, 0.15f));
	}
	if (!Config.WarningSound.IsEmpty())
	{
		if (USoundBase* Sound = Cast<USoundBase>(FSoftObjectPath(Config.WarningSound).TryLoad()))
		{
			UGameplayStatics::PlaySound2D(World, Sound);
		}
	}
	OnHordeReleased.Broadcast(HordeIndex, LastHorde.Num(), Point);
	return LastHorde.Num();
}

namespace HordeCommands
{
	static FAutoConsoleCommandWithWorld Release(
		TEXT("CodexTactics.Horde.Release"),
		TEXT("Dev: releases a horde now (spawn point 25-45 m from the squad, out of its sight when possible)."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			if (UHordeSubsystem* Horde = World ? World->GetSubsystem<UHordeSubsystem>() : nullptr)
			{
				UE_LOG(LogCodexTactics, Display, TEXT("[Horde] released by command: %d enemies"), Horde->ReleaseHorde());
			}
		}));
}
