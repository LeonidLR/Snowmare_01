#include "Combat/HordeSubsystem.h"

#include "AI/WorldAIPauseSubsystem.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/CombatPressureRules.h"
#include "Combat/HealthComponent.h"
#include "Combat/HordeIntroSubsystem.h"
#include "Combat/TacticalSightSubsystem.h"
#include "Combat/WaveSubsystem.h"
#include "Core/CodexTacticsGameMode.h"
#include "Data/WaveConfigTypes.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "GameFlow/LevelEncounterSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Sound/SoundBase.h"
#include "UI/FloatingTextSubsystem.h"
#include "UI/GameMessageSubsystem.h"

namespace HordeTuning
{
	// 2026-11-09 (game designer): Codex.Horde.TriggerSeconds / Codex.Horde.RepeatSeconds are gone with the real-time-fight clock.
	static TAutoConsoleVariable<int32> CVarEnabled(TEXT("Codex.Horde.Enabled"), -1,
		TEXT("Horde: 1 on, 0 off, -1 data (horde.json enabled && level horde_enabled)"));
	static TAutoConsoleVariable<int32> CVarAfterFirefights(TEXT("Codex.Horde.AfterFirefights"), -1,
		TEXT("Horde #12: >= 1 = the horde after this many firefights in a row (-1: the level JSON horde_after_firefights, default 10; the HQ hint one firefight before)"));
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
	// A new fight re-reads the config (the console / a dev override may have changed it). No clock any more (2026-11-09): only the firefight
	// streak summons the horde (RequestFirefightHorde).
	if (Phase == ECodexGamePhase::WaveCombat && LastPhase != ECodexGamePhase::WaveCombat)
	{
		RefreshConfig();
	}
	if (Phase != ECodexGamePhase::WaveCombat)
	{
		DropQueue(TEXT("the fight is over"));
	}
	LastPhase = Phase;
}

void UHordeSubsystem::Tick(float DeltaTime)
{
	TickFirefightHorde(DeltaTime);
	TickActiveHorde(DeltaTime);
}

FHordePlan UHordeSubsystem::PlanNextHorde() const
{
	const UWorld* World = GetWorld();
	const ACodexTacticsGameMode* GameMode = World ? World->GetAuthGameMode<ACodexTacticsGameMode>() : nullptr;
	const ULevelConfigAsset* Level = GameMode ? GameMode->GetActiveLevelConfig() : nullptr;
	const TArray<FWaveDefinition> NoWaves;
	const TArray<FWaveDefinition>& LevelWaves = Level ? Level->Config.Waves : NoWaves;
	const ULevelEncounterSubsystem* Encounter = World ? World->GetSubsystem<ULevelEncounterSubsystem>() : nullptr;
	const UWaveSubsystem* Waves = World ? World->GetSubsystem<UWaveSubsystem>() : nullptr;
	if (HordeRules::ShouldWavesFeedHorde(Config, LevelWaves.Num(), Encounter && Encounter->IsAmbushCombatStart(), Waves && Waves->HasPlayedLevelWaves()))
	{
		const int32 Wave = HordeRules::WaveForHorde(FirefightHordesReleased + 1, LevelWaves.Num());
		return HordeRules::PlanFromWave(LevelWaves[Wave], Wave + 1);
	}
	FHordePlan Plan = HordeRules::PlanFromConfig(Config);
	// The level's difficulty: the current wave's modifiers (hp / damage / speed) apply to the horde too.
	const UGameFlowSubsystem* Flow = World ? World->GetSubsystem<UGameFlowSubsystem>() : nullptr;
	const int32 Wave = Flow ? Flow->GetWaveIndex() : 0;
	if (Config.bApplyWaveModifiers && LevelWaves.IsValidIndex(Wave - 1))
	{
		Plan.bHasModifiers = true;
		Plan.Modifiers = LevelWaves[Wave - 1].Modifiers;
	}
	return Plan;
}

