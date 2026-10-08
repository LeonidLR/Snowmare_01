#include "Core/SaveGameSubsystem.h"
#include "Subsystems/CodexEventBus.h"
#include "Characters/RecruitSubsystem.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "Combat/HealthComponent.h"
#include "Data/WeaponDataAsset.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/FileManager.h"
#include "Interactables/GateActor.h"
#include "Interactables/LootCrateActor.h"
#include "Interactables/DroppedItemActor.h"
#include "Interactables/ItemStashComponent.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Quests/QuestSubsystem.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Survival/ColdSurvivalComponent.h"
#include "UI/GameMessageSubsystem.h"

namespace
{
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
		if (Object->TryGetArrayField(Field, Values) && Values->Num() == 3)
		{
			return FVector((*Values)[0]->AsNumber(), (*Values)[1]->AsNumber(), (*Values)[2]->AsNumber());
		}
		return Fallback;
	}

	int32 SaveInt(const TSharedPtr<FJsonObject>& Object, const FString& Field, int32 Fallback)
	{
		double Value = Fallback;
		Object->TryGetNumberField(Field, Value);
		return static_cast<int32>(Value);
	}

	float SaveFloat(const TSharedPtr<FJsonObject>& Object, const FString& Field, float Fallback)
	{
		double Value = Fallback;
		Object->TryGetNumberField(Field, Value);
		return static_cast<float>(Value);
	}

	bool SaveBool(const TSharedPtr<FJsonObject>& Object, const FString& Field, bool Fallback)
	{
		bool Value = Fallback;
		Object->TryGetBoolField(Field, Value);
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
}

bool USaveGameSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
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

TSharedRef<FJsonObject> USaveGameSubsystem::BuildSaveData(const FString& SlotName, const FString& Title, const FString& Author) const
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
		TSharedRef<FJsonObject> Soldier = MakeShared<FJsonObject>();
		Soldier->SetStringField(TEXT("node_name"), UEnum::GetValueAsString(Member->SquadRole));
		Soldier->SetStringField(TEXT("character_name"), Member->DisplayName.ToString());
		Soldier->SetBoolField(TEXT("is_leader"), Squad->GetLeader() == Member);
		Soldier->SetArrayField(TEXT("pos"), SaveVector(Member->GetActorLocation()));
		Soldier->SetNumberField(TEXT("rot_y"), Member->GetActorRotation().Yaw);
		const float Current = Member->HealthComponent ? Member->HealthComponent->GetCurrentHealth() : 0.f;
		const float Max = Member->HealthComponent ? Member->HealthComponent->GetMaxHealth() : 100.f;
		Health.Emplace(Current, Max);
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
		SquadList.Add(MakeShared<FJsonValueObject>(Soldier));
	}

	TSharedRef<FJsonObject> GameState = MakeShared<FJsonObject>();
	GameState->SetBoolField(TEXT("is_game_started"), true);
	GameState->SetBoolField(TEXT("is_combat_phase_unlocked"), Flow && Flow->IsCombatUnlocked());
	GameState->SetBoolField(TEXT("is_preparation_active"), Flow && Flow->GetPhase() == ECodexGamePhase::Preparation);
	GameState->SetBoolField(TEXT("is_wave_active"), Flow && Flow->IsWaveActive());
	GameState->SetNumberField(TEXT("current_wave_index"), Flow ? FMath::Max(1, Flow->GetWaveIndex()) : 1);
	GameState->SetBoolField(TEXT("is_solo_mode"), Squad && Squad->IsSoloMode());

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
	// Sprint 13: piles dropped on the ground.
	TArray<TSharedPtr<FJsonValue>> Piles;
	for (TActorIterator<ADroppedItemActor> It(World); It; ++It)
	{
		if (It->IsActorBeingDestroyed() || It->GetStash()->IsEmpty())
		{
			continue;
		}
		TSharedRef<FJsonObject> Pile = MakeShared<FJsonObject>();
		Pile->SetArrayField(TEXT("location"), SaveVector(It->GetActorLocation()));
		Pile->SetObjectField(TEXT("items"), SaveStash(It->GetStash()));
		Piles.Add(MakeShared<FJsonValueObject>(Pile));
	}
	WorldState->SetArrayField(TEXT("dropped_items"), Piles);
	WorldState->SetArrayField(TEXT("dismantled_objects"), {});

	const FString Stage = GetCurrentStageName();
	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetNumberField(TEXT("version"), 1);
	Data->SetNumberField(TEXT("timestamp"), static_cast<double>(Now.ToUnixTimestamp()));
	Data->SetStringField(TEXT("datetime_str"), Now.ToString(TEXT("%Y-%m-%d %H:%M:%S")));
	Data->SetStringField(TEXT("save_type"), SaveGameRules::GetSaveType(SlotName));
	Data->SetStringField(TEXT("slot_name"), SaveGameRules::SanitizeSlotName(SlotName));
	Data->SetStringField(TEXT("title"), Title);
	Data->SetStringField(TEXT("author"), Author);
	Data->SetStringField(TEXT("stage_name"), Stage);
	Data->SetStringField(TEXT("squad_summary"), SaveGameRules::GetSquadSummary(Health));
	Data->SetArrayField(TEXT("squad"), SquadList);
	Data->SetObjectField(TEXT("game_state"), GameState);
	Data->SetObjectField(TEXT("quest_state"), QuestState);
	Data->SetObjectField(TEXT("world_state"), WorldState);
	// Godot save_manager.gd: "susanin" only once he exists.
	if (const URecruitSubsystem* Recruits = GetWorld()->GetSubsystem<URecruitSubsystem>(); Recruits && Recruits->GetSusanin())
	{
		const TSharedRef<FJsonObject> SusaninState = MakeShared<FJsonObject>();
		SusaninState->SetBoolField(TEXT("is_recruited"), Recruits->IsSusaninRecruited());
		Data->SetObjectField(TEXT("susanin"), SusaninState);
	}
	return Data;
}

