#include "Core/SaveGameSubsystem.h"
#include "AI/PatrolRouteActor.h"
#include "AIController.h"
#include "Camera/TacticalCameraPawn.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/RecruitSubsystem.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Components/SplineComponent.h"
#include "Core/MissionSessionSubsystem.h"
#include "Data/WeaponDataAsset.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/BarrelActor.h"
#include "Interactables/BarricadeActor.h"
#include "Interactables/DeployableActor.h"
#include "Interactables/DroppedItemActor.h"
#include "Interactables/GateActor.h"
#include "Interactables/ItemStashComponent.h"
#include "Interactables/LootCrateActor.h"
#include "Interactables/NarrativeElementActor.h"
#include "Interactables/ProximityMineActor.h"
#include "Interactables/RelocationSubsystem.h"
#include "Interactables/TripwireActor.h"
#include "Interactables/TurretActor.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Quests/DialogueTriggerVolume.h"
#include "Quests/QuestSubsystem.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Subsystems/CodexEventBus.h"
#include "Survival/ColdSurvivalComponent.h"
#include "TimerManager.h"
#include "UI/GameMessageSubsystem.h"
#include "Engine/GameViewportClient.h"
#include "HAL/PlatformTime.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"

namespace
{
	/** Load cover (pending load): stays black this long after the save is applied, then fades out over LoadCoverFadeSeconds. */
	constexpr float LoadCoverHoldSeconds = 0.2f;
	constexpr float LoadCoverFadeSeconds = 0.35f;
	/** Above the HUD, the CommonUI layout and the death-cinematic overlay. */
	constexpr int32 LoadCoverZOrder = 10000;
}

namespace
{
	TAutoConsoleVariable<int32> CVarSaveAutosave(TEXT("Codex.Save.Autosave"), 1,
		TEXT("Autosave before a fight starts and after it ends (save policy 2026-10-08). 0 = off."));

