#include "Interactables/CorpseLootRules.h"

#include "Data/WeaponRoster.h"
#include "Dom/JsonObject.h"
#include "Interactables/ItemStashComponent.h"
#include "Math/RandomStream.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#define LOCTEXT_NAMESPACE "CorpseLootRules"

CorpseLootRules::FRemainsBox CorpseLootRules::FitBoxToBody(TConstArrayView<FVector> Points, float PaddingCm)
{
	FRemainsBox Box;
	if (Points.IsEmpty())
	{
		return Box;
	}
	// The main direction on the ground: the principal axis of the points' 2D spread (0.5 atan2(2 Cxy, Cxx - Cyy)).
	FVector2D Mean = FVector2D::ZeroVector;
	for (const FVector& Point : Points)
	{
		Mean += FVector2D(Point.X, Point.Y);
	}
	Mean /= static_cast<double>(Points.Num());
	double Cxx = 0.0;
	double Cyy = 0.0;
	double Cxy = 0.0;
	for (const FVector& Point : Points)
	{
		const FVector2D D = FVector2D(Point.X, Point.Y) - Mean;
		Cxx += D.X * D.X;
		Cyy += D.Y * D.Y;
		Cxy += D.X * D.Y;
	}
	const double Angle = 0.5 * FMath::Atan2(2.0 * Cxy, Cxx - Cyy);
	const FVector2D Along(FMath::Cos(Angle), FMath::Sin(Angle));
	const FVector2D Across(-Along.Y, Along.X);
	double MinA = TNumericLimits<double>::Max();
	double MaxA = TNumericLimits<double>::Lowest();
	double MinB = MinA;
	double MaxB = MaxA;
	double MinZ = MinA;
	double MaxZ = MaxA;
	for (const FVector& Point : Points)
	{
		const FVector2D D = FVector2D(Point.X, Point.Y) - Mean;
		const double A = FVector2D::DotProduct(D, Along);
		const double B = FVector2D::DotProduct(D, Across);
		MinA = FMath::Min(MinA, A);
		MaxA = FMath::Max(MaxA, A);
		MinB = FMath::Min(MinB, B);
		MaxB = FMath::Max(MaxB, B);
		MinZ = FMath::Min(MinZ, Point.Z);
		MaxZ = FMath::Max(MaxZ, Point.Z);
	}
	const double Bottom = MinZ - PaddingCm * 0.5;
	const double HalfHeight = FMath::Max<double>((MaxZ + PaddingCm * 0.5 - Bottom) * 0.5, MinHalfHeightCm);
	const FVector2D Center2D = Mean + Along * ((MinA + MaxA) * 0.5) + Across * ((MinB + MaxB) * 0.5);
	Box.bValid = true;
	Box.Center = FVector(Center2D.X, Center2D.Y, Bottom + HalfHeight);
	Box.Extent = FVector(FMath::Max<double>((MaxA - MinA) * 0.5 + PaddingCm, MinHalfLengthCm),
		FMath::Max<double>((MaxB - MinB) * 0.5 + PaddingCm, MinHalfWidthCm), HalfHeight);
	Box.Yaw = static_cast<float>(FMath::RadiansToDegrees(Angle));
	return Box;
}

bool CorpseLootRules::IsBodyBone(const FString& BoneName)
{
	return !BoneName.IsEmpty() && !BoneName.Equals(TEXT("root"), ESearchCase::IgnoreCase) && !BoneName.StartsWith(TEXT("ik_"), ESearchCase::IgnoreCase)
		&& !BoneName.StartsWith(TEXT("VB "), ESearchCase::IgnoreCase) && !BoneName.StartsWith(TEXT("attach"), ESearchCase::IgnoreCase)
		&& !BoneName.Equals(TEXT("interaction"), ESearchCase::IgnoreCase) && !BoneName.Equals(TEXT("center_of_mass"), ESearchCase::IgnoreCase);
}

