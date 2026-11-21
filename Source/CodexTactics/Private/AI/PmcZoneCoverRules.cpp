// Zone cover (smart enemies T-08 slice 2): pure rules, no world access. See PmcZoneCoverRules.h.

#include "AI/PmcZoneCoverRules.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace PmcZoneCoverLocal
{
	void ReadZoneCoverNumber(const FJsonObject& Json, const TCHAR* Key, float& Out, float Scale, float Min, float Max)
	{
		double Value = 0.0;
		if (Json.TryGetNumberField(Key, Value))
		{
			Out = static_cast<float>(Value) * Scale;
		}
		Out = FMath::Clamp(Out, Min, Max);
	}

	FVector2D AxisX(const FPmcZoneCoverBlock& Block)
	{
		const float Rad = FMath::DegreesToRadians(Block.YawDeg);
		return FVector2D(FMath::Cos(Rad), FMath::Sin(Rad));
	}

	FVector2D AxisY(const FPmcZoneCoverBlock& Block)
	{
		const FVector2D X = AxisX(Block);
		return FVector2D(-X.Y, X.X);
	}

	/** The corners' projections onto Axis: [Min, Max]. */
	void Project(const FVector2D Corners[4], const FVector2D& Axis, double& OutMin, double& OutMax)
	{
		OutMin = TNumericLimits<double>::Max();
		OutMax = TNumericLimits<double>::Lowest();
		for (int32 Index = 0; Index < 4; ++Index)
		{
			const double Dot = FVector2D::DotProduct(Corners[Index], Axis);
			OutMin = FMath::Min(OutMin, Dot);
			OutMax = FMath::Max(OutMax, Dot);
		}
	}
}

namespace PmcZoneCover
{
	FPmcZoneCoverConfig ConfigFromJson(const FJsonObject& Json, const FPmcZoneCoverConfig& Base)
	{
		using PmcZoneCoverLocal::ReadZoneCoverNumber;
		FPmcZoneCoverConfig Out = Base;
		float Count = static_cast<float>(Out.Count);
		ReadZoneCoverNumber(Json, TEXT("count"), Count, 1.f, 1.f, 3.f);
		Out.Count = FMath::Clamp(FMath::RoundToInt(Count), 1, 3);
		ReadZoneCoverNumber(Json, TEXT("centre_dist_m"), Out.CentreDistCm, 100.f, 0.f, 3000.f);
		ReadZoneCoverNumber(Json, TEXT("side_dist_m"), Out.SideDistCm, 100.f, 0.f, 3000.f);
		ReadZoneCoverNumber(Json, TEXT("side_angle_deg"), Out.SideAngleDeg, 1.f, 0.f, 180.f);
		ReadZoneCoverNumber(Json, TEXT("side_yaw_follow"), Out.SideYawFollow, 1.f, 0.f, 1.f);
		ReadZoneCoverNumber(Json, TEXT("length_cm"), Out.LengthCm, 1.f, 20.f, 1000.f);
		ReadZoneCoverNumber(Json, TEXT("depth_cm"), Out.DepthCm, 1.f, 10.f, 500.f);
		ReadZoneCoverNumber(Json, TEXT("height_cm"), Out.HeightCm, 1.f, 30.f, 400.f);
		ReadZoneCoverNumber(Json, TEXT("edge_margin_cm"), Out.EdgeMarginCm, 1.f, 0.f, 500.f);
		ReadZoneCoverNumber(Json, TEXT("pull_in_step_cm"), Out.PullInStepCm, 1.f, 5.f, 500.f);
		ReadZoneCoverNumber(Json, TEXT("min_dist_m"), Out.MinDistCm, 100.f, 0.f, 3000.f);
		ReadZoneCoverNumber(Json, TEXT("spawn_clear_m"), Out.SpawnClearCm, 100.f, 0.f, 3000.f);
		ReadZoneCoverNumber(Json, TEXT("ground_max_dz_cm"), Out.GroundMaxDzCm, 1.f, 0.f, 1000.f);
		return Out;
	}