	/** Sprint 13: a stash as {"RifleAmmo": 15, ...} (ETransferItem names). */
	TSharedRef<FJsonObject> SaveStash(const UItemStashComponent* Stash)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		if (Stash)
		{
			for (const TPair<ETransferItem, int32>& Pair : Stash->GetItems())
			{
				Object->SetNumberField(StaticEnum<ETransferItem>()->GetNameStringByValue(static_cast<int64>(Pair.Key)), Pair.Value);
			}
		}
		return Object;
	}

	TMap<ETransferItem, int32> LoadStash(const TSharedPtr<FJsonObject>& Object)
	{
		TMap<ETransferItem, int32> Items;
		if (!Object.IsValid())
		{
			return Items;
		}
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Object->Values)
		{
			const int64 Value = StaticEnum<ETransferItem>()->GetValueByNameString(Field.Key);
			double Count = 0.0;
			if (Value != INDEX_NONE && Field.Value.IsValid() && Field.Value->TryGetNumber(Count) && Count > 0.0)
			{
				Items.Add(static_cast<ETransferItem>(Value), FMath::RoundToInt(Count));
			}
		}
		return Items;
	}

	TArray<TSharedPtr<FJsonValue>> SaveVector(const FVector& V)
	{
		return { MakeShared<FJsonValueNumber>(V.X), MakeShared<FJsonValueNumber>(V.Y), MakeShared<FJsonValueNumber>(V.Z) };
	}

	FVector LoadVector(const TSharedPtr<FJsonObject>& Object, const FString& Field, const FVector& Fallback)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (Object.IsValid() && Object->TryGetArrayField(Field, Values) && Values->Num() == 3)
		{
			return FVector((*Values)[0]->AsNumber(), (*Values)[1]->AsNumber(), (*Values)[2]->AsNumber());
		}
		return Fallback;
	}

	TArray<TSharedPtr<FJsonValue>> SaveRotator(const FRotator& R)
	{
		return { MakeShared<FJsonValueNumber>(R.Pitch), MakeShared<FJsonValueNumber>(R.Yaw), MakeShared<FJsonValueNumber>(R.Roll) };
	}

	FRotator LoadRotator(const TSharedPtr<FJsonObject>& Object, const FString& Field, const FRotator& Fallback)
	{
		const FVector V = LoadVector(Object, Field, FVector(Fallback.Pitch, Fallback.Yaw, Fallback.Roll));
		return FRotator(V.X, V.Y, V.Z);
	}

	int32 SaveInt(const TSharedPtr<FJsonObject>& Object, const FString& Field, int32 Fallback)
	{
		double Value = Fallback;
		if (Object.IsValid())
		{
			Object->TryGetNumberField(Field, Value);
		}
		return static_cast<int32>(Value);
	}

	float SaveFloat(const TSharedPtr<FJsonObject>& Object, const FString& Field, float Fallback)
	{
		double Value = Fallback;
		if (Object.IsValid())
		{
			Object->TryGetNumberField(Field, Value);
		}
		return static_cast<float>(Value);
	}

	bool SaveBool(const TSharedPtr<FJsonObject>& Object, const FString& Field, bool Fallback)
	{
		bool Value = Fallback;
		if (Object.IsValid())
		{
			Object->TryGetBoolField(Field, Value);
		}
		return Value;
	}

	FString SaveString(const TSharedPtr<FJsonObject>& Object, const FString& Field, const FString& Fallback = FString())
	{
		FString Value = Fallback;
		if (Object.IsValid())
		{
			Object->TryGetStringField(Field, Value);
		}
		return Value;
	}

	AGateActor* SaveFindGate(UWorld* World)
	{
		for (TActorIterator<AGateActor> It(World); It; ++It)
		{
			return *It;
		}
		return nullptr;
	}

	template <typename EnumType>
	FString SaveEnumName(EnumType Value)
	{
		return StaticEnum<EnumType>()->GetNameStringByValue(static_cast<int64>(Value));
	}

	template <typename EnumType>
	bool LoadEnumName(const FString& Text, EnumType& OutValue)
	{
		const int64 Value = Text.IsEmpty() ? INDEX_NONE : StaticEnum<EnumType>()->GetValueByNameString(Text);
		if (Value == INDEX_NONE)
		{
			return false;
		}
		OutValue = static_cast<EnumType>(Value);
		return true;
	}

	/** Actors the save keeps per object (crates and piles have their own sections; previews / blown charges are skipped). */
	bool SaveIsTrackedObject(const AInteractableActor* Actor)
	{
		if (!IsValid(Actor) || Actor->IsActorBeingDestroyed() || Actor->IsA<ALootCrateActor>() || Actor->IsA<ADroppedItemActor>())
		{
			return false;
		}
		if (const ATripwireActor* Wire = Cast<ATripwireActor>(Actor))
		{
			return !Wire->IsPreview() && !Wire->IsTripped();
		}
		return true;
	}

	/** Placed / picked-up objects the load may spawn (missing) or remove (not in the save). */
	bool SaveIsDynamicObjectClass(const UClass* Class)
	{
		return Class && (Class->IsChildOf(ADeployableActor::StaticClass()) || Class->IsChildOf(ATripwireActor::StaticClass()));
	}

	FActorSpawnParameters SaveSpawnParams(const FString& Name)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.Name = FName(*Name);
		Params.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Requested; // a name still held by a destroyed actor: a new one
		return Params;
	}

	// --- Operatives ---

	TSharedRef<FJsonObject> SaveOperative(const AOperativeCharacter* Member, const USquadSubsystem* Squad)
	{
		TSharedRef<FJsonObject> Soldier = MakeShared<FJsonObject>();
		Soldier->SetStringField(TEXT("node_name"), UEnum::GetValueAsString(Member->SquadRole));
		Soldier->SetStringField(TEXT("character_name"), Member->DisplayName.ToString());
		Soldier->SetBoolField(TEXT("is_leader"), Squad && Squad->GetLeader() == Member);
		Soldier->SetArrayField(TEXT("pos"), SaveVector(Member->GetActorLocation()));
		Soldier->SetNumberField(TEXT("rot_y"), Member->GetActorRotation().Yaw);
		const float Current = Member->HealthComponent ? Member->HealthComponent->GetCurrentHealth() : 0.f;
		const float Max = Member->HealthComponent ? Member->HealthComponent->GetMaxHealth() : 100.f;
		Soldier->SetNumberField(TEXT("current_health"), Current);
		Soldier->SetNumberField(TEXT("max_health"), Max);
		Soldier->SetNumberField(TEXT("cold_level"), Member->ColdLevel);
		Soldier->SetNumberField(TEXT("level"), Member->Level);
		Soldier->SetNumberField(TEXT("current_exp"), Member->CurrentExp);
		Soldier->SetNumberField(TEXT("unspent_stat_points"), Member->UnspentStatPoints);
		Soldier->SetNumberField(TEXT("accuracy"), Member->Accuracy);
		Soldier->SetNumberField(TEXT("luck"), Member->Luck);
		Soldier->SetNumberField(TEXT("fortitude"), Member->ColdSurvival ? Member->ColdSurvival->Fortitude : 15.f);
		Soldier->SetNumberField(TEXT("current_stance"), static_cast<int32>(Member->GetStance()));
		Soldier->SetStringField(TEXT("current_weapon_id"), Member->CurrentWeapon ? Member->CurrentWeapon->WeaponId : FString());
		Soldier->SetNumberField(TEXT("matches_count"), Member->MatchesCount);
		Soldier->SetNumberField(TEXT("turrets_count"), Member->TurretsCount);
		Soldier->SetNumberField(TEXT("barricades_count"), Member->BarricadesCount);
		Soldier->SetNumberField(TEXT("mines_count"), Member->MinesCount);
		Soldier->SetNumberField(TEXT("medkits_count"), Member->MedkitsCount);
		Soldier->SetNumberField(TEXT("canned_food_count"), Member->CannedFoodCount);
		Soldier->SetNumberField(TEXT("bread_count"), Member->BreadCount);
		Soldier->SetNumberField(TEXT("chocolate_count"), Member->ChocolateCount);
		Soldier->SetNumberField(TEXT("grenades_count"), Member->GrenadesCount);
		Soldier->SetBoolField(TEXT("is_guarding"), Member->bGuarding);
		// Version 2: the own fire posture ("" = follows the squad) and the bonus items found in crates.
		Soldier->SetStringField(TEXT("posture_override"), Member->bHasPostureOverride ? SaveEnumName(Member->PostureOverride) : FString());
		TArray<TSharedPtr<FJsonValue>> Bonus;
		for (const FString& Item : Member->BonusItems)
		{
			Bonus.Add(MakeShared<FJsonValueString>(Item));
		}
		Soldier->SetArrayField(TEXT("bonus_items"), Bonus);
		TSharedRef<FJsonObject> Ammo = MakeShared<FJsonObject>();
		for (const UWeaponDataAsset* Weapon : Member->AvailableWeapons)
		{
			if (Weapon)
			{
				const FWeaponAmmoState State = Member->GetAmmoState(Weapon->WeaponId);
				TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetNumberField(TEXT("clip"), State.Clip);
				Entry->SetNumberField(TEXT("reserve"), State.Reserve);
				Ammo->SetObjectField(Weapon->WeaponId, Entry);
			}
		}
		Soldier->SetObjectField(TEXT("ammo_inventory"), Ammo);
		TSharedRef<FJsonObject> Extra = MakeShared<FJsonObject>();
		for (const TPair<FName, int32>& Entry : Member->ExtraAmmo)
		{
			Extra->SetNumberField(Entry.Key.ToString(), Entry.Value);
		}
		Soldier->SetObjectField(TEXT("extra_ammo"), Extra);
		return Soldier;
	}

	void ApplyOperative(AOperativeCharacter* Member, const TSharedPtr<FJsonObject>& Info)
	{
		Member->StopOperative();
		// Stance first: changing it moves the capsule; the saved position is the one of the saved stance.
		Member->SetStance(static_cast<EOperativeStance>(FMath::Clamp(SaveInt(Info, TEXT("current_stance"), 0), 0, 2)));
		Member->TeleportTo(LoadVector(Info, TEXT("pos"), Member->GetActorLocation()),
			FRotator(0.f, SaveFloat(Info, TEXT("rot_y"), Member->GetActorRotation().Yaw), 0.f), false, true);
		Member->Level = SaveInt(Info, TEXT("level"), 1);
		Member->CurrentExp = SaveInt(Info, TEXT("current_exp"), 0);
		Member->UnspentStatPoints = SaveInt(Info, TEXT("unspent_stat_points"), 0);
		Member->Accuracy = SaveFloat(Info, TEXT("accuracy"), Member->Accuracy);
		Member->Luck = SaveFloat(Info, TEXT("luck"), Member->Luck);
		if (Member->ColdSurvival)
		{
			Member->ColdSurvival->Fortitude = SaveFloat(Info, TEXT("fortitude"), Member->ColdSurvival->Fortitude);
		}
		if (UHealthComponent* Health = Member->HealthComponent)
		{
			const float Max = SaveFloat(Info, TEXT("max_health"), Health->GetMaxHealth());
			Health->RestoreHealth(FMath::Max(1.f, SaveFloat(Info, TEXT("current_health"), Max)), Max);
		}
		Member->ColdLevel = SaveFloat(Info, TEXT("cold_level"), 0.f);
		Member->MatchesCount = SaveInt(Info, TEXT("matches_count"), Member->MatchesCount);
		Member->TurretsCount = SaveInt(Info, TEXT("turrets_count"), Member->TurretsCount);
		Member->BarricadesCount = SaveInt(Info, TEXT("barricades_count"), Member->BarricadesCount);
		Member->MinesCount = SaveInt(Info, TEXT("mines_count"), Member->MinesCount);
		Member->MedkitsCount = SaveInt(Info, TEXT("medkits_count"), Member->MedkitsCount);
		Member->CannedFoodCount = SaveInt(Info, TEXT("canned_food_count"), Member->CannedFoodCount);
		Member->BreadCount = SaveInt(Info, TEXT("bread_count"), Member->BreadCount);
		Member->ChocolateCount = SaveInt(Info, TEXT("chocolate_count"), Member->ChocolateCount);
		Member->GrenadesCount = SaveInt(Info, TEXT("grenades_count"), Member->GrenadesCount);
		Member->bGuarding = SaveBool(Info, TEXT("is_guarding"), false);
		FString Posture;
		if (Info->TryGetStringField(TEXT("posture_override"), Posture))
		{
			ESquadFirePosture Parsed = FirePostureRules::DefaultPosture;
			Member->bHasPostureOverride = LoadEnumName(Posture, Parsed);
			Member->PostureOverride = Parsed;
		}
		const TArray<TSharedPtr<FJsonValue>>* Bonus = nullptr;
		if (Info->TryGetArrayField(TEXT("bonus_items"), Bonus))
		{
			Member->BonusItems.Reset();
			for (const TSharedPtr<FJsonValue>& Item : *Bonus)
			{
				Member->BonusItems.Add(Item->AsString());
			}
		}
		// Weapon and ammo: the saved inventory, then the weapon in hands takes its clip / reserve from it.
		const TSharedPtr<FJsonObject>* Ammo = nullptr;
		if (Info->TryGetObjectField(TEXT("ammo_inventory"), Ammo))
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*Ammo)->Values)
			{
				const TSharedPtr<FJsonObject> State = Entry.Value->AsObject();
				Member->AmmoInventory.Add(Entry.Key, { SaveInt(State, TEXT("clip"), 0), SaveInt(State, TEXT("reserve"), 0) });
			}
		}
		const TSharedPtr<FJsonObject>* Extra = nullptr;
		if (Info->TryGetObjectField(TEXT("extra_ammo"), Extra))
		{
			Member->ExtraAmmo.Reset();
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*Extra)->Values)
			{
				Member->ExtraAmmo.Add(FName(*Entry.Key), static_cast<int32>(Entry.Value->AsNumber()));
			}
		}
		const FString WeaponId = SaveString(Info, TEXT("current_weapon_id"));
		Member->CurrentWeapon = nullptr; // the saved state of the weapon in hands is in the inventory now
		if (WeaponId.IsEmpty() || !Member->SwitchToWeaponById(WeaponId))
		{
			Member->SwitchToWeaponById(TEXT("m16"));
		}
	}

	// --- Interactables (version 2) ---

	TSharedRef<FJsonObject> SaveObject(AInteractableActor* Object)
	{
		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("class"), Object->GetClass()->GetPathName());
		Entry->SetArrayField(TEXT("pos"), SaveVector(Object->GetActorLocation()));
		Entry->SetArrayField(TEXT("rot"), SaveRotator(Object->GetActorRotation()));
		Entry->SetBoolField(TEXT("hidden"), Object->IsHidden());
		Entry->SetBoolField(TEXT("collision"), Object->GetActorEnableCollision());
		Entry->SetBoolField(TEXT("trapped"), Object->bTrapped);
		Entry->SetBoolField(TEXT("defused"), Object->bDefused);
		Entry->SetNumberField(TEXT("failed_defusals"), Object->FailedDefusalAttempts);
		Entry->SetBoolField(TEXT("defusal_warned"), Object->bDefusalWarned);
		if (Object->ObjectType == EInteractableType::Generator)
		{
			Entry->SetNumberField(TEXT("generator_health"), Object->GeneratorHealth);
			Entry->SetBoolField(TEXT("generator_broken"), Object->bGeneratorBroken);
		}
		if (const ABarrelActor* Barrel = Cast<ABarrelActor>(Object))
		{
			Entry->SetBoolField(TEXT("burning"), Barrel->IsBurning());
			Entry->SetBoolField(TEXT("burnt"), Barrel->HasBeenLit());
			Entry->SetNumberField(TEXT("burn_left"), Barrel->GetBurnTimeLeft());
		}
		if (const AProximityMineActor* Mine = Cast<AProximityMineActor>(Object))
		{
			Entry->SetBoolField(TEXT("placed_by_squad"), Mine->bPlacedBySquad);
			Entry->SetBoolField(TEXT("revealed"), Mine->IsRevealed());
			Entry->SetNumberField(TEXT("arming_left"), Mine->GetArmingTimeLeft());
		}
		if (const ATripwireActor* Wire = Cast<ATripwireActor>(Object))
		{
			Entry->SetArrayField(TEXT("anchor_a"), SaveVector(Wire->GetAnchorA()));
			Entry->SetArrayField(TEXT("anchor_b"), SaveVector(Wire->GetAnchorB()));
			Entry->SetBoolField(TEXT("anchor_a_on_object"), Wire->IsAnchorAOnObject());
			Entry->SetBoolField(TEXT("anchor_b_on_object"), Wire->IsAnchorBOnObject());
			Entry->SetNumberField(TEXT("arming_left"), Wire->GetArmingLeft());
		}
		const UHealthComponent* Health = nullptr;
		if (const ABarricadeActor* Barricade = Cast<ABarricadeActor>(Object))
		{
			Health = Barricade->Health;
		}
		if (const ATurretActor* Turret = Cast<ATurretActor>(Object))
		{
			Health = Turret->Health;
			Entry->SetBoolField(TEXT("powered"), Turret->IsPowered());
			Entry->SetBoolField(TEXT("broken"), Turret->IsBroken());
		}
		if (Health)
		{
			Entry->SetNumberField(TEXT("health"), Health->GetCurrentHealth());
			Entry->SetNumberField(TEXT("max_health"), Health->GetMaxHealth());
		}
		if (const ANarrativeElementActor* Narrative = Cast<ANarrativeElementActor>(Object))
		{
			Entry->SetBoolField(TEXT("read"), Narrative->bHasBeenRead);
		}
		return Entry;
	}

	void ApplyObject(AInteractableActor* Object, const TSharedPtr<FJsonObject>& Entry)
	{
		if (ATripwireActor* Wire = Cast<ATripwireActor>(Object))
		{
			// Setup places the wire between its anchors (location / rotation follow from them).
			Wire->Setup(LoadVector(Entry, TEXT("anchor_a"), Wire->GetAnchorA()), LoadVector(Entry, TEXT("anchor_b"), Wire->GetAnchorB()),
				SaveBool(Entry, TEXT("anchor_a_on_object"), false), SaveBool(Entry, TEXT("anchor_b_on_object"), false), false);
			Wire->RestoreArmingLeft(SaveFloat(Entry, TEXT("arming_left"), 0.f));
		}
		else
		{
			Object->SetActorLocationAndRotation(LoadVector(Entry, TEXT("pos"), Object->GetActorLocation()),
				LoadRotator(Entry, TEXT("rot"), Object->GetActorRotation()), false, nullptr, ETeleportType::TeleportPhysics);
		}
		Object->SetActorHiddenInGame(SaveBool(Entry, TEXT("hidden"), Object->IsHidden()));
		Object->SetActorEnableCollision(SaveBool(Entry, TEXT("collision"), Object->GetActorEnableCollision()));
		Object->bTrapped = SaveBool(Entry, TEXT("trapped"), Object->bTrapped);
		Object->bDefused = SaveBool(Entry, TEXT("defused"), Object->bDefused);
		Object->FailedDefusalAttempts = SaveInt(Entry, TEXT("failed_defusals"), Object->FailedDefusalAttempts);
		Object->bDefusalWarned = SaveBool(Entry, TEXT("defusal_warned"), Object->bDefusalWarned);
		if (Object->ObjectType == EInteractableType::Generator)
		{
			Object->RestoreGeneratorState(SaveFloat(Entry, TEXT("generator_health"), Object->GeneratorHealth),
				SaveBool(Entry, TEXT("generator_broken"), Object->bGeneratorBroken));
		}
		if (ABarrelActor* Barrel = Cast<ABarrelActor>(Object))
		{
			Barrel->RestoreBurnState(SaveBool(Entry, TEXT("burning"), false), SaveBool(Entry, TEXT("burnt"), false), SaveFloat(Entry, TEXT("burn_left"), 0.f));
		}
		if (AProximityMineActor* Mine = Cast<AProximityMineActor>(Object))
		{
			Mine->RestoreSaved(SaveBool(Entry, TEXT("placed_by_squad"), Mine->bPlacedBySquad), Object->bTrapped,
				SaveBool(Entry, TEXT("revealed"), Mine->IsRevealed()), SaveFloat(Entry, TEXT("arming_left"), 0.f));
		}
		UHealthComponent* Health = nullptr;
		if (ABarricadeActor* Barricade = Cast<ABarricadeActor>(Object))
		{
			Health = Barricade->Health;
		}
		ATurretActor* Turret = Cast<ATurretActor>(Object);
		if (Turret)
		{
			Health = Turret->Health;
		}
		if (Health && Entry->HasField(TEXT("health")))
		{
			Health->RestoreHealth(SaveFloat(Entry, TEXT("health"), Health->GetCurrentHealth()), SaveFloat(Entry, TEXT("max_health"), Health->GetMaxHealth()));
		}
		if (Turret)
		{
			Turret->RestoreSaved(SaveBool(Entry, TEXT("powered"), Turret->IsPowered()), SaveBool(Entry, TEXT("broken"), Turret->IsBroken()));
		}
		if (ANarrativeElementActor* Narrative = Cast<ANarrativeElementActor>(Object))
		{
			Narrative->bHasBeenRead = SaveBool(Entry, TEXT("read"), Narrative->bHasBeenRead);
		}
	}

	// --- Patrol routes and enemies (version 2) ---

	TSharedRef<FJsonObject> SaveRoute(const APatrolRouteActor* Route)
	{
		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("class"), Route->GetClass()->GetPathName());
		Entry->SetBoolField(TEXT("loop"), Route->bIsLoop);
		Entry->SetBoolField(TEXT("ping_pong"), Route->bPingPong);
		Entry->SetNumberField(TEXT("wait"), Route->DefaultWaitTimeSeconds);
		TArray<TSharedPtr<FJsonValue>> Waits;
		for (const float Wait : Route->PerPointWaitTime)
		{
			Waits.Add(MakeShared<FJsonValueNumber>(Wait));
		}
		Entry->SetArrayField(TEXT("point_waits"), Waits);
		TArray<TSharedPtr<FJsonValue>> Points;
		for (int32 Index = 0; Index < Route->GetNumberOfWaypoints(); ++Index)
		{
			Points.Add(MakeShared<FJsonValueArray>(SaveVector(Route->GetWaypointWorldLocation(Index))));
		}
		Entry->SetArrayField(TEXT("points"), Points);
		return Entry;
	}

	APatrolRouteActor* SpawnRoute(UWorld* World, const FString& Name, const TSharedPtr<FJsonObject>& Entry)
	{
		const TArray<TSharedPtr<FJsonValue>>* Points = nullptr;
		if (!Entry->TryGetArrayField(TEXT("points"), Points) || Points->IsEmpty())
		{
			return nullptr;
		}
		UClass* Class = LoadClass<APatrolRouteActor>(nullptr, *SaveString(Entry, TEXT("class")));
		Class = Class ? Class : APatrolRouteActor::StaticClass();
		const TArray<TSharedPtr<FJsonValue>>& First = (*Points)[0]->AsArray();
		const FVector Origin = First.Num() == 3 ? FVector(First[0]->AsNumber(), First[1]->AsNumber(), First[2]->AsNumber()) : FVector::ZeroVector;
		APatrolRouteActor* Route = World->SpawnActor<APatrolRouteActor>(Class, Origin, FRotator::ZeroRotator, SaveSpawnParams(Name));
		if (!Route)
		{
			return nullptr;
		}
		Route->bIsLoop = SaveBool(Entry, TEXT("loop"), true);
		Route->bPingPong = SaveBool(Entry, TEXT("ping_pong"), false);
		Route->DefaultWaitTimeSeconds = SaveFloat(Entry, TEXT("wait"), 3.f);
		const TArray<TSharedPtr<FJsonValue>>* Waits = nullptr;
		if (Entry->TryGetArrayField(TEXT("point_waits"), Waits))
		{
			for (const TSharedPtr<FJsonValue>& Wait : *Waits)
			{
				Route->PerPointWaitTime.Add(static_cast<float>(Wait->AsNumber()));
			}
		}
		USplineComponent* Spline = Route->GetRouteSpline();
		Spline->ClearSplinePoints(false);
		for (const TSharedPtr<FJsonValue>& Value : *Points)
		{
			const TArray<TSharedPtr<FJsonValue>>& P = Value->AsArray();
			if (P.Num() == 3)
			{
				Spline->AddSplinePoint(FVector(P[0]->AsNumber(), P[1]->AsNumber(), P[2]->AsNumber()), ESplineCoordinateSpace::World, false);
			}
		}
		for (int32 Index = 0; Index < Spline->GetNumberOfSplinePoints(); ++Index)
		{
			Spline->SetSplinePointType(Index, ESplinePointType::Linear, false);
		}
		Spline->SetClosedLoop(Route->bIsLoop, false);
		Spline->UpdateSpline();
		return Route;
	}

	TSharedRef<FJsonObject> SaveEnemy(const AEnemyCharacter* Enemy, bool bPreCombat)
	{
		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Enemy->GetName());
		Entry->SetStringField(TEXT("class"), Enemy->GetClass()->GetPathName());
		Entry->SetStringField(TEXT("archetype"), SaveEnumName(Enemy->GetArchetype()));
		Entry->SetArrayField(TEXT("pos"), SaveVector(Enemy->GetActorLocation()));
		Entry->SetNumberField(TEXT("rot_y"), Enemy->GetActorRotation().Yaw);
		const UHealthComponent* Health = Enemy->GetHealthComponent();
		Entry->SetNumberField(TEXT("health"), Health ? Health->GetCurrentHealth() : 0.f);
		Entry->SetNumberField(TEXT("max_health"), Health ? Health->GetMaxHealth() : 0.f);
		Entry->SetStringField(TEXT("route"), Enemy->AssignedPatrolRoute ? Enemy->AssignedPatrolRoute->GetName() : FString());
		const AEnemyCharacter* Leader = Enemy->GetEscortLeader();
		Entry->SetStringField(TEXT("escort_leader"), Leader ? Leader->GetName() : FString());
		FEnemyPatrolSnapshot Snapshot = Enemy->CapturePatrolSnapshot();
		if (bPreCombat && (Enemy->AssignedPatrolRoute || Leader))
		{
			// The ambush autosave: the patrol that just detected the squad is back on duty, not alerted.
			Snapshot.bOnPatrol = true;
			Snapshot.bSearching = false;
			Snapshot.Suspicion = 0.f;
		}
		TSharedRef<FJsonObject> Patrol = MakeShared<FJsonObject>();
		Patrol->SetBoolField(TEXT("active"), Snapshot.bOnPatrol);
		Patrol->SetNumberField(TEXT("waypoint"), Snapshot.WaypointIndex);
		Patrol->SetBoolField(TEXT("forward"), Snapshot.bForward);
		Patrol->SetNumberField(TEXT("phase"), Snapshot.Phase);
		Patrol->SetNumberField(TEXT("wait_left"), Snapshot.WaitLeft);
		Patrol->SetBoolField(TEXT("searching"), Snapshot.bSearching);
		Patrol->SetArrayField(TEXT("search_origin"), SaveVector(Snapshot.SearchOrigin));
		Patrol->SetNumberField(TEXT("search_elapsed"), Snapshot.SearchElapsed);
		Patrol->SetNumberField(TEXT("suspicion"), Snapshot.Suspicion);
		Entry->SetObjectField(TEXT("patrol"), Patrol);
		return Entry;
	}

	FEnemyPatrolSnapshot LoadPatrol(const TSharedPtr<FJsonObject>& Entry)
	{
		FEnemyPatrolSnapshot Snapshot;
		const TSharedPtr<FJsonObject>* Patrol = nullptr;
		if (!Entry->TryGetObjectField(TEXT("patrol"), Patrol))
		{
			return Snapshot;
		}
		Snapshot.bOnPatrol = SaveBool(*Patrol, TEXT("active"), false);
		Snapshot.WaypointIndex = SaveInt(*Patrol, TEXT("waypoint"), 0);
		Snapshot.bForward = SaveBool(*Patrol, TEXT("forward"), true);
		Snapshot.Phase = SaveInt(*Patrol, TEXT("phase"), 0);
		Snapshot.WaitLeft = SaveFloat(*Patrol, TEXT("wait_left"), 0.f);
		Snapshot.bSearching = SaveBool(*Patrol, TEXT("searching"), false);
		Snapshot.SearchOrigin = LoadVector(*Patrol, TEXT("search_origin"), FVector::ZeroVector);
		Snapshot.SearchElapsed = SaveFloat(*Patrol, TEXT("search_elapsed"), 0.f);
		Snapshot.Suspicion = SaveFloat(*Patrol, TEXT("suspicion"), 0.f);
		return Snapshot;
	}
}