bool UHordeSubsystem::RequestFirefightHorde()
{
	UWorld* World = GetWorld();
	RefreshConfig(); // the console / a dev override set since the fight began (the arena switches the horde off)
	if (!World || !Config.bEnabled || IsFirefightHordePending())
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Horde] firefight horde not announced (%s)"),
			!Config.bEnabled ? TEXT("the horde is off on this level") : TEXT("one is already on its way"));
		return false;
	}
	FirefightHordeCountdown = FMath::Max(Config.Firefight.WarningLeadSeconds, 0.f);
	FirefightRetryCooldown = 0.f;
	++FirefightHordesAnnounced;
	const FHordePlan Plan = PlanNextHorde();
	UE_LOG(LogCodexTactics, Display, TEXT("[Horde] %d firefights in a row: the horde comes in %.0f s (HQ warning now) - %s"), Config.AfterFirefights,
		FirefightHordeCountdown, *Plan.Describe());
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
	bCountdownRunning = false;
	if (!IsFirefightHordePending() || !Flow)
	{
		return;
	}
	if (!Config.bEnabled)
	{
		FirefightHordeCountdown = -1.f; // switched off meanwhile (console)
		return;
	}
	bCountdownRunning = HordeRules::IsCountdownRunning(Flow->GetPhase(), Flow->GetCombatMode(), UWorldAIPauseSubsystem::IsPausedIn(World));
	if (!bCountdownRunning)
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
	FHordeFarPick Pick;
	float Bearing = 0.f;
	if (!Flow || !GetSquadCentre(Centre) || !FindFarSpawnPoint(Centre, true, Point, bHidden, Pick, Bearing))
	{
		return false;
	}
	const FHordePlan Plan = PlanNextHorde();
	if (Plan.Members.IsEmpty())
	{
		UE_LOG(LogCodexTactics, Warning, TEXT("[Horde] firefight horde: its wave %d has nobody the horde may spawn - none comes"), Plan.SourceWave);
		return true; // nothing to wait for: the announced horde is spent
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
	LastSpawnPick = Pick.Kind;
	LastSpawnBearingDeg = Bearing;
	bLastSpawnBearingRelaxed = Pick.bBearingRelaxed;
	const int32 Spawned = SpawnHorde(Plan, Point, Centre, bHidden);
	if (Spawned == 0)
	{
		return false;
	}
	bHasLastFirefightBearing = true;
	LastFirefightBearingDeg = Bearing;
	++HordesReleased;
	UE_LOG(LogCodexTactics, Display, TEXT("[Horde] firefight horde out: %d enemies now, %d to follow"), Spawned, GetActiveQueued());
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
	FVector Centre;
	FVector Point;
	bool bHidden = false;
	if (!GetSquadCentre(Centre) || !FindSpawnPoint(Centre, Point, bHidden))
	{
		return 0;
	}
	LastSpawnPick = EHordeFarPick::None;
	LastSpawnBearingDeg = HordeRules::BearingDeg(Centre, Point);
	const int32 Spawned = SpawnHorde(PlanNextHorde(), Point, Centre, bHidden);
	HordesReleased += Spawned > 0 ? 1 : 0;
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

bool UHordeSubsystem::IsVisibleToSquad(const FVector& Point, bool bWithinSightRange) const
{
	UWorld* World = GetWorld();
	const USquadSubsystem* Squad = World ? World->GetSubsystem<USquadSubsystem>() : nullptr;
	if (!Squad)
	{
		return false;
	}
	const UTacticalSightSubsystem* Sight = bWithinSightRange ? World->GetSubsystem<UTacticalSightSubsystem>() : nullptr;
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
		const FVector Eye = UTacticalSightSubsystem::EyePoint(*Member);
		if (Sight && FVector::Dist(Eye, Point) > Sight->GetSightRangeCm(*Member))
		{
			continue; // beyond his eyes' reach (the squad's own sight range: weather / time of day / height)
		}
		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit, Eye, Point, ECC_Visibility, Params))
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

