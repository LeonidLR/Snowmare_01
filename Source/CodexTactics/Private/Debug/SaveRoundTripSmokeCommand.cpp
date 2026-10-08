// Dev-only headless round trip of the save system (save policy + format version 2, user decision 2026-10-08) on
// L_MovementTest (runtime patrols like PatrolSmoke; nothing is saved to the map):
//   Scripts/smoke.ps1 -Command CodexTactics.SaveRoundTripSmoke -Log Smoke-SaveRoundTrip.log
// Slots go to Saved/SmokeSaves (exclusive in Scripts/test_map.json). A non-trivial out-of-combat world is built: squad
// hurt / cold / moved / crouched / prone, ammo per weapon, items, bonus item, extra ammo, postures (squad + own), guard;
// 10 rounds stored in a crate, a pile dropped, a barrel pushed 3 m and lit, a squad mine and a tripwire placed, a
// turret and a barricade damaged (turret unpowered), canister + diesel quest steps (the canister hides), the level's
// dialogue trigger played and a note read; one level enemy killed, one hurt; a marksman + escort hound walking a runtime
// route mid-way, a frostbitten searching on a second route. Then:
//  1. SNAPSHOT (the save data of the live world, flattened), save, mutate everything (heal, move, items, posture, crate,
//     pile gone, barrel out, mine / tripwire removed, quest reset, canister shown, note unread, trigger reset, patrol
//     reset, the hound killed, an extra enemy spawned), load in place, SNAPSHOT again: every field must match (each
//     mismatch is listed) plus direct checks of the world.
//  2. Save policy: cutscene and fight refuse F5 / dialog saves (CanSaveNow hint "Saving is disabled during combat",
//     which the frontend pause screen shows), also in the tactical pause; autosave right before the wave (written as its preparation) and right
//     after it is cleared; loading the after-combat autosave resumes the next wave's preparation.
//  3. Load with travel ("Continue"): the map is reopened, the save applied to the fresh world and its SNAPSHOT diffed
//     against the first one (runtime routes / enemies / mine / tripwire / turret recreated, killed level enemies stay dead).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AI/PatrolRouteActor.h"
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
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/BarrelActor.h"
#include "Interactables/BarricadeActor.h"
#include "Interactables/DroppedItemActor.h"
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
#include "UObject/UObjectGlobals.h"

namespace SaveRoundTripSmoke
{
	constexpr float StepSeconds = 0.25f;
	const TCHAR* Slot = TEXT("roundtrip");

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		TMap<FString, FString> SnapshotA;
		TMap<FString, FString> SnapshotTravel;
		bool bTravelApplied = false;
		FDelegateHandle AppliedHandle;
		TWeakObjectPtr<AMarksmanEnemyCharacter> Marksman;
		TWeakObjectPtr<AEnemyCharacter> Hound;
		TWeakObjectPtr<AEnemyCharacter> Searcher;
		TWeakObjectPtr<APatrolRouteActor> RouteA;
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
		Saves->bAutosaveEnabled = true;

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
		ABarricadeActor* Barricade = Cast<ABarricadeActor>(SpawnDeployable(EDeployableType::Barricade, SmokeUtils::ClearPoint(World, Feet, Feet - F * 500.f + R * 400.f)));
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
		Marksman->StartPatrol(RouteA, nullptr);
		Hound->StartPatrol(nullptr, Marksman);
		Searcher->StartPatrol(RouteB, nullptr);
		Searcher->GetHealthComponent()->ApplyDirectHealthLoss(10.f, TEXT("Smoke"));
		State.Marksman = Marksman;
		State.Hound = Hound;
		State.Searcher = Searcher;
		State.RouteA = RouteA;
		State.RouteB = RouteB;
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
		FText Hint;
		Check(State, Saves->CanSaveNow(&Hint) && Hint.IsEmpty(), TEXT("exploration: saving allowed"));
		Check(State, Saves->SaveToSlotWithMessage(Slot) && Saves->HasSave(Slot), TEXT("saved \"roundtrip\""));
		FSaveSlotInfo Info;
		const bool bInfo = Saves->GetSaveInfo(Slot, Info);
		Check(State, bInfo && Info.Version == SaveGameRules::CurrentSaveVersion && Info.MapName.EndsWith(TEXT("L_MovementTest")),
			FString::Printf(TEXT("save info: version %d, map %s"), Info.Version, *Info.MapName));