FOnSaveAppliedNative& USaveGameSubsystem::OnSaveApplied()
{
	static FOnSaveAppliedNative Delegate;
	return Delegate;
}

bool USaveGameSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void USaveGameSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// Headless dev checks (-ExecCmds) run side by side and must not write Saved/SaveGames behind each other's back.
	bAutosaveEnabled = !FString(FCommandLine::Get()).Contains(TEXT("-ExecCmds"));
}

void USaveGameSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (UGameFlowSubsystem* Flow = InWorld.GetSubsystem<UGameFlowSubsystem>())
	{
		FlowTransitionHandle = Flow->OnBeforeGameFlowChanged.AddUObject(this, &USaveGameSubsystem::HandleFlowTransition);
	}
	const UGameInstance* GameInstance = InWorld.GetGameInstance();
	const UMissionSessionSubsystem* Session = GameInstance ? GameInstance->GetSubsystem<UMissionSessionSubsystem>() : nullptr;
	if (Session && Session->HasPendingLoad())
	{
		// Before the first rendered frame: the level's own start stays hidden until the save is in place.
		ShowLoadCover(InWorld);
		// After every actor began play and the mission start (squad, loadout) ran.
		InWorld.GetTimerManager().SetTimer(PendingLoadTimer, FTimerDelegate::CreateUObject(this, &USaveGameSubsystem::ApplyPendingLoad), 0.5f, false);
	}
}

