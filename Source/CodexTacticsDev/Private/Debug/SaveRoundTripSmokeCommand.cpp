// Dev-only headless round trip of the save system (save policy + format version 2, user decision 2026-10-08) on
// L_MovementTest (runtime patrols like PatrolSmoke; nothing is saved to the map):
//   Scripts/smoke.ps1 -Command CodexTactics.SaveRoundTripSmoke -Log Smoke-SaveRoundTrip.log
// Slots go to Saved/SmokeSaves (exclusive in Scripts/test_map.json). A non-trivial out-of-combat world is built: squad
// hurt / cold / moved / crouched / prone, ammo per weapon, items, bonus item, extra ammo, postures (squad + own), guard;
// 10 rounds stored in a crate, a pile dropped, a barrel pushed 3 m and lit, a squad mine and a tripwire placed, a
// turret and a barricade damaged (turret unpowered), the generator damaged and the blast gate's creep at 37 % (2026-10-11),
// canister + diesel quest steps (the canister hides), the level's
// dialogue trigger played and a note read; one level enemy killed, one hurt; a marksman + escort hound walking a runtime
// route mid-way, a frostbitten searching on a second route. Then:
//  1. SNAPSHOT (the save data of the live world, flattened), save, mutate everything (heal, move, items, posture, crate,
//     pile gone, barrel out, mine / tripwire removed, quest reset, canister shown, note unread, trigger reset, patrol
//     reset, the hound killed, an extra enemy spawned), load in place, SNAPSHOT again: every field must match (each
//     mismatch is listed) plus direct checks of the world.
//  2. Save policy: cutscene and fight refuse F5 / dialog saves (CanSaveNow hint "Saving is disabled during combat",
//     which the frontend pause screen shows), also in the tactical pause; rev 5f (user decision 2026-10-14): NOTHING is saved on
//     its own - the wave start, the pause and the wave cleared write no file (no autosave); F5 after the wave resumes the next
//     wave's preparation; a legacy "autosave" file on disk is hidden from the list / Continue / F9; F9 picks the newest of the
//     quicksave and the checkpoint (a newer manual save does not count).
//     Tutorial progress (Demo 01): completed / skipped / shown steps and the beat survive the round trip.
//  3. Load with travel ("Continue"): the map is reopened, the save applied to the fresh world and its SNAPSHOT diffed
//     against the first one (runtime routes / enemies / mine / tripwire / turret recreated, killed level enemies stay dead).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AI/PatrolRouteActor.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/MarksmanEnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Components/SplineComponent.h"
#include "Containers/Ticker.h"
#include "Core/SaveGameSubsystem.h"
#include "Data/WeaponDataAsset.h"
#include "Debug/SmokeUtils.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/BarrelActor.h"
#include "Combat/FirePuddleActor.h"
#include "Combat/SignalFlareActor.h"
#include "Interactables/CampfireActor.h"
#include "Interactables/HeatSourceComponent.h"
#include "Interactables/PortableStoveActor.h"
#include "Survival/ColdSurvivalComponent.h"
#include "Interactables/BarricadeActor.h"
#include "Interactables/DeployableActor.h"
#include "Interactables/DroppedItemActor.h"
#include "Interactables/GateActor.h"
#include "Interactables/InteractableActor.h"
#include "Interactables/ItemStashComponent.h"
#include "Interactables/LootCrateActor.h"
#include "Interactables/NarrativeElementActor.h"
#include "Interactables/ProximityMineActor.h"
#include "Interactables/RelocationSubsystem.h"
#include "Interactables/TripwireActor.h"
#include "Interactables/TurretActor.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Quests/DialogueTriggerVolume.h"
#include "Quests/QuestSubsystem.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "TimerManager.h"
#include "UI/DialogueSubsystem.h"
#include "Core/PlayerCommands.h"
#include "Tutorial/TutorialRules.h"
#include "Tutorial/TutorialSubsystem.h"
#include "UObject/UObjectGlobals.h"

namespace SaveRoundTripSmoke
{
	constexpr float StepSeconds = 0.25f;
	const TCHAR* Slot = TEXT("roundtrip");