	FPmcZoneCoverConfig LoadConfig()
	{
		const FString Path = FPaths::ProjectContentDir() / TEXT("Data/AI/pmc_arena_zone_cover.json");
		FString Text;
		TSharedPtr<FJsonObject> Json;
		if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) || !Json.IsValid())
		{
			return FPmcZoneCoverConfig();
		}
		return ConfigFromJson(*Json);
	}

	FVector2D ApproachDirection(const FVector& Centre, float RadiusCm, TConstArrayView<FVector> AttackerPath, const FVector& FallbackFrom, bool* bOutFromPath)
	{
		if (bOutFromPath)
		{
			*bOutFromPath = false;
		}
		const FVector2D C(Centre);
		const double RadiusSq = static_cast<double>(RadiusCm) * RadiusCm;
		// The first crossing into the circle along the path (a segment that starts outside and reaches it, at an end point or in between).
		if (AttackerPath.Num() >= 2 && RadiusCm > 0.f && FVector2D::DistSquared(FVector2D(AttackerPath[0]), C) > RadiusSq)
		{
			for (int32 Index = 0; Index + 1 < AttackerPath.Num(); ++Index)
			{
				const FVector2D P(AttackerPath[Index]);
				const FVector2D D = FVector2D(AttackerPath[Index + 1]) - P;
				const FVector2D F = P - C;
				const double A = D.SizeSquared();
				if (A <= UE_DOUBLE_SMALL_NUMBER)
				{
					continue;
				}
				const double B = 2.0 * FVector2D::DotProduct(F, D);
				const double Cq = F.SizeSquared() - RadiusSq;
				const double Disc = B * B - 4.0 * A * Cq;
				if (Disc < 0.0)
				{
					continue;
				}
				const double T = (-B - FMath::Sqrt(Disc)) / (2.0 * A);
				if (T < 0.0 || T > 1.0)
				{
					continue;
				}
				const FVector2D Entry = P + D * T;
				const FVector2D Dir = (Entry - C).GetSafeNormal();
				if (!Dir.IsNearlyZero())
				{
					if (bOutFromPath)
					{
						*bOutFromPath = true;
					}
					return Dir;
				}
			}
		}
		const FVector2D Dir = (FVector2D(FallbackFrom) - C).GetSafeNormal();
		return Dir.IsNearlyZero() ? FVector2D(1.0, 0.0) : Dir;
	}

	void FootprintCorners(const FPmcZoneCoverBlock& Block, FVector2D OutCorners[4])
	{
		const FVector2D C(Block.Centre);
		const FVector2D X = PmcZoneCoverLocal::AxisX(Block) * Block.HalfExtent.X;
		const FVector2D Y = PmcZoneCoverLocal::AxisY(Block) * Block.HalfExtent.Y;
		OutCorners[0] = C + X + Y;
		OutCorners[1] = C + X - Y;
		OutCorners[2] = C - X - Y;
		OutCorners[3] = C - X + Y;
	}

	bool IsInsideZone(const FPmcZoneCoverBlock& Block, const FVector& Centre, float RadiusCm, float MarginCm)
	{
		const double Limit = static_cast<double>(RadiusCm) - MarginCm;
		if (Limit <= 0.0)
		{
			return false;
		}
		FVector2D Corners[4];
		FootprintCorners(Block, Corners);
		for (const FVector2D& Corner : Corners)
		{
			if (FVector2D::DistSquared(Corner, FVector2D(Centre)) > Limit * Limit + UE_KINDA_SMALL_NUMBER)
			{
				return false;
			}
		}
		return true;
	}

	float FootprintDistance(const FPmcZoneCoverBlock& Block, const FVector& Point)
	{
		const FVector2D Rel = FVector2D(Point) - FVector2D(Block.Centre);
		const double Dx = FMath::Max(0.0, FMath::Abs(FVector2D::DotProduct(Rel, PmcZoneCoverLocal::AxisX(Block))) - Block.HalfExtent.X);
		const double Dy = FMath::Max(0.0, FMath::Abs(FVector2D::DotProduct(Rel, PmcZoneCoverLocal::AxisY(Block))) - Block.HalfExtent.Y);
		return static_cast<float>(FMath::Sqrt(Dx * Dx + Dy * Dy));
	}

	bool FootprintsOverlap(const FPmcZoneCoverBlock& A, const FPmcZoneCoverBlock& B)
	{
		FVector2D CornersA[4];
		FVector2D CornersB[4];
		FootprintCorners(A, CornersA);
		FootprintCorners(B, CornersB);
		const FVector2D Axes[4] = { PmcZoneCoverLocal::AxisX(A), PmcZoneCoverLocal::AxisY(A), PmcZoneCoverLocal::AxisX(B), PmcZoneCoverLocal::AxisY(B) };
		for (const FVector2D& Axis : Axes)
		{
			double MinA = 0.0, MaxA = 0.0, MinB = 0.0, MaxB = 0.0;
			PmcZoneCoverLocal::Project(CornersA, Axis, MinA, MaxA);
			PmcZoneCoverLocal::Project(CornersB, Axis, MinB, MaxB);
			if (MaxA < MinB || MaxB < MinA)
			{
				return false;
			}
		}
		return true;
	}

	TArray<FPmcZoneCoverBlock> LayoutBlocks(const FVector& Centre, const FVector2D& Approach, float RadiusCm, const FPmcZoneCoverConfig& Config, TArray<FString>* OutDropped)
	{
		TArray<FPmcZoneCoverBlock> Out;
		const FVector2D Dir = Approach.GetSafeNormal();
		if (RadiusCm <= 0.f || Dir.IsNearlyZero())
		{
			return Out;
		}
		const float ApproachYaw = FMath::RadiansToDegrees(FMath::Atan2(static_cast<float>(Dir.Y), static_cast<float>(Dir.X)));
		const int32 Count = FMath::Clamp(Config.Count, 1, 3);
		for (int32 Slot = 0; Slot < Count; ++Slot)
		{
			const float Bearing = Slot == 0 ? 0.f : (Slot == 1 ? Config.SideAngleDeg : -Config.SideAngleDeg);
			const float WantDist = Slot == 0 ? Config.CentreDistCm : Config.SideDistCm;
			FPmcZoneCoverBlock Block;
			Block.Slot = Slot;
			Block.YawDeg = FRotator::NormalizeAxis(ApproachYaw + (Slot == 0 ? 0.f : Bearing * Config.SideYawFollow));
			Block.HalfExtent = FVector(Config.DepthCm * 0.5f, Config.LengthCm * 0.5f, Config.HeightCm * 0.5f);
			const FVector BearingDir = FRotator(0.f, ApproachYaw + Bearing, 0.f).Vector();
			bool bPlaced = false;
			// Pull a block over the edge toward the centre, step by step, never nearer than MinDistCm.
			for (float Dist = WantDist; Dist >= Config.MinDistCm - KINDA_SMALL_NUMBER; Dist -= Config.PullInStepCm)
			{
				Block.DistCm = Dist;
				Block.Centre = FVector(Centre.X + BearingDir.X * Dist, Centre.Y + BearingDir.Y * Dist, Centre.Z);
				Block.bPulledIn = Dist < WantDist - KINDA_SMALL_NUMBER;
				if (IsInsideZone(Block, Centre, RadiusCm, Config.EdgeMarginCm))
				{
					bPlaced = true;
					break;
				}
			}
			if (!bPlaced)
			{
				if (OutDropped)
				{
					OutDropped->Add(FString::Printf(TEXT("slot %d: does not fit inside the zone (r %.0f cm, margin %.0f cm, min dist %.0f cm)"), Slot, RadiusCm,
						Config.EdgeMarginCm, Config.MinDistCm));
				}
				continue;
			}
			const FPmcZoneCoverBlock* Hit = Out.FindByPredicate([&Block](const FPmcZoneCoverBlock& Other) { return FootprintsOverlap(Block, Other); });
			if (Hit)
			{
				if (OutDropped)
				{
					OutDropped->Add(FString::Printf(TEXT("slot %d: overlaps slot %d"), Slot, Hit->Slot));
				}
				continue;
			}
			Out.Add(Block);
		}
		return Out;
	}
}