void USaveGameSubsystem::ShowLoadCover(UWorld& InWorld)
{
	UGameViewportClient* Viewport = InWorld.GetGameViewport();
	if (!Viewport || LoadCoverWidget.IsValid())
	{
		return; // headless (-nullrhi): nothing is drawn anyway
	}
	LoadCoverFadeStart = -1.0;
	TWeakObjectPtr<USaveGameSubsystem> WeakThis(this);
	LoadCoverWidget = SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
		.BorderBackgroundColor_Lambda([WeakThis]()
		{
			const USaveGameSubsystem* Self = WeakThis.Get();
			if (!Self || Self->LoadCoverFadeStart < 0.0)
			{
				return FSlateColor(FLinearColor::Black);
			}
			const float Alpha = 1.f - FMath::Clamp(static_cast<float>((FPlatformTime::Seconds() - Self->LoadCoverFadeStart) / LoadCoverFadeSeconds), 0.f, 1.f);
			return FSlateColor(FLinearColor(0.f, 0.f, 0.f, Alpha));
		})
		.Visibility(EVisibility::HitTestInvisible);
	Viewport->AddViewportWidgetContent(LoadCoverWidget.ToSharedRef(), LoadCoverZOrder);
}

void USaveGameSubsystem::HideLoadCover()
{
	UWorld* World = GetWorld();
	if (!LoadCoverWidget.IsValid() || !World)
	{
		return;
	}
	// The camera follows the restored squad on the next frames; fade once it has settled, then drop the widget.
	World->GetTimerManager().SetTimer(LoadCoverTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		LoadCoverFadeStart = FPlatformTime::Seconds();
		if (UWorld* Inner = GetWorld())
		{
			Inner->GetTimerManager().SetTimer(LoadCoverTimer, FTimerDelegate::CreateUObject(this, &USaveGameSubsystem::RemoveLoadCover),
				LoadCoverFadeSeconds, false);
		}
	}), LoadCoverHoldSeconds, false);
}

void USaveGameSubsystem::RemoveLoadCover()
{
	if (!LoadCoverWidget.IsValid())
	{
		return;
	}
	if (UWorld* World = GetWorld())
	{
		if (UGameViewportClient* Viewport = World->GetGameViewport())
		{
			Viewport->RemoveViewportWidgetContent(LoadCoverWidget.ToSharedRef());
		}
	}
	LoadCoverWidget.Reset();
	LoadCoverFadeStart = -1.0;
}

void USaveGameSubsystem::Deinitialize()
{
	RemoveLoadCover();
	if (UWorld* World = GetWorld())
	{
		if (UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>())
		{
			Flow->OnBeforeGameFlowChanged.Remove(FlowTransitionHandle);
		}
	}
	Super::Deinitialize();
}

FString USaveGameSubsystem::GetSaveDirectory() const
{
	return SaveDirectoryOverride.IsEmpty() ? FPaths::ProjectSavedDir() / TEXT("SaveGames") : SaveDirectoryOverride;
}

FString USaveGameSubsystem::GetSavePath(const FString& SlotName) const
{
	return GetSaveDirectory() / (SaveGameRules::SanitizeSlotName(SlotName) + TEXT(".json"));
}

bool USaveGameSubsystem::HasSave(const FString& SlotName) const
{
	return FPaths::FileExists(GetSavePath(SlotName));
}

bool USaveGameSubsystem::DeleteSave(const FString& SlotName) const
{
	return HasSave(SlotName) && IFileManager::Get().Delete(*GetSavePath(SlotName));
}

void USaveGameSubsystem::Post(const FString& Speaker, const FString& Text) const
{
	if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
	{
		Messages->PostMessage(FText::FromString(Speaker), FText::FromString(Text));
	}
}

FString USaveGameSubsystem::GetCurrentStageName() const
{
	UWorld* World = GetWorld();
	const UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
	const UQuestSubsystem* Quests = World->GetSubsystem<UQuestSubsystem>();
	const USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
	const AGateActor* Gate = SaveFindGate(World);
	const bool bPreparation = Flow && Flow->GetPhase() == ECodexGamePhase::Preparation && Flow->GetWaveIndex() <= 1;
	return SaveGameRules::GetStageName(Quests ? Quests->GetState() : FQuestChainState(), Gate && Gate->IsOpen(),
		Flow && Flow->IsWaveActive(), bPreparation, Flow ? FMath::Max(1, Flow->GetWaveIndex()) : 1, Squad && Squad->IsSoloMode());
}

TSharedRef<FJsonObject> USaveGameSubsystem::CaptureWorldState() const
{
	return BuildSaveData(TEXT("snapshot"), TEXT("snapshot"), TEXT("snapshot"), FPreCombatOverride());
}

