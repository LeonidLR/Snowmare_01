#include "Interactables/CorpseLootRules.h"

#include "Interactables/ItemStashComponent.h"

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
	return !WeaponId.IsEmpty() && WeaponId != TEXT("grenade") && WeaponId != TEXT("knife");
}

FLootContents CorpseLootRules::BuildRemainsContents(const FCarriedGear& Gear)
{
	FLootContents Contents;
	Contents.DestroyAll(); // no supply-crate defaults: only what he carried
	for (const TPair<ETransferItem, int32>& Pair : Gear.Items)
	{
		if (Pair.Value > 0)
		{
			Contents.AddCount(ItemStash::ToLootItem(Pair.Key), Pair.Value);
		}
	}
	Contents.Grenades = FMath::Max(0, Gear.Grenades);
	Contents.BonusWeaponId = IsLootableWeapon(Gear.WeaponId) ? Gear.WeaponId : FString();
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