bool UHordeSubsystem::FindFarSpawnPoint(const FVector& Centre, bool bUsePreviousBearing, FVector& OutPoint, bool& bOutHidden, FHordeFarPick& OutPick,
	float& OutBearing)
{
	const double Started = FPlatformTime::Seconds();
	UWorld* World = GetWorld();
	UNavigationSystemV1* Nav = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
	FNavLocation CentreOnNav;
	const bool bCentreOnNav = Nav && Nav->ProjectPointToNavigation(Centre, CentreOnNav, FVector(300.f, 300.f, 500.f));
	const TArray<float> Radii = HordeRules::FarProbeRadii(Config);
	const int32 Bearings = FMath::Max(4, Config.FirefightSpawnBearings);
	const float Offset = SpawnRandom.FRandRange(0.f, 360.f / Bearings);
	constexpr int32 MaxPathTriesPerBearing = 6; // unreachable / seen probes along one bearing before it gives up (a seen one is kept as a last resort)
	TArray<FHordeFarCandidate> Candidates;
	int32 PathQueries = 0;
	for (int32 Index = 0; Index < Bearings; ++Index)
	{
		const float Yaw = Offset + Index * 360.f / Bearings;
		const FVector Direction = FVector(1.f, 0.f, 0.f).RotateAngleAxis(Yaw, FVector::UpVector);
		int32 PathTries = 0;
		bool bVisibleKept = false;
		for (const float Radius : Radii)
		{
			const FVector Probe = Centre + Direction * Radius;
			FHordeFarCandidate Candidate;
			if (Nav && bCentreOnNav)
			{
				FNavLocation OnNav;
				if (!Nav->ProjectPointToNavigation(Probe, OnNav, FVector(400.f, 400.f, 1000.f)))
				{
					continue; // off the map / off the navmesh that far: nearer along this bearing
				}
				Candidate.Location = OnNav.Location;
				const UNavigationPath* Path = Nav->FindPathToLocationSynchronously(World, OnNav.Location, CentreOnNav.Location);
				++PathQueries;
				Candidate.bReachable = Path && Path->IsValid() && !Path->IsPartial();
			}
			else
			{
				Candidate.Location = FVector(Probe.X, Probe.Y, Centre.Z); // no navmesh (a bare test world): reachable by assumption
				Candidate.bReachable = true;
			}
			if (!Candidate.bReachable)
			{
				if (++PathTries >= MaxPathTriesPerBearing)
				{
					break;
				}
				continue;
			}
			Candidate.DistanceCm = FVector::Dist2D(Candidate.Location, Centre);
			Candidate.BearingDeg = HordeRules::BearingDeg(Centre, Candidate.Location);
			Candidate.bVisibleToSquad = IsVisibleToSquad(Candidate.Location + FVector(0.f, 0.f, 100.f), true);
			if (!Candidate.bVisibleToSquad)
			{
				Candidates.Add(Candidate); // this bearing's farthest reachable out-of-sight point
				break;
			}
			if (!bVisibleKept)
			{
				Candidates.Add(Candidate); // kept as a last resort; look nearer along the bearing for a hidden one
				bVisibleKept = true;
			}
			if (++PathTries >= MaxPathTriesPerBearing)
			{
				break;
			}
		}
	}
	OutPick = HordeRules::PickFarSpawnPoint(Candidates, Config, bUsePreviousBearing && bHasLastFirefightBearing, LastFirefightBearingDeg, SpawnRandom);
	LastSpawnSearchMs = (FPlatformTime::Seconds() - Started) * 1000.0;
	const float BandMin = HordeRules::FarBandMinCm(Config) / 100.f;
	const float BandMax = HordeRules::FarBandMaxCm(Config) / 100.f;
	if (OutPick.Index == INDEX_NONE)
	{
		UE_LOG(LogCodexTactics, Warning, TEXT("[Horde] no reachable spawn point for the firefight horde (%d bearings, %d path queries, %.1f ms)"),
			Bearings, PathQueries, LastSpawnSearchMs);
		return false;
	}
	const FHordeFarCandidate& Chosen = Candidates[OutPick.Index];
	float Farthest = 0.f;
	for (const FHordeFarCandidate& Each : Candidates)
	{
		Farthest = FMath::Max(Farthest, Each.DistanceCm);
	}
	OutPoint = Chosen.Location;
	bOutHidden = !Chosen.bVisibleToSquad;
	OutBearing = Chosen.BearingDeg;
	const FString Band = OutPick.Kind == EHordeFarPick::InBand ? FString::Printf(TEXT("in the %.0f-%.0f m band"), BandMin, BandMax)
		: FString::Printf(TEXT("no point %.0f-%.0f m on this map: the farthest"), BandMin, BandMax);
	const FString BearingNote = OutPick.bBearingRelaxed ? FString(TEXT(", near the previous bearing - nowhere else"))
		: bUsePreviousBearing && bHasLastFirefightBearing ? FString::Printf(TEXT(", previous bearing %.0f deg"), LastFirefightBearingDeg) : FString();
	UE_LOG(LogCodexTactics, Display, TEXT("[Horde] firefight spawn: %.0f m at bearing %.0f deg (%s%s%s; %d candidates, farthest %.0f m, %d path queries, %.1f ms)"),
		Chosen.DistanceCm / 100.f, Chosen.BearingDeg, *Band, OutPick.bVisible ? TEXT(", IN SIGHT - no hidden point") : TEXT(", out of sight"), *BearingNote,
		Candidates.Num(), Farthest / 100.f, PathQueries, LastSpawnSearchMs);
	return true;
}