TSharedRef<FJsonObject> USaveGameSubsystem::BuildSaveData(const FString& SlotName, const FString& Title, const FString& Author,
	const FPreCombatOverride& PreCombat) const
{
	UWorld* World = GetWorld();
	const USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
	const UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
	const UQuestSubsystem* Quests = World->GetSubsystem<UQuestSubsystem>();
	const FDateTime Now = FDateTime::Now();

	TArray<TSharedPtr<FJsonValue>> SquadList;
	TArray<TPair<float, float>> Health;
	for (const AOperativeCharacter* Member : Squad ? Squad->GetMembers() : TArray<AOperativeCharacter*>())
	{
		Health.Emplace(Member->HealthComponent ? Member->HealthComponent->GetCurrentHealth() : 0.f,
			Member->HealthComponent ? Member->HealthComponent->GetMaxHealth() : 100.f);
		SquadList.Add(MakeShared<FJsonValueObject>(SaveOperative(Member, Squad)));
	}

	TSharedRef<FJsonObject> GameState = MakeShared<FJsonObject>();
	const ECodexGamePhase LivePhase = Flow ? Flow->GetPhase() : ECodexGamePhase::Exploration;
	ECodexGamePhase Phase = LivePhase;
	bool bCombatUnlocked = Flow && Flow->IsCombatUnlocked();
	int32 WaveIndex = Flow ? FMath::Max(1, Flow->GetWaveIndex()) : 1;
	float PreparationLeft = Flow && LivePhase == ECodexGamePhase::Preparation ? Flow->GetPreparationTimeRemaining() : 0.f;
	if (PreCombat.bActive)
	{
		// The fight has just begun: the save holds the moment before it (preparation of the wave / exploration before the ambush).
		Phase = PreCombat.bAmbush ? ECodexGamePhase::Exploration : ECodexGamePhase::Preparation;
		bCombatUnlocked = !PreCombat.bAmbush;
		WaveIndex = PreCombat.bAmbush ? 1 : FMath::Max(1, PreCombat.WaveIndex);
		PreparationLeft = 0.f; // the full preparation / rest
	}
	GameState->SetBoolField(TEXT("is_game_started"), true);
	GameState->SetBoolField(TEXT("is_combat_phase_unlocked"), bCombatUnlocked);
	GameState->SetBoolField(TEXT("is_preparation_active"), Phase == ECodexGamePhase::Preparation);
	GameState->SetBoolField(TEXT("is_wave_active"), Phase == ECodexGamePhase::WaveCombat);
	GameState->SetNumberField(TEXT("current_wave_index"), WaveIndex);
	GameState->SetBoolField(TEXT("is_solo_mode"), Squad && Squad->IsSoloMode());
	// Version 2.
	GameState->SetStringField(TEXT("phase"), SaveGameRules::PhaseToString(Phase));
	GameState->SetNumberField(TEXT("preparation_time_left"), PreparationLeft);
	GameState->SetBoolField(TEXT("is_ambush_fight"), Flow && Flow->IsAmbushFight() && !PreCombat.bActive);
	GameState->SetStringField(TEXT("squad_posture"), Squad ? SaveEnumName(Squad->GetSquadPosture()) : FString());
	GameState->SetBoolField(TEXT("autonomous_combat"), Squad && Squad->IsAutonomousSquadCombat());

	TSharedRef<FJsonObject> QuestState = MakeShared<FJsonObject>();
	const FQuestChainState Chain = Quests ? Quests->GetState() : FQuestChainState();
	const AGateActor* Gate = SaveFindGate(World);
	QuestState->SetBoolField(TEXT("has_empty_canister"), Chain.bHasEmptyCanister);
	QuestState->SetBoolField(TEXT("has_fuel_canister"), Chain.bHasFuelCanister);
	QuestState->SetBoolField(TEXT("is_generator_running"), Chain.bIsGeneratorRunning);
	QuestState->SetBoolField(TEXT("is_gate_powered"), Chain.bIsGatePowered);
	QuestState->SetBoolField(TEXT("is_gate_open"), Gate && (Gate->IsOpen() || Gate->IsOpening()));

	TSharedRef<FJsonObject> WorldState = MakeShared<FJsonObject>();
	TSharedRef<FJsonObject> Crates = MakeShared<FJsonObject>();
	for (TActorIterator<ALootCrateActor> It(World); It; ++It)
	{
		TSharedRef<FJsonObject> Crate = MakeShared<FJsonObject>();
		Crate->SetBoolField(TEXT("is_looted"), It->IsLooted());
		Crate->SetBoolField(TEXT("is_defused"), It->bDefused);
		Crate->SetBoolField(TEXT("is_destroyed"), It->IsDestroyed());
		Crate->SetObjectField(TEXT("stash"), SaveStash(It->GetSyncedStash())); // Sprint 13: exact contents (two-way crates)
		Crates->SetObjectField(It->GetName(), Crate);
	}
	WorldState->SetObjectField(TEXT("crates"), Crates);
	// Sprint 13: piles dropped on the ground (sorted by place: the order does not depend on spawn order).
	TArray<ADroppedItemActor*> PileActors;
	for (TActorIterator<ADroppedItemActor> It(World); It; ++It)
	{
		if (!It->IsActorBeingDestroyed() && !It->GetStash()->IsEmpty())
		{
			PileActors.Add(*It);
		}
	}
	PileActors.Sort([](const ADroppedItemActor& A, const ADroppedItemActor& B)
	{
		const FVector LA = A.GetActorLocation();
		const FVector LB = B.GetActorLocation();
		return LA.X != LB.X ? LA.X < LB.X : LA.Y < LB.Y;
	});
	TArray<TSharedPtr<FJsonValue>> Piles;
	for (ADroppedItemActor* PileActor : PileActors)
	{
		TSharedRef<FJsonObject> Pile = MakeShared<FJsonObject>();
		Pile->SetArrayField(TEXT("location"), SaveVector(PileActor->GetActorLocation()));
		Pile->SetObjectField(TEXT("items"), SaveStash(PileActor->GetStash()));
		Piles.Add(MakeShared<FJsonValueObject>(Pile));
	}
	WorldState->SetArrayField(TEXT("dropped_items"), Piles);
	WorldState->SetArrayField(TEXT("dismantled_objects"), {});

	// Version 2: every other interactable by its actor name.
	TSharedRef<FJsonObject> Objects = MakeShared<FJsonObject>();
	for (TActorIterator<AInteractableActor> It(World); It; ++It)
	{
		if (SaveIsTrackedObject(*It))
		{
			Objects->SetObjectField(It->GetName(), SaveObject(*It));
		}
	}
	WorldState->SetObjectField(TEXT("objects"), Objects);
	TSharedRef<FJsonObject> Gates = MakeShared<FJsonObject>();
	for (TActorIterator<AGateActor> It(World); It; ++It)
	{
		Gates->SetBoolField(It->GetName(), It->IsOpen() || It->IsOpening());
	}
	WorldState->SetObjectField(TEXT("gates"), Gates);
	TSharedRef<FJsonObject> Triggers = MakeShared<FJsonObject>();
	for (TActorIterator<ADialogueTriggerVolume> It(World); It; ++It)
	{
		Triggers->SetBoolField(It->GetName(), It->HasTriggered());
	}
	WorldState->SetObjectField(TEXT("dialogue_triggers"), Triggers);
	TSharedRef<FJsonObject> Routes = MakeShared<FJsonObject>();
	for (TActorIterator<APatrolRouteActor> It(World); It; ++It)
	{
		if (!It->IsActorBeingDestroyed())
		{
			Routes->SetObjectField(It->GetName(), SaveRoute(*It));
		}
	}
	WorldState->SetObjectField(TEXT("patrol_routes"), Routes);
	TArray<AEnemyCharacter*> EnemyActors;
	for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
	{
		const UHealthComponent* EnemyHealth = It->GetHealthComponent();
		if (!It->IsActorBeingDestroyed() && !It->IsDying() && EnemyHealth && EnemyHealth->IsAlive())
		{
			EnemyActors.Add(*It);
		}
	}
	EnemyActors.Sort([](const AEnemyCharacter& A, const AEnemyCharacter& B) { return A.GetName() < B.GetName(); });
	TArray<TSharedPtr<FJsonValue>> Enemies;
	for (const AEnemyCharacter* Enemy : EnemyActors)
	{
		Enemies.Add(MakeShared<FJsonValueObject>(SaveEnemy(Enemy, PreCombat.bActive && PreCombat.bAmbush)));
	}
	WorldState->SetArrayField(TEXT("enemies"), Enemies);

	const FString Stage = GetCurrentStageName();
	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetNumberField(TEXT("version"), SaveGameRules::CurrentSaveVersion);
	Data->SetNumberField(TEXT("timestamp"), static_cast<double>(Now.ToUnixTimestamp()));
	Data->SetStringField(TEXT("datetime_str"), Now.ToString(TEXT("%Y-%m-%d %H:%M:%S")));
	Data->SetStringField(TEXT("save_type"), SaveGameRules::GetSaveType(SlotName));
	Data->SetStringField(TEXT("slot_name"), SaveGameRules::SanitizeSlotName(SlotName));
	Data->SetStringField(TEXT("title"), Title);
	Data->SetStringField(TEXT("author"), Author);
	Data->SetStringField(TEXT("stage_name"), Stage);
	Data->SetStringField(TEXT("squad_summary"), SaveGameRules::GetSquadSummary(Health));
	Data->SetStringField(TEXT("map"), UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()));
	Data->SetArrayField(TEXT("squad"), SquadList);
	Data->SetObjectField(TEXT("game_state"), GameState);
	Data->SetObjectField(TEXT("quest_state"), QuestState);
	Data->SetObjectField(TEXT("world_state"), WorldState);
	// Godot save_manager.gd: "susanin" only once he exists (version 2: also his rescue and, out of the squad, himself).
	if (const URecruitSubsystem* Recruits = GetWorld()->GetSubsystem<URecruitSubsystem>(); Recruits && Recruits->GetSusanin())
	{
		const TSharedRef<FJsonObject> SusaninState = MakeShared<FJsonObject>();
		SusaninState->SetBoolField(TEXT("is_recruited"), Recruits->IsSusaninRecruited());
		SusaninState->SetBoolField(TEXT("rescue_triggered"), Recruits->IsRescueTriggered());
		if (!Recruits->IsSusaninRecruited())
		{
			SusaninState->SetObjectField(TEXT("operative"), SaveOperative(Recruits->GetSusanin(), Squad));
		}
		Data->SetObjectField(TEXT("susanin"), SusaninState);
	}
	return Data;
}

