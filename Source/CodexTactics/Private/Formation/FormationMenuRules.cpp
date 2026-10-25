#include "Formation/FormationMenuRules.h"

namespace FormationMenuRules
{
	const TCHAR* ToString(EFormationMenuItem Item)
	{
		switch (Item)
		{
		case EFormationMenuItem::Line: return TEXT("line");
		case EFormationMenuItem::Diamond: return TEXT("diamond");
		case EFormationMenuItem::EchelonRight: return TEXT("echelon_right");
		case EFormationMenuItem::Blizzard: return TEXT("blizzard");
		case EFormationMenuItem::Column: return TEXT("column");
		case EFormationMenuItem::Hedgehog: return TEXT("hedgehog");
		case EFormationMenuItem::EchelonLeft: return TEXT("echelon_left");
		case EFormationMenuItem::AllForward: return TEXT("all_forward");
		default: return TEXT("line");
		}
	}

	bool Parse(const FString& Id, EFormationMenuItem& Out)
	{
		for (int32 Index = 0; Index < ItemCount; ++Index)
		{
			const EFormationMenuItem Item = static_cast<EFormationMenuItem>(Index);
			if (Id.Equals(ToString(Item), ESearchCase::IgnoreCase))
			{
				Out = Item;
				return true;
			}
		}
		return false;
	}

	bool ParseChoice(const FString& Id, TOptional<EFormationMenuItem>& Out)
	{
		if (Id.Equals(TEXT("auto"), ESearchCase::IgnoreCase))
		{
			Out.Reset();
			return true;
		}
		EFormationMenuItem Item = EFormationMenuItem::Line;
		if (!Parse(Id, Item))
		{
			return false;
		}
		Out = Item;
		return true;
	}

	float ItemClock(EFormationMenuItem Item)
	{
		const float Clock = 1.5f * static_cast<float>(static_cast<int32>(Item));
		return Clock == 0.f ? 12.f : Clock;
	}

	int32 MinMembers(EFormationMenuItem /*Item*/)
	{
		return 2;
	}

	TOptional<EFormationMenuItem> ItemAt(float Dx, float Dy, float DeadZonePx)
	{
		const double X = Dx;
		const double Y = Dy;
		if (FMath::Sqrt(X * X + Y * Y) <= static_cast<double>(DeadZonePx))
		{
			return TOptional<EFormationMenuItem>();
		}
		// Clockwise from up (screen y points down).
		double Angle = FMath::RadiansToDegrees(FMath::Atan2(X, -Y));
		if (Angle < 0.0)
		{
			Angle += 360.0;
		}
		const int32 Sector = static_cast<int32>(FMath::FloorToDouble(Angle / static_cast<double>(SectorDeg) + 0.5)) % ItemCount;
		return static_cast<EFormationMenuItem>(Sector);
	}

	bool IsEnabled(EFormationMenuItem Item, int32 MemberCount)
	{
		return MemberCount >= MinMembers(Item);
	}

	EFormationMenuEffect Open(FFormationMenuState& State)
	{
		State.bOpen = true;
		State.Highlighted.Reset();
		return EFormationMenuEffect::Opened;
	}

	void Move(FFormationMenuState& State, float Dx, float Dy, float DeadZonePx)
	{
		if (State.bOpen)
		{
			State.Highlighted = ItemAt(Dx, Dy, DeadZonePx);
		}
	}

	EFormationMenuEffect Release(FFormationMenuState& State, int32 MemberCount)
	{
		if (!State.bOpen)
		{
			return EFormationMenuEffect::None;
		}
		const TOptional<EFormationMenuItem> Picked = State.Highlighted;
		State.bOpen = false;
		State.Highlighted.Reset();
		if (!Picked.IsSet())
		{
			State.Choice.Reset();
			return EFormationMenuEffect::Auto;
		}
		if (!IsEnabled(Picked.GetValue(), MemberCount))
		{
			return EFormationMenuEffect::Disabled;
		}
		State.Choice = Picked;
		return EFormationMenuEffect::Picked;
	}

	EFormationMenuEffect Cancel(FFormationMenuState& State)
	{
		if (!State.bOpen)
		{
			return EFormationMenuEffect::None;
		}
		State.bOpen = false;
		State.Highlighted.Reset();
		return EFormationMenuEffect::Cancelled;
	}

	FFormationMenuResolved Resolve(const TOptional<EFormationMenuItem>& Choice, int32 MemberCount)
	{
		FFormationMenuResolved Out;
		if (!Choice.IsSet() || !IsEnabled(Choice.GetValue(), MemberCount))
		{
			return Out;
		}
		switch (Choice.GetValue())
		{
		case EFormationMenuItem::AllForward: Out.bAllForward = true; break;
		case EFormationMenuItem::Diamond: Out.ManualFormation = MemberCount >= 4 ? EFormationKind::Diamond : EFormationKind::Wedge; break;
		case EFormationMenuItem::Line: Out.ManualFormation = EFormationKind::Line; break;
		case EFormationMenuItem::EchelonRight: Out.ManualFormation = EFormationKind::EchelonRight; break;
		case EFormationMenuItem::Blizzard: Out.ManualFormation = EFormationKind::Blizzard; break;
		case EFormationMenuItem::Column: Out.ManualFormation = EFormationKind::Column; break;
		case EFormationMenuItem::Hedgehog: Out.ManualFormation = EFormationKind::Hedgehog; break;
		case EFormationMenuItem::EchelonLeft: Out.ManualFormation = EFormationKind::EchelonLeft; break;
		default: break;
		}
		return Out;
	}
}
