#include "Telemetry/RunTelemetrySubsystem.h"

#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Core/CodexTacticsGameMode.h"
#include "Data/WaveConfigTypes.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/FileManager.h"
#include "Interactables/BarricadeActor.h"
#include "Interactables/TurretActor.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Survival/ColdSurvivalComponent.h"

void URunTelemetrySubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	bEnabled = !FParse::Param(FCommandLine::Get(), TEXT("NoTelemetry")) && InWorld.IsGameWorld();
	StartAppTime = FApp::GetCurrentTime();
	if (UGameFlowSubsystem* Flow = InWorld.GetSubsystem<UGameFlowSubsystem>())
	{
		Flow->OnGameFlowChanged.AddDynamic(this, &URunTelemetrySubsystem::HandleGameFlowChanged);
	}
	if (UWaveSubsystem* Waves = InWorld.GetSubsystem<UWaveSubsystem>())
	{
		Waves->OnEnemySpawnedNative.AddUObject(this, &URunTelemetrySubsystem::HandleEnemySpawned);
	}
}

FString URunTelemetrySubsystem::GetRunsFilePath()
{
	// Parallel bot runs (Scripts/bot_run.ps1 -Parallel) each write their own file the script then appends to runs.jsonl.
	FString Override;
	if (FParse::Value(FCommandLine::Get(), TEXT("TelemetryRunsFile="), Override) && !Override.IsEmpty())
	{
		return Override;
	}
	return FPaths::ProjectSavedDir() / TEXT("Telemetry/raw_runs/runs.jsonl");
}

FMemberRunStats& URunTelemetrySubsystem::StatsFor(const AOperativeCharacter* Operative)
{
	const FString Name = Operative ? Operative->DisplayName.ToString() : FString(TEXT("?"));
	FMemberRunStats& Stats = MemberStats.FindOrAdd(Name);
	Stats.Name = Name;
	return Stats;
}

FDeployableRunStats* URunTelemetrySubsystem::DeployableFor(const FString& Source)
{
	// Godot register_deployable_damage / register_enemy_kill: by the attacker source string.
	if (Source.Contains(TEXT("Турель")))
	{
		return &Turrets;
	}
	if (Source.Contains(TEXT("Мина")))
	{
		return &Mines;
	}
	if (Source.Contains(TEXT("Баррикада")))
	{
		return &Barricades;
	}
	return nullptr;
}

void URunTelemetrySubsystem::RecordWeaponHit(const AOperativeCharacter* Operative, const FString& WeaponId, float Damage)
{
	const UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
	RunTelemetryRules::RecordShot(StatsFor(Operative), Flow ? FMath::Max(1, Flow->GetWaveIndex()) : 1, WeaponId, Damage);
}

void URunTelemetrySubsystem::HandleEnemySpawned(AEnemyCharacter* Enemy, EEnemyArchetype Archetype)
{
	if (UHealthComponent* Health = Enemy ? Enemy->GetHealthComponent() : nullptr)
	{
		Health->OnDamaged.AddDynamic(this, &URunTelemetrySubsystem::HandleEnemyDamaged);
		Health->OnDiedNative.AddUObject(this, &URunTelemetrySubsystem::HandleEnemyDied);
	}
}

void URunTelemetrySubsystem::HandleEnemyDamaged(const FDamageSpec& Spec, float FinalDamage)
{
	if (FDeployableRunStats* Stats = DeployableFor(Spec.AttackerSource))
	{
		Stats->Damage += FinalDamage;
	}
}

void URunTelemetrySubsystem::HandleEnemyDied(AActor* Victim, const FString& Source)
{
	if (FDeployableRunStats* Stats = DeployableFor(Source))
	{
		++Stats->Kills;
	}
}

void URunTelemetrySubsystem::HandleGameFlowChanged(ECodexGamePhase Phase, ECodexCombatMode CombatMode)
{
	if (bRecorded || !bEnabled)
	{
		return;
	}
	// Godot: _trigger_game_over records the defeat, the post-combat sequence after the last wave the victory.
	if (Phase == ECodexGamePhase::GameOver)
	{
		RecordRun(false);
	}
	else if (Phase == ECodexGamePhase::PostCombat)
	{
		RecordRun(true);
	}
}