bool USaveGameSubsystem::WriteSave(const FString& SlotName, const FString& CustomTitle, const FString& Author, const FPreCombatOverride& PreCombat)
{
	const FString Slot = SaveGameRules::SanitizeSlotName(SlotName);
	const FString Type = SaveGameRules::GetSaveType(Slot);
	FString Title = CustomTitle;
	if (Title.IsEmpty())
	{
		Title = Type == TEXT("autosave") ? TEXT("Autosave: ") + GetCurrentStageName()
			: (Type == TEXT("quicksave") ? TEXT("Quicksave: ") + GetCurrentStageName() : Slot);
	}
	FString Json;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	if (!FJsonSerializer::Serialize(BuildSaveData(Slot, Title, Author, PreCombat), Writer))
	{
		return false;
	}
	IFileManager::Get().MakeDirectory(*GetSaveDirectory(), true);
	const bool bSaved = FFileHelper::SaveStringToFile(Json, *GetSavePath(Slot), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	UCodexEventBus* Bus = UCodexEventBus::Get(this);
	if (bSaved && Bus)
	{
		Bus->OnGameSaved.Broadcast(Slot, Type == TEXT("autosave"));
	}
	return bSaved;
}

bool USaveGameSubsystem::SaveGame(const FString& SlotName, const FString& CustomTitle, const FString& Author)
{
	return WriteSave(SlotName, CustomTitle, Author, FPreCombatOverride());
}

bool USaveGameSubsystem::LoadGame(const FString& SlotName)
{
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *GetSavePath(SlotName)))
	{
		return false;
	}
	TSharedPtr<FJsonObject> Data;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Data) || !Data.IsValid())
	{
		return false;
	}
	const int32 Version = SaveInt(Data, TEXT("version"), 1);
	if (Version > SaveGameRules::CurrentSaveVersion)
	{
		UE_LOG(LogCodexTactics, Warning, TEXT("Save \"%s\" has format version %d (this build writes %d): loading what is known."),
			*SlotName, Version, SaveGameRules::CurrentSaveVersion);
	}
	ApplySaveData(Data.ToSharedRef());
	const FString Slot = SaveGameRules::SanitizeSlotName(SlotName);
	if (UCodexEventBus* Bus = UCodexEventBus::Get(this))
	{
		Bus->OnGameLoaded.Broadcast(Slot);
	}
	OnSaveApplied().Broadcast(GetWorld(), Slot);
	return true;
}

bool USaveGameSubsystem::LoadGameWithTravel(const FString& SlotName)
{
	FSaveSlotInfo Info;
	UWorld* World = GetWorld();
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	UMissionSessionSubsystem* Session = GameInstance ? GameInstance->GetSubsystem<UMissionSessionSubsystem>() : nullptr;
	if (!Session || !GetSaveInfo(SlotName, Info))
	{
		return false;
	}
	Session->PendingLoadSlot = SaveGameRules::SanitizeSlotName(SlotName);
	Session->PendingLoadDirectory = SaveDirectoryOverride;
	const FString Map = Info.MapName.IsEmpty() ? UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()) : Info.MapName;
	UE_LOG(LogCodexTactics, Display, TEXT("Save: loading \"%s\" on %s (level reopened)"), *Session->PendingLoadSlot, *Map);
	UGameplayStatics::OpenLevel(World, FName(*Map));
	return true;
}

void USaveGameSubsystem::ApplyPendingLoad()
{
	UWorld* World = GetWorld();
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	UMissionSessionSubsystem* Session = GameInstance ? GameInstance->GetSubsystem<UMissionSessionSubsystem>() : nullptr;
	if (!Session || !Session->HasPendingLoad())
	{
		return;
	}
	const FString Slot = Session->PendingLoadSlot;
	SaveDirectoryOverride = Session->PendingLoadDirectory;
	Session->PendingLoadSlot.Reset();
	Session->PendingLoadDirectory.Reset();
	const bool bLoaded = LoadGame(Slot);
	UE_LOG(LogCodexTactics, Display, TEXT("Save: pending load \"%s\" %s"), *Slot, bLoaded ? TEXT("applied") : TEXT("FAILED"));
	if (bLoaded)
	{
		Post(TEXT("SYSTEM"), FString::Printf(TEXT("📂 Save \"%s\" loaded!"), *Slot));
		// The squad was teleported to its saved places: the camera jumps there instead of gliding over the level.
		if (ATacticalCameraPawn* Camera = Cast<ATacticalCameraPawn>(UGameplayStatics::GetPlayerPawn(World, 0)))
		{
			Camera->SnapToFollowTarget();
		}
	}
	HideLoadCover();
}

FString USaveGameSubsystem::GetContinueSlot() const
{
	const TArray<FSaveSlotInfo> All = GetAllSaves();
	return All.IsEmpty() ? FString() : All[0].SlotName;
}

