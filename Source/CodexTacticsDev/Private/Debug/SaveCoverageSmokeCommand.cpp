// Dev-only headless COVERAGE check of the save system (audit 2026-10-19): a LIVE-WORLD state dump, independent of the save format,
// taken right before F5 and right after F9 reopened the level, then diffed - any runtime state that is not saved / restored shows up
// as a mismatch (the dump reads the game objects, not the JSON: squad members incl. the fallen, cold flags, cover, standing orders,
// enemies incl. scent, interactables, crates, waves / kill statistics, tutorial, minimap ...). Two commands, one body:
//   Scripts/smoke_parallel.ps1 -Commands SaveCoverageSmoke        (map /Game/Maps/L_PatrolTest, placed patrols, from test_map.json)
//   Scripts/smoke_parallel.ps1 -Commands SaveCoverageLakeSmoke    (map /Game/Maps/L_Demo01_A_FrozenLake + its tutorial)
// Slots go to Saved/SmokeSaves/Coverage (exclusive in Scripts/test_map.json). Flow:
//  0. A non-trivial out-of-combat world: every enemy blind (no fight: the squad is Passive, cease fire), the Commander frostbitten with a
//     frozen weapon at cold 88, the campfire chosen on F, blind fire from cover, pressed into a cover slot; the Medic-Sapper holds a
//     defense line (Commander Mode); the Engineer KILLED IN ACTION (not on the lake: the tutorial owns its deaths) with his remains
//     crate; a barrel rigged with a grenade (4 m blast); the mission's event waves / breach / cold drain / kill statistics set; a
//     preparation station recorded; Solo ON; the Commander whistled (cooldown).
//  1. DUMP A, F5 (allowed), three broken files beside it (not JSON / no save sections / a map that does not exist): the list skips
//     the first two and marks the third, which refuses to load; F9 reopens the level.
//  2. In the fresh world: DUMP B at the moment the save is applied, diffed against A (0 mismatches wanted, each one listed), direct
//     checks of the audit's gaps, a settled dump 1 s later (cover / Solo / defense still hold).
//  3. Robustness in place: a version-1 file with empty sections loads (defaults, no crash), the broken ones are refused.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AI/PatrolRouteActor.h"
#include "AI/StealthLureSubsystem.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadAutonomySubsystem.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/DeathCinematicSubsystem.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Combat/WaveVictorySubsystem.h"
#include "Containers/Ticker.h"
#include "Core/PlayerCommands.h"
#include "Core/SaveGameRules.h"
#include "Core/SaveGameSubsystem.h"
#include "Data/WeaponDataAsset.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Environment/EnvironmentRules.h"
#include "Environment/EnvironmentSubsystem.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "GameFlow/LevelEncounterSubsystem.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/BarrelActor.h"
#include "Interactables/DroppedItemActor.h"
#include "Interactables/GateActor.h"
#include "Interactables/InteractableActor.h"
#include "Interactables/ItemStashComponent.h"
#include "Interactables/LootCrateActor.h"
#include "Interactables/TripwireActor.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Quests/DialogueTriggerVolume.h"
#include "Quests/QuestSubsystem.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Survival/ColdSurvivalComponent.h"
#include "Survival/TacticalHeatActor.h"
#include "Tactics/CoverTypes.h"
#include "Tutorial/TutorialSubsystem.h"
#include "UI/MinimapSubsystem.h"

namespace SaveCoverageSmoke
{
	constexpr float StepSeconds = 0.25f;

	struct FState
	{
		bool bLake = false;
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		TMap<FString, FString> DumpA;
		TMap<FString, FString> DumpB;
		bool bApplied = false;
		FDelegateHandle AppliedHandle;
		FString CommanderName;
		FString MedicName;
		FString EngineerName;
		FString BarrelName;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	FString SaveDir()
	{
		return FPaths::ProjectSavedDir() / TEXT("SmokeSaves/Coverage");
	}

	bool Finish(FState& State, bool bComplete)
	{
		if (State.AppliedHandle.IsValid())
		{
			USaveGameSubsystem::OnSaveApplied().Remove(State.AppliedHandle);
		}
		IFileManager::Get().DeleteDirectory(*SaveDir(), false, true);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("SaveCoverageSmoke"));
		return false;
	}

