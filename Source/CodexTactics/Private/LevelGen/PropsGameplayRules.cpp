#include "LevelGen/PropsGameplayRules.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Interactables/LootRules.h"
#include "LevelGen/LevelPropsTypes.h"
#include "LevelGen/PropsRng.h"
#include "UObject/UnrealType.h"

namespace PropsGameplayRulesLocal
{
	TSharedPtr<FJsonObject> GameplayChild(const FJsonObject* Parent, const TCHAR* Key)
	{
		const TSharedPtr<FJsonObject>* Out = nullptr;
		if (Parent && Parent->TryGetObjectField(Key, Out) && Out && Out->IsValid())
		{
			return *Out;
		}
		return nullptr;
	}

	TSharedPtr<FJsonObject> GameplayBlock(const FJsonObject* Balance)
	{
		return GameplayChild(Balance, TEXT("gameplay"));
	}

	bool IsCommentKey(const FString& Key)
	{
		return Key.Contains(TEXT("comment"));
	}

	/** name -> count of an object of integer counts (comment keys and non-numbers skipped). */
	void ReadCounts(const FJsonObject* Object, TMap<FString, int32>& Out)
	{
		if (!Object)
		{
			return;
		}
		for (const auto& Entry : Object->Values)
		{
			double Value = 0.0;
			const FString Key(Entry.Key);
			if (!IsCommentKey(Key) && Entry.Value.IsValid() && Entry.Value->TryGetNumber(Value))
			{
				Out.FindOrAdd(Key) += FMath::RoundToInt(Value);
			}
		}
	}
}

FString PropsGameplayRules::ClassKeyFor(const FString& LayerKind, const FLevelPropsObject& Object, bool bContainer)
{
	if (bContainer)
	{
		return TEXT("crate_container");
	}
	if (LayerKind == TEXT("mines") || Object.Variant.StartsWith(TEXT("mine")))
	{
		if (Object.Trigger.Equals(TEXT("tripwire"), ESearchCase::IgnoreCase))
		{
			return TEXT("mine_tripwire");
		}
		if (Object.Trigger.Equals(TEXT("proximity"), ESearchCase::IgnoreCase))
		{
			return TEXT("mine_proximity");
		}
	}
	return Object.Variant;
}

FString PropsGameplayRules::ClassPathFor(const FJsonObject* Balance, const FString& Key)
{
	using namespace PropsGameplayRulesLocal;
	const TSharedPtr<FJsonObject> Classes = GameplayChild(GameplayBlock(Balance).Get(), TEXT("classes"));
	FString Path;
	if (Classes.IsValid() && !IsCommentKey(Key))
	{
		Classes->TryGetStringField(Key, Path);
	}
	return Path;
}

float PropsGameplayRules::RevealCm(const FJsonObject* Balance)
{
	double Value = 300.0;
	if (const TSharedPtr<FJsonObject> Block = PropsGameplayRulesLocal::GameplayBlock(Balance))
	{
		Block->TryGetNumberField(TEXT("reveal_cm"), Value);
	}
	return static_cast<float>(FMath::Max(Value, 0.0));
}

double PropsGameplayRules::SpawnFrameBudgetMs(const FJsonObject* Balance)
{
	double Value = 20.0;
	if (const TSharedPtr<FJsonObject> Block = PropsGameplayRulesLocal::GameplayBlock(Balance))
	{
		Block->TryGetNumberField(TEXT("spawn_frame_budget_ms"), Value);
	}
	return FMath::Max(Value, 1.0);
}

FString PropsGameplayRules::MinedCrateTable(const FJsonObject* Balance)
{
	FString Table = TEXT("ammo_common");
	const TSharedPtr<FJsonObject> Loot = PropsGameplayRulesLocal::GameplayChild(PropsGameplayRulesLocal::GameplayBlock(Balance).Get(), TEXT("loot"));
	if (Loot.IsValid())
	{
		Loot->TryGetStringField(TEXT("mined_crate_table"), Table);
	}
	return Table;
}