void USaveGameSubsystem::ApplySaveData(const TSharedRef<FJsonObject>& Data)
{
	UWorld* World = GetWorld();
	USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
	const int32 Version = SaveInt(Data, TEXT("version"), 1);

	// Whatever was being carried / placed in this session is dropped first (the objects get their saved places below).
	if (URelocationSubsystem* Relocation = World->GetSubsystem<URelocationSubsystem>())
	{
		Relocation->CancelPlacement();
		for (AOperativeCharacter* Member : Squad ? Squad->GetMembers() : TArray<AOperativeCharacter*>())
		{
			Relocation->CancelActiveTask(Member);
		}
	}

	// 0. Susanin first, so a recruited Susanin takes his squad entry below.
	const TSharedPtr<FJsonObject>* SusaninState = nullptr;
	URecruitSubsystem* Recruits = World->GetSubsystem<URecruitSubsystem>();
	if (Recruits)
	{
		if (Data->TryGetObjectField(TEXT("susanin"), SusaninState))
		{
			const bool bRecruited = SaveBool(*SusaninState, TEXT("is_recruited"), false);
			if (Version >= 2)
			{
				Recruits->RestoreRescue(SaveBool(*SusaninState, TEXT("rescue_triggered"), true), bRecruited);
				const TSharedPtr<FJsonObject>* Operative = nullptr;
				if (!bRecruited && Recruits->GetSusanin() && (*SusaninState)->TryGetObjectField(TEXT("operative"), Operative))
				{
					ApplyOperative(Recruits->GetSusanin(), *Operative);
				}
			}
			else
			{
				Recruits->RestoreRecruited(bRecruited);
			}
		}
		else if (Version >= 2)
		{
			Recruits->RestoreRescue(false, false); // no Susanin at the save
		}
	}

	// 1. Squad.
	AOperativeCharacter* NewLeader = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* SquadList = nullptr;
	if (Squad && Data->TryGetArrayField(TEXT("squad"), SquadList))
	{
		for (const TSharedPtr<FJsonValue>& Value : *SquadList)
		{
			const TSharedPtr<FJsonObject> Info = Value->AsObject();
			if (!Info.IsValid())
			{
				continue;
			}
			const FString Name = SaveString(Info, TEXT("character_name"));
			const FString Role = SaveString(Info, TEXT("node_name"));
			AOperativeCharacter* Member = nullptr;
			for (AOperativeCharacter* Candidate : Squad->GetMembers())
			{
				if (Candidate->DisplayName.ToString() == Name || (!Member && UEnum::GetValueAsString(Candidate->SquadRole) == Role))
				{
					Member = Candidate;
				}
			}
			if (!Member)
			{
				continue;
			}
			ApplyOperative(Member, Info);
			if (SaveBool(Info, TEXT("is_leader"), false))
			{
				NewLeader = Member;
			}
		}
	}

	// 2. Game state (and solo mode, posture, Commander Mode).
	const TSharedPtr<FJsonObject>* GameState = nullptr;
	if (Data->TryGetObjectField(TEXT("game_state"), GameState))
	{
		if (UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>())
		{
			ECodexGamePhase SavedPhase = ECodexGamePhase::Exploration;
			const bool bHasPhase = SaveGameRules::ParsePhase(SaveString(*GameState, TEXT("phase")), SavedPhase);
			const FGameFlowConfig& Config = Flow->GetConfig();
			const FSaveLoadFlow Resolved = SaveGameRules::ResolveLoadFlow(bHasPhase ? Version : 1, SavedPhase,
				SaveBool(*GameState, TEXT("is_combat_phase_unlocked"), false), SaveBool(*GameState, TEXT("is_preparation_active"), false),
				SaveBool(*GameState, TEXT("is_wave_active"), false), SaveInt(*GameState, TEXT("current_wave_index"), 1), Config.TotalWaves,
				SaveBool(*GameState, TEXT("is_ambush_fight"), false), Config.bAmbushSingleFight);
			const bool bSamePreparation = bHasPhase && SavedPhase == ECodexGamePhase::Preparation;
			Flow->RestoreForLoad(Resolved.bCombatUnlocked, Resolved.Phase == ECodexGamePhase::Preparation, Resolved.WaveIndex,
				bSamePreparation ? SaveFloat(*GameState, TEXT("preparation_time_left"), -1.f) : -1.f);
		}
		if (Squad)
		{
			if (NewLeader)
			{
				Squad->SetLeader(NewLeader);
			}
			const bool bSolo = SaveBool(*GameState, TEXT("is_solo_mode"), false);
			if (bSolo && !Squad->IsSoloMode())
			{
				Squad->EnterSoloMode();
			}
			else if (!bSolo && Squad->IsSoloMode())
			{
				Squad->ExitSoloMode();
			}
			ESquadFirePosture Posture = FirePostureRules::DefaultPosture;
			if (LoadEnumName(SaveString(*GameState, TEXT("squad_posture")), Posture))
			{
				// SetSquadPosture clears the overrides: put the saved ones back.
				TMap<AOperativeCharacter*, TPair<bool, ESquadFirePosture>> Overrides;
				for (AOperativeCharacter* Member : Squad->GetMembers())
				{
					Overrides.Add(Member, { Member->bHasPostureOverride, Member->PostureOverride });
				}
				Squad->SetSquadPosture(Posture);
				for (const TPair<AOperativeCharacter*, TPair<bool, ESquadFirePosture>>& Entry : Overrides)
				{
					Entry.Key->bHasPostureOverride = Entry.Value.Key;
					Entry.Key->PostureOverride = Entry.Value.Value;
				}
			}
			if ((*GameState)->HasField(TEXT("autonomous_combat")))
			{
				Squad->SetAutonomousSquadCombat(SaveBool(*GameState, TEXT("autonomous_combat"), false));
			}
			Squad->RefreshFormation();
		}
	}

	// 3. Quest chain.
	const TSharedPtr<FJsonObject>* QuestState = nullptr;
	if (Data->TryGetObjectField(TEXT("quest_state"), QuestState))
	{
		if (UQuestSubsystem* Quests = World->GetSubsystem<UQuestSubsystem>())
		{
			FQuestChainState Chain;
			Chain.bHasEmptyCanister = SaveBool(*QuestState, TEXT("has_empty_canister"), false);
			Chain.bHasFuelCanister = SaveBool(*QuestState, TEXT("has_fuel_canister"), false);
			Chain.bIsGeneratorRunning = SaveBool(*QuestState, TEXT("is_generator_running"), false);
			Chain.bIsGatePowered = SaveBool(*QuestState, TEXT("is_gate_powered"), false) || SaveBool(*QuestState, TEXT("is_gate_open"), false);
			Quests->RestoreState(Chain);
		}
	}

	// 4. World: supply crates.
	const TSharedPtr<FJsonObject>* WorldState = nullptr;
	const TSharedPtr<FJsonObject>* Crates = nullptr;
	if (Data->TryGetObjectField(TEXT("world_state"), WorldState) && (*WorldState)->TryGetObjectField(TEXT("crates"), Crates))
	{
		for (TActorIterator<ALootCrateActor> It(World); It; ++It)
		{
			const TSharedPtr<FJsonObject>* Crate = nullptr;
			if ((*Crates)->TryGetObjectField(It->GetName(), Crate))
			{
				It->RestoreSaved(SaveBool(*Crate, TEXT("is_looted"), false), SaveBool(*Crate, TEXT("is_defused"), false),
					SaveBool(*Crate, TEXT("is_destroyed"), false));
				const TSharedPtr<FJsonObject>* Stash = nullptr;
				if (!It->IsDestroyed() && (*Crate)->TryGetObjectField(TEXT("stash"), Stash))
				{
					It->GetSyncedStash()->SetContents(LoadStash(*Stash)); // older saves: the authored loot stays
				}
			}
		}
	}
	if (!WorldState)
	{
		return;
	}
	// Sprint 13: piles on the ground (older saves have none: the piles of the session are cleared either way).
	for (TActorIterator<ADroppedItemActor> It(World); It; ++It)
	{
		It->Destroy();
	}
	const TArray<TSharedPtr<FJsonValue>>* Piles = nullptr;
	if ((*WorldState)->TryGetArrayField(TEXT("dropped_items"), Piles))
	{
		for (const TSharedPtr<FJsonValue>& Value : *Piles)
		{
			const TSharedPtr<FJsonObject>* Pile = nullptr;
			const TSharedPtr<FJsonObject>* Items = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(Pile) || !(*Pile)->HasField(TEXT("location")) || !(*Pile)->TryGetObjectField(TEXT("items"), Items))
			{
				continue;
			}
			const FVector Point = LoadVector(*Pile, TEXT("location"), FVector::ZeroVector);
			for (const TPair<ETransferItem, int32>& Pair : LoadStash(*Items))
			{
				ADroppedItemActor::SpawnOrMerge(World, Point, Pair.Key, Pair.Value);
			}
		}
	}

	// 5. Version 2: gates (after the quest chain, which starts opening a powered gate), dialogue triggers.
	const TSharedPtr<FJsonObject>* Gates = nullptr;
	if ((*WorldState)->TryGetObjectField(TEXT("gates"), Gates))
	{
		for (TActorIterator<AGateActor> It(World); It; ++It)
		{
			bool bOpen = false;
			if ((*Gates)->TryGetBoolField(It->GetName(), bOpen))
			{
				It->RestoreOpen(bOpen);
			}
		}
	}
	const TSharedPtr<FJsonObject>* Triggers = nullptr;
	if ((*WorldState)->TryGetObjectField(TEXT("dialogue_triggers"), Triggers))
	{
		for (TActorIterator<ADialogueTriggerVolume> It(World); It; ++It)
		{
			bool bTriggered = false;
			if ((*Triggers)->TryGetBoolField(It->GetName(), bTriggered))
			{
				It->RestoreTriggered(bTriggered);
			}
		}
	}

	// 6. Version 2: interactables. Saved ones are restored; deployables / tripwires placed after the save are removed and
	// the ones picked up / blown since are put back.
	const TSharedPtr<FJsonObject>* Objects = nullptr;
	if ((*WorldState)->TryGetObjectField(TEXT("objects"), Objects))
	{
		TSet<FString> Seen;
		TArray<AInteractableActor*> Existing;
		for (TActorIterator<AInteractableActor> It(World); It; ++It)
		{
			if (SaveIsTrackedObject(*It))
			{
				Existing.Add(*It);
			}
		}
		for (AInteractableActor* Object : Existing)
		{
			const TSharedPtr<FJsonObject>* Entry = nullptr;
			if ((*Objects)->TryGetObjectField(Object->GetName(), Entry) && SaveString(*Entry, TEXT("class")) == Object->GetClass()->GetPathName())
			{
				ApplyObject(Object, *Entry);
				Seen.Add(Object->GetName());
			}
			else if (SaveIsDynamicObjectClass(Object->GetClass()))
			{
				Object->Destroy();
			}
		}
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : (*Objects)->Values)
		{
			const TSharedPtr<FJsonObject> Entry = Field.Value.IsValid() ? Field.Value->AsObject() : nullptr;
			if (Seen.Contains(Field.Key) || !Entry.IsValid())
			{
				continue;
			}
			UClass* Class = LoadClass<AInteractableActor>(nullptr, *SaveString(Entry, TEXT("class")));
			if (!SaveIsDynamicObjectClass(Class))
			{
				continue; // a level object missing from this world is not recreated
			}
			AInteractableActor* Spawned = World->SpawnActor<AInteractableActor>(Class, LoadVector(Entry, TEXT("pos"), FVector::ZeroVector),
				LoadRotator(Entry, TEXT("rot"), FRotator::ZeroRotator), SaveSpawnParams(Field.Key));
			if (Spawned)
			{
				ApplyObject(Spawned, Entry);
			}
		}
	}

	// 7. Version 2: patrol routes (a missing runtime route is rebuilt), then the enemies: the living ones of the save come
	// back where they were with their health and patrol / search; anyone not in the save is gone (dead stay dead).
	TMap<FString, APatrolRouteActor*> RouteByName;
	for (TActorIterator<APatrolRouteActor> It(World); It; ++It)
	{
		RouteByName.Add(It->GetName(), *It);
	}
	const TSharedPtr<FJsonObject>* Routes = nullptr;
	if ((*WorldState)->TryGetObjectField(TEXT("patrol_routes"), Routes))
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : (*Routes)->Values)
		{
			if (!RouteByName.Contains(Field.Key) && Field.Value.IsValid() && Field.Value->AsObject().IsValid())
			{
				if (APatrolRouteActor* Route = SpawnRoute(World, Field.Key, Field.Value->AsObject()))
				{
					RouteByName.Add(Field.Key, Route);
				}
			}
		}
	}
	const TArray<TSharedPtr<FJsonValue>>* Enemies = nullptr;
	if ((*WorldState)->TryGetArrayField(TEXT("enemies"), Enemies))
	{
		TMap<FString, TSharedPtr<FJsonObject>> Saved;
		for (const TSharedPtr<FJsonValue>& Value : *Enemies)
		{
			const TSharedPtr<FJsonObject> Entry = Value.IsValid() ? Value->AsObject() : nullptr;
			if (Entry.IsValid())
			{
				Saved.Add(SaveString(Entry, TEXT("name")), Entry);
			}
		}
		TMap<FString, AEnemyCharacter*> EnemyBySavedName;
		TArray<AEnemyCharacter*> Living;
		for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
		{
			if (!It->IsActorBeingDestroyed())
			{
				Living.Add(*It);
			}
		}
		for (AEnemyCharacter* Enemy : Living)
		{
			const TSharedPtr<FJsonObject>* Entry = Saved.Find(Enemy->GetName());
			const bool bUsable = Entry && !Enemy->IsDying() && Enemy->GetHealthComponent() && Enemy->GetHealthComponent()->IsAlive()
				&& SaveString(*Entry, TEXT("class")) == Enemy->GetClass()->GetPathName();
			if (bUsable)
			{
				EnemyBySavedName.Add(Enemy->GetName(), Enemy);
			}
			else
			{
				Enemy->Destroy();
			}
		}
		for (const TPair<FString, TSharedPtr<FJsonObject>>& Pair : Saved)
		{
			const TSharedPtr<FJsonObject>& Entry = Pair.Value;
			AEnemyCharacter* Enemy = EnemyBySavedName.FindRef(Pair.Key);
			const FVector Location = LoadVector(Entry, TEXT("pos"), FVector::ZeroVector);
			const FRotator Rotation(0.f, SaveFloat(Entry, TEXT("rot_y"), 0.f), 0.f);
			if (!Enemy)
			{
				UClass* Class = LoadClass<AEnemyCharacter>(nullptr, *SaveString(Entry, TEXT("class")));
				Enemy = Class ? World->SpawnActor<AEnemyCharacter>(Class, Location, Rotation, SaveSpawnParams(Pair.Key)) : nullptr;
				EEnemyArchetype Archetype = EEnemyArchetype::FrostHound;
				if (Enemy && LoadEnumName(SaveString(Entry, TEXT("archetype")), Archetype))
				{
					Enemy->InitializeArchetype(Archetype);
				}
				if (!Enemy)
				{
					UE_LOG(LogCodexTactics, Warning, TEXT("Save: enemy %s (%s) could not be recreated"), *Pair.Key, *SaveString(Entry, TEXT("class")));
					continue;
				}
				EnemyBySavedName.Add(Pair.Key, Enemy);
			}
			if (AAIController* AIC = Cast<AAIController>(Enemy->GetController()))
			{
				AIC->StopMovement();
			}
			Enemy->TeleportTo(Location, Rotation, false, true);
			if (UHealthComponent* EnemyHealth = Enemy->GetHealthComponent())
			{
				const float Max = SaveFloat(Entry, TEXT("max_health"), EnemyHealth->GetMaxHealth());
				EnemyHealth->RestoreHealth(FMath::Max(1.f, SaveFloat(Entry, TEXT("health"), Max)), Max);
			}
		}
		// Patrols once everybody exists (an escort needs its leader).
		for (const TPair<FString, TSharedPtr<FJsonObject>>& Pair : Saved)
		{
			if (AEnemyCharacter* Enemy = EnemyBySavedName.FindRef(Pair.Key))
			{
				APatrolRouteActor* Route = RouteByName.FindRef(SaveString(Pair.Value, TEXT("route")));
				AEnemyCharacter* Leader = EnemyBySavedName.FindRef(SaveString(Pair.Value, TEXT("escort_leader")));
				Enemy->RestorePatrolSnapshot(Route, Leader, LoadPatrol(Pair.Value));
			}
		}
	}
}