	UWorld* FindGameWorld()
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if ((Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE) && Context.World() && Context.World()->HasBegunPlay())
			{
				return Context.World();
			}
		}
		return nullptr;
	}

	AOperativeCharacter* FindRole(UWorld* World, EOperativeRole Role)
	{
		// Every operative of the world (a fallen one left the squad).
		for (TActorIterator<AOperativeCharacter> It(World); It; ++It)
		{
			if (It->SquadRole == Role && !It->IsActorBeingDestroyed())
			{
				return *It;
			}
		}
		return nullptr;
	}

	/** No enemy sees / hears / smells anything: the world stays out of combat (saves allowed), re-applied to the reopened level too. */
	void BlindEnemies(UWorld* World)
	{
		FEnemyPerceptionParams Senseless;
		Senseless.SightRangeCm = 0.f;
		Senseless.ProximityCm = 0.f;
		Senseless.HearWalkCm = Senseless.HearRunCm = Senseless.HearCrouchWalkCm = Senseless.HearCrawlCm = 0.f;
		Senseless.HearGunshotCm = Senseless.HearExplosionCm = 0.f;
		Senseless.SmellRadiusCm = 0.f;
		for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
		{
			It->bOverridePerception = true;
			It->PerceptionOverride = Senseless;
		}
		if (USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>())
		{
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Member->bTacticalCeaseFire = true;
			}
		}
	}

	// --- The live dump ---

	FString Num(double Value)
	{
		return FString::Printf(TEXT("%.2f"), Value);
	}

	FString Vec(const FVector& V)
	{
		return FString::Printf(TEXT("%.0f %.0f %.0f"), V.X, V.Y, V.Z);
	}

	FString Flag(bool bValue)
	{
		return bValue ? TEXT("true") : TEXT("false");
	}

	template <typename EnumType>
	FString EnumName(EnumType Value)
	{
		return StaticEnum<EnumType>()->GetNameStringByValue(static_cast<int64>(Value));
	}

	void DumpOperative(TMap<FString, FString>& Out, UWorld* World, const AOperativeCharacter& Member)
	{
		const FString K = TEXT("op.") + Member.DisplayName.ToString() + TEXT(".");
		const bool bAlive = Member.HealthComponent && Member.HealthComponent->IsAlive();
		Out.Add(K + TEXT("alive"), Flag(bAlive));
		Out.Add(K + TEXT("kia"), Flag(Member.IsKilledInAction()));
		Out.Add(K + TEXT("pos"), Vec(Member.GetActorLocation()));
		Out.Add(K + TEXT("yaw"), Num(FRotator::NormalizeAxis(Member.GetActorRotation().Yaw)));
		Out.Add(K + TEXT("stance"), EnumName(Member.GetStance()));
		Out.Add(K + TEXT("health"), Num(Member.HealthComponent ? Member.HealthComponent->GetCurrentHealth() : 0.f));
		Out.Add(K + TEXT("max_health"), Num(Member.HealthComponent ? Member.HealthComponent->GetMaxHealth() : 0.f));
		Out.Add(K + TEXT("cold"), Num(Member.ColdLevel));
		Out.Add(K + TEXT("frostbitten"), Flag(Member.ColdSurvival && Member.ColdSurvival->IsFrostbitten()));
		Out.Add(K + TEXT("weapon_frozen"), Flag(Member.ColdSurvival && Member.ColdSurvival->IsWeaponFrozen()));
		Out.Add(K + TEXT("cold_tier"), Member.ColdSurvival ? EnumName(Member.ColdSurvival->GetTier()) : TEXT("-"));
		Out.Add(K + TEXT("heat_buffer"), Num(Member.ColdSurvival ? FMath::RoundToFloat(Member.ColdSurvival->GetHeatBufferSecondsLeft()) : 0.f));
		Out.Add(K + TEXT("medkits"), Num(Member.MedkitsCount));
		Out.Add(K + TEXT("grenades"), Num(Member.GrenadesCount));
		Out.Add(K + TEXT("heat_packs"), Num(Member.HeatPacksCount));
		Out.Add(K + TEXT("matches"), Num(Member.MatchesCount));
		Out.Add(K + TEXT("mines"), Num(Member.MinesCount));
		Out.Add(K + TEXT("weapon"), Member.CurrentWeapon ? Member.CurrentWeapon->WeaponId : TEXT("-"));
		Out.Add(K + TEXT("clip"), Num(Member.CurrentClip));
		Out.Add(K + TEXT("reserve"), Num(Member.ReserveAmmo));
		Out.Add(K + TEXT("guarding"), Flag(Member.bGuarding));
		Out.Add(K + TEXT("posture_override"), Member.bHasPostureOverride ? EnumName(Member.PostureOverride) : TEXT("-"));
		Out.Add(K + TEXT("deploy"), EnumName(Member.SelectedDeployType));
		Out.Add(K + TEXT("fire_mode"), EnumName(Member.CoverFireMode));
		Out.Add(K + TEXT("in_cover"), Flag(Member.bInCover));
		Out.Add(K + TEXT("cover_slot"), Member.bInCover ? Vec(Member.GetCoverSlot().WorldLocation) : TEXT("-"));
		Out.Add(K + TEXT("cover_height"), Member.bInCover ? EnumName(Member.GetCoverSlot().Height) : TEXT("-"));
		const FDefenseDirective& Defense = Member.TacticalAnchor.Defense;
		Out.Add(K + TEXT("defense"), Flag(Defense.IsActive()));
		Out.Add(K + TEXT("defense_actor"), Defense.IsActive() && Defense.DefendedActor.IsValid() ? Defense.DefendedActor->GetName() : TEXT("-"));
		Out.Add(K + TEXT("defense_point"), Defense.IsActive() ? Vec(Defense.DefendedLocation) : TEXT("-"));
		FVector Station = FVector::ZeroVector;
		const UWaveVictorySubsystem* Victory = World->GetSubsystem<UWaveVictorySubsystem>();
		Out.Add(K + TEXT("prep_station"), Victory && Victory->GetRecordedPrepStation(&Member, Station) ? Vec(Station) : TEXT("-"));
		Out.Add(K + TEXT("knocked_down"), Flag(Member.IsKnockedDown()));
		Out.Add(K + TEXT("panicking"), Flag(Member.IsPanicking()));
		Out.Add(K + TEXT("raging"), Flag(Member.IsRaging()));
		if (const UStealthLureSubsystem* Lure = World->GetSubsystem<UStealthLureSubsystem>())
		{
			Out.Add(K + TEXT("whistle_cd"), Num(FMath::RoundToFloat(Lure->GetCooldownLeft(&Member, EStealthLureKind::Whistle))));
			Out.Add(K + TEXT("throw_cd"), Num(FMath::RoundToFloat(Lure->GetCooldownLeft(&Member, EStealthLureKind::Throw))));
		}
	}

	TMap<FString, FString> DumpWorld(UWorld* World)
	{
		TMap<FString, FString> Out;
		if (const UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>())
		{
			Out.Add(TEXT("flow.phase"), EnumName(Flow->GetPhase()));
			Out.Add(TEXT("flow.wave"), Num(Flow->GetWaveIndex()));
			Out.Add(TEXT("flow.combat_unlocked"), Flag(Flow->IsCombatUnlocked()));
			Out.Add(TEXT("flow.ambush"), Flag(Flow->IsAmbushFight()));
		}
		if (const USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>())
		{
			Out.Add(TEXT("squad.leader"), Squad->GetLeader() ? Squad->GetLeader()->DisplayName.ToString() : TEXT("-"));
			Out.Add(TEXT("squad.members"), Num(Squad->GetMembers().Num()));
			Out.Add(TEXT("squad.solo"), Flag(Squad->IsSoloMode()));
			Out.Add(TEXT("squad.trap_solo"), Flag(Squad->IsTrapSolo()));
			Out.Add(TEXT("squad.posture"), EnumName(Squad->GetSquadPosture()));
			Out.Add(TEXT("squad.autonomous"), Flag(Squad->IsAutonomousSquadCombat()));
			Out.Add(TEXT("squad.followers_holding"), Flag(Squad->AreFollowersHolding()));
		}
		if (const UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>())
		{
			Out.Add(TEXT("waves.breach_wave"), Num(Waves->GetDynamicBreachWave()));
			Out.Add(TEXT("waves.susanin_wave"), Num(Waves->GetSusaninRescueWave()));
			Out.Add(TEXT("waves.breach_triggered"), Flag(Waves->IsDynamicBreachTriggered()));
			Out.Add(TEXT("waves.cold_drain"), Num(Waves->GetColdDrainMultiplier()));
		}
		if (const UEnvironmentSubsystem* Environment = UEnvironmentSubsystem::Get(World); Environment && Environment->IsInitialised())
		{
			// game_state.environment (2026-10-22): the day cycle, the weather and the persistent snow cover.
			const FEnvironmentState& Env = Environment->GetStateRef();
			Out.Add(TEXT("env.hour"), Num(Env.Hour));
			Out.Add(TEXT("env.weather"), EnvironmentRules::WeatherToString(Env.Weather));
			Out.Add(TEXT("env.next_weather"), EnvironmentRules::WeatherToString(Env.NextWeather));
			Out.Add(TEXT("env.weather_elapsed"), Num(Env.WeatherElapsedSeconds));
			Out.Add(TEXT("env.weather_dwell"), Num(Env.WeatherDwellSeconds));
			Out.Add(TEXT("env.seed"), FString::FromInt(Env.RandomSeed));
			Out.Add(TEXT("env.locked"), Flag(Env.bLocked));
			Out.Add(TEXT("env.snow"), FString::Printf(TEXT("%.3f"), Env.SnowAmount));
		}
		if (const UWaveVictorySubsystem* Victory = World->GetSubsystem<UWaveVictorySubsystem>())
		{
			const FSquadKillStats& Kills = Victory->GetKillStats();
			Out.Add(TEXT("kills.total"), Num(Kills.GetTotal()));
			Out.Add(TEXT("kills.commander_hound"), Num(Kills.Commander.Hound));
			Out.Add(TEXT("kills.commander_turret"), Num(Kills.Commander.TurretKills));
			Out.Add(TEXT("kills.turret"), Num(Kills.TurretKills));
			Out.Add(TEXT("kills.barricade"), Num(Kills.BarricadeKills));
		}
		if (const UQuestSubsystem* Quests = World->GetSubsystem<UQuestSubsystem>())
		{
			const FQuestChainState& Chain = Quests->GetState();
			Out.Add(TEXT("quest.canister"), Flag(Chain.bHasEmptyCanister));
			Out.Add(TEXT("quest.fuel"), Flag(Chain.bHasFuelCanister));
			Out.Add(TEXT("quest.generator"), Flag(Chain.bIsGeneratorRunning));
			Out.Add(TEXT("quest.gate_powered"), Flag(Chain.bIsGatePowered));
		}
		for (TActorIterator<AOperativeCharacter> It(World); It; ++It)
		{
			if (!It->IsActorBeingDestroyed())
			{
				DumpOperative(Out, World, **It);
			}
		}
		const ULevelEncounterSubsystem* Encounter = World->GetSubsystem<ULevelEncounterSubsystem>();
		int32 LivingEnemies = 0;
		for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
		{
			const AEnemyCharacter& Enemy = **It;
			if (Enemy.IsActorBeingDestroyed() || Enemy.IsDying() || !Enemy.GetHealthComponent() || !Enemy.GetHealthComponent()->IsAlive())
			{
				continue;
			}
			++LivingEnemies;
			const FString K = TEXT("enemy.") + Enemy.GetName() + TEXT(".");
			Out.Add(K + TEXT("class"), Enemy.GetClass()->GetName());
			Out.Add(K + TEXT("archetype"), EnumName(Enemy.GetArchetype()));
			Out.Add(K + TEXT("pos"), Vec(Enemy.GetActorLocation()));
			Out.Add(K + TEXT("health"), Num(Enemy.GetHealthComponent()->GetCurrentHealth()));
			Out.Add(K + TEXT("patrol"), Flag(Enemy.IsOnPatrol()));
			Out.Add(K + TEXT("guard_post"), Flag(Enemy.IsGuardingPost()));
			Out.Add(K + TEXT("searching"), Flag(Enemy.IsSearching()));
			Out.Add(K + TEXT("scent"), EnumName(Enemy.GetScentPhase()));
			Out.Add(K + TEXT("walk_speed"), Num(Enemy.PatrolWalkSpeed));
			Out.Add(K + TEXT("level_enemy"), Flag(Encounter && Encounter->IsLevelEnemy(&Enemy)));
			Out.Add(K + TEXT("has_target"), Flag(Enemy.GetCurrentTarget() != nullptr));
			Out.Add(K + TEXT("route"), Enemy.AssignedPatrolRoute ? Enemy.AssignedPatrolRoute->GetName() : TEXT("-"));
			Out.Add(K + TEXT("escort"), Enemy.GetEscortLeader() ? Enemy.GetEscortLeader()->GetName() : TEXT("-"));
		}
		Out.Add(TEXT("enemies.living"), Num(LivingEnemies));
		for (TActorIterator<AInteractableActor> It(World); It; ++It)
		{
			const AInteractableActor& Object = **It;
			if (Object.IsActorBeingDestroyed() || Object.IsA<ALootCrateActor>() || Object.IsA<ADroppedItemActor>())
			{
				continue;
			}
			if (const ATripwireActor* Wire = Cast<ATripwireActor>(&Object); Wire && (Wire->IsPreview() || Wire->IsTripped()))
			{
				continue;
			}
			const FString K = TEXT("obj.") + Object.GetName() + TEXT(".");
			Out.Add(K + TEXT("class"), Object.GetClass()->GetName());
			Out.Add(K + TEXT("pos"), Vec(Object.GetActorLocation()));
			Out.Add(K + TEXT("hidden"), Flag(Object.IsHidden()));
			Out.Add(K + TEXT("trapped"), Flag(Object.bTrapped));
			Out.Add(K + TEXT("trap_radius"), Num(Object.TrapRadius));
			Out.Add(K + TEXT("defused"), Flag(Object.bDefused));
			if (Object.ObjectType == EInteractableType::Generator)
			{
				Out.Add(K + TEXT("generator_health"), Num(Object.GeneratorHealth));
			}
			if (const ABarrelActor* Barrel = Cast<ABarrelActor>(&Object))
			{
				Out.Add(K + TEXT("burning"), Flag(Barrel->IsBurning()));
			}
		}
		int32 RuntimeCrates = 0;
		for (TActorIterator<ALootCrateActor> It(World); It; ++It)
		{
			if (It->IsActorBeingDestroyed())
			{
				continue;
			}
			RuntimeCrates += It->IsNetStartupActor() ? 0 : 1;
			const FString K = TEXT("crate.") + It->GetName() + TEXT(".");
			Out.Add(K + TEXT("looted"), Flag(It->IsLooted()));
			Out.Add(K + TEXT("destroyed"), Flag(It->IsDestroyed()));
			Out.Add(K + TEXT("defused"), Flag(It->bDefused));
			Out.Add(K + TEXT("stash"), Num(It->GetSyncedStash() ? It->GetSyncedStash()->GetTotal() : 0));
			Out.Add(K + TEXT("name"), It->CrateName.ToString());
			Out.Add(K + TEXT("corpse"), Flag(It->Tags.Contains(FName(TEXT("CorpseLoot")))));
			Out.Add(K + TEXT("hidden"), Flag(It->IsHidden()));
			Out.Add(K + TEXT("pos"), Vec(It->GetActorLocation()));
		}
		Out.Add(TEXT("crates.runtime"), Num(RuntimeCrates));
		int32 Piles = 0;
		for (TActorIterator<ADroppedItemActor> It(World); It; ++It)
		{
			Piles += It->IsActorBeingDestroyed() ? 0 : 1;
		}
		Out.Add(TEXT("piles"), Num(Piles));
		int32 HeatBurning = 0;
		for (TActorIterator<ATacticalHeatActor> It(World); It; ++It)
		{
			HeatBurning += !It->IsActorBeingDestroyed() && It->IsBurning() ? 1 : 0;
		}
		Out.Add(TEXT("heat.burning"), Num(HeatBurning));
		for (TActorIterator<AGateActor> It(World); It; ++It)
		{
			Out.Add(TEXT("gate.") + It->GetName() + TEXT(".open"), Flag(It->IsOpen() || It->IsOpening()));
			Out.Add(TEXT("gate.") + It->GetName() + TEXT(".progress"), Num(It->GetOpenProgress()));
		}
		for (TActorIterator<ADialogueTriggerVolume> It(World); It; ++It)
		{
			Out.Add(TEXT("trigger.") + It->GetName(), Flag(It->HasTriggered()));
		}
		if (const UTutorialSubsystem* Tutorial = World->GetSubsystem<UTutorialSubsystem>())
		{
			Out.Add(TEXT("tutorial.steps"), Flag(Tutorial->HasSteps()));
			Out.Add(TEXT("tutorial.active"), Flag(Tutorial->IsTutorialActive()));
			Out.Add(TEXT("tutorial.beat"), Tutorial->GetCurrentBeat().ToString());
			Out.Add(TEXT("tutorial.skipped"), Flag(Tutorial->IsTutorialSkipped()));
			Out.Add(TEXT("tutorial.confining"), Flag(Tutorial->IsConfining()));
			Out.Add(TEXT("tutorial.lock_sprint"), Flag(Tutorial->IsCommandLocked(PlayerCommands::Sprint)));
			Out.Add(TEXT("tutorial.lock_shot"), Flag(Tutorial->IsCommandLocked(PlayerCommands::TargetedShot)));
			Out.Add(TEXT("tutorial.heat_packs_given"), Num(Tutorial->GetConditionStat(TEXT("heat_packs_given"))));
			Out.Add(TEXT("tutorial.precredited"), Num(Tutorial->GetPrecreditedCount()));
			Out.Add(TEXT("tutorial.guide"), UTutorialSubsystem::DescribeGuide(Tutorial->GetGuideView().Guide)); // 2026-10-20: the current target
		}
		if (const UMinimapSubsystem* Minimap = World->GetSubsystem<UMinimapSubsystem>())
		{
			Out.Add(TEXT("minimap.panel"), Minimap->GetPanelMode() == EMinimapPanelMode::Map ? TEXT("map") : TEXT("messages"));
		}
		return Out;
	}

	/** Tolerances by key: places 15 cm (a teleport snaps to the floor), yaw 1 deg, cooldowns 1 s, everything else exact. */
	bool Same(const FString& Key, const FString& A, const FString& B)
	{
		if (A == B)
		{
			return true;
		}
		const bool bVector = Key.EndsWith(TEXT(".pos")) || Key.EndsWith(TEXT("_point")) || Key.EndsWith(TEXT("_slot")) || Key.EndsWith(TEXT("_station"));
		if (bVector)
		{
			TArray<FString> PartsA;
			TArray<FString> PartsB;
			A.ParseIntoArray(PartsA, TEXT(" "));
			B.ParseIntoArray(PartsB, TEXT(" "));
			if (PartsA.Num() != 3 || PartsB.Num() != 3)
			{
				return false;
			}
			for (int32 Index = 0; Index < 3; ++Index)
			{
				if (FMath::Abs(FCString::Atod(*PartsA[Index]) - FCString::Atod(*PartsB[Index])) > 15.0)
				{
					return false;
				}
			}
			return true;
		}
		if (A.IsNumeric() && B.IsNumeric())
		{
			const double Tolerance = Key.EndsWith(TEXT(".yaw")) ? 1.0 : (Key.EndsWith(TEXT("_cd")) ? 1.0 : 0.011);
			return FMath::Abs(FCString::Atod(*A) - FCString::Atod(*B)) <= Tolerance;
		}
		return false;
	}

	TArray<FString> Diff(const TMap<FString, FString>& Before, const TMap<FString, FString>& After)
	{
		TArray<FString> Mismatches;
		TSet<FString> Keys;
		for (const TPair<FString, FString>& Pair : Before)
		{
			Keys.Add(Pair.Key);
		}
		for (const TPair<FString, FString>& Pair : After)
		{
			Keys.Add(Pair.Key);
		}
		TArray<FString> Sorted = Keys.Array();
		Sorted.Sort();
		for (const FString& Key : Sorted)
		{
			const FString* A = Before.Find(Key);
			const FString* B = After.Find(Key);
			if (!A || !B || !Same(Key, *A, *B))
			{
				Mismatches.Add(FString::Printf(TEXT("%s: %s -> %s"), *Key, A ? **A : TEXT("<missing>"), B ? **B : TEXT("<missing>")));
			}
		}
		return Mismatches;
	}

	void ReportDiff(FState& State, const TMap<FString, FString>& Before, const TMap<FString, FString>& After, const FString& What)
	{
		const TArray<FString> Mismatches = Diff(Before, After);
		for (const FString& Line : Mismatches)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke diff [%s] %s"), *What, *Line);
		}
		Check(State, Mismatches.IsEmpty() && Before.Num() > 60, FString::Printf(TEXT("%s: %d live fields compared, %d mismatches"), *What, Before.Num(), Mismatches.Num()));
	}

	void WriteFile(const FString& Name, const FString& Text)
	{
		FFileHelper::SaveStringToFile(Text, *(SaveDir() / Name), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	/** A copy of the quicksave with its map renamed to one that does not exist. */
	void WriteBadMapCopy(const USaveGameSubsystem& Saves)
	{
		FString Json;
		TSharedPtr<FJsonObject> Data;
		if (FFileHelper::LoadFileToString(Json, *(Saves.GetSaveDirectory() / TEXT("quicksave.json"))) && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Data) && Data.IsValid())
		{
			Data->SetStringField(TEXT("map"), TEXT("/Game/Maps/L_Renamed_Away"));
			Data->SetStringField(TEXT("slot_name"), TEXT("badmap"));
			Data->SetStringField(TEXT("title"), TEXT("badmap"));
			Data->SetNumberField(TEXT("timestamp"), 1000.0); // old: never picked by F9
			FString Out;
			FJsonSerializer::Serialize(Data.ToSharedRef(), TJsonWriterFactory<>::Create(&Out));
			WriteFile(TEXT("badmap.json"), Out);
		}
	}

	// --- Stage 0: the world ---

	bool BuildWorld(UWorld* World, FState& State)
	{
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		USaveGameSubsystem* Saves = World->GetSubsystem<USaveGameSubsystem>();
		UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>();
		UWaveVictorySubsystem* Victory = World->GetSubsystem<UWaveVictorySubsystem>();
		USquadAutonomySubsystem* Autonomy = World->GetSubsystem<USquadAutonomySubsystem>();
		UStealthLureSubsystem* Lure = World->GetSubsystem<UStealthLureSubsystem>();
		AOperativeCharacter* Commander = FindRole(World, EOperativeRole::Commander);
		AOperativeCharacter* Engineer = FindRole(World, EOperativeRole::Engineer);
		AOperativeCharacter* Medic = FindRole(World, EOperativeRole::MedicSapper);
		if (!Squad || !Saves || !Waves || !Victory || !Autonomy || !Commander || !Engineer || !Medic)
		{
			Check(State, false, TEXT("squad of three, saves, waves, victory, autonomy"));
			return false;
		}
		State.CommanderName = Commander->DisplayName.ToString();
		State.MedicName = Medic->DisplayName.ToString();
		State.EngineerName = Engineer->DisplayName.ToString();
		Saves->SaveDirectoryOverride = SaveDir();
		IFileManager::Get().DeleteDirectory(*SaveDir(), false, true);
		Saves->bCheckpointsEnabled = true;
		Squad->SetLeader(Commander);
		Squad->SetSquadPosture(ESquadFirePosture::Passive);

		// Cold flags (gap: re-derived from the cold level, the sticky frostbite / frozen weapon came back clean).
		Commander->ColdLevel = 88.f;
		Commander->ColdSurvival->RestoreColdState(true, true);
		// The F-key deployable and the cover fire mode (gaps: back to Turret / CornerLean).
		Commander->SelectedDeployType = EDeployableType::Campfire;
		Commander->SetCoverFireMode(ECoverFireMode::BlindFire);
		// A defense line for the Medic-Sapper where she stands (gap: the standing Commander-Mode order was dropped with the anchor). A point,
		// not an object: an object's stand could be a walk away, and the restored order would send her walking on the fresh level.
		Squad->SetAutonomousSquadCombat(true);
		Check(State, Autonomy->SetDefenseObjective(Medic, nullptr, Medic->GetActorLocation()), TEXT("the Medic-Sapper holds a defense line"));
		ABarrelActor* Barrel = nullptr;
		for (TActorIterator<ABarrelActor> It(World); It && !Barrel; ++It)
		{
			Barrel = !It->IsActorBeingDestroyed() ? *It : nullptr;
		}
		// A grenade-rigged barrel: a 4 m blast (gap: 3.5 m after a load).
		if (Barrel)
		{
			Commander->GrenadesCount = FMath::Max(Commander->GrenadesCount, 2);
			State.BarrelName = Barrel->GetName();
			Check(State, Barrel->TrapWithGrenade(Commander) && Barrel->bTrapped && FMath::IsNearlyEqual(Barrel->TrapRadius, 400.f),
				FString::Printf(TEXT("barrel rigged with a grenade (radius %.0f)"), Barrel->TrapRadius));
		}
		// The mission's event waves, the breach done, the rest's cold drain, kills so far (gaps: re-rolled / 1.0 / zero).
		Waves->RestoreRandomEvents(3, 2, true);
		Waves->RestoreColdDrainMultiplier(1.7f);
		// The environment (gap until 2026-10-22: a reopened level re-resolved it - the snow cover back to its start value). The lake is
		// locked (its fixed night): only the snow there.
		if (UEnvironmentSubsystem* Environment = UEnvironmentSubsystem::Get(World))
		{
			Environment->SetSnowAmount(0.73f);
			if (!State.bLake)
			{
				Environment->SetHour(16.5f);
			}
		}
		Victory->RegisterEnemyKill(EEnemyArchetype::FrostHound, TEXT("Commander"));
		Victory->RegisterEnemyKill(EEnemyArchetype::FrostHound, TEXT("Commander"));
		Victory->RegisterEnemyKill(EEnemyArchetype::Brute, TEXT("Turret"));
		Victory->RestorePrepStation(Commander, Commander->GetActorLocation() + FVector(100.f, 0.f, 0.f));
		// The Commander whistles (the cooldown is saved; every enemy is blind / deaf: nobody comes).
		if (Lure)
		{
			Lure->Whistle(Commander);
		}
		// The Engineer killed in action (gap: alive again with the default loadout on the reopened level; not on the lake - the
		// tutorial reacts to deaths).
		if (!State.bLake)
		{
			Engineer->HealthComponent->ApplyDirectHealthLoss(100000.f, TEXT("Smoke"));
			Check(State, Engineer->IsKilledInAction() && !Squad->GetMembers().Contains(Engineer), TEXT("the Engineer killed in action, out of the squad"));
		}
		// Solo ON (the followers hold where they stand) and the Commander pressed into a cover slot (gap: out of cover after a load).
		Squad->EnterSoloMode();
		FCoverSlot Slot;
		Slot.WorldLocation = Commander->GetActorLocation();
		Slot.WallNormal = Commander->GetActorForwardVector().GetSafeNormal2D();
		Slot.WallPoint = Commander->GetActorLocation() - Slot.WallNormal * 45.f + FVector(0.f, 0.f, 40.f);
		Slot.Height = ECoverHeight::LowCover;
		Slot.WallTopCm = 120.f;
		Commander->EnterCover(Slot);
		Check(State, Commander->bInCover && Squad->IsSoloMode(), TEXT("Commander in cover, Solo ON"));
		return true;
	}

	bool Step(TSharedRef<FState> StatePtr)
	{
		FState& State = *StatePtr;
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		UWorld* World = FindGameWorld();
		if (State.Time > 240.f)
		{
			Check(State, false, FString::Printf(TEXT("timed out in stage %d"), State.Stage));
			return Finish(State, false);
		}
		if (!World)
		{
			return true;
		}
		if (State.Stage == 0)
		{
			BlindEnemies(World); // from the first frame: no detection before the set-up
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		USaveGameSubsystem* Saves = World->GetSubsystem<USaveGameSubsystem>();
		if (!Squad || !Saves || !Squad->GetLeader() || State.Time < 3.f)
		{
			return true;
		}
		switch (State.Stage)
		{
		case 0:
		{
			if (State.bLake)
			{
				const UTutorialSubsystem* Tutorial = World->GetSubsystem<UTutorialSubsystem>();
				if ((!Tutorial || !Tutorial->HasSteps()) && State.Time < 15.f)
				{
					return true; // the lake's tutorial still loading its steps
				}
			}
			if (!BuildWorld(World, State))
			{
				return Finish(State, false);
			}
			State.Stage = 1;
			State.StageTime = 0.f;
			return true;
		}
		case 1:
		{
			// The death cinematic of the fallen Engineer runs its course first (no slow motion in the saved moment).
			const UDeathCinematicSubsystem* DeathCam = World->GetSubsystem<UDeathCinematicSubsystem>();
			if ((State.StageTime < 1.f || (DeathCam && DeathCam->IsActive())) && State.StageTime < 30.f)
			{
				return true;
			}
			Check(State, !DeathCam || !DeathCam->IsActive(), TEXT("death cinematic over"));
			const UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
			Check(State, Flow && Flow->GetPhase() == ECodexGamePhase::Exploration, FString::Printf(TEXT("still exploring (%s)"), Flow ? *EnumName(Flow->GetPhase()) : TEXT("-")));
			// The cold flags right before the save (the cold kept draining while the death cinematic ran).
			if (AOperativeCharacter* Commander = FindRole(World, EOperativeRole::Commander); Commander && Commander->ColdSurvival)
			{
				Commander->ColdLevel = 88.f;
				Commander->ColdSurvival->RestoreColdState(true, true);
			}
			State.DumpA = DumpWorld(World);
			FText Hint;
			Check(State, Saves->CanSaveNow(&Hint), FString::Printf(TEXT("saving allowed (%s)"), *Hint.ToString()));
			Check(State, Saves->QuickSave() && Saves->HasSave(TEXT("quicksave")), TEXT("F5"));
			// Broken files beside it: not JSON, no save sections, a map that does not exist.
			WriteFile(TEXT("corrupt.json"), TEXT("{ \"version\": 3, \"squad\": [ {\"character_name\": \"x\""));
			WriteFile(TEXT("foreign.json"), TEXT("{ \"settings\": { \"volume\": 0.5 }, \"title\": \"options\" }"));
			WriteBadMapCopy(*Saves);
			const TArray<FSaveSlotInfo> Listed = Saves->GetAllSaves();
			bool bCorruptListed = false;
			bool bBadMapListed = false;
			bool bBadMapMarked = false;
			for (const FSaveSlotInfo& Info : Listed)
			{
				bCorruptListed |= Info.SlotName == TEXT("corrupt") || Info.SlotName == TEXT("foreign");
				bBadMapListed |= Info.SlotName == TEXT("badmap");
				bBadMapMarked |= Info.SlotName == TEXT("badmap") && !Info.bMapAvailable;
			}
			Check(State, !bCorruptListed && bBadMapListed && bBadMapMarked, FString::Printf(TEXT("the list skips the broken files and marks the missing map (%d listed)"), Listed.Num()));
			Check(State, !Saves->LoadGameWithTravel(TEXT("badmap")) && !Saves->LoadGame(TEXT("corrupt")) && !Saves->LoadGame(TEXT("foreign")),
				TEXT("a missing map / broken files refuse to load (no crash)"));
			Check(State, SaveGameRules::PickQuickLoadIndex(Listed) != INDEX_NONE && Listed[SaveGameRules::PickQuickLoadIndex(Listed)].SlotName == TEXT("quicksave"),
				TEXT("F9 picks the quicksave"));
			State.AppliedHandle = USaveGameSubsystem::OnSaveApplied().AddLambda([StatePtr](UWorld* AppliedWorld, const FString& Slot)
			{
				if (Slot == SaveGameRules::GetQuicksaveSlotName() && AppliedWorld)
				{
					BlindEnemies(AppliedWorld); // the fresh enemies are blind again before their first perception step
					StatePtr->DumpB = DumpWorld(AppliedWorld);
					StatePtr->bApplied = true;
				}
			});
			Check(State, Saves->QuickLoad(), TEXT("F9: the level reopens"));
			State.Stage = 2;
			State.StageTime = 0.f;
			return true;
		}
		case 2:
		{
			if (!State.bApplied)
			{
				if (State.StageTime > 60.f)
				{
					Check(State, false, TEXT("save applied after the travel"));
					return Finish(State, false);
				}
				return true;
			}
			if (State.StageTime < 1.f)
			{
				return true; // settle a second after the apply
			}
			if (State.bLake)
			{
				// The lake's tutorial loads its steps from the level JSON AFTER the save is applied (the restore is deferred until then): its
				// keys are dumped once the steps are in (TutorialSaveSmoke waits the same way).
				const UTutorialSubsystem* Tutorial = World->GetSubsystem<UTutorialSubsystem>();
				if ((!Tutorial || !Tutorial->HasSteps()) && State.StageTime < 20.f)
				{
					return true;
				}
				const TMap<FString, FString> Later = DumpWorld(World);
				for (const TPair<FString, FString>& Pair : Later)
				{
					if (Pair.Key.StartsWith(TEXT("tutorial.")))
					{
						State.DumpB.Add(Pair.Key, Pair.Value);
					}
				}
			}
			ReportDiff(State, State.DumpA, State.DumpB, TEXT("live state before F5 vs right after F9"));
			// The audit's gaps, checked directly on the fresh world.
			AOperativeCharacter* Commander = FindRole(World, EOperativeRole::Commander);
			AOperativeCharacter* Engineer = FindRole(World, EOperativeRole::Engineer);
			AOperativeCharacter* Medic = FindRole(World, EOperativeRole::MedicSapper);
			Check(State, Commander && Commander->ColdSurvival && Commander->ColdSurvival->IsFrostbitten() && Commander->ColdSurvival->IsWeaponFrozen()
				&& FMath::IsNearlyEqual(Commander->ColdLevel, 88.f, 3.f), FString::Printf(TEXT("Commander frostbitten (%d) with a frozen weapon (%d) at cold 88 (%.1f)"),
				Commander && Commander->ColdSurvival && Commander->ColdSurvival->IsFrostbitten() ? 1 : 0,
				Commander && Commander->ColdSurvival && Commander->ColdSurvival->IsWeaponFrozen() ? 1 : 0, Commander ? Commander->ColdLevel : -1.f));
			Check(State, Commander && Commander->SelectedDeployType == EDeployableType::Campfire && Commander->CoverFireMode == ECoverFireMode::BlindFire,
				TEXT("campfire on F, blind fire from cover"));
			Check(State, Commander && Commander->bInCover && Commander->GetCoverSlot().Height == ECoverHeight::LowCover, TEXT("Commander back in his cover slot"));
			Check(State, Medic && Medic->TacticalAnchor.Defense.IsActive(), TEXT("the Medic-Sapper still holds her defense line"));
			Check(State, Squad->IsSoloMode() && Squad->AreFollowersHolding() && Squad->IsAutonomousSquadCombat(), TEXT("Solo ON, followers holding, Commander Mode"));
			if (!State.bLake)
			{
				int32 Remains = 0;
				for (TActorIterator<ALootCrateActor> It(World); It; ++It)
				{
					Remains += !It->IsActorBeingDestroyed() && It->Tags.Contains(FName(TEXT("CorpseLoot"))) ? 1 : 0;
				}
				Check(State, Engineer && Engineer->IsKilledInAction() && !Squad->GetMembers().Contains(Engineer) && Engineer->HealthComponent
					&& !Engineer->HealthComponent->IsAlive() && Squad->GetMembers().Num() == 2,
					FString::Printf(TEXT("the Engineer stays dead on the reopened level (squad of %d)"), Squad->GetMembers().Num()));
				Check(State, Remains == 1, FString::Printf(TEXT("his remains crate recreated (%d)"), Remains));
			}
			const UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>();
			Check(State, Waves && Waves->GetDynamicBreachWave() == 3 && Waves->GetSusaninRescueWave() == 2 && Waves->IsDynamicBreachTriggered()
				&& FMath::IsNearlyEqual(Waves->GetColdDrainMultiplier(), 1.7f), TEXT("event waves 3 / 2, breach done, cold drain 1.7"));
			const UEnvironmentSubsystem* Environment = UEnvironmentSubsystem::Get(World);
			Check(State, Environment && FMath::IsNearlyEqual(Environment->GetSnowAmount(), 0.73f, 1.e-3f) && (State.bLake || FMath::Abs(Environment->GetHour() - 16.5f) < 0.5f),
				FString::Printf(TEXT("the environment back: snow cover 0.73 (%.3f), hour ~16.5 (%.2f)"), Environment ? Environment->GetSnowAmount() : -1.f,
				Environment ? Environment->GetHour() : -1.f));
			const UWaveVictorySubsystem* Victory = World->GetSubsystem<UWaveVictorySubsystem>();
			Check(State, Victory && Victory->GetKillStats().GetTotal() == 3 && Victory->GetKillStats().Commander.TurretKills == 1, TEXT("kill statistics back"));
			if (!State.BarrelName.IsEmpty())
			{
				const ABarrelActor* Barrel = nullptr;
				for (TActorIterator<ABarrelActor> It(World); It && !Barrel; ++It)
				{
					Barrel = It->GetName() == State.BarrelName ? *It : nullptr;
				}
				Check(State, Barrel && Barrel->bTrapped && FMath::IsNearlyEqual(Barrel->TrapRadius, 400.f), TEXT("the grenade trap keeps its 4 m blast"));
			}
			// Settled: nothing undid the restored orders in the first second (places: the leader's only - idle followers may be pulled to a
			// warm spot by the weather idle, which is theirs to do).
			const TMap<FString, FString> Settled = DumpWorld(World);
			TArray<FString> Drift;
			const FString LeaderPosKey = TEXT("op.") + State.CommanderName + TEXT(".pos");
			for (const TCHAR* Key : { TEXT(".in_cover"), TEXT(".cover_slot"), TEXT(".defense"), TEXT(".kia"), TEXT(".alive"), TEXT(".stance"), TEXT(".pos") })
			{
				for (const TPair<FString, FString>& Pair : State.DumpB)
				{
					if (Pair.Key.EndsWith(TEXT(".pos")) && Pair.Key != LeaderPosKey)
					{
						continue;
					}
					if (Pair.Key.StartsWith(TEXT("op.")) && Pair.Key.EndsWith(Key) && (!Settled.Contains(Pair.Key) || !Same(Pair.Key, Pair.Value, Settled[Pair.Key])))
					{
						Drift.Add(FString::Printf(TEXT("%s: %s -> %s"), *Pair.Key, *Pair.Value, Settled.Contains(Pair.Key) ? *Settled[Pair.Key] : TEXT("<missing>")));
					}
				}
			}
			for (const FString& Line : Drift)
			{
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke drift [1 s after the apply] %s"), *Line);
			}
			Check(State, Drift.IsEmpty(), FString::Printf(TEXT("the restored squad holds for a second (%d drifts)"), Drift.Num()));
			State.Stage = 3;
			State.StageTime = 0.f;
			return true;
		}
		case 3:
		{
			// Robustness in place: a version-1 file with empty sections applies its defaults without a crash; the broken ones refuse.
			WriteFile(TEXT("oldver.json"), TEXT("{ \"version\": 1, \"squad\": [], \"game_state\": {}, \"quest_state\": {}, \"world_state\": { \"crates\": {} } }"));
			Check(State, Saves->LoadGame(TEXT("oldver")), TEXT("a sparse version-1 file loads in place (defaults, no crash)"));
			Check(State, !Saves->LoadGame(TEXT("corrupt")) && !Saves->LoadGame(TEXT("foreign")) && !Saves->LoadGame(TEXT("nosuchslot")), TEXT("broken / missing files refuse in place"));
			return Finish(State, true);
		}
		default:
			return Finish(State, false);
		}
	}

	void Run(bool bLake, UWorld* World)
	{
		if (IConsoleVariable* Headless = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Save.HeadlessCheckpoints")))
		{
			Headless->Set(1, ECVF_SetByCode); // the lake's tutorial may write its checkpoint (the CVar outlives the travel)
		}
		TSharedRef<FState> State = MakeShared<FState>();
		State->bLake = bLake;
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float)
		{
			return Step(State);
		}), StepSeconds);
	}

	void RunPatrol(const TArray<FString>& Args, UWorld* World)
	{
		Run(false, World);
	}

	void RunLake(const TArray<FString>& Args, UWorld* World)
	{
		Run(true, World);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.SaveCoverageSmoke"),
		TEXT("Dev check (L_PatrolTest): live-world state dump before F5 and right after F9 reopened the level, diffed; the audit's gaps checked; broken save files refused; PASS / FAIL."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunPatrol));

	static FAutoConsoleCommandWithWorldAndArgs LakeCommand(
		TEXT("CodexTactics.SaveCoverageLakeSmoke"),
		TEXT("Dev check (lake map + tutorial): the SaveCoverageSmoke dump / diff with the tutorial running; PASS / FAIL."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunLake));
}

#endif // !UE_BUILD_SHIPPING