	/** Rev 2026-10-19: the per-instance patrol speed a designer set (the class default is 190). */
	constexpr float DesignerPatrolSpeed = 65.f;

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		TMap<FString, FString> SnapshotA;
		/** The Medic-Sapper's body before the save (whatever the designer's roster gives her; 2026-10-14 the roster is a BP). */
		FString MedicBodyBefore;
		TMap<FString, FString> SnapshotTravel;
		bool bTravelApplied = false;
		FDelegateHandle AppliedHandle;
		TWeakObjectPtr<AMarksmanEnemyCharacter> Marksman;
		TWeakObjectPtr<AEnemyCharacter> Hound;
		TWeakObjectPtr<AEnemyCharacter> Searcher;
		TWeakObjectPtr<APatrolRouteActor> RouteA;
		/** Rev 2026-10-19: patrol speed checks after the reopened level. */
		bool bPatrolSpeedWrong = false;
		float SearchEndedAt = -1.f;
		TWeakObjectPtr<APatrolRouteActor> RouteB;
		FString KilledLevelEnemy;
		FString HurtLevelEnemy;
		FString BarrelName;
		FString CanisterName;
		FString NoteName;
		FString TriggerName;
		FString CrateName;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State, bool bComplete)
	{
		USaveGameSubsystem::OnSaveApplied().Remove(State.AppliedHandle);
		IFileManager::Get().DeleteDirectory(*(FPaths::ProjectSavedDir() / TEXT("SmokeSaves")), false, true);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("SaveRoundTripSmoke"));
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

	// --- Snapshot: the save data flattened to "path = value" (metadata left out) ---

	void Flatten(const TSharedPtr<FJsonValue>& Value, const FString& Path, TMap<FString, FString>& Out)
	{
		if (!Value.IsValid())
		{
			Out.Add(Path, TEXT("null"));
			return;
		}
		switch (Value->Type)
		{
		case EJson::Object:
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Value->AsObject()->Values)
			{
				Flatten(Field.Value, Path + TEXT(".") + Field.Key, Out);
			}
			return;
		case EJson::Array:
		{
			const TArray<TSharedPtr<FJsonValue>>& Items = Value->AsArray();
			for (int32 Index = 0; Index < Items.Num(); ++Index)
			{
				// Objects with a name are keyed by it (order-independent): squad members, enemies.
				FString Key = FString::FromInt(Index);
				if (Items[Index].IsValid() && Items[Index]->Type == EJson::Object)
				{
					const TSharedPtr<FJsonObject> Object = Items[Index]->AsObject();
					FString Name;
					if (Object->TryGetStringField(TEXT("character_name"), Name) || Object->TryGetStringField(TEXT("name"), Name))
					{
						Key = Name;
					}
				}
				Flatten(Items[Index], FString::Printf(TEXT("%s[%s]"), *Path, *Key), Out);
			}
			Out.Add(Path + TEXT(".#"), FString::FromInt(Items.Num()));
			return;
		}
		case EJson::Number:
			Out.Add(Path, FString::Printf(TEXT("%.3f"), Value->AsNumber()));
			return;
		case EJson::Boolean:
			Out.Add(Path, Value->AsBool() ? TEXT("true") : TEXT("false"));
			return;
		default:
			Out.Add(Path, Value->AsString());
			return;
		}
	}

	TMap<FString, FString> Snapshot(const USaveGameSubsystem& Saves)
	{
		const TSharedRef<FJsonObject> Data = Saves.CaptureWorldState();
		TMap<FString, FString> Out;
		for (const TCHAR* Section : { TEXT("squad"), TEXT("game_state"), TEXT("quest_state"), TEXT("world_state"), TEXT("susanin") })
		{
			if (const TSharedPtr<FJsonValue> Value = Data->TryGetField(Section))
			{
				Flatten(Value, Section, Out);
			}
		}
		return Out;
	}

	/** Every differing field (numbers within 0.5 for positions, 0.01 otherwise). */
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
			if (!A || !B)
			{
				Mismatches.Add(FString::Printf(TEXT("%s: %s -> %s"), *Key, A ? **A : TEXT("<missing>"), B ? **B : TEXT("<missing>")));
				continue;
			}
			if (*A == *B)
			{
				continue;
			}
			if (A->IsNumeric() && B->IsNumeric())
			{
				const bool bPosition = Key.Contains(TEXT("pos")) || Key.Contains(TEXT("location")) || Key.Contains(TEXT("anchor"))
					|| Key.Contains(TEXT("points")) || Key.Contains(TEXT("origin")) || Key.Contains(TEXT("rot"));
				if (FMath::Abs(FCString::Atod(**A) - FCString::Atod(**B)) <= (bPosition ? 0.5 : 0.01))
				{
					continue;
				}
			}
			Mismatches.Add(FString::Printf(TEXT("%s: %s -> %s"), *Key, **A, **B));
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
		Check(State, Mismatches.IsEmpty() && Before.Num() > 50, FString::Printf(TEXT("%s: %d fields compared, %d mismatches"), *What, Before.Num(), Mismatches.Num()));
	}

	TSharedPtr<FJsonObject> ReadSaveFile(const USaveGameSubsystem& Saves, const FString& SlotName)
	{
		FString Json;
		TSharedPtr<FJsonObject> Data;
		if (FFileHelper::LoadFileToString(Json, *(Saves.GetSaveDirectory() / SlotName + TEXT(".json"))))
		{
			FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Data);
		}
		return Data;
	}

	FString SavedPhase(const TSharedPtr<FJsonObject>& Data)
	{
		const TSharedPtr<FJsonObject>* GameState = nullptr;
		FString Phase;
		if (Data.IsValid() && Data->TryGetObjectField(TEXT("game_state"), GameState))
		{
			(*GameState)->TryGetStringField(TEXT("phase"), Phase);
		}
		return Phase;
	}

	template <typename T>
	T* FindNamed(UWorld* World, const FString& Name)
	{
		for (TActorIterator<T> It(World); It; ++It)
		{
			if (It->GetName() == Name && !It->IsActorBeingDestroyed())
			{
				return *It;
			}
		}
		return nullptr;
	}

	AOperativeCharacter* FindRole(USquadSubsystem* Squad, EOperativeRole Role)
	{
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			if (Member->SquadRole == Role)
			{
				return Member;
			}
		}
		return nullptr;
	}

	FEnemyPerceptionParams Senseless()
	{
		FEnemyPerceptionParams Params;
		Params.SightRangeCm = 0.f;
		Params.ProximityCm = 0.f;
		Params.HearWalkCm = Params.HearRunCm = Params.HearCrouchWalkCm = Params.HearCrawlCm = 0.f;
		Params.HearGunshotCm = Params.HearExplosionCm = 0.f;
		Params.SmellRadiusCm = 0.f;
		return Params;
	}

	/**
	 * A search origin for the frostbitten 3 m from where it stands, on the side AWAY from the nearest deployable (fix 2026-10-19: its
	 * 12 m search sweep could reach the smoke's barricade, whose contact damage restarted the search every second - the rule is fixed
	 * too (StealthLureRules::SelfNoiseCm) - and the enemy died on it before the "back on the route" check).
	 */
	void StartSearchAwayFromDeployables(UWorld* World, AEnemyCharacter& Searcher)
	{
		FVector Away = FVector::ForwardVector;
		float Nearest = TNumericLimits<float>::Max();
		for (TActorIterator<ADeployableActor> It(World); It; ++It)
		{
			const float Distance = FVector::Dist2D(It->GetActorLocation(), Searcher.GetActorLocation());
			if (!It->IsActorBeingDestroyed() && Distance < Nearest)
			{
				Nearest = Distance;
				Away = (Searcher.GetActorLocation() - It->GetActorLocation()).GetSafeNormal2D(UE_SMALL_NUMBER, FVector::ForwardVector);
			}
		}
		// A tight 3 m sweep (the default 12 m one could still reach a deployable placed by the map's ClearPoint fall-backs).
		FPatrolSearchSpec Spec;
		Spec.SweepRadiusCm = 300.f;
		Searcher.StartPatrolSearchWith(Searcher.GetActorLocation() + Away * 300.f, Spec);
	}

	APatrolRouteActor* SpawnRoute(UWorld* World, const TArray<FVector>& Points, float Wait)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		APatrolRouteActor* Route = World->SpawnActor<APatrolRouteActor>(Points[0], FRotator::ZeroRotator, Params);
		if (USplineComponent* Spline = Route ? Route->GetRouteSpline() : nullptr)
		{
			Spline->ClearSplinePoints(false);
			for (const FVector& Point : Points)
			{
				Spline->AddSplinePoint(Point, ESplineCoordinateSpace::World, false);
			}
			Spline->UpdateSpline();
			Route->bIsLoop = true;
			Route->DefaultWaitTimeSeconds = Wait;
		}
		return Route;
	}

	// --- Stage 0: the non-trivial out-of-combat world ---

	bool BuildWorld(UWorld* World, FState& State)
	{
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		USaveGameSubsystem* Saves = World->GetSubsystem<USaveGameSubsystem>();
		UQuestSubsystem* Quests = World->GetSubsystem<UQuestSubsystem>();
		URelocationSubsystem* Relocation = World->GetSubsystem<URelocationSubsystem>();
		UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>();
		AOperativeCharacter* Commander = Squad ? FindRole(Squad, EOperativeRole::Commander) : nullptr;
		AOperativeCharacter* Engineer = Squad ? FindRole(Squad, EOperativeRole::Engineer) : nullptr;
		AOperativeCharacter* Medic = Squad ? FindRole(Squad, EOperativeRole::MedicSapper) : nullptr;
		if (!Commander || !Engineer || !Medic || !Saves || !Quests || !Relocation || !Waves)
		{
			Check(State, false, TEXT("squad of three, saves, quests, relocation, waves"));
			return false;
		}
		Saves->SaveDirectoryOverride = FPaths::ProjectSavedDir() / TEXT("SmokeSaves");
		IFileManager::Get().DeleteDirectory(*Saves->SaveDirectoryOverride, false, true);
		Saves->bCheckpointsEnabled = true;

		const FVector F = Commander->GetActorForwardVector().GetSafeNormal2D();
		const FVector R = FVector::CrossProduct(FVector::UpVector, F);
		const FVector Feet = Commander->GetActorLocation() - FVector(0.f, 0.f, Commander->GetSimpleCollisionHalfHeight());

		// Squad.
		Commander->HealthComponent->ApplyDirectHealthLoss(37.f, TEXT("Smoke"));
		Commander->ColdLevel = 22.f;
		Commander->MedkitsCount = 0;
		Commander->GrenadesCount = 1;
		Commander->MatchesCount = 3;
		Commander->BonusItems.Add(TEXT("shotgun"));
		Commander->ExtraAmmo.Add(TEXT("shotgun"), 6);
		Commander->Level = 2;
		Commander->CurrentExp = 40;
		Commander->SwitchToWeaponById(TEXT("pistol"));
		Commander->CurrentClip = 4;
		Commander->TeleportTo(SmokeUtils::ClearPoint(World, Feet, Feet + R * 250.f) + FVector(0.f, 0.f, 100.f), FRotator(0.f, 33.f, 0.f), false, true);
		Engineer->SetStance(EOperativeStance::Crouching);
		Engineer->ColdLevel = 41.f;
		Engineer->CannedFoodCount = 0;
		Medic->SetStance(EOperativeStance::Prone);
		Medic->HealthComponent->ApplyDirectHealthLoss(12.f, TEXT("Smoke"));
		// User request 2026-10-09: her sniper rifle (in hands, 3 rounds left) must survive the round trip.
		Medic->SwitchToWeaponById(TEXT("sniper_rifle"));
		Medic->CurrentClip = 3;
		Squad->SetSquadPosture(ESquadFirePosture::Passive);
		Engineer->bHasPostureOverride = true;
		Engineer->PostureOverride = ESquadFirePosture::Defensive;
		Squad->ToggleGuard(Medic);

		// Crate: 10 rounds stored; a pile on the ground.
		ALootCrateActor* Crate = nullptr;
		for (TActorIterator<ALootCrateActor> It(World); It && !Crate; ++It)
		{
			Crate = !It->IsDestroyed() ? *It : nullptr;
		}
		if (Crate)
		{
			Crate->GetSyncedStash()->Add(ETransferItem::RifleAmmo, 10, true);
			State.CrateName = Crate->GetName();
		}
		ADroppedItemActor::SpawnOrMerge(World, SmokeUtils::ClearPoint(World, Feet, Feet - R * 300.f), ETransferItem::RifleAmmo, 15);

		// A barrel pushed 3 m and lit.
		ABarrelActor* Barrel = nullptr;
		for (TActorIterator<ABarrelActor> It(World); It && !Barrel; ++It)
		{
			Barrel = !It->HasBeenLit() ? *It : nullptr;
		}
		if (Barrel)
		{
			Barrel->SetActorLocation(Barrel->GetActorLocation() + Barrel->GetActorForwardVector() * 300.f);
			Barrel->Ignite(Commander);
			State.BarrelName = Barrel->GetName();
		}

		// Deployables: a squad mine, a tripwire, a damaged unpowered turret, a damaged barricade.
		auto SpawnDeployable = [World, Relocation](EDeployableType Type, const FVector& Point) -> ADeployableActor*
		{
			const TSubclassOf<ADeployableActor> Class = Relocation->GetDeployableClass(Type);
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			return Class ? World->SpawnActor<ADeployableActor>(Class, Point + FVector(0.f, 0.f, 30.f), FRotator(0.f, 45.f, 0.f), Params) : nullptr;
		};
		AProximityMineActor* Mine = Cast<AProximityMineActor>(SpawnDeployable(EDeployableType::Mine, SmokeUtils::ClearPoint(World, Feet, Feet + F * 900.f + R * 600.f)));
		if (Mine)
		{
			Mine->SetPlacedBySquad();
		}
		ATurretActor* Turret = Cast<ATurretActor>(SpawnDeployable(EDeployableType::Turret, SmokeUtils::ClearPoint(World, Feet, Feet - F * 500.f)));
		if (Turret)
		{
			Turret->Health->ApplyDirectHealthLoss(30.f, TEXT("Smoke"));
			Turret->SetPowered(false);
		}
		// The barricade (its contact damage hurts any enemy within 2.2 m every second) at least 15 m from the frostbitten's route B
		// (fix 2026-10-19: the map's ClearPoint fall-backs once put it inside that enemy's search sweep, and it died on it after the load).
		const FVector RouteBStart = SmokeUtils::ClearPoint(World, Feet, Feet + F * 1400.f - R * 900.f);
		const FVector RouteBEnd = SmokeUtils::ClearPoint(World, RouteBStart, RouteBStart + F * 600.f);
		FVector BarricadeSpot = SmokeUtils::ClearPoint(World, Feet, Feet - F * 500.f + R * 400.f);
		for (const FVector& Offset : { -F * 500.f + R * 400.f, -F * 500.f - R * 400.f, -F * 900.f + R * 800.f, F * 500.f + R * 900.f, -F * 1200.f })
		{
			const FVector Candidate = SmokeUtils::ClearPoint(World, Feet, Feet + Offset);
			if (FVector::Dist2D(Candidate, RouteBStart) >= 1500.f && FVector::Dist2D(Candidate, RouteBEnd) >= 1500.f)
			{
				BarricadeSpot = Candidate;
				break;
			}
		}
		ABarricadeActor* Barricade = Cast<ABarricadeActor>(SpawnDeployable(EDeployableType::Barricade, BarricadeSpot));
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke note: barricade %.1f / %.1f m from route B's ends"), FVector::Dist2D(BarricadeSpot, RouteBStart) / 100.f,
			FVector::Dist2D(BarricadeSpot, RouteBEnd) / 100.f);
		if (Barricade)
		{
			Barricade->Health->ApplyDirectHealthLoss(25.f, TEXT("Smoke"));
		}
		FActorSpawnParameters WireParams;
		WireParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		const FVector WireA = SmokeUtils::ClearPoint(World, Feet, Feet - F * 800.f - R * 200.f);
		const FVector WireB = WireA + R * 300.f;
		ATripwireActor* Wire = World->SpawnActor<ATripwireActor>((WireA + WireB) * 0.5f, FRotator::ZeroRotator, WireParams);
		if (Wire)
		{
			Wire->Setup(WireA, WireB, false, false, false);
		}
		Check(State, Mine && Turret && Barricade && Wire && Barrel && Crate, TEXT("mine, turret, barricade, tripwire placed; barrel lit; crate filled"));

		// Sprint 15 heat: items, the heat buffer, the stove's gas; a campfire burning, a stove running, a flare, a fire puddle
		// (behind the squad, away from the patrol routes; timers long enough for the whole check, no noise).
		Commander->HeatPacksCount = 2;
		Commander->FirewoodCount = 3;
		Commander->FlaresCount = 1;
		Commander->ColdSurvival->RestoreHeatBuffer(20.f);
		Engineer->StoveGasSeconds = 123.f;
		ACampfireActor* Campfire = Cast<ACampfireActor>(SpawnDeployable(EDeployableType::Campfire, SmokeUtils::ClearPoint(World, Feet, Feet - F * 1000.f)));
		if (Campfire)
		{
			Campfire->RestoreHeatState(true, false, 300.f, 18.75f);
		}
		APortableStoveActor* Stove = Cast<APortableStoveActor>(SpawnDeployable(EDeployableType::PortableStove,
			SmokeUtils::ClearPoint(World, Feet, Feet - F * 1000.f + R * 500.f)));
		if (Stove)
		{
			Stove->StartWithGas(250.f);
		}
		ASignalFlareActor* Flare = ASignalFlareActor::SpawnLit(World, SmokeUtils::ClearPoint(World, Feet, Feet - F * 1300.f - R * 400.f));
		if (Flare)
		{
			Flare->RestoreHeatState(true, false, 300.f, 20.f);
		}
		AFirePuddleActor* Puddle = AFirePuddleActor::SpawnAt(World, SmokeUtils::ClearPoint(World, Feet, Feet - F * 1600.f + R * 300.f), TEXT("Smoke"));
		if (Puddle)
		{
			Puddle->LightPuddle(300.f);
		}
		Check(State, Campfire && Stove && Flare && Puddle, TEXT("campfire, stove, flare and fire puddle burning"));

		// Quest: the canister picked up (it hides) and filled at the APC.
		for (TActorIterator<AInteractableActor> It(World); It; ++It)
		{
			if (It->ObjectType == EInteractableType::Canister && !It->IsA<ADeployableActor>() && State.CanisterName.IsEmpty())
			{
				State.CanisterName = It->GetName();
				Quests->InteractWith(EInteractableType::Canister, *It);
			}
		}
		Quests->InteractWith(EInteractableType::Vehicle, nullptr);
		Check(State, Quests->HasFuelCanister() && !State.CanisterName.IsEmpty(), TEXT("canister found and filled"));

		// The blast gate paced by the generator and the waves (2026-10-11): its creep and the generator's damage.
		AGateActor* Gate = AGateActor::FindLevelGate(World);
		AInteractableActor* Generator = nullptr;
		for (TActorIterator<AInteractableActor> It(World); It && !Generator; ++It)
		{
			Generator = It->ObjectType == EInteractableType::Generator ? *It : nullptr;
		}
		if (Gate && Generator)
		{
			Gate->RestoreProgress(false, 0.37f, false);
			Generator->TakeGeneratorDamage(60.f);
		}
		Check(State, Gate && Generator && FMath::IsNearlyEqual(Gate->GetOpenProgress(), 0.37f) && Generator->GeneratorHealth == 140.f,
			TEXT("blast gate creep 37 %, generator at 140 HP"));

		// Narrative: the dialogue trigger played, a note read.
		for (TActorIterator<ADialogueTriggerVolume> It(World); It && State.TriggerName.IsEmpty(); ++It)
		{
			if (It->TryTrigger(Commander))
			{
				State.TriggerName = It->GetName();
			}
		}
		if (UDialogueSubsystem* Dialogue = World->GetSubsystem<UDialogueSubsystem>(); Dialogue && Dialogue->IsDialogueOpen())
		{
			Dialogue->SkipDialogue();
		}
		for (TActorIterator<ANarrativeElementActor> It(World); It && State.NoteName.IsEmpty(); ++It)
		{
			It->PerformAction(Commander);
			State.NoteName = It->GetName();
		}
		Check(State, !State.TriggerName.IsEmpty() && !State.NoteName.IsEmpty(), TEXT("dialogue trigger played, note read"));

		// Level enemies: one killed (stays dead), one hurt; the rest removed (they must not interfere with the patrols).
		TArray<AEnemyCharacter*> LevelEnemies;
		for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
		{
			LevelEnemies.Add(*It);
		}
		for (int32 Index = 0; Index < LevelEnemies.Num(); ++Index)
		{
			if (Index == 0)
			{
				State.HurtLevelEnemy = LevelEnemies[Index]->GetName();
				LevelEnemies[Index]->GetHealthComponent()->ApplyDirectHealthLoss(LevelEnemies[Index]->GetHealthComponent()->GetMaxHealth() * 0.4f, TEXT("Smoke"));
				LevelEnemies[Index]->bOverridePerception = true;
				LevelEnemies[Index]->PerceptionOverride = Senseless();
				continue;
			}
			if (Index == 1)
			{
				State.KilledLevelEnemy = LevelEnemies[Index]->GetName();
			}
			LevelEnemies[Index]->Destroy();
		}

		// Patrols: marksman + escort hound on route A, a frostbitten on route B (searching).
		const FVector A = SmokeUtils::ClearPoint(World, Feet, Feet + F * 1400.f);
		const FVector B = SmokeUtils::ClearPoint(World, A, A + R * 700.f);
		const FVector C = SmokeUtils::ClearPoint(World, B, B + F * 600.f);
		APatrolRouteActor* RouteA = SpawnRoute(World, { A, B, C }, 0.5f);
		const FVector D = SmokeUtils::ClearPoint(World, Feet, Feet + F * 1400.f - R * 900.f);
		const FVector E = SmokeUtils::ClearPoint(World, D, D + F * 600.f);
		APatrolRouteActor* RouteB = SpawnRoute(World, { D, E }, 2.f);
		AMarksmanEnemyCharacter* Marksman = Cast<AMarksmanEnemyCharacter>(Waves->SpawnEnemy(EEnemyArchetype::Marksman, A + FVector(0.f, 0.f, 100.f), R.Rotation()));
		AEnemyCharacter* Hound = Waves->SpawnEnemy(EEnemyArchetype::FrostHound, A - F * 300.f + FVector(0.f, 0.f, 80.f), R.Rotation());
		AEnemyCharacter* Searcher = Waves->SpawnEnemy(EEnemyArchetype::Frostbitten, D + FVector(0.f, 0.f, 100.f), F.Rotation());
		if (!RouteA || !RouteB || !Marksman || !Hound || !Searcher)
		{
			Check(State, false, TEXT("routes and patrol enemies spawned"));
			return false;
		}
		Marksman->MarksmanConfig.DetectionRange = 100.f;
		for (AEnemyCharacter* Enemy : { static_cast<AEnemyCharacter*>(Marksman), Hound, Searcher })
		{
			Enemy->bOverridePerception = true;
			Enemy->PerceptionOverride = Senseless();
		}
		// Rev 2026-10-19 (user: the designer's patrol speed 65 came back as 190 after a load): a per-instance patrol speed.
		for (AEnemyCharacter* Enemy : { static_cast<AEnemyCharacter*>(Marksman), Hound, Searcher })
		{
			Enemy->PatrolWalkSpeed = DesignerPatrolSpeed;
		}
		Marksman->StartPatrol(RouteA, nullptr);
		Hound->StartPatrol(nullptr, Marksman);
		Searcher->StartPatrol(RouteB, nullptr);
		Searcher->GetHealthComponent()->ApplyDirectHealthLoss(10.f, TEXT("Smoke"));
		State.Marksman = Marksman;
		State.Hound = Hound;
		State.Searcher = Searcher;
		State.RouteA = RouteA;
		State.RouteB = RouteB;

		// Demo 01 tutorial progress: one step completed, one skipped, one shown, the beat (saved in "tutorial").
		if (UTutorialSubsystem* Tutorial = World->GetSubsystem<UTutorialSubsystem>())
		{
			FTutorialStepsData Data;
			TArray<FString> Warnings;
			FString Error;
			TutorialRules::ParseSteps(TEXT(R"({"start_beat":"T0","beats":[{"id":"T0","checkpoint":false}],"steps":[
				{"id":"rt_done","beat":"T0","complete":{"commands":["Squad.Guard"]}},
				{"id":"rt_skip","beat":"T0","trigger":{"type":"after_step","step":"rt_done"},"complete":{"commands":["Squad.Solo"]}},
				{"id":"rt_obj","beat":"T0","kind":"objective","complete":{"event":"Never"}}]})"), Data, Warnings, Error);
			Tutorial->LoadSteps(Data);
			Tutorial->HandlePlayerCommand(PlayerCommands::Guard, FPlayerCommandContext());
			Tutorial->SkipStep(TEXT("rt_skip"));
		}
		return true;
	}

	// --- Stage 1: snapshot, save, mutate, load, snapshot, diff ---

	void InPlaceRoundTrip(UWorld* World, FState& State)
	{
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		USaveGameSubsystem* Saves = World->GetSubsystem<USaveGameSubsystem>();
		UQuestSubsystem* Quests = World->GetSubsystem<UQuestSubsystem>();
		UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>();
		AOperativeCharacter* Commander = FindRole(Squad, EOperativeRole::Commander);
		AOperativeCharacter* Engineer = FindRole(Squad, EOperativeRole::Engineer);
		AOperativeCharacter* Medic = FindRole(Squad, EOperativeRole::MedicSapper);
		AMarksmanEnemyCharacter* Marksman = State.Marksman.Get();
		const int32 MarksmanWaypoint = Marksman ? Marksman->GetPatrolWaypointIndex() : -1;

		State.SnapshotA = Snapshot(*Saves);
		State.MedicBodyBefore = Medic && Medic->GetMesh()->GetSkeletalMeshAsset() ? Medic->GetMesh()->GetSkeletalMeshAsset()->GetName() : FString();
		FText Hint;
		Check(State, Saves->CanSaveNow(&Hint) && Hint.IsEmpty(), TEXT("exploration: saving allowed"));
		Check(State, Saves->SaveToSlotWithMessage(Slot) && Saves->HasSave(Slot), TEXT("saved \"roundtrip\""));
		FSaveSlotInfo Info;
		const bool bInfo = Saves->GetSaveInfo(Slot, Info);
		Check(State, bInfo && Info.Version == SaveGameRules::CurrentSaveVersion && Info.MapName.EndsWith(TEXT("L_MovementTest")),
			FString::Printf(TEXT("save info: version %d, map %s"), Info.Version, *Info.MapName));

		// Mutate everything.
		UTutorialSubsystem* Tutorial = World->GetSubsystem<UTutorialSubsystem>();
		if (Tutorial)
		{
			Tutorial->ResetTutorial();
		}
		Commander->HealthComponent->Heal(1000.f);
		Commander->ColdLevel = 0.f;
		Commander->MedkitsCount = 5;
		Commander->GrenadesCount = 4;
		Commander->BonusItems.Reset();
		Commander->ExtraAmmo.Reset();
		Commander->Level = 1;
		Commander->SwitchToWeaponById(TEXT("m16"));
		Commander->CurrentClip = 30;
		Commander->TeleportTo(Commander->GetActorLocation() + FVector(600.f, 200.f, 0.f), FRotator::ZeroRotator, false, true);
		Engineer->SetStance(EOperativeStance::Standing);
		Engineer->bHasPostureOverride = false;
		Medic->SetStance(EOperativeStance::Standing);
		Medic->SwitchToWeaponById(TEXT("m16"));
		Squad->ToggleGuard(Medic);
		Squad->SetSquadPosture(ESquadFirePosture::Aggressive);
		if (ALootCrateActor* Crate = FindNamed<ALootCrateActor>(World, State.CrateName))
		{
			Crate->GetSyncedStash()->SetContents({});
		}
		for (TActorIterator<ADroppedItemActor> It(World); It; ++It)
		{
			It->Destroy();
		}
		if (ABarrelActor* Barrel = FindNamed<ABarrelActor>(World, State.BarrelName))
		{
			Barrel->Extinguish();
			Barrel->SetActorLocation(Barrel->GetActorLocation() + FVector(0.f, 400.f, 0.f));
		}
		for (TActorIterator<AProximityMineActor> It(World); It; ++It)
		{
			if (It->bPlacedBySquad)
			{
				It->Destroy();
			}
		}
		for (TActorIterator<ATripwireActor> It(World); It; ++It)
		{
			It->Destroy();
		}
		// Sprint 15: heat items used up, the campfire out, the stove nearly empty, the flare / puddle gone.
		Commander->HeatPacksCount = 0;
		Commander->FirewoodCount = 0;
		Commander->FlaresCount = 3;
		Commander->ColdSurvival->RestoreHeatBuffer(0.f);
		Engineer->StoveGasSeconds = 0.f;
		for (TActorIterator<ACampfireActor> It(World); It; ++It)
		{
			It->RestoreHeatState(false, true, 0.f, 0.f);
		}
		for (TActorIterator<APortableStoveActor> It(World); It; ++It)
		{
			It->StartWithGas(5.f);
		}
		for (TActorIterator<ATacticalHeatActor> It(World); It; ++It)
		{
			It->Destroy();
		}
		for (TActorIterator<ATurretActor> It(World); It; ++It)
		{
			It->Repair();
			It->SetPowered(true);
		}
		Quests->RestoreState(FQuestChainState());
		if (AGateActor* Gate = AGateActor::FindLevelGate(World))
		{
			Gate->RestoreProgress(false, 0.f, false);
		}
		for (TActorIterator<AInteractableActor> It(World); It; ++It)
		{
			if (It->ObjectType == EInteractableType::Generator)
			{
				It->RestoreGeneratorState(It->GeneratorMaxHealth, false);
			}
		}
		if (AInteractableActor* Canister = FindNamed<AInteractableActor>(World, State.CanisterName))
		{
			Canister->SetActorHiddenInGame(false);
			Canister->SetActorEnableCollision(true);
		}
		if (ADialogueTriggerVolume* Trigger = FindNamed<ADialogueTriggerVolume>(World, State.TriggerName))
		{
			Trigger->ResetTrigger();
		}
		if (ANarrativeElementActor* Note = FindNamed<ANarrativeElementActor>(World, State.NoteName))
		{
			Note->bHasBeenRead = false;
		}
		if (Marksman && State.RouteA.IsValid())
		{
			Marksman->TeleportTo(State.RouteA->GetWaypointWorldLocation(0) + FVector(0.f, 0.f, 100.f), FRotator::ZeroRotator, false, true);
			Marksman->StartPatrol(State.RouteA.Get(), nullptr);
		}
		if (AEnemyCharacter* Hound = State.Hound.Get())
		{
			Hound->Destroy(); // killed after the save: it comes back
		}
		if (AEnemyCharacter* Searcher = State.Searcher.Get())
		{
			Searcher->GetHealthComponent()->Heal(1000.f);
		}
		AEnemyCharacter* Extra = Waves->SpawnEnemy(EEnemyArchetype::FrostHound, Commander->GetActorLocation() + FVector(1500.f, 0.f, 0.f));
		const FString ExtraName = Extra ? Extra->GetName() : FString();
		const TMap<FString, FString> Mutated = Snapshot(*Saves);
		Check(State, Diff(State.SnapshotA, Mutated).Num() > 20, FString::Printf(TEXT("mutated world differs (%d fields)"), Diff(State.SnapshotA, Mutated).Num()));
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS); // frees the names of the destroyed actors (recreated under the same name)

		Check(State, Saves->LoadFromSlotWithMessage(Slot), TEXT("loaded \"roundtrip\" in place"));
		const TMap<FString, FString> SnapshotB = Snapshot(*Saves);
		ReportDiff(State, State.SnapshotA, SnapshotB, TEXT("in-place load"));

		// Direct checks of the world.
		Check(State, Tutorial && Tutorial->GetStepState(TEXT("rt_done")) == ETutorialStepState::Completed
			&& Tutorial->GetStepState(TEXT("rt_skip")) == ETutorialStepState::Skipped && Tutorial->GetStepState(TEXT("rt_obj")) == ETutorialStepState::Active
			&& Tutorial->GetCurrentBeat() == FName(TEXT("T0")), TEXT("tutorial: completed / skipped steps, the shown objective and the beat restored"));
		Check(State, Medic->IsSniperWeaponEquipped() && Medic->CurrentClip == 3 && Medic->GetMesh()->GetSkeletalMeshAsset()
			&& Medic->GetMesh()->GetSkeletalMeshAsset()->GetName() == State.MedicBodyBefore,
			FString::Printf(TEXT("Medic-Sapper: same body (%s), sniper rifle back in hands [%d/%d]"), *State.MedicBodyBefore, Medic->CurrentClip, Medic->ReserveAmmo));
		const ABarrelActor* Barrel = FindNamed<ABarrelActor>(World, State.BarrelName);
		Check(State, Barrel && Barrel->IsBurning(), TEXT("barrel burns again at its pushed spot"));
		int32 SquadMines = 0;
		for (TActorIterator<AProximityMineActor> It(World); It; ++It)
		{
			SquadMines += It->bPlacedBySquad ? 1 : 0;
		}
		int32 Wires = 0;
		for (TActorIterator<ATripwireActor> It(World); It; ++It)
		{
			Wires += It->IsPreview() ? 0 : 1;
		}
		Check(State, SquadMines == 1 && Wires == 1, FString::Printf(TEXT("mine (%d) and tripwire (%d) back"), SquadMines, Wires));
		int32 Flares = 0;
		int32 Puddles = 0;
		for (TActorIterator<ATacticalHeatActor> It(World); It; ++It)
		{
			Flares += It->IsA<ASignalFlareActor>() && It->IsBurning() ? 1 : 0;
			Puddles += It->IsA<AFirePuddleActor>() && It->IsBurning() ? 1 : 0;
		}
		const ACampfireActor* Fire = nullptr;
		for (TActorIterator<ACampfireActor> It(World); It && !Fire; ++It)
		{
			Fire = *It;
		}
		const APortableStoveActor* Stove = nullptr;
		for (TActorIterator<APortableStoveActor> It(World); It && !Stove; ++It)
		{
			Stove = *It;
		}
		Check(State, Flares == 1 && Puddles == 1 && Fire && Fire->IsBurning() && Fire->HeatSource->IsHeatActive() && Stove && Stove->GetGasLeft() > 200.f
			&& Commander->HeatPacksCount == 2 && Engineer->StoveGasSeconds == 123.f && Commander->ColdSurvival->GetHeatBufferSecondsLeft() > 1.f,
			FString::Printf(TEXT("heat back: flares %d, puddles %d, campfire %d, stove gas %.0f, packs %d, engineer gas %.0f, buffer %.1f"), Flares, Puddles,
				Fire && Fire->IsBurning() ? 1 : 0, Stove ? Stove->GetGasLeft() : 0.f, Commander->HeatPacksCount, Engineer->StoveGasSeconds,
				Commander->ColdSurvival->GetHeatBufferSecondsLeft()));
		const AInteractableActor* Canister = FindNamed<AInteractableActor>(World, State.CanisterName);
		Check(State, Canister && Canister->IsHidden() && Quests->HasFuelCanister(), TEXT("canister hidden, fuel canister in the quest"));
		const AGateActor* Gate = AGateActor::FindLevelGate(World);
		float GeneratorHealth = -1.f;
		for (TActorIterator<AInteractableActor> It(World); It; ++It)
		{
			GeneratorHealth = It->ObjectType == EInteractableType::Generator ? It->GeneratorHealth : GeneratorHealth;
		}
		Check(State, Gate && FMath::IsNearlyEqual(Gate->GetOpenProgress(), 0.37f) && !Gate->IsOpen() && GeneratorHealth == 140.f,
			FString::Printf(TEXT("blast gate creep back at %.2f, generator %.0f HP"), Gate ? Gate->GetOpenProgress() : -1.f, GeneratorHealth));
		const ADialogueTriggerVolume* Trigger = FindNamed<ADialogueTriggerVolume>(World, State.TriggerName);
		Check(State, Trigger && Trigger->HasTriggered(), TEXT("dialogue already played"));
		Marksman = State.Marksman.Get();
		Check(State, Marksman && Marksman->IsOnPatrol() && Marksman->GetPatrolWaypointIndex() == MarksmanWaypoint && MarksmanWaypoint >= 1,
			FString::Printf(TEXT("marksman back mid-route on waypoint %d"), MarksmanWaypoint));
		AEnemyCharacter* NewHound = nullptr;
		bool bExtraGone = true;
		for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
		{
			if (It->IsActorBeingDestroyed())
			{
				continue;
			}
			NewHound = It->GetEscortLeader() == Marksman ? *It : NewHound;
			bExtraGone &= It->GetName() != ExtraName || ExtraName.IsEmpty();
		}
		Check(State, NewHound && NewHound->IsOnPatrol(), TEXT("killed hound recreated, escorting the marksman"));
		Check(State, NewHound && FMath::IsNearlyEqual(NewHound->PatrolWalkSpeed, DesignerPatrolSpeed) && Marksman
			&& FMath::IsNearlyEqual(Marksman->PatrolWalkSpeed, DesignerPatrolSpeed),
			FString::Printf(TEXT("rev 2026-10-19: the recreated hound keeps the designer's patrol speed (%.0f), the marksman too (%.0f)"),
				NewHound ? NewHound->PatrolWalkSpeed : -1.f, Marksman ? Marksman->PatrolWalkSpeed : -1.f));
		Check(State, bExtraGone, TEXT("enemy spawned after the save removed"));
		const AEnemyCharacter* Searcher = State.Searcher.Get();
		Check(State, Searcher && Searcher->IsSearching(), TEXT("frostbitten still searching"));
		Check(State, Squad->GetEffectivePosture(Engineer) == ESquadFirePosture::Defensive && Squad->GetSquadPosture() == ESquadFirePosture::Passive,
			TEXT("postures back (squad Passive, engineer Defensive)"));
	}

	// --- Stage 2: save policy; rev 5f: no automatic saves ---

	/** Save files in the smoke's save directory. */
	int32 CountSaveFiles(const USaveGameSubsystem& Saves)
	{
		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *(Saves.GetSaveDirectory() / TEXT("*.json")), true, false);
		return Files.Num();
	}

	void PolicyAndAutosave(UWorld* World, FState& State)
	{
		USaveGameSubsystem* Saves = World->GetSubsystem<USaveGameSubsystem>();
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		if (!Flow)
		{
			Check(State, false, TEXT("game flow"));
			return;
		}
		Saves->DeleteSave(TEXT("quicksave"));
		FText Hint;
		// Sudden horde start (user decision 2026-10-11): no cutscene / preparation; nothing can be saved from then on. Rev 5f (user
		// decision 2026-10-14): the fight start writes NO save (the autosave before the wave is gone).
		Check(State, Saves->CanSaveNow(), TEXT("exploration: saving allowed"));
		const int32 WrittenBefore = Saves->GetWrittenSaveCount();
		const int32 FilesBefore = CountSaveFiles(*Saves);
		Check(State, Flow->TriggerCombatZone() == EGameFlowResult::Ok && Flow->GetPhase() == ECodexGamePhase::WaveCombat && Flow->IsAwaitingHordeIntro(),
			TEXT("wave 1 started suddenly (horde intro armed)"));
		Check(State, Saves->GetWrittenSaveCount() == WrittenBefore && CountSaveFiles(*Saves) == FilesBefore && !Saves->HasSave(TEXT("autosave")),
			TEXT("no autosave when the wave starts (rev 5f)"));
		Check(State, !Saves->CanSaveNow(&Hint) && Hint.ToString() == TEXT("Saving is disabled during combat"),
			FString::Printf(TEXT("sudden wave: saving refused (\"%s\")"), *Hint.ToString()));
		SmokeUtils::ReleaseHordeIntroPause(Flow);

		const bool bFightRefused = !Saves->CanSaveNow(&Hint);
		Check(State, bFightRefused && Hint.ToString() == TEXT("Saving is disabled during combat"), FString::Printf(TEXT("fight: \"%s\""), *Hint.ToString()));
		Check(State, !Saves->QuickSave() && !Saves->HasSave(TEXT("quicksave")), TEXT("F5 refused in the fight"));
		Check(State, !Saves->SaveToSlotWithMessage(TEXT("combat_try")) && !Saves->HasSave(TEXT("combat_try")), TEXT("dialog save refused in the fight"));
		if (Flow->ToggleTacticalPause() == EGameFlowResult::Ok)
		{
			Check(State, Flow->GetCombatMode() == ECodexCombatMode::TacticalPause && !Saves->CanSaveNow(), TEXT("tactical pause: saving refused"));
			Flow->ToggleTacticalPause();
		}
		Check(State, Saves->GetWrittenSaveCount() == WrittenBefore, TEXT("nothing saved inside the fight (pause)"));

		Check(State, Flow->NotifyWaveCleared() == EGameFlowResult::Ok && Saves->GetWrittenSaveCount() == WrittenBefore && CountSaveFiles(*Saves) == FilesBefore,
			TEXT("no autosave when the wave is cleared (rev 5f)"));
		Check(State, Saves->CanSaveNow() && Saves->QuickSave() && SavedPhase(ReadSaveFile(*Saves, TEXT("quicksave"))) == TEXT("WaveCleared"),
			TEXT("after the wave: F5 allowed, the quicksave holds WaveCleared"));
		const FGameFlowConfig& Config = Flow->GetConfig();
		const FSaveLoadFlow Expected = SaveGameRules::ResolveLoadFlow(2, ECodexGamePhase::WaveCleared, true, false, false, 1, Config.TotalWaves, false,
			Config.bAmbushSingleFight);
		const int32 WrittenBeforeLoad = Saves->GetWrittenSaveCount();
		const bool bQuickLoaded = Saves->LoadGame(TEXT("quicksave"));
		Check(State, bQuickLoaded && Flow->GetPhase() == Expected.Phase && Flow->GetWaveIndex() == Expected.WaveIndex,
			FString::Printf(TEXT("the after-the-wave quicksave resumes %s of wave %d"), *UEnum::GetValueAsString(Flow->GetPhase()), Flow->GetWaveIndex()));
		Check(State, Saves->GetWrittenSaveCount() == WrittenBeforeLoad, TEXT("loading writes no save"));

		// Rev 5f: a legacy autosave file (written by an older build) stays on disk but is never listed / continued / F9-loaded.
		const FString Legacy = Saves->GetSaveDirectory() / TEXT("autosave.json");
		if (TSharedPtr<FJsonObject> LegacyData = ReadSaveFile(*Saves, Slot))
		{
			// The NEWEST file on disk: an old build's autosave written after everything else.
			LegacyData->SetStringField(TEXT("slot_name"), TEXT("autosave"));
			LegacyData->SetStringField(TEXT("save_type"), TEXT("autosave"));
			LegacyData->SetStringField(TEXT("title"), TEXT("Autosave: legacy"));
			LegacyData->SetNumberField(TEXT("timestamp"), static_cast<double>(FDateTime::UtcNow().ToUnixTimestamp() + 3600));
			FString LegacyJson;
			FJsonSerializer::Serialize(LegacyData.ToSharedRef(), TJsonWriterFactory<>::Create(&LegacyJson));
			FFileHelper::SaveStringToFile(LegacyJson, *Legacy);
		}
		const TArray<FSaveSlotInfo> Listed = Saves->GetAllSaves();
		const TArray<FSaveSlotInfo> All = Saves->GetAllSaves(/*bIncludeHidden*/ true);
		const bool bListed = Listed.ContainsByPredicate([](const FSaveSlotInfo& Info) { return Info.SlotName == TEXT("autosave"); });
		Check(State, IFileManager::Get().FileExists(*Legacy) && !bListed && All.Num() == Listed.Num() + 1 && Saves->GetContinueSlot() != TEXT("autosave"),
			FString::Printf(TEXT("legacy autosave file kept on disk but hidden (listed %d, on disk %d, continue \"%s\")"), Listed.Num(), All.Num(), *Saves->GetContinueSlot()));
		// F9 = the newest of the quicksave and the checkpoint; a newer manual save does not count.
		FPlatformProcess::Sleep(1.1f); // the save timestamp has whole seconds
		Check(State, Saves->SaveGame(SaveGameRules::GetCheckpointSlotName(), TEXT("Checkpoint: smoke")), TEXT("a checkpoint written after the quicksave"));
		FPlatformProcess::Sleep(1.1f);
		Check(State, Saves->SaveGame(TEXT("manual_newest"), TEXT("manual_newest")), TEXT("a manual save written last"));
		const TArray<FSaveSlotInfo> AfterManual = Saves->GetAllSaves();
		const int32 Pick = SaveGameRules::PickQuickLoadIndex(AfterManual);
		Check(State, AfterManual.IsValidIndex(Pick) && AfterManual[Pick].SlotName == SaveGameRules::GetCheckpointSlotName() && AfterManual[0].SlotName == TEXT("manual_newest"),
			FString::Printf(TEXT("F9 picks the checkpoint (newer than the quicksave), not the newest manual save (%s)"), AfterManual.IsValidIndex(Pick) ? *AfterManual[Pick].SlotName : TEXT("none")));
		FPlatformProcess::Sleep(1.1f);
		Check(State, Saves->QuickSave(), TEXT("F5 again"));
		const TArray<FSaveSlotInfo> AfterF5 = Saves->GetAllSaves();
		const int32 PickF5 = SaveGameRules::PickQuickLoadIndex(AfterF5);
		Check(State, AfterF5.IsValidIndex(PickF5) && AfterF5[PickF5].SlotName == TEXT("quicksave"), TEXT("then F9 picks the newer quicksave"));
		Saves->DeleteSave(TEXT("manual_newest"));
		Saves->DeleteSave(SaveGameRules::GetCheckpointSlotName());
	}

	bool Step(TSharedRef<FState> StatePtr)
	{
		FState& State = *StatePtr;
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		UWorld* World = FindGameWorld();
		if (State.Time > 150.f)
		{
			Check(State, false, FString::Printf(TEXT("timed out in stage %d"), State.Stage));
			return Finish(State, false);
		}
		if (!World || State.Time < 3.f)
		{
			return true;
		}
		USaveGameSubsystem* Saves = World->GetSubsystem<USaveGameSubsystem>();
		if (USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>(); Squad && State.Stage < 4)
		{
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Member->bTacticalCeaseFire = true;
			}
		}
		switch (State.Stage)
		{
		case 0:
			if (!BuildWorld(World, State))
			{
				return Finish(State, false);
			}
			State.Stage = 1;
			State.StageTime = 0.f;
			return true;
		case 1:
		{
			// Let the patrols walk: the marksman past waypoint 1, the frostbitten searching for a while.
			AMarksmanEnemyCharacter* Marksman = State.Marksman.Get();
			if (State.StageTime > 1.f && State.Searcher.IsValid() && !State.Searcher->IsSearching())
			{
				StartSearchAwayFromDeployables(World, *State.Searcher);
			}
			const bool bMidRoute = Marksman && Marksman->GetPatrolWaypointIndex() >= 1;
			if ((!bMidRoute || State.StageTime < 4.f) && State.StageTime < 20.f)
			{
				return true;
			}
			Check(State, bMidRoute && State.Searcher.IsValid() && State.Searcher->IsSearching(), TEXT("patrols mid-route, frostbitten searching"));
			InPlaceRoundTrip(World, State);
			PolicyAndAutosave(World, State);
			// 3. Continue: the map is reopened and the save applied to the fresh world.
			State.AppliedHandle = USaveGameSubsystem::OnSaveApplied().AddLambda([StatePtr](UWorld* AppliedWorld, const FString& AppliedSlot)
			{
				if (AppliedSlot == Slot && AppliedWorld)
				{
					if (const USaveGameSubsystem* AppliedSaves = AppliedWorld->GetSubsystem<USaveGameSubsystem>())
					{
						StatePtr->SnapshotTravel = Snapshot(*AppliedSaves);
						StatePtr->bTravelApplied = true;
					}
				}
			});
			Check(State, Saves->LoadGameWithTravel(Slot), TEXT("load with travel: the map reopens"));
			State.Stage = 2;
			State.StageTime = 0.f;
			return true;
		}
		case 2:
		{
			if (!State.bTravelApplied)
			{
				if (State.StageTime > 40.f)
				{
					Check(State, false, TEXT("save applied after the travel"));
					return Finish(State, false);
				}
				return true;
			}
			Check(State, true, TEXT("save applied to the fresh world"));
			ReportDiff(State, State.SnapshotA, State.SnapshotTravel, TEXT("load after travel"));
			if (!State.KilledLevelEnemy.IsEmpty())
			{
				Check(State, !FindNamed<AEnemyCharacter>(World, State.KilledLevelEnemy),
					FString::Printf(TEXT("killed level enemy %s stays dead"), *State.KilledLevelEnemy));
			}
			if (!State.HurtLevelEnemy.IsEmpty())
			{
				const AEnemyCharacter* Hurt = FindNamed<AEnemyCharacter>(World, State.HurtLevelEnemy);
				Check(State, Hurt && Hurt->GetHealthComponent()->GetCurrentHealth() < Hurt->GetHealthComponent()->GetMaxHealth(),
					TEXT("hurt level enemy keeps its wound"));
			}
			if (State.KilledLevelEnemy.IsEmpty() && State.HurtLevelEnemy.IsEmpty())
			{
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke note: the map has no placed enemies (dead-stay-dead checked in place by the removed extra enemy)"));
			}
			int32 Enemies = 0;
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				Enemies += It->IsActorBeingDestroyed() ? 0 : 1;
			}
			Check(State, Enemies == 3 + (State.HurtLevelEnemy.IsEmpty() ? 0 : 1), FString::Printf(TEXT("fresh world: %d enemies (patrols recreated)"), Enemies));
			const UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
			Check(State, Flow && Flow->GetPhase() == ECodexGamePhase::Exploration, TEXT("fresh world resumes the exploration"));
			// Rev 2026-10-19: the patrollers (recreated in the fresh world: they are not map actors) keep the patrol speed 65.
			int32 Slow = 0;
			int32 Patrollers = 0;
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				if (!It->IsActorBeingDestroyed() && (It->IsOnPatrol() || It->IsSearching() || It->GetEscortLeader()))
				{
					// The smoke's senseless patrollers (a runtime override, not a saved / map property): blind again in the fresh world.
					It->bOverridePerception = true;
					It->PerceptionOverride = Senseless();
					++Patrollers;
					Slow += FMath::IsNearlyEqual(It->PatrolWalkSpeed, DesignerPatrolSpeed) ? 1 : 0;
					if (It->GetArchetype() == EEnemyArchetype::Frostbitten)
					{
						State.Searcher = *It; // the route-B frostbitten (searching when saved; its search may already be over)
					}
					if (AMarksmanEnemyCharacter* FreshMarksman = Cast<AMarksmanEnemyCharacter>(*It))
					{
						FreshMarksman->MarksmanConfig.DetectionRange = 100.f;
						State.Marksman = FreshMarksman;
					}
				}
			}
			Check(State, Patrollers == 3 && Slow == 3, FString::Printf(TEXT("after the reopened level: %d of %d patrollers at the designer's patrol speed %.0f"),
				Slow, Patrollers, DesignerPatrolSpeed));
			if (AEnemyCharacter* Searcher = State.Searcher.Get(); Searcher && !Searcher->IsSearching())
			{
				StartSearchAwayFromDeployables(World, *Searcher); // a search to come back from
			}
			State.Stage = 3;
			State.StageTime = 0.f;
			return true;
		}
		case 3:
		{
			// Walking the route after the load, and back on it after the search ends: the designer's speed, not the class default 190.
			const AEnemyCharacter* Searcher = State.Searcher.Get();
			const AMarksmanEnemyCharacter* Marksman = State.Marksman.Get();
			if (Searcher && Searcher->IsSearching() && State.StageTime < 45.f)
			{
				if (Marksman && Marksman->IsOnPatrol() && Marksman->GetVelocity().Size2D() > 10.f
					&& Marksman->GetCharacterMovement()->MaxWalkSpeed > DesignerPatrolSpeed + 1.f)
				{
					State.bPatrolSpeedWrong = true;
				}
				return true;
			}
			State.SearchEndedAt = State.SearchEndedAt < 0.f ? State.StageTime : State.SearchEndedAt;
			// Back on the route: sampled once he walks again (he may first wait at the nearest waypoint with the search pace still set).
			const bool bWalking = Searcher && !Searcher->IsSearching() && Searcher->GetVelocity().Size2D() > 10.f;
			if (!bWalking && State.StageTime < State.SearchEndedAt + 20.f)
			{
				return true;
			}
			const float Speed = Searcher ? Searcher->GetCharacterMovement()->MaxWalkSpeed : -1.f;
			Check(State, bWalking && FMath::IsNearlyEqual(Speed, DesignerPatrolSpeed, 1.f) && Searcher->GetVelocity().Size2D() <= DesignerPatrolSpeed + 5.f,
				FString::Printf(TEXT("the search ended, walking the route again at the patrol speed %.0f (wanted %.0f, moving %.0f cm/s)"), Speed,
					DesignerPatrolSpeed, Searcher ? Searcher->GetVelocity().Size2D() : -1.f));
			Check(State, !State.bPatrolSpeedWrong, TEXT("the marksman walked his route at the patrol speed after the load"));
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
		{
			FTimerHandle PlaceHandle;
			TWeakObjectPtr<UWorld> PlaceWorld(World);
			World->GetTimerManager().SetTimer(PlaceHandle, FTimerDelegate::CreateLambda([PlaceWorld]()
			{
				SmokeUtils::PlaceSquadAtTestStart(PlaceWorld.Get());
			}), 0.5f, false);
		}
		TSharedRef<FState> State = MakeShared<FState>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float)
		{
			return Step(State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.SaveRoundTripSmoke"),
		TEXT("Dev check: snapshot / save / mutate / load / diff of every saved system, the save policy and autosaves, load with travel; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