bool USaveGameSubsystem::GetSaveInfo(const FString& SlotName, FSaveSlotInfo& OutInfo) const
{
	FString Json;
	TSharedPtr<FJsonObject> Data;
	if (!FFileHelper::LoadFileToString(Json, *GetSavePath(SlotName)) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Data)
		|| !Data.IsValid())
	{
		return false;
	}
	const FString Slot = SaveGameRules::SanitizeSlotName(SlotName);
	OutInfo.SlotName = Data->HasField(TEXT("slot_name")) ? Data->GetStringField(TEXT("slot_name")) : Slot;
	OutInfo.Title = Data->HasField(TEXT("title")) ? Data->GetStringField(TEXT("title")) : Slot;
	double Timestamp = 0.0;
	Data->TryGetNumberField(TEXT("timestamp"), Timestamp);
	OutInfo.Timestamp = static_cast<int64>(Timestamp);
	Data->TryGetStringField(TEXT("datetime_str"), OutInfo.DateTime);
	OutInfo.SaveType = Data->HasField(TEXT("save_type")) ? Data->GetStringField(TEXT("save_type")) : SaveGameRules::GetSaveType(Slot);
	OutInfo.Author = Data->HasField(TEXT("author")) ? Data->GetStringField(TEXT("author")) : TEXT("Commander");
	OutInfo.StageName = Data->HasField(TEXT("stage_name")) ? Data->GetStringField(TEXT("stage_name")) : TEXT("Unknown stage");
	Data->TryGetStringField(TEXT("squad_summary"), OutInfo.SquadSummary);
	const TSharedPtr<FJsonObject>* GameState = nullptr;
	OutInfo.Wave = Data->TryGetObjectField(TEXT("game_state"), GameState) ? SaveInt(*GameState, TEXT("current_wave_index"), 1) : 1;
	const TArray<TSharedPtr<FJsonValue>>* SquadList = nullptr;
	OutInfo.SquadCount = Data->TryGetArrayField(TEXT("squad"), SquadList) ? SquadList->Num() : 0;
	OutInfo.Version = SaveInt(Data, TEXT("version"), 1);
	Data->TryGetStringField(TEXT("map"), OutInfo.MapName);
	return true;
}

TArray<FSaveSlotInfo> USaveGameSubsystem::GetAllSaves() const
{
	TArray<FSaveSlotInfo> Saves;
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *(GetSaveDirectory() / TEXT("*.json")), true, false);
	for (const FString& File : Files)
	{
		FSaveSlotInfo Info;
		if (GetSaveInfo(FPaths::GetBaseFilename(File), Info))
		{
			Saves.Add(Info);
		}
	}
	Saves.Sort([](const FSaveSlotInfo& A, const FSaveSlotInfo& B) { return A.Timestamp > B.Timestamp; });
	return Saves;
}

FString USaveGameSubsystem::SuggestNextSlotName() const
{
	TArray<FString> Slots;
	for (const FSaveSlotInfo& Info : GetAllSaves())
	{
		Slots.Add(Info.SlotName);
	}
	return SaveGameRules::SuggestNextSlotName(Slots);
}

ESaveBlockReason USaveGameSubsystem::GetSaveBlockReason() const
{
	const UGameFlowSubsystem* Flow = GetWorld() ? GetWorld()->GetSubsystem<UGameFlowSubsystem>() : nullptr;
	return Flow ? SaveGameRules::GetSaveBlockReason(Flow->GetPhase()) : ESaveBlockReason::None;
}

bool USaveGameSubsystem::CanSaveNow(FText* OutReason) const
{
	const ESaveBlockReason Reason = GetSaveBlockReason();
	if (OutReason)
	{
		*OutReason = SaveGameRules::GetSaveBlockText(Reason);
	}
	return Reason == ESaveBlockReason::None;
}

bool USaveGameSubsystem::QuickSave()
{
	FText Reason;
	if (!CanSaveNow(&Reason))
	{
		Post(TEXT("SYSTEM"), FString::Printf(TEXT("⛔ %s."), *Reason.ToString()));
		return false;
	}
	const bool bSaved = SaveGame(TEXT("quicksave"), TEXT("Quicksave"));
	Post(TEXT("SYSTEM"), bSaved ? TEXT("⚡ Game quicksaved [F5]!") : TEXT("❌ Quicksave failed!"));
	return bSaved;
}

bool USaveGameSubsystem::QuickLoad()
{
	const FString Slot = GetContinueSlot();
	if (Slot.IsEmpty())
	{
		Post(TEXT("SYSTEM"), TEXT("⛔ No saves to load."));
		return false;
	}
	const bool bLoading = LoadGameWithTravel(Slot);
	if (!bLoading)
	{
		Post(TEXT("SYSTEM"), FString::Printf(TEXT("❌ Could not load \"%s\"!"), *Slot));
	}
	return bLoading;
}

bool USaveGameSubsystem::SaveToSlotWithMessage(const FString& SlotName)
{
	FText Reason;
	if (!CanSaveNow(&Reason))
	{
		Post(TEXT("SYSTEM"), FString::Printf(TEXT("⛔ %s."), *Reason.ToString()));
		return false;
	}
	const bool bOverwrite = HasSave(SlotName);
	const bool bSaved = SaveGame(SlotName, SlotName);
	if (bSaved)
	{
		Post(TEXT("SYSTEM"), bOverwrite ? FString::Printf(TEXT("💾 Save \"%s\" overwritten!"), *SlotName)
			: FString::Printf(TEXT("💾 Save \"%s\" created!"), *SlotName));
	}
	return bSaved;
}

bool USaveGameSubsystem::LoadFromSlotWithMessage(const FString& SlotName)
{
	const bool bLoaded = LoadGame(SlotName);
	if (bLoaded)
	{
		Post(TEXT("SYSTEM"), FString::Printf(TEXT("📂 Save \"%s\" loaded!"), *SlotName));
	}
	return bLoaded;
}

bool USaveGameSubsystem::Autosave(EAutosaveMoment Moment)
{
	if (Moment == EAutosaveMoment::None || !bAutosaveEnabled || CVarSaveAutosave.GetValueOnGameThread() == 0)
	{
		return false;
	}
	FPreCombatOverride PreCombat;
	if (Moment == EAutosaveMoment::BeforeCombat)
	{
		const UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
		PreCombat.bActive = true;
		PreCombat.bAmbush = Flow && Flow->IsAmbushFight();
		PreCombat.WaveIndex = Flow ? FMath::Max(1, Flow->GetWaveIndex()) : 1;
	}
	const bool bSaved = WriteSave(SaveGameRules::GetAutosaveSlotName(), FString(), TEXT("Commander"), PreCombat);
	if (bSaved)
	{
		++AutosaveCount;
		LastAutosaveMoment = Moment;
		UE_LOG(LogCodexTactics, Display, TEXT("Save: autosave (%s)"), Moment == EAutosaveMoment::BeforeCombat ? TEXT("before combat") : TEXT("after combat"));
		Post(TEXT("SYSTEM"), TEXT("💾 Autosaved."));
	}
	return bSaved;
}

void USaveGameSubsystem::HandleFlowTransition(ECodexGamePhase OldPhase, ECodexGamePhase NewPhase, ECodexCombatMode NewMode)
{
	Autosave(SaveGameRules::GetAutosaveMoment(OldPhase, NewPhase));
}