FString URunTelemetrySubsystem::RecordRun(bool bVictory)
{
	UWorld* World = GetWorld();
	const UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
	const int32 Wave = Flow ? FMath::Max(1, Flow->GetWaveIndex()) : 1;

	FRunRecord Record;
	// A GUID: parallel bot runs start at the same moment with the same unseeded FMath::Rand.
	Record.SessionId = FString::Printf(TEXT("%s_%.3f"), *FGuid::NewGuid().ToString(EGuidFormats::Short), FDateTime::UtcNow().ToUnixTimestampDecimal());
	Record.TimestampUtc = FDateTime::UtcNow().ToString(TEXT("%Y-%m-%dT%H:%M:%S"));
	Record.TesterProfile = TesterProfile;
	const ACodexTacticsGameMode* GameMode = World->GetAuthGameMode<ACodexTacticsGameMode>();
	const ULevelConfigAsset* Level = GameMode ? GameMode->GetActiveLevelConfig() : nullptr;
	Record.LevelId = Level && !Level->Config.LevelId.IsEmpty() ? Level->Config.LevelId : FString(TEXT("outpost_gate_01"));
	Record.Result = bVictory ? TEXT("VICTORY") : TEXT("DEFEAT");
	Record.WavesCleared = RunTelemetryRules::WavesCleared(bVictory, Wave);
	Record.TotalWaves = Flow ? Flow->GetConfig().TotalWaves : 0;
	Record.RunDurationSec = static_cast<float>(FApp::GetCurrentTime() - StartAppTime);

	bool bFrozenDeath = false;
	if (const USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>())
	{
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			FMemberRunStats Stats = StatsFor(Member);
			Stats.FinalColdPct = Member->ColdLevel;
			if (const UColdSurvivalComponent* Cold = Member->ColdSurvival)
			{
				Stats.ColdDamageTaken = Cold->GetColdDamageTaken();
				Stats.ExtremeColdTimeSec = Cold->GetExtremeColdTime();
			}
			Stats.Medkits = Member->MedkitsCount;
			Stats.CannedFood = Member->CannedFoodCount;
			Stats.Chocolate = Member->ChocolateCount;
			Stats.Matches = Member->MatchesCount;
			auto Ammo = [Member](const TCHAR* Id)
			{
				const FWeaponAmmoState* State = Member->AmmoInventory.Find(Id);
				return State ? State->Clip + FMath::Max(0, State->Reserve) : 0; // reserve -1 = unlimited, counted as 0
			};
			Stats.AmmoM16 = Ammo(TEXT("m16"));
			Stats.AmmoPistol = Ammo(TEXT("pistol"));
			// Godot _trigger_game_over: the fallen operative froze when his cold was >= 99 %.
			bFrozenDeath |= Member->HealthComponent && !Member->HealthComponent->IsAlive() && Member->ColdLevel >= 99.f;
			Record.Members.Add(Stats);
		}
	}

	auto Survivors = [World](UClass* Class, FDeployableRunStats& Stats)
	{
		float TotalPct = 0.f;
		for (TActorIterator<AActor> It(World, Class); It; ++It)
		{
			const UHealthComponent* Health = It->FindComponentByClass<UHealthComponent>();
			if (Health && Health->IsAlive())
			{
				++Stats.Survived;
				TotalPct += Health->GetMaxHealth() > 0.f ? Health->GetCurrentHealth() / Health->GetMaxHealth() * 100.f : 100.f;
			}
		}
		Stats.AvgHpPct = Stats.Survived > 0 ? TotalPct / Stats.Survived : 0.f;
	};
	Record.Turrets = Turrets;
	Record.Barricades = Barricades;
	Record.Mines = Mines;
	Survivors(ATurretActor::StaticClass(), Record.Turrets);
	Survivors(ABarricadeActor::StaticClass(), Record.Barricades);
	if (!bVictory)
	{
		Record.FailedWave = Wave;
		Record.DeathCause = bFrozenDeath ? TEXT("FREEZING_FATIGUE") : TEXT("HP_DEPLETED");
	}

	LastRecordJson = RunTelemetryRules::BuildRunJson(Record);
	bRecorded = true;
	if (bEnabled)
	{
		const FString Path = GetRunsFilePath();
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
		FFileHelper::SaveStringToFile(LastRecordJson + TEXT("\n"), *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
			&IFileManager::Get(), FILEWRITE_Append);
		UE_LOG(LogCodexTactics, Display, TEXT("[Telemetry] %s %s: %d / %d waves -> %s"), *Record.TesterProfile, *Record.Result,
			Record.WavesCleared, Record.TotalWaves, *Path);
	}
	return LastRecordJson;
}