bool CorpseLootRules::IsLootableWeapon(const FString& WeaponId)
{
	return !WeaponId.IsEmpty() && WeaponId != TEXT("grenade") && WeaponId != TEXT("knife") && !WeaponRoster::IsOutOfPlay(WeaponId);
}

bool CorpseLootRules::IsRemainsItem(ETransferItem Item)
{
	switch (Item)
	{
	case ETransferItem::Medkit:
	case ETransferItem::MedicalBag:
	case ETransferItem::Diesel: // TURRET_POWER.md 6: a fallen operative's jerrycan drops with him
	case ETransferItem::CannedFood:
	case ETransferItem::Bread:
	case ETransferItem::Chocolate:
		return true;
	default:
		return !TransferRules::GetAmmoWeaponId(Item).IsEmpty(); // a weapon's ammo (M16 / pistol / shotgun rounds, flame fuel, cryo, plasma)
	}
}

FLootContents CorpseLootRules::BuildRemainsContents(const FCarriedGear& Gear)
{
	FLootContents Contents;
	Contents.DestroyAll(); // no supply-crate defaults: only what he carried
	for (const TPair<ETransferItem, int32>& Pair : Gear.Items)
	{
		if (Pair.Value > 0 && IsRemainsItem(Pair.Key))
		{
			Contents.AddCount(ItemStash::ToLootItem(Pair.Key), Pair.Value);
		}
	}
	Contents.Grenades = FMath::Max(0, Gear.Grenades);
	Contents.BonusWeaponId = IsLootableWeapon(Gear.WeaponId) ? Gear.WeaponId : FString();
	return Contents;
}

namespace CorpseLootRulesInternal
{
	bool bHasOverride = false;
	CorpseLootRules::FCorpseRandomLootConfig OverrideConfig;

	bool FindAmmoTransferItem(const FString& WeaponId, ETransferItem& OutItem)
	{
		static const ETransferItem AmmoItems[] = {
			ETransferItem::RifleAmmo,
			ETransferItem::PistolAmmo,
			ETransferItem::ShotgunAmmo,
			ETransferItem::FlameFuel,
			ETransferItem::CryoAmmo,
			ETransferItem::PlasmaAmmo
		};
		for (const ETransferItem Candidate : AmmoItems)
		{
			if (TransferRules::GetAmmoWeaponId(Candidate) == WeaponId)
			{
				OutItem = Candidate;
				return true;
			}
		}
		return false;
	}
}

bool CorpseLootRules::ParseCorpseLootConfig(const FString& JsonContent, FCorpseRandomLootConfig& OutConfig)
{
	OutConfig = FCorpseRandomLootConfig();
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonContent);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		return false;
	}
	const TSharedPtr<FJsonObject>* LootObject = nullptr;
	if (!Root->TryGetObjectField(TEXT("corpse_loot"), LootObject) || !LootObject || !LootObject->IsValid())
	{
		return true;
	}
	double Value = 0.0;
	if ((*LootObject)->TryGetNumberField(TEXT("ammo_mag_min_fraction"), Value))
	{
		OutConfig.AmmoMagMinFraction = FMath::Clamp(static_cast<float>(Value), 0.f, 1.f);
	}
	if ((*LootObject)->TryGetNumberField(TEXT("ammo_mag_max_fraction"), Value))
	{
		OutConfig.AmmoMagMaxFraction = FMath::Clamp(static_cast<float>(Value), 0.f, 1.f);
	}
	OutConfig.AmmoMagMaxFraction = FMath::Max(OutConfig.AmmoMagMinFraction, OutConfig.AmmoMagMaxFraction);
	if ((*LootObject)->TryGetNumberField(TEXT("medkit_chance"), Value))
	{
		OutConfig.MedkitChance = FMath::Clamp(static_cast<float>(Value), 0.f, 1.f);
	}
	if ((*LootObject)->TryGetNumberField(TEXT("grenade_chance"), Value))
	{
		OutConfig.GrenadeChance = FMath::Clamp(static_cast<float>(Value), 0.f, 1.f);
	}
	return true;
}