TMap<FString, int32> PropsGameplayRules::RollLoot(const FJsonObject* Balance, const FString& Table, const FString& ObjectId, bool bPile)
{
	using namespace PropsGameplayRulesLocal;
	TMap<FString, int32> Result;
	const TSharedPtr<FJsonObject> Loot = GameplayChild(GameplayBlock(Balance).Get(), TEXT("loot"));
	const TSharedPtr<FJsonObject> Items = GameplayChild(Loot.Get(), TEXT("items"));
	const TSharedPtr<FJsonObject> Tables = GameplayChild(Balance, TEXT("loot_tables"));
	const TSharedPtr<FJsonObject> TableObject = GameplayChild(Tables.Get(), *Table);
	const TSharedPtr<FJsonObject> Entries = GameplayChild(TableObject.Get(), TEXT("entries"));
	if (!Entries.IsValid() || !Items.IsValid())
	{
		return Result;
	}
	const TCHAR* Target = bPile ? TEXT("pile") : TEXT("crate");

	// Candidate items in key order (deterministic), each with its weight and its counts for this target.
	struct FCandidate
	{
		FString Item;
		uint32 Weight = 0;
		TMap<FString, int32> Counts;
	};
	TArray<FString> Keys;
	for (const auto& Entry : Entries->Values)
	{
		Keys.Add(FString(Entry.Key));
	}
	Keys.Sort();
	TArray<FCandidate> Candidates;
	uint32 Total = 0;
	for (const FString& Key : Keys)
	{
		double Weight = 0.0;
		if (IsCommentKey(Key) || !Entries->TryGetNumberField(Key, Weight) || Weight <= 0.0)
		{
			continue;
		}
		const TSharedPtr<FJsonObject> Item = GameplayChild(Items.Get(), *Key);
		const TSharedPtr<FJsonObject> Counts = GameplayChild(Item.Get(), Target);
		if (!Counts.IsValid())
		{
			continue;
		}
		FCandidate& C = Candidates.AddDefaulted_GetRef();
		C.Item = Key;
		C.Weight = static_cast<uint32>(FMath::RoundToInt(Weight));
		ReadCounts(Counts.Get(), C.Counts);
		Total += C.Weight;
	}
	if (Candidates.IsEmpty() || Total == 0)
	{
		return Result;
	}

	double Rolls = bPile ? 1.0 : 2.0;
	if (Loot.IsValid())
	{
		Loot->TryGetNumberField(bPile ? TEXT("pile_rolls") : TEXT("crate_rolls"), Rolls);
	}
	FPropsRng Rng(PropsRng::Fnv1a(ObjectId));
	for (int32 Roll = 0; Roll < FMath::RoundToInt(Rolls); ++Roll)
	{
		uint32 Pick = Rng.NextBelow(Total);
		for (const FCandidate& C : Candidates)
		{
			if (Pick < C.Weight)
			{
				for (const TPair<FString, int32>& Count : C.Counts)
				{
					Result.FindOrAdd(Count.Key) += Count.Value;
				}
				break;
			}
			Pick -= C.Weight;
		}
	}
	return Result;
}

TArray<FString> PropsGameplayRules::ApplyToContents(const TMap<FString, int32>& Fields, FLootContents& Contents)
{
	TArray<FString> Unknown;
	UScriptStruct* Struct = FLootContents::StaticStruct();
	for (TFieldIterator<FIntProperty> It(Struct); It; ++It)
	{
		It->SetPropertyValue_InContainer(&Contents, 0);
	}
	for (const TPair<FString, int32>& Field : Fields)
	{
		FIntProperty* Property = CastField<FIntProperty>(Struct->FindPropertyByName(FName(*Field.Key)));
		if (!Property)
		{
			Unknown.Add(Field.Key);
			continue;
		}
		Property->SetPropertyValue_InContainer(&Contents, Property->GetPropertyValue_InContainer(&Contents) + Field.Value);
	}
	return Unknown;
}