		// Mutate everything.
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
		for (TActorIterator<ATurretActor> It(World); It; ++It)
		{
			It->Repair();
			It->SetPowered(true);
		}
		Quests->RestoreState(FQuestChainState());
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
		const AInteractableActor* Canister = FindNamed<AInteractableActor>(World, State.CanisterName);
		Check(State, Canister && Canister->IsHidden() && Quests->HasFuelCanister(), TEXT("canister hidden, fuel canister in the quest"));
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
		Check(State, bExtraGone, TEXT("enemy spawned after the save removed"));
		const AEnemyCharacter* Searcher = State.Searcher.Get();
		Check(State, Searcher && Searcher->IsSearching(), TEXT("frostbitten still searching"));
		Check(State, Squad->GetEffectivePosture(Engineer) == ESquadFirePosture::Defensive && Squad->GetSquadPosture() == ESquadFirePosture::Passive,
			TEXT("postures back (squad Passive, engineer Defensive)"));
	}

	// --- Stage 2: save policy and autosaves ---

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
		const bool bCutscene = Flow->TriggerCombatZone() == EGameFlowResult::Ok;
		const bool bCutsceneRefused = !Saves->CanSaveNow(&Hint);
		Check(State, bCutscene && bCutsceneRefused && !Hint.IsEmpty(),
			FString::Printf(TEXT("cutscene: saving refused (%s)"), *Hint.ToString()));
		Check(State, Flow->FinishCutscene() == EGameFlowResult::Ok && Saves->CanSaveNow(), TEXT("preparation: saving allowed"));
		const int32 Before = Saves->GetAutosaveCount();
		Check(State, Flow->FinishPreparation() == EGameFlowResult::Ok && Flow->GetPhase() == ECodexGamePhase::WaveCombat, TEXT("wave 1 started"));
		Check(State, Saves->GetAutosaveCount() == Before + 1 && Saves->GetLastAutosaveMoment() == EAutosaveMoment::BeforeCombat
			&& Saves->HasSave(SaveGameRules::GetAutosaveSlotName()), TEXT("autosave right before the wave"));
		const TSharedPtr<FJsonObject> PreCombat = ReadSaveFile(*Saves, SaveGameRules::GetAutosaveSlotName());
		Check(State, SavedPhase(PreCombat) == TEXT("Preparation"), FString::Printf(TEXT("pre-combat autosave holds the preparation (%s)"), *SavedPhase(PreCombat)));

		const bool bFightRefused = !Saves->CanSaveNow(&Hint);
		Check(State, bFightRefused && Hint.ToString() == TEXT("Saving is disabled during combat"), FString::Printf(TEXT("fight: \"%s\""), *Hint.ToString()));
		Check(State, !Saves->QuickSave() && !Saves->HasSave(TEXT("quicksave")), TEXT("F5 refused in the fight"));
		Check(State, !Saves->SaveToSlotWithMessage(TEXT("combat_try")) && !Saves->HasSave(TEXT("combat_try")), TEXT("dialog save refused in the fight"));
		if (Flow->ToggleTacticalPause() == EGameFlowResult::Ok)
		{
			Check(State, Flow->GetCombatMode() == ECodexCombatMode::TacticalPause && !Saves->CanSaveNow(), TEXT("tactical pause: saving refused"));
			Flow->ToggleTacticalPause();
		}
		Check(State, Saves->GetAutosaveCount() == Before + 1, TEXT("no autosave inside the fight (pause)"));

		Check(State, Flow->NotifyWaveCleared() == EGameFlowResult::Ok && Saves->GetAutosaveCount() == Before + 2
			&& Saves->GetLastAutosaveMoment() == EAutosaveMoment::AfterCombat, TEXT("autosave right after the wave is cleared"));
		const TSharedPtr<FJsonObject> PostCombat = ReadSaveFile(*Saves, SaveGameRules::GetAutosaveSlotName());
		Check(State, SavedPhase(PostCombat) == TEXT("WaveCleared") && Saves->CanSaveNow(), TEXT("after the wave: saving allowed, autosave holds WaveCleared"));
		const FGameFlowConfig& Config = Flow->GetConfig();
		const FSaveLoadFlow Expected = SaveGameRules::ResolveLoadFlow(2, ECodexGamePhase::WaveCleared, true, false, false, 1, Config.TotalWaves, false,
			Config.bAmbushSingleFight);
		const bool bAutoLoaded = Saves->LoadGame(SaveGameRules::GetAutosaveSlotName());
		Check(State, bAutoLoaded && Flow->GetPhase() == Expected.Phase && Flow->GetWaveIndex() == Expected.WaveIndex,
			FString::Printf(TEXT("after-combat autosave resumes %s of wave %d"), *UEnum::GetValueAsString(Flow->GetPhase()), Flow->GetWaveIndex()));
		Check(State, Saves->GetAutosaveCount() == Before + 2, TEXT("loading writes no autosave"));
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
		if (USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>(); Squad && State.Stage < 3)
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
				State.Searcher->StartPatrolSearch(State.Searcher->GetActorLocation() + FVector(300.f, 0.f, 0.f));
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
