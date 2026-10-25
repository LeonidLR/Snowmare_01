#include "Formation/FormationTypes.h"

namespace FormationNames
{
	const TCHAR* ToString(EFormationKind Kind)
	{
		switch (Kind)
		{
		case EFormationKind::Diamond: return TEXT("diamond");
		case EFormationKind::Wedge: return TEXT("wedge");
		case EFormationKind::Column: return TEXT("column");
		case EFormationKind::Line: return TEXT("line");
		case EFormationKind::EchelonLeft: return TEXT("echelon_left");
		case EFormationKind::EchelonRight: return TEXT("echelon_right");
		case EFormationKind::Wall: return TEXT("wall");
		case EFormationKind::Hedgehog: return TEXT("hedgehog");
		case EFormationKind::Blizzard: return TEXT("blizzard");
		default: return TEXT("diamond");
		}
	}

	const TCHAR* ToString(EFormationRole Role)
	{
		switch (Role)
		{
		case EFormationRole::Commander: return TEXT("K");
		case EFormationRole::Right: return TEXT("P");
		case EFormationRole::Left: return TEXT("L");
		case EFormationRole::Rear: return TEXT("Z");
		default: return TEXT("K");
		}
	}

	const TCHAR* ToString(EFormationMode Mode)
	{
		switch (Mode)
		{
		case EFormationMode::Walk: return TEXT("walk");
		case EFormationMode::Run: return TEXT("run");
		case EFormationMode::Halt: return TEXT("halt");
		case EFormationMode::Wall: return TEXT("wall");
		case EFormationMode::Forward: return TEXT("forward");
		default: return TEXT("walk");
		}
	}

	const TCHAR* ToString(EFormationTerrainClass Class)
	{
		switch (Class)
		{
		case EFormationTerrainClass::Open: return TEXT("open");
		case EFormationTerrainClass::WallLeft: return TEXT("wall_left");
		case EFormationTerrainClass::WallRight: return TEXT("wall_right");
		case EFormationTerrainClass::Corridor: return TEXT("corridor");
		case EFormationTerrainClass::AtWall: return TEXT("at_wall");
		default: return TEXT("open");
		}
	}

	/** Finds Name among the ToString names of the enum's values 0..Count-1. */
	template <typename TEnum>
	static bool ParseFormationEnumName(const FString& Name, int32 Count, TEnum& Out)
	{
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const TEnum Value = static_cast<TEnum>(Index);
			if (Name.Equals(ToString(Value), ESearchCase::IgnoreCase))
			{
				Out = Value;
				return true;
			}
		}
		return false;
	}

	bool Parse(const FString& Name, EFormationKind& Out)
	{
		return ParseFormationEnumName(Name, static_cast<int32>(EFormationKind::Blizzard) + 1, Out);
	}

	bool Parse(const FString& Name, EFormationRole& Out)
	{
		return ParseFormationEnumName(Name, static_cast<int32>(EFormationRole::Rear) + 1, Out);
	}

	bool Parse(const FString& Name, EFormationMode& Out)
	{
		return ParseFormationEnumName(Name, static_cast<int32>(EFormationMode::Forward) + 1, Out);
	}

	bool Parse(const FString& Name, EFormationTerrainClass& Out)
	{
		return ParseFormationEnumName(Name, static_cast<int32>(EFormationTerrainClass::AtWall) + 1, Out);
	}
}

namespace FormationMath
{
	float ClockToYaw(float Clock, float HeadingYaw)
	{
		return FRotator::NormalizeAxis(HeadingYaw + 30.f * Clock);
	}

	float YawToClock(float Yaw, float HeadingYaw)
	{
		const float Relative = FMath::Fmod(FRotator::NormalizeAxis(Yaw - HeadingYaw) + 360.f, 360.f);
		return Relative / 30.f;
	}

	FVector2D Forward(float Yaw)
	{
		const double Radians = FMath::DegreesToRadians(static_cast<double>(Yaw));
		return FVector2D(FMath::Cos(Radians), FMath::Sin(Radians));
	}

	FVector2D Right(float Yaw)
	{
		const double Radians = FMath::DegreesToRadians(static_cast<double>(Yaw));
		return FVector2D(-FMath::Sin(Radians), FMath::Cos(Radians));
	}

	float YawOf(const FVector2D& Direction)
	{
		if (Direction.X == 0.0 && Direction.Y == 0.0)
		{
			return 0.f;
		}
		return static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(Direction.Y, Direction.X)));
	}

	float AngleBetween(float YawA, float YawB)
	{
		return FMath::Abs(FRotator::NormalizeAxis(YawA - YawB));
	}

	FVector2D Offset(const FVector2D& LeaderPos, float LeaderYaw, float ForwardCm, float RightCm)
	{
		return LeaderPos + Forward(LeaderYaw) * ForwardCm + Right(LeaderYaw) * RightCm;
	}
}