CorpseLootRules::FCorpseRandomLootConfig CorpseLootRules::GetCorpseLootConfig()
{
	if (CorpseLootRulesInternal::bHasOverride)
	{
		return CorpseLootRulesInternal::OverrideConfig;
	}
	FCorpseRandomLootConfig Config;
	const FString ConfigPath = FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Data/Combat/death_ragdoll.json"));
	FString JsonContent;
	if (FFileHelper::LoadFileToString(JsonContent, *ConfigPath))
	{
		ParseCorpseLootConfig(JsonContent, Config);
	}
	return Config;
}

void CorpseLootRules::SetCorpseLootConfigForTesting(const FCorpseRandomLootConfig* InOverrideConfig)
{
	if (InOverrideConfig)
	{
		CorpseLootRulesInternal::OverrideConfig = *InOverrideConfig;
		CorpseLootRulesInternal::bHasOverride = true;
	}
	else
	{
		CorpseLootRulesInternal::bHasOverride = false;
	}
}

FLootContents CorpseLootRules::BuildHostileRemainsContents(
	TConstArrayView<FString> BodyWeaponIds,
	TConstArrayView<FString> SearcherUsableWeapons,
	const TMap<FString, int32>& ClipSizesByWeapon,
	int32 Seed,
	const FCorpseRandomLootConfig& Config)
{
	FLootContents Contents;
	Contents.DestroyAll();
	FRandomStream Stream(Seed);

	const float MinFrac = FMath::Clamp(Config.AmmoMagMinFraction, 0.f, 1.f);
	const float MaxFrac = FMath::Clamp(FMath::Max(MinFrac, Config.AmmoMagMaxFraction), 0.f, 1.f);

	TArray<FString> RolledWeapons;
	for (const FString& WeaponId : BodyWeaponIds)
	{
		if (!IsLootableWeapon(WeaponId) || RolledWeapons.Contains(WeaponId))
		{
			continue;
		}
		RolledWeapons.Add(WeaponId);
		if (!SearcherUsableWeapons.Contains(WeaponId))
		{
			continue;
		}
		ETransferItem AmmoItem = ETransferItem::RifleAmmo;
		if (!CorpseLootRulesInternal::FindAmmoTransferItem(WeaponId, AmmoItem))
		{
			continue;
		}
		const int32* ExplicitClip = ClipSizesByWeapon.Find(WeaponId);
		const int32 ClipSize = (ExplicitClip && *ExplicitClip > 0) ? *ExplicitClip : TransferRules::GetAmmoPack(AmmoItem);
		if (ClipSize > 0 && MaxFrac > 0.f)
		{
			const float Fraction = Stream.FRandRange(MinFrac, MaxFrac);
			const int32 Rounds = FMath::Max(1, FMath::RoundToInt(static_cast<float>(ClipSize) * Fraction));
			Contents.AddCount(ItemStash::ToLootItem(AmmoItem), Rounds);
		}
	}

	if (Config.MedkitChance > 0.f && Stream.GetFraction() < Config.MedkitChance)
	{
		Contents.Medkits += 1;
	}
	if (Config.GrenadeChance > 0.f && Stream.GetFraction() < Config.GrenadeChance)
	{
		Contents.Grenades += 1;
	}
	return Contents;
}

FText CorpseLootRules::GetRemainsName(const FText& OperativeName)
{
	return FText::Format(LOCTEXT("RemainsName", "Remains: {0}"), OperativeName);
}

FString CorpseLootRules::GetSearchPrompt(const FText& OperativeName, bool bEmpty)
{
	return bEmpty ? FString::Printf(TEXT("[Searched] Body: %s (empty)"), *OperativeName.ToString())
		: FString::Printf(TEXT("[Click] Search the body: %s"), *OperativeName.ToString());
}

#undef LOCTEXT_NAMESPACE
