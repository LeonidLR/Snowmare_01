#include "AI/PmcOperatorRules.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace PmcOperatorRules
{
	namespace PmcOperatorLocal
	{
		FPmcOperatorConfig Cached;
		bool bLoaded = false;

		inline uint32 Hash(int32 Seed, int32 Index, uint32 Salt)
		{
			uint32 H = static_cast<uint32>(Seed) * 2654435761u ^ (static_cast<uint32>(Index) + 1u) * 2246822519u ^ Salt * 3266489917u;
			H ^= H >> 15;
			H *= 2246822519u;
			H ^= H >> 13;
			return H;
		}

		inline FPmcOperatorConfig Defaults()
		{
			FPmcOperatorConfig Config;
			Config.Names = { TEXT("Operator Kessler"), TEXT("Operator Vance"), TEXT("Operator Roux"), TEXT("Operator Moreau"), TEXT("Operator Dahl"),
				TEXT("Operator Petrov") };
			FPmcLook Grey;
			Grey.Id = TEXT("grey");
			Grey.Tint = FLinearColor::FromSRGBColor(FColor(0x9A, 0x9E, 0xA3));
			FPmcLook Dark;
			Dark.Id = TEXT("dark");
			Dark.Tint = FLinearColor::FromSRGBColor(FColor(0x33, 0x37, 0x3B));
			FPmcLook Snowed;
			Snowed.Id = TEXT("snowed");
			Snowed.Tint = FLinearColor::FromSRGBColor(FColor(0xD8, 0xDC, 0xE0));
			Snowed.SnowShare = 1.f;
			Config.Looks = { Grey, Dark, Snowed };
			Config.WeaponPool = { { TEXT("m16"), 3 }, { TEXT("shotgun"), 1 }, { TEXT("sniper_rifle"), 1 } };
			return Config;
		}
	}

	FString DefaultPath()
	{
		return FPaths::ProjectContentDir() / TEXT("Data/AI/pmc_operators.json");
	}

	FPmcOperatorConfig FromJson(const FJsonObject& Json, const FPmcOperatorConfig& Base)
	{
		FPmcOperatorConfig Out = Base;
		Json.TryGetStringField(TEXT("body_mesh"), Out.BodyMesh);
		const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
		if (Json.TryGetArrayField(TEXT("display_names"), Array))
		{
			Out.Names.Reset();
			for (const TSharedPtr<FJsonValue>& Value : *Array)
			{
				FString Name;
				if (Value.IsValid() && Value->TryGetString(Name) && !Name.IsEmpty())
				{
					Out.Names.Add(Name);
				}
			}
		}
		if (Json.TryGetArrayField(TEXT("looks"), Array))
		{
			Out.Looks.Reset();
			for (const TSharedPtr<FJsonValue>& Value : *Array)
			{
				const TSharedPtr<FJsonObject>* Object = nullptr;
				if (!Value.IsValid() || !Value->TryGetObject(Object) || !Object->IsValid())
				{
					continue;
				}
				FPmcLook Look;
				(*Object)->TryGetStringField(TEXT("id"), Look.Id);
				(*Object)->TryGetStringField(TEXT("coat_material"), Look.CoatMaterial);
				(*Object)->TryGetStringField(TEXT("coat_slot"), Look.CoatSlot);
				(*Object)->TryGetStringField(TEXT("tint_parameter"), Look.TintParameter);
				double Snow = 0.0;
				if ((*Object)->TryGetNumberField(TEXT("snow"), Snow))
				{
					Look.SnowShare = FMath::Clamp(static_cast<float>(Snow), 0.f, 1.f);
				}
				const TArray<TSharedPtr<FJsonValue>>* Tint = nullptr;
				if ((*Object)->TryGetArrayField(TEXT("tint_srgb"), Tint) && Tint->Num() >= 3)
				{
					Look.Tint = FLinearColor::FromSRGBColor(FColor(static_cast<uint8>(FMath::Clamp((*Tint)[0]->AsNumber(), 0.0, 255.0)),
						static_cast<uint8>(FMath::Clamp((*Tint)[1]->AsNumber(), 0.0, 255.0)), static_cast<uint8>(FMath::Clamp((*Tint)[2]->AsNumber(), 0.0, 255.0))));
				}
				Out.Looks.Add(Look);
			}
		}
		if (Json.TryGetArrayField(TEXT("weapon_pool"), Array))
		{
			Out.WeaponPool.Reset();
			for (const TSharedPtr<FJsonValue>& Value : *Array)
			{
				const TSharedPtr<FJsonObject>* Object = nullptr;
				if (!Value.IsValid() || !Value->TryGetObject(Object) || !Object->IsValid())
				{
					continue;
				}
				FPmcWeaponChoice Choice;
				(*Object)->TryGetStringField(TEXT("id"), Choice.WeaponId);
				double Weight = 1.0;
				(*Object)->TryGetNumberField(TEXT("weight"), Weight);
				Choice.Weight = FMath::Max(0, FMath::RoundToInt(Weight));
				if (!Choice.WeaponId.IsEmpty())
				{
					Out.WeaponPool.Add(Choice);
				}
			}
		}
		const TSharedPtr<FJsonObject>* Stats = nullptr;
		if (Json.TryGetObjectField(TEXT("stats"), Stats) && Stats->IsValid())
		{
			double Value = 0.0;
			if ((*Stats)->TryGetNumberField(TEXT("accuracy"), Value)) { Out.Accuracy = static_cast<float>(Value); }
			if ((*Stats)->TryGetNumberField(TEXT("luck"), Value)) { Out.Luck = static_cast<float>(Value); }
			if ((*Stats)->TryGetNumberField(TEXT("fortitude"), Value)) { Out.Fortitude = static_cast<float>(Value); }
			if ((*Stats)->TryGetNumberField(TEXT("medkits"), Value)) { Out.Medkits = FMath::Max(0, FMath::RoundToInt(Value)); }
			if ((*Stats)->TryGetNumberField(TEXT("grenades"), Value)) { Out.Grenades = FMath::Max(0, FMath::RoundToInt(Value)); }
		}
		const TSharedPtr<FJsonObject>* Factions = nullptr;
		if (Json.TryGetObjectField(TEXT("factions"), Factions) && Factions->IsValid())
		{
			bool bFlag = false;
			if ((*Factions)->TryGetBoolField(TEXT("monsters_attack_pmc"), bFlag)) { Out.bMonstersAttackPmc = bFlag; }
			if ((*Factions)->TryGetBoolField(TEXT("cold_applies"), bFlag)) { Out.bColdApplies = bFlag; }
			double Metres = 0.0;
			if ((*Factions)->TryGetNumberField(TEXT("pmc_self_defence_m"), Metres)) { Out.SelfDefenceCm = static_cast<float>(Metres) * 100.f; }
		}
		const TSharedPtr<FJsonObject>* Gunner = nullptr;
		if (Json.TryGetObjectField(TEXT("rpg_gunner"), Gunner) && Gunner->IsValid())
		{
			(*Gunner)->TryGetStringField(TEXT("weapon"), Out.RpgGunner.WeaponId);
			double Value = 0.0;
			if ((*Gunner)->TryGetNumberField(TEXT("share"), Value)) { Out.RpgGunner.Share = FMath::Clamp(static_cast<float>(Value), 0.f, 1.f); }
			if ((*Gunner)->TryGetNumberField(TEXT("max_per_squad"), Value)) { Out.RpgGunner.MaxPerSquad = FMath::Max(0, FMath::RoundToInt(Value)); }
			if ((*Gunner)->TryGetNumberField(TEXT("slot_span"), Value)) { Out.RpgGunner.SlotSpan = FMath::Max(1, FMath::RoundToInt(Value)); }
		}
		return Out;
	}

	bool Reload()
	{
		using namespace PmcOperatorLocal;
		Cached = Defaults();
		bLoaded = true;
		FString Text;
		TSharedPtr<FJsonObject> Json;
		const FString Path = DefaultPath();
		if (!FPaths::FileExists(Path) || !FFileHelper::LoadFileToString(Text, *Path)
			|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) || !Json.IsValid())
		{
			return false;
		}
		Cached = FromJson(*Json, Cached);
		return true;
	}

	const FPmcOperatorConfig& Get()
	{
		if (!PmcOperatorLocal::bLoaded)
		{
			Reload();
		}
		return PmcOperatorLocal::Cached;
	}

	const FPmcLook& PickLook(const FPmcOperatorConfig& Config, int32 Seed, int32 Index)
	{
		static const FPmcLook Fallback;
		if (Config.Looks.IsEmpty())
		{
			return Fallback;
		}
		// Fighters 0..N-1 of one squad walk the looks in a seeded order, so a squad of three wears three different coats.
		const int32 Start = static_cast<int32>(PmcOperatorLocal::Hash(Seed, 0, 11u) % static_cast<uint32>(Config.Looks.Num()));
		return Config.Looks[(Start + Index) % Config.Looks.Num()];
	}

	FString PickName(const FPmcOperatorConfig& Config, int32 Seed, int32 Index)
	{
		if (Config.Names.IsEmpty())
		{
			return FString::Printf(TEXT("PMC Operator %d"), Index + 1);
		}
		const int32 Start = static_cast<int32>(PmcOperatorLocal::Hash(Seed, 0, 23u) % static_cast<uint32>(Config.Names.Num()));
		return Config.Names[(Start + Index) % Config.Names.Num()];
	}

	FString PickWeaponId(const FPmcOperatorConfig& Config, int32 Seed, int32 Index)
	{
		int32 Total = 0;
		for (const FPmcWeaponChoice& Choice : Config.WeaponPool)
		{
			Total += Choice.Weight;
		}
		if (Total <= 0)
		{
			return TEXT("m16");
		}
		int32 Roll = static_cast<int32>(PmcOperatorLocal::Hash(Seed, Index, 37u) % static_cast<uint32>(Total));
		for (const FPmcWeaponChoice& Choice : Config.WeaponPool)
		{
			Roll -= Choice.Weight;
			if (Roll < 0)
			{
				return Choice.WeaponId;
			}
		}
		return Config.WeaponPool.Last().WeaponId;
	}

	bool IsRpgGunner(const FPmcOperatorConfig& Config, int32 Seed, int32 Index)
	{
		const FPmcRpgGunnerConfig& Gunner = Config.RpgGunner;
		const int32 Span = FMath::Max(1, Gunner.SlotSpan);
		const int32 Count = FMath::Min(Gunner.MaxPerSquad, Span);
		if (Gunner.WeaponId.IsEmpty() || Count <= 0 || Gunner.Share <= 0.f || Index < 0 || Index >= Span)
		{
			return false;
		}
		// The squad's roll (one per seed), then Count consecutive slots from a seeded start.
		const float Roll = static_cast<float>(PmcOperatorLocal::Hash(Seed, 0, 53u) % 10000u) / 10000.f;
		if (Roll >= Gunner.Share)
		{
			return false;
		}
		const int32 Start = static_cast<int32>(PmcOperatorLocal::Hash(Seed, 0, 59u) % static_cast<uint32>(Span));
		return (Index - Start + Span) % Span < Count;
	}

	FString WeaponAssetPath(const FString& WeaponId)
	{
		return FString::Printf(TEXT("/Game/Data/Weapons/DA_Weapon_%s.DA_Weapon_%s"), *WeaponId, *WeaponId);
	}
}