bool USaveGameSubsystem::SaveGame(const FString& SlotName, const FString& CustomTitle, const FString& Author)
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
	if (!FJsonSerializer::Serialize(BuildSaveData(Slot, Title, Author), Writer))
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
	ApplySaveData(Data.ToSharedRef());
	if (UCodexEventBus* Bus = UCodexEventBus::Get(this))
	{
		Bus->OnGameLoaded.Broadcast(SaveGameRules::SanitizeSlotName(SlotName));
	}
	return true;
}

void USaveGameSubsystem::ApplySaveData(const TSharedRef<FJsonObject>& Data)
{
	UWorld* World = GetWorld();
	USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();

	// 0. Susanin first, so a recruited Susanin takes his squad entry below.
	const TSharedPtr<FJsonObject>* SusaninState = nullptr;
	if (URecruitSubsystem* Recruits = World->GetSubsystem<URecruitSubsystem>(); Recruits && Data->TryGetObjectField(TEXT("susanin"), SusaninState))
	{
		Recruits->RestoreRecruited(SaveBool(*SusaninState, TEXT("is_recruited"), false));
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
			const FString Name = Info->GetStringField(TEXT("character_name"));
			const FString Role = Info->GetStringField(TEXT("node_name"));
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
			Member->StopOperative();
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
				const float Current = SaveFloat(Info, TEXT("current_health"), Max);
				Health->SetMaxHealth(Max, true);
				if (Current < Max)
				{
					Health->ApplyDirectHealthLoss(Max - Current, TEXT("Load"));
				}
			}
			Member->ColdLevel = SaveFloat(Info, TEXT("cold_level"), 0.f);
			Member->SetStance(static_cast<EOperativeStance>(FMath::Clamp(SaveInt(Info, TEXT("current_stance"), 0), 0, 2)));
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
			const FString WeaponId = Info->GetStringField(TEXT("current_weapon_id"));
			Member->CurrentWeapon = nullptr; // the saved state of the weapon in hands is in the inventory now
			if (WeaponId.IsEmpty() || !Member->SwitchToWeaponById(WeaponId))
			{
				Member->SwitchToWeaponById(TEXT("m16"));
			}
			if (SaveBool(Info, TEXT("is_leader"), false))
			{
				NewLeader = Member;
			}
		}
	}

	// 2. Game state (and solo mode).
	const TSharedPtr<FJsonObject>* GameState = nullptr;
	if (Data->TryGetObjectField(TEXT("game_state"), GameState))
	{
		if (UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>())
		{
			const bool bCombat = SaveBool(*GameState, TEXT("is_preparation_active"), false) || SaveBool(*GameState, TEXT("is_wave_active"), false);
			Flow->RestoreForLoad(SaveBool(*GameState, TEXT("is_combat_phase_unlocked"), false), bCombat,
				SaveInt(*GameState, TEXT("current_wave_index"), 1));
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
	// Sprint 13: piles on the ground (older saves have none: the piles of the session are cleared either way).
	if (WorldState)
	{
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
				const TArray<TSharedPtr<FJsonValue>>* Location = nullptr;
				const TSharedPtr<FJsonObject>* Items = nullptr;
				if (!Value.IsValid() || !Value->TryGetObject(Pile) || !(*Pile)->TryGetArrayField(TEXT("location"), Location)
					|| Location->Num() < 3 || !(*Pile)->TryGetObjectField(TEXT("items"), Items))
				{
					continue;
				}
				const FVector Point((*Location)[0]->AsNumber(), (*Location)[1]->AsNumber(), (*Location)[2]->AsNumber());
				for (const TPair<ETransferItem, int32>& Pair : LoadStash(*Items))
				{
					ADroppedItemActor::SpawnOrMerge(World, Point, Pair.Key, Pair.Value);
				}
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

bool USaveGameSubsystem::QuickSave()
{
	const UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
	if (Flow && Flow->GetPhase() == ECodexGamePhase::GameOver)
	{
		return false;
	}
	const bool bSaved = SaveGame(TEXT("quicksave"), TEXT("Quicksave"));
	Post(TEXT("SYSTEM"), bSaved ? TEXT("⚡ Game quicksaved [F5]!") : TEXT("❌ Quicksave failed!"));
	return bSaved;
}

bool USaveGameSubsystem::SaveToSlotWithMessage(const FString& SlotName)
{
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