AEnemyCharacter* UHordeSubsystem::SpawnNextMember(const FVector& Point, const FRotator& Facing)
{
	UWorld* World = GetWorld();
	UWaveSubsystem* Waves = World ? World->GetSubsystem<UWaveSubsystem>() : nullptr;
	if (!Waves || !ActivePlan.Members.IsValidIndex(ActiveSpawned))
	{
		return nullptr;
	}
	const int32 Index = ActiveSpawned++;
	const FHordeMember& Member = ActivePlan.Members[Index];
	const int32 ClusterCount = FMath::Max(1, ActivePlan.MaxAlive > 0 ? FMath::Min(ActivePlan.MaxAlive, ActivePlan.Members.Num()) : ActivePlan.Members.Num());
	const FVector Spot = Waves->FindFreeSpawnSpot(Point + HordeRules::ClusterOffset(Index % ClusterCount, ClusterCount, Config.ClusterRadiusCm));
	AEnemyCharacter* Enemy = Waves->SpawnEnemyVariant(Member.Enemy.Type, Spot, Facing, Member.Enemy.Variant); // MILITARY_ZOMBIE: its BP
	if (!Enemy)
	{
		return nullptr;
	}
	Enemy->bKnowsSquadPosition = true;
	if (ActivePlan.Entries.IsValidIndex(Member.EntryIndex))
	{
		Enemy->ApplySpawnEntry(ActivePlan.Modifiers, ActivePlan.Entries[Member.EntryIndex]); // the wave's modifiers + the entry's custom_stats
	}
	else if (ActivePlan.bHasModifiers)
	{
		Enemy->ApplyWaveModifiers(ActivePlan.Modifiers.EnemyHpMult, ActivePlan.Modifiers.EnemyDamageMult, ActivePlan.Modifiers.EnemySpeedMult, 0.f);
	}
	LastHorde.Add(Enemy);
	FVector Centre;
	const float Distance = GetSquadCentre(Centre) ? FVector::Dist2D(Spot, Centre) : 0.f;
	PendingArrivals.Add(Enemy, FVector2D(World->GetTimeSeconds(), Distance));
	return Enemy;
}

