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

	double NormYaw(double Yaw)
	{
		double Result = FMath::Fmod(FMath::Fmod(Yaw + 180.0, 360.0) + 360.0, 360.0) - 180.0;
		if (Result == -180.0)
		{
			Result = 180.0;
		}
		return Result;
	}

	FVector2D DirOf(double Yaw)
	{
		const double Radians = FMath::DegreesToRadians(Yaw);
		return FVector2D(FMath::Cos(Radians), FMath::Sin(Radians));
	}

	double YawOfD(const FVector2D& V)
	{
		return (V.X == 0.0 && V.Y == 0.0) ? 0.0 : FMath::RadiansToDegrees(FMath::Atan2(V.Y, V.X));
	}

	FVector2D ClosestOnSegment(const FVector2D& P, const FVector2D& A, const FVector2D& B, double* OutT)
	{
		const FVector2D Edge = B - A;
		const double LengthSquared = FVector2D::DotProduct(Edge, Edge);
		const double T = LengthSquared == 0.0 ? 0.0 : FMath::Clamp(FVector2D::DotProduct(P - A, Edge) / LengthSquared, 0.0, 1.0);
		if (OutT)
		{
			*OutT = T;
		}
		return A + Edge * T;
	}

	double DistToWall(const FVector2D& P, const FFormationWall& Wall)
	{
		return FMath::Max(0.0, FVector2D::Distance(P, ClosestOnSegment(P, Wall.A, Wall.B)) - 0.5 * Wall.Thickness);
	}

	/** Proper crossing of P -> Q and A -> B (strict signs; touching / collinear does not count). */
	static bool FormationSegmentsIntersect(const FVector2D& P, const FVector2D& Q, const FVector2D& A, const FVector2D& B)
	{
		const auto Cross = [](const FVector2D& U, const FVector2D& V) { return U.X * V.Y - U.Y * V.X; };
		const double D1 = Cross(Q - P, A - P);
		const double D2 = Cross(Q - P, B - P);
		const double D3 = Cross(B - A, P - A);
		const double D4 = Cross(B - A, Q - A);
		return ((D1 > 0.0 && D2 < 0.0) || (D1 < 0.0 && D2 > 0.0)) && ((D3 > 0.0 && D4 < 0.0) || (D3 < 0.0 && D4 > 0.0));
	}

	double SegSegDist(const FVector2D& P, const FVector2D& Q, const FVector2D& A, const FVector2D& B)
	{
		if (FormationSegmentsIntersect(P, Q, A, B))
		{
			return 0.0;
		}
		return FMath::Min(FMath::Min(FVector2D::Distance(P, ClosestOnSegment(P, A, B)), FVector2D::Distance(Q, ClosestOnSegment(Q, A, B))),
			FMath::Min(FVector2D::Distance(A, ClosestOnSegment(A, P, Q)), FVector2D::Distance(B, ClosestOnSegment(B, P, Q))));
	}

	bool SegmentBlocked(const FVector2D& P, const FVector2D& Q, const TArray<FFormationWall>& Walls, double Radius)
	{
		if (FVector2D::Distance(P, Q) < 1e-6)
		{
			for (const FFormationWall& Wall : Walls)
			{
				if (DistToWall(P, Wall) < Radius)
				{
					return true;
				}
			}
			return false;
		}
		for (const FFormationWall& Wall : Walls)
		{
			if (SegSegDist(P, Q, Wall.A, Wall.B) < 0.5 * Wall.Thickness + Radius)
			{
				return true;
			}
		}
		return false;
	}
}
