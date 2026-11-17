#include "LevelGen/PropsGreybox.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace PropsGreyboxLocal
{
	const TSharedPtr<FJsonObject> GreyboxObject(const FJsonObject* Parent, const TCHAR* Key)
	{
		const TSharedPtr<FJsonObject>* Out = nullptr;
		if (Parent && Parent->TryGetObjectField(Key, Out) && Out && Out->IsValid())
		{
			return *Out;
		}
		return nullptr;
	}

	bool GreyboxNumber(const FJsonObject* Parent, const TCHAR* Key, double& Out)
	{
		return Parent && Parent->TryGetNumberField(Key, Out);
	}

	/** [a, b, c] of numbers. */
	bool GreyboxVector3(const FJsonObject* Parent, const TCHAR* Key, FVector& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!Parent || !Parent->TryGetArrayField(Key, Values) || !Values || Values->Num() != 3)
		{
			return false;
		}
		double V[3];
		for (int32 I = 0; I < 3; ++I)
		{
			if (!(*Values)[I].IsValid() || !(*Values)[I]->TryGetNumber(V[I]))
			{
				return false;
			}
		}
		Out = FVector(V[0], V[1], V[2]);
		return true;
	}

	bool GreyboxColor(const FJsonObject* Colors, const FString& Name, FLinearColor& Out)
	{
		FVector Rgb;
		if (!Name.IsEmpty() && GreyboxVector3(Colors, *Name, Rgb))
		{
			Out = FLinearColor(static_cast<float>(Rgb.X), static_cast<float>(Rgb.Y), static_cast<float>(Rgb.Z));
			return true;
		}
		return false;
	}
}

FString PropsGreybox::KindKeyFor(const FJsonObject* Balance, const FString& Variant)
{
	using namespace PropsGreyboxLocal;
	const TSharedPtr<FJsonObject> Kinds = GreyboxObject(Balance, TEXT("kinds"));
	if (Kinds.IsValid() && Kinds->HasField(Variant))
	{
		return Variant;
	}
	if (Variant.StartsWith(TEXT("mine")))
	{
		return TEXT("mine");
	}
	return Variant;
}

FPropGreyboxSpec PropsGreybox::Resolve(const FJsonObject* Balance, const FString& Variant, bool bDangerous, const FString& Trigger, float WidthCm)
{
	using namespace PropsGreyboxLocal;
	FPropGreyboxSpec Spec;
	const FString KindKey = KindKeyFor(Balance, Variant);
	const TSharedPtr<FJsonObject> Kinds = GreyboxObject(Balance, TEXT("kinds"));
	const TSharedPtr<FJsonObject> Kind = GreyboxObject(Kinds.Get(), *KindKey);
	const TSharedPtr<FJsonObject> Greybox = GreyboxObject(Balance, TEXT("greybox"));
	const TSharedPtr<FJsonObject> Look = GreyboxObject(Greybox.Get(), *KindKey);
	const TSharedPtr<FJsonObject> Colors = GreyboxObject(Greybox.Get(), TEXT("colors"));

	// Size + shape: kinds.size_cm (a box) or kinds.radius_cm / height_cm (a cylinder), then the greybox block's own overrides.
	FVector Size;
	double Radius = 0.0;
	double Height = 0.0;
	if (GreyboxVector3(Kind.Get(), TEXT("size_cm"), Size))
	{
		Spec.Shape = EPropGreyboxShape::Box;
		Spec.SizeCm = Size;
	}
	else if (GreyboxNumber(Kind.Get(), TEXT("radius_cm"), Radius))
	{
		Spec.Shape = EPropGreyboxShape::Cylinder;
		Spec.SizeCm = FVector(2.0 * Radius, 2.0 * Radius, GreyboxNumber(Kind.Get(), TEXT("height_cm"), Height) ? Height : 2.0 * Radius);
	}
	FString ShapeName;
	if ((Look.IsValid() && Look->TryGetStringField(TEXT("shape"), ShapeName)) || (Kind.IsValid() && Kind->TryGetStringField(TEXT("shape"), ShapeName)))
	{
		Spec.Shape = ShapeName.Equals(TEXT("cylinder"), ESearchCase::IgnoreCase) ? EPropGreyboxShape::Cylinder : EPropGreyboxShape::Box;
	}
	if (GreyboxVector3(Look.Get(), TEXT("size_cm"), Size))
	{
		Spec.SizeCm = Size;
	}
	if (GreyboxNumber(Look.Get(), TEXT("height_cm"), Height))
	{
		Spec.SizeCm.Z = Height;
	}
	Spec.SizeCm = Spec.SizeCm.ComponentMax(FVector(1.0));

	bool bNav = true;
	if (Kind.IsValid() && Kind->TryGetBoolField(TEXT("nav_affecting"), bNav))
	{
		Spec.bBlocks = bNav;
	}

	// Colour: a crate by crate_colors (designer 6.1 #1: blue = safe, red = dangerous), anything else by its kind key.
	FString ColorName = KindKey;
	if (KindKey.StartsWith(TEXT("crate")))
	{
		const TSharedPtr<FJsonObject> CrateColors = GreyboxObject(Balance, TEXT("crate_colors"));
		FString Named;
		if (CrateColors.IsValid() && CrateColors->TryGetStringField(bDangerous ? TEXT("dangerous") : TEXT("safe"), Named))
		{
			ColorName = Named;
		}
	}
	if (!GreyboxColor(Colors.Get(), ColorName, Spec.Color))
	{
		GreyboxColor(Colors.Get(), TEXT("default"), Spec.Color);
	}

	FVector Muzzle;
	if (GreyboxVector3(Look.Get(), TEXT("muzzle_cm"), Muzzle))
	{
		Spec.MuzzleCm = Muzzle.ComponentMax(FVector::ZeroVector);
	}

	double WireHeight = 0.0;
	if (Trigger.Equals(TEXT("tripwire"), ESearchCase::IgnoreCase) && GreyboxNumber(Look.Get(), TEXT("wire_height_cm"), WireHeight))
	{
		double WireThickness = 2.0;
		GreyboxNumber(Look.Get(), TEXT("wire_thickness_cm"), WireThickness);
		Spec.WireLengthCm = FMath::Max(WidthCm, 1.f);
		Spec.WireHeightCm = static_cast<float>(WireHeight);
		Spec.WireThicknessCm = static_cast<float>(FMath::Max(WireThickness, 0.5));
	}
	return Spec;
}