int32 UHordeSubsystem::SpawnHorde(const FHordePlan& Plan, const FVector& Point, const FVector& Centre, bool bHidden)
{
	UWorld* World = GetWorld();
	UWaveSubsystem* Waves = World ? World->GetSubsystem<UWaveSubsystem>() : nullptr;
	if (!Waves || Plan.Members.IsEmpty())
	{
		return 0;
	}
	DropQueue(TEXT("a new horde"));
	ActivePlan = Plan;
	ActiveSpawned = 0;
	ActiveHordeNumber = HordesReleased + 1;
	ActiveSpawnPoint = Point;
	ReinforceCooldown = 0.f;
	LastHorde.Reset();
	PendingArrivals.Reset();
	const FRotator Facing(0.f, (Centre - Point).Rotation().Yaw, 0.f);
	const int32 First = HordeRules::MembersToSpawnNow(ActivePlan, 0, 0);
	for (int32 Count = 0; Count < First; ++Count)
	{
		SpawnNextMember(Point, Facing);
	}
	if (LastHorde.IsEmpty())
	{
		ActivePlan = FHordePlan();
		ActiveSpawned = 0;
		return 0;
	}
	Waves->SetExternalPendingSpawns(GetActiveQueued());
	LastSpawnPoint = Point;
	LastSquadCentre = Centre;
	bLastSpawnHidden = bHidden;
	WarningCount = LastHorde.Num();
	WarningUntil = World->GetRealTimeSeconds() + Config.WarningSeconds;
	const float Distance = FVector::Dist2D(Point, Centre) / 100.f;
	const FString Source = ActivePlan.SourceWave > 0 ? FString::Printf(TEXT("wave %d"), ActivePlan.SourceWave) : FString(TEXT("the horde block"));
	const FString Swaps = ActivePlan.SpitterSwaps > 0
		? FString::Printf(TEXT(" (%d SPITTER -> MILITARY_ZOMBIE: no Spitter in the horde)"), ActivePlan.SpitterSwaps) : FString();
	const FString Cap = ActivePlan.MaxAlive > 0 ? FString::FromInt(ActivePlan.MaxAlive) : FString(TEXT("all"));
	UE_LOG(LogCodexTactics, Display, TEXT("[Horde] horde %d on this level: %s from %s%s; %d now at %.0f m from the squad (%s), at most %s alive, hp x%.2f dmg x%.2f speed x%.2f"),
		ActiveHordeNumber, *ActivePlan.Describe(), *Source, *Swaps, LastHorde.Num(), Distance, bHidden ? TEXT("out of sight") : TEXT("in sight"), *Cap,
		ActivePlan.bHasModifiers ? ActivePlan.Modifiers.EnemyHpMult : 1.f, ActivePlan.bHasModifiers ? ActivePlan.Modifiers.EnemyDamageMult : 1.f,
		ActivePlan.bHasModifiers ? ActivePlan.Modifiers.EnemySpeedMult : 1.f);
	if (UGameMessageSubsystem* Messages = World->GetSubsystem<UGameMessageSubsystem>())
	{
		Messages->PostMessage(FText::FromString(TEXT("HQ")), FText::FromString(FString::Printf(
			TEXT("⚠️ HORDE! Creatures heading straight for the squad - %.0f m! Take up defensive positions!"), Distance))); // no count (bug 08)
	}
	if (AEnemyCharacter* FirstMember = LastHorde[0].Get())
	{
		UFloatingTextSubsystem::SpawnAboveEnemy(FirstMember, TEXT("⚠️ HORDE!"), FLinearColor(1.f, 0.2f, 0.15f));
	}
	if (!Config.WarningSound.IsEmpty())
	{
		if (USoundBase* Sound = Cast<USoundBase>(FSoftObjectPath(Config.WarningSound).TryLoad()))
		{
			UGameplayStatics::PlaySound2D(World, Sound);
		}
	}
	OnHordeReleased.Broadcast(HordesReleased + 1, LastHorde.Num(), Point);
	return LastHorde.Num();
}

void UHordeSubsystem::DropQueue(const TCHAR* Why)
{
	if (GetActiveQueued() > 0)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Horde] horde %d: %d held-back members dropped (%s)"), ActiveHordeNumber, GetActiveQueued(), Why);
		ActiveSpawned = ActivePlan.Members.Num();
	}
	if (UWaveSubsystem* Waves = GetWorld() ? GetWorld()->GetSubsystem<UWaveSubsystem>() : nullptr)
	{
		Waves->SetExternalPendingSpawns(0);
	}
}

