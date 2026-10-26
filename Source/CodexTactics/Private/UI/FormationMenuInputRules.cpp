#include "UI/FormationMenuInputRules.h"

#define LOCTEXT_NAMESPACE "FormationMenuInput"

void FFormationMenuKeyTracker::Press()
{
	bPressed = true;
	bMenuOpen = false;
	bSpent = false;
	HeldTime = 0.f;
}

EFormationMenuKeyAction FFormationMenuKeyTracker::Tick(float RealDeltaSeconds, float HoldSeconds, bool bMenuAvailable)
{
	if (!bPressed || bMenuOpen || bSpent)
	{
		return EFormationMenuKeyAction::None;
	}
	HeldTime += FMath::Max(RealDeltaSeconds, 0.f);
	if (HeldTime >= HoldSeconds && bMenuAvailable)
	{
		bMenuOpen = true;
		return EFormationMenuKeyAction::Open;
	}
	return EFormationMenuKeyAction::None;
}

EFormationMenuKeyAction FFormationMenuKeyTracker::Release()
{
	const EFormationMenuKeyAction Action = !bPressed ? EFormationMenuKeyAction::None
		: bMenuOpen ? EFormationMenuKeyAction::Pick
		: bSpent ? EFormationMenuKeyAction::None
		: EFormationMenuKeyAction::Toggle;
	bPressed = false;
	bMenuOpen = false;
	bSpent = false;
	HeldTime = 0.f;
	return Action;
}

void FFormationMenuKeyTracker::Close()
{
	if (bMenuOpen)
	{
		bMenuOpen = false;
		bSpent = true;
	}
}

namespace FormationMenuInput
{
	FVector2D ClampCentre(const FVector2D& Cursor, const FVector2D& View, const FVector2D& HalfExtent, float ScreenMargin)
	{
		auto ClampAxis = [ScreenMargin](double Value, double Size, double Half)
		{
			const double Low = Half + ScreenMargin;
			const double High = Size - Half - ScreenMargin;
			return Low > High ? Size * 0.5 : FMath::Clamp(Value, Low, High);
		};
		return FVector2D(ClampAxis(Cursor.X, View.X, HalfExtent.X), ClampAxis(Cursor.Y, View.Y, HalfExtent.Y));
	}

	FVector2D ItemDirection(EFormationMenuItem Item)
	{
		const double Radians = FMath::DegreesToRadians(static_cast<double>(FormationMenuRules::SectorDeg) * static_cast<double>(static_cast<int32>(Item)));
		return FVector2D(FMath::Sin(Radians), -FMath::Cos(Radians));
	}

	FText Label(const TOptional<EFormationMenuItem>& Item, int32 MemberCount)
	{
		if (!Item.IsSet())
		{
			return LOCTEXT("Auto", "Auto");
		}
		switch (Item.GetValue())
		{
		case EFormationMenuItem::Line: return LOCTEXT("Line", "Line");
		case EFormationMenuItem::Diamond:
			return MemberCount >= 4 ? LOCTEXT("Diamond", "Diamond") : MemberCount == 3 ? LOCTEXT("Wedge", "Wedge") : LOCTEXT("Pair", "Pair");
		case EFormationMenuItem::EchelonRight: return LOCTEXT("EchelonRight", "Echelon right");
		case EFormationMenuItem::Blizzard: return LOCTEXT("Blizzard", "Blizzard diamond");
		case EFormationMenuItem::Column: return LOCTEXT("Column", "Column");
		case EFormationMenuItem::Hedgehog: return LOCTEXT("Hedgehog", "Hedgehog");
		case EFormationMenuItem::EchelonLeft: return LOCTEXT("EchelonLeft", "Echelon left");
		case EFormationMenuItem::AllForward: return LOCTEXT("AllForward", "All forward");
		default: return LOCTEXT("Auto", "Auto");
		}
	}

	FString IconAssetName(const TOptional<EFormationMenuItem>& Item)
	{
		return FString::Printf(TEXT("T_FormationMenu_%s"), Item.IsSet() ? FormationMenuRules::ToString(Item.GetValue()) : TEXT("auto"));
	}
}

#undef LOCTEXT_NAMESPACE