void UHordeSubsystem::TickActiveHorde(float DeltaTime)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	// Arrivals (2 Hz): how long the members took from their spawn to the squad (the designer's "time to prepare").
	ArrivalCheckCooldown -= DeltaTime;
	if (ArrivalCheckCooldown <= 0.f && !PendingArrivals.IsEmpty())
	{
		ArrivalCheckCooldown = 0.5f;
		FVector Centre;
		const bool bCentre = GetSquadCentre(Centre);
		for (auto It = PendingArrivals.CreateIterator(); It; ++It)
		{
			const AEnemyCharacter* Enemy = It->Key.Get();
			if (!Enemy || Enemy->IsDying())
			{
				It.RemoveCurrent();
				continue;
			}
			if (bCentre && FVector::Dist2D(Enemy->GetActorLocation(), Centre) <= ArrivalRangeCm)
			{
				const bool bZombie = GetNameSafe(Enemy->GetClass()).Contains(TEXT("MilitaryZombie"));
				FArrival& Arrival = Arrivals.AddDefaulted_GetRef();
				Arrival.Label = CombatPressureRules::EnemyLabel(FPressureEnemyType(Enemy->GetArchetype(),
					bZombie ? CombatPressureRules::MilitaryZombieVariant() : NAME_None)).Replace(TEXT("EEnemyArchetype::"), TEXT(""));
				Arrival.Seconds = static_cast<float>(World->GetTimeSeconds() - It->Value.X);
				Arrival.SpawnDistanceCm = static_cast<float>(It->Value.Y);
				Arrival.Horde = ActiveHordeNumber;
				UE_LOG(LogCodexTactics, Display, TEXT("[Horde] horde %d: %s at the squad (%.0f m) %.1f s after its spawn %.0f m away"), Arrival.Horde,
					*Arrival.Label, ArrivalRangeCm / 100.f, Arrival.Seconds, Arrival.SpawnDistanceCm / 100.f);
				It.RemoveCurrent();
			}
		}
	}
	// The held-back members (max_simultaneous_enemies): as the living fall, the next come - real-time fight only.
	if (GetActiveQueued() == 0)
	{
		return;
	}
	UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
	UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>();
	ReinforceCooldown -= DeltaTime;
	if (!Flow || !Waves || Flow->GetPhase() != ECodexGamePhase::WaveCombat || Flow->GetCombatMode() != ECodexCombatMode::RealTime
		|| UWorldAIPauseSubsystem::IsPausedIn(World) || ReinforceCooldown > 0.f)
	{
		return;
	}
	ReinforceCooldown = 0.25f;
	int32 Alive = 0;
	for (const TWeakObjectPtr<AEnemyCharacter>& Member : LastHorde)
	{
		Alive += Member.IsValid() && !Member->IsDying() ? 1 : 0;
	}
	const int32 Now = HordeRules::MembersToSpawnNow(ActivePlan, ActiveSpawned, Alive);
	FVector Centre;
	if (Now <= 0 || !GetSquadCentre(Centre))
	{
		return;
	}
	// They come where the horde came from while the squad cannot see it; a seen spot gets a new far one.
	if (IsVisibleToSquad(ActiveSpawnPoint + FVector(0.f, 0.f, 100.f), true))
	{
		FVector Point;
		bool bHidden = false;
		FHordeFarPick Pick;
		float Bearing = 0.f;
		if (!FindFarSpawnPoint(Centre, false, Point, bHidden, Pick, Bearing))
		{
			ReinforceCooldown = HordeTuning::RetrySeconds;
			return;
		}
		ActiveSpawnPoint = Point;
	}
	const FRotator Facing(0.f, (Centre - ActiveSpawnPoint).Rotation().Yaw, 0.f);
	int32 Spawned = 0;
	for (int32 Count = 0; Count < Now; ++Count)
	{
		Spawned += SpawnNextMember(ActiveSpawnPoint, Facing) ? 1 : 0;
	}
	Waves->SetExternalPendingSpawns(GetActiveQueued());
	UE_LOG(LogCodexTactics, Display, TEXT("[Horde] horde %d: %d more come (%d alive, %d still held back)"), ActiveHordeNumber, Spawned, Alive + Spawned,
		GetActiveQueued());
}

namespace HordeCommands
{
	static FAutoConsoleCommandWithWorld Release(
		TEXT("CodexTactics.Horde.Release"),
		TEXT("Dev: releases the next firefight horde's members now, no warning, 25-45 m from the squad (out of its sight when possible; it joins the running fight)."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			if (UHordeSubsystem* Horde = World ? World->GetSubsystem<UHordeSubsystem>() : nullptr)
			{
				UE_LOG(LogCodexTactics, Display, TEXT("[Horde] released by command: %d enemies"), Horde->ReleaseHorde());
			}
		}));
}
