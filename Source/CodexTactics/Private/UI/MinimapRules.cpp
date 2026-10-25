#include "UI/MinimapRules.h"

#include "Misc/Base64.h"

// --- FMinimapIsoProjection -----------------------------------------------------------------------------------------------

FVector FMinimapIsoProjection::GetForward() const
{
	return FRotationMatrix(FRotator(PitchDeg, YawDeg, 0.f)).GetUnitAxis(EAxis::X);
}

FVector FMinimapIsoProjection::GetRight() const
{
	return FRotationMatrix(FRotator(PitchDeg, YawDeg, 0.f)).GetUnitAxis(EAxis::Y);
}

FVector FMinimapIsoProjection::GetUp() const
{
	return FRotationMatrix(FRotator(PitchDeg, YawDeg, 0.f)).GetUnitAxis(EAxis::Z);
}

FVector2D FMinimapIsoProjection::WorldToMap(const FVector& World) const
{
	const FVector Delta = World - Center;
	const float SafeWidth = FMath::Max(WidthCm, 1.f);
	const float SafeHeight = FMath::Max(HeightCm, 1.f);
	return FVector2D(0.5f + FVector::DotProduct(Delta, GetRight()) / SafeWidth, 0.5f - FVector::DotProduct(Delta, GetUp()) / SafeHeight);
}

FVector2D FMinimapIsoProjection::MapDirection(const FVector& Direction) const
{
	const FVector Flat = Direction.GetSafeNormal2D();
	if (Flat.IsNearlyZero())
	{
		return FVector2D::ZeroVector;
	}
	return FVector2D(FVector::DotProduct(Flat, GetRight()), -FVector::DotProduct(Flat, GetUp())).GetSafeNormal();
}

FVector FMinimapIsoProjection::MapToImagePlane(const FVector2D& Map) const
{
	return Center + GetRight() * ((Map.X - 0.5f) * WidthCm) + GetUp() * ((0.5f - Map.Y) * HeightCm);
}

FVector FMinimapIsoProjection::MapToWorldOnPlane(const FVector2D& Map, float PlaneZ) const
{
	const FVector OnImage = MapToImagePlane(Map);
	const FVector Forward = GetForward();
	if (FMath::Abs(Forward.Z) < KINDA_SMALL_NUMBER)
	{
		return FVector(OnImage.X, OnImage.Y, PlaneZ);
	}
	const float T = (PlaneZ - OnImage.Z) / Forward.Z;
	return OnImage + Forward * T;
}

FMinimapIsoProjection FMinimapIsoProjection::Fit(const FBox& Bounds, float YawDeg, float PitchDeg, float MarginCm)
{
	FMinimapIsoProjection Out;
	Out.YawDeg = YawDeg;
	Out.PitchDeg = PitchDeg;
	if (!Bounds.IsValid)
	{
		return Out;
	}
	const FVector BoundsCenter = Bounds.GetCenter();
	const FVector Right = Out.GetRight();
	const FVector Up = Out.GetUp();
	float MinR = TNumericLimits<float>::Max();
	float MaxR = TNumericLimits<float>::Lowest();
	float MinU = TNumericLimits<float>::Max();
	float MaxU = TNumericLimits<float>::Lowest();
	for (int32 Corner = 0; Corner < 8; ++Corner)
	{
		const FVector Point((Corner & 1) ? Bounds.Max.X : Bounds.Min.X, (Corner & 2) ? Bounds.Max.Y : Bounds.Min.Y, (Corner & 4) ? Bounds.Max.Z : Bounds.Min.Z);
		const FVector Delta = Point - BoundsCenter;
		const float R = FVector::DotProduct(Delta, Right);
		const float U = FVector::DotProduct(Delta, Up);
		MinR = FMath::Min(MinR, R);
		MaxR = FMath::Max(MaxR, R);
		MinU = FMath::Min(MinU, U);
		MaxU = FMath::Max(MaxU, U);
	}
	Out.Center = BoundsCenter + Right * ((MinR + MaxR) * 0.5f) + Up * ((MinU + MaxU) * 0.5f);
	Out.WidthCm = FMath::Max(100.f, MaxR - MinR + 2.f * MarginCm);
	Out.HeightCm = FMath::Max(100.f, MaxU - MinU + 2.f * MarginCm);
	return Out;
}

FMinimapIsoProjection FMinimapIsoProjection::WithPixelAspect(int32 SizeX, int32 SizeY) const
{
	FMinimapIsoProjection Out = *this;
	if (SizeX <= 0 || SizeY <= 0)
	{
		return Out;
	}
	const float PixelAspect = static_cast<float>(SizeX) / static_cast<float>(SizeY);
	if (GetAspect() > PixelAspect)
	{
		Out.HeightCm = WidthCm / PixelAspect; // grow, never crop
	}
	else
	{
		Out.WidthCm = HeightCm * PixelAspect;
	}
	return Out;
}

FIntPoint FMinimapIsoProjection::ChooseTextureSize(int32 MaxSide) const
{
	MaxSide = FMath::Max(64, MaxSide);
	const float Aspect = GetAspect();
	if (Aspect >= 1.f)
	{
		return FIntPoint(MaxSide, FMath::Clamp(FMath::RoundToInt(MaxSide / Aspect), 64, MaxSide));
	}
	return FIntPoint(FMath::Clamp(FMath::RoundToInt(MaxSide * Aspect), 64, MaxSide), MaxSide);
}

float MinimapRules::CapturePitch(bool bTopDown, float CameraPitchDeg)
{
	if (bTopDown)
	{
		return FMinimapIsoProjection::TopDownPitchDeg;
	}
	// The iso view needs a downward pitch (a level camera configured level / upwards would see no ground).
	return FMath::Clamp(CameraPitchDeg, -89.f, -10.f);
}

// --- FMinimapView --------------------------------------------------------------------------------------------------------

FMinimapView FMinimapView::Make(const FVector2D& Origin, const FVector2D& Size, const FVector2D& CenterUV, float SpanU, float ProjectionAspect)
{
	FMinimapView View;
	View.ScreenOrigin = Origin;
	View.ScreenSize = FVector2D(FMath::Max(Size.X, 1.f), FMath::Max(Size.Y, 1.f));
	View.CenterUV = CenterUV;
	View.SpanU = FMath::Max(SpanU, 0.001f);
	View.SpanV = View.SpanU * (View.ScreenSize.Y / View.ScreenSize.X) * FMath::Max(ProjectionAspect, 0.001f);
	return View;
}

float FMinimapView::FitSpanU(const FVector2D& Size, float ProjectionAspect)
{
	const float K = (FMath::Max(Size.Y, 1.f) / FMath::Max(Size.X, 1.f)) * FMath::Max(ProjectionAspect, 0.001f);
	return FMath::Max(1.f, 1.f / K);
}

FVector2D FMinimapView::MapToScreen(const FVector2D& UV) const
{
	return ScreenOrigin + ScreenSize * 0.5f + FVector2D((UV.X - CenterUV.X) * ScreenSize.X / SpanU, (UV.Y - CenterUV.Y) * ScreenSize.Y / SpanV);
}

FVector2D FMinimapView::ScreenToMap(const FVector2D& Screen) const
{
	const FVector2D Local = Screen - ScreenOrigin - ScreenSize * 0.5f;
	return CenterUV + FVector2D(Local.X * SpanU / ScreenSize.X, Local.Y * SpanV / ScreenSize.Y);
}

bool FMinimapView::Contains(const FVector2D& Screen) const
{
	return Screen.X >= ScreenOrigin.X && Screen.Y >= ScreenOrigin.Y && Screen.X <= ScreenOrigin.X + ScreenSize.X && Screen.Y <= ScreenOrigin.Y + ScreenSize.Y;
}

// --- MinimapRules --------------------------------------------------------------------------------------------------------

namespace MinimapRules
{
	float StepZoomSpan(float SpanU, float Notches, float MinSpan, float MaxSpan)
	{
		return FMath::Clamp(SpanU * FMath::Pow(1.25f, Notches), MinSpan, FMath::Max(MinSpan, MaxSpan));
	}

	FMinimapView ZoomAround(const FMinimapView& View, const FVector2D& Cursor, float NewSpanU, float ProjectionAspect)
	{
		const FVector2D Anchor = View.ScreenToMap(Cursor);
		FMinimapView Zoomed = FMinimapView::Make(View.ScreenOrigin, View.ScreenSize, View.CenterUV, NewSpanU, ProjectionAspect);
		const FVector2D Offset = Cursor - View.ScreenOrigin - View.ScreenSize * 0.5f;
		Zoomed.CenterUV = Anchor - FVector2D(Offset.X * Zoomed.SpanU / Zoomed.ScreenSize.X, Offset.Y * Zoomed.SpanV / Zoomed.ScreenSize.Y);
		return Zoomed;
	}

	FVector2D ClampCenter(const FVector2D& CenterUV, float SpanU, float SpanV)
	{
		const auto Axis = [](float Value, float Span)
		{
			return Span >= 1.f ? 0.5f : FMath::Clamp(Value, Span * 0.5f, 1.f - Span * 0.5f);
		};
		return FVector2D(Axis(CenterUV.X, SpanU), Axis(CenterUV.Y, SpanV));
	}

	FVector RayToPlane(const FVector& Origin, const FVector& Direction, float PlaneZ, float MaxDistanceCm)
	{
		const FVector Dir = Direction.GetSafeNormal();
		if (Dir.Z < -KINDA_SMALL_NUMBER)
		{
			const float T = (PlaneZ - Origin.Z) / Dir.Z;
			if (T >= 0.f && T <= MaxDistanceCm)
			{
				return Origin + Dir * T;
			}
		}
		FVector Flat = Dir.GetSafeNormal2D();
		if (Flat.IsNearlyZero())
		{
			Flat = FVector::ForwardVector;
		}
		const FVector Far = Origin + Flat * MaxDistanceCm;
		return FVector(Far.X, Far.Y, PlaneZ);
	}

	TArray<FVector> FrustumFootprint(const FVector& Location, const FRotator& Rotation, float HorizontalFovDeg, float Aspect, float PlaneZ, float MaxDistanceCm)
	{
		const FRotationMatrix Axes(Rotation);
		const FVector Forward = Axes.GetUnitAxis(EAxis::X);
		const FVector Right = Axes.GetUnitAxis(EAxis::Y);
		const FVector Up = Axes.GetUnitAxis(EAxis::Z);
		const float TanH = FMath::Tan(FMath::DegreesToRadians(FMath::Clamp(HorizontalFovDeg, 1.f, 170.f) * 0.5f));
		const float TanV = TanH / FMath::Max(Aspect, 0.01f);
		const FVector2D Corners[4] = { FVector2D(-1.f, 1.f), FVector2D(1.f, 1.f), FVector2D(1.f, -1.f), FVector2D(-1.f, -1.f) };
		TArray<FVector> Out;
		Out.Reserve(4);
		for (const FVector2D& Corner : Corners)
		{
			Out.Add(RayToPlane(Location, Forward + Right * (Corner.X * TanH) + Up * (Corner.Y * TanV), PlaneZ, MaxDistanceCm));
		}
		return Out;
	}

	void ClipPolygonToRect(const TArray<FVector2D>& Points, const TArray<FVector2D>& UVs, const FBox2D& Rect, TArray<FVector2D>& OutPoints, TArray<FVector2D>& OutUVs)
	{
		OutPoints = Points;
		OutUVs = UVs;
		OutUVs.SetNumZeroed(OutPoints.Num());
		// Each edge: inside test value (>= 0 = inside) for axis / sign.
		for (int32 Edge = 0; Edge < 4 && OutPoints.Num() > 0; ++Edge)
		{
			const auto Side = [&Rect, Edge](const FVector2D& P) -> double
			{
				switch (Edge)
				{
				case 0: return P.X - Rect.Min.X;
				case 1: return Rect.Max.X - P.X;
				case 2: return P.Y - Rect.Min.Y;
				default: return Rect.Max.Y - P.Y;
				}
			};
			TArray<FVector2D> InPoints = MoveTemp(OutPoints);
			TArray<FVector2D> InUVs = MoveTemp(OutUVs);
			OutPoints.Reset();
			OutUVs.Reset();
			for (int32 Index = 0; Index < InPoints.Num(); ++Index)
			{
				const int32 Next = (Index + 1) % InPoints.Num();
				const double SideA = Side(InPoints[Index]);
				const double SideB = Side(InPoints[Next]);
				if (SideA >= 0.0)
				{
					OutPoints.Add(InPoints[Index]);
					OutUVs.Add(InUVs[Index]);
				}
				if ((SideA >= 0.0) != (SideB >= 0.0))
				{
					const double T = SideA / (SideA - SideB);
					OutPoints.Add(FMath::Lerp(InPoints[Index], InPoints[Next], T));
					OutUVs.Add(FMath::Lerp(InUVs[Index], InUVs[Next], T));
				}
			}
		}
		if (OutPoints.Num() < 3)
		{
			OutPoints.Reset();
			OutUVs.Reset();
		}
	}

	bool ClipSegmentToRect(FVector2D& A, FVector2D& B, const FBox2D& Rect)
	{
		const FVector2D D = B - A;
		double T0 = 0.0;
		double T1 = 1.0;
		const double P[4] = { -D.X, D.X, -D.Y, D.Y };
		const double Q[4] = { A.X - Rect.Min.X, Rect.Max.X - A.X, A.Y - Rect.Min.Y, Rect.Max.Y - A.Y };
		for (int32 Index = 0; Index < 4; ++Index)
		{
			if (FMath::IsNearlyZero(P[Index]))
			{
				if (Q[Index] < 0.0)
				{
					return false;
				}
				continue;
			}
			const double R = Q[Index] / P[Index];
			if (P[Index] < 0.0)
			{
				T0 = FMath::Max(T0, R);
			}
			else
			{
				T1 = FMath::Min(T1, R);
			}
			if (T0 > T1)
			{
				return false;
			}
		}
		const FVector2D Start = A + D * T0;
		B = A + D * T1;
		A = Start;
		return true;
	}

	TArray<float> DetectFloorBases(TConstArrayView<float> StackedSurfaceZ, const FFloorParams& Params)
	{
		TArray<float> Out;
		const float Bin = FMath::Max(Params.BinCm, 1.f);
		TMap<int32, int32> Counts;
		for (const float Z : StackedSurfaceZ)
		{
			Counts.FindOrAdd(FMath::FloorToInt(Z / Bin))++;
		}
		TArray<TPair<int32, int32>> Peaks;
		for (const TPair<int32, int32>& Entry : Counts)
		{
			if (Entry.Value >= FMath::Max(1, Params.MinSamples))
			{
				Peaks.Add(Entry);
			}
		}
		Peaks.Sort([](const TPair<int32, int32>& A, const TPair<int32, int32>& B)
		{
			return A.Value != B.Value ? A.Value > B.Value : A.Key < B.Key;
		});
		TArray<float> Centres;
		for (const TPair<int32, int32>& Peak : Peaks)
		{
			const float Centre = (Peak.Key + 0.5f) * Bin;
			if (!Centres.ContainsByPredicate([&Params, Centre](float Other) { return FMath::Abs(Other - Centre) < Params.MinSeparationCm; }))
			{
				Centres.Add(Centre);
			}
		}
		for (const float Centre : Centres)
		{
			double Sum = 0.0;
			int32 Num = 0;
			for (const float Z : StackedSurfaceZ)
			{
				if (FMath::Abs(Z - Centre) <= Bin)
				{
					Sum += Z;
					++Num;
				}
			}
			Out.Add(Num > 0 ? static_cast<float>(Sum / Num) : Centre);
		}
		Out.Sort();
		if (Out.Num() > Params.MaxUpperFloors)
		{
			Out.SetNum(FMath::Max(0, Params.MaxUpperFloors));
		}
		return Out;
	}

	TArray<float> NormalizeFloorBases(TConstArrayView<float> Bases, const FFloorParams& Params)
	{
		TArray<float> Sorted(Bases.GetData(), Bases.Num());
		Sorted.Sort();
		TArray<float> Out;
		for (const float Base : Sorted)
		{
			if (Out.IsEmpty() || Base - Out.Last() >= Params.MinSeparationCm)
			{
				Out.Add(Base);
			}
		}
		if (Out.Num() > Params.MaxUpperFloors)
		{
			Out.SetNum(FMath::Max(0, Params.MaxUpperFloors));
		}
		return Out;
	}

	int32 SelectFloor(float FeetZ, float AboveLowestCm, TConstArrayView<float> UpperBases, int32 PreviousFloor, const FFloorParams& Params)
	{
		if (UpperBases.IsEmpty() || AboveLowestCm < Params.MinAboveLowestCm)
		{
			return 0;
		}
		int32 Floor = 0;
		for (int32 Index = 0; Index < UpperBases.Num(); ++Index)
		{
			const int32 Candidate = Index + 1;
			float Threshold = UpperBases[Index] - Params.EnterBelowCm;
			Threshold += PreviousFloor >= Candidate ? -Params.HysteresisCm : Params.HysteresisCm;
			if (FeetZ >= Threshold)
			{
				Floor = Candidate;
			}
		}
		return Floor;
	}

	int32 FloorWithLadder(bool bOnLadder, int32 PreviousFloor, int32 SelectedFloor)
	{
		return bOnLadder ? PreviousFloor : SelectedFloor;
	}

	bool HideOnFloor(int32 Floor, float BottomZ, float BottomAboveGroundCm, TConstArrayView<float> UpperBases, const FFloorParams& Params)
	{
		if (Floor <= 0 || Floor > UpperBases.Num())
		{
			return BottomAboveGroundCm >= Params.CutClearanceCm;
		}
		return BottomZ >= UpperBases[Floor - 1] + Params.CutClearanceCm;
	}

	TOptional<float> FloorClipZ(int32 Floor, TConstArrayView<float> UpperBases, bool bTopDown, const FFloorParams& Params)
	{
		if (!bTopDown || Floor <= 0 || Floor > UpperBases.Num())
		{
			return TOptional<float>();
		}
		return UpperBases[Floor - 1] + Params.CutClearanceCm;
	}

	FString FloorLabel(int32 Floor)
	{
		return Floor <= 0 ? FString(TEXT("GROUND")) : FString::Printf(TEXT("FLOOR %d"), Floor + 1);
	}

	float EnemyIconAlpha(bool bSeenNow, double SecondsSinceSeen, float FadeSeconds, float LastKnownAlpha)
	{
		if (bSeenNow)
		{
			return 1.f;
		}
		if (FadeSeconds <= 0.f || SecondsSinceSeen < 0.0)
		{
			return 0.f;
		}
		return LastKnownAlpha * FMath::Clamp(1.f - static_cast<float>(SecondsSinceSeen) / FadeSeconds, 0.f, 1.f);
	}
}

// --- FMinimapFogGrid -----------------------------------------------------------------------------------------------------

void FMinimapFogGrid::Init(const FBox2D& InBounds, float InCellCm, int32 NumFloors, int32 MaxCellsPerSide)
{
	Reset();
	if (!InBounds.bIsValid || NumFloors <= 0)
	{
		return;
	}
	Bounds = InBounds;
	const FVector2D Extent = Bounds.GetSize();
	MaxCellsPerSide = FMath::Max(1, MaxCellsPerSide);
	CellCm = FMath::Max3(FMath::Max(InCellCm, 1.f), static_cast<float>(Extent.X / MaxCellsPerSide), static_cast<float>(Extent.Y / MaxCellsPerSide));
	Size = FIntPoint(FMath::Clamp(FMath::CeilToInt(Extent.X / CellCm), 1, MaxCellsPerSide), FMath::Clamp(FMath::CeilToInt(Extent.Y / CellCm), 1, MaxCellsPerSide));
	const int32 Bytes = (Size.X * Size.Y + 7) / 8;
	Layers.SetNum(NumFloors);
	for (TArray<uint8>& Layer : Layers)
	{
		Layer.SetNumZeroed(Bytes);
	}
	RevealedCounts.SetNumZeroed(NumFloors);
	++Revision;
}

void FMinimapFogGrid::Reset()
{
	Layers.Reset();
	RevealedCounts.Reset();
	Size = FIntPoint::ZeroValue;
	Bounds = FBox2D(ForceInit);
	++Revision;
}

int32 FMinimapFogGrid::RevealDisc(int32 Floor, const FVector2D& Center, float RadiusCm)
{
	if (!IsValid() || !Layers.IsValidIndex(Floor) || RadiusCm <= 0.f)
	{
		return 0;
	}
	const int32 X0 = FMath::Max(0, FMath::FloorToInt((Center.X - RadiusCm - Bounds.Min.X) / CellCm));
	const int32 X1 = FMath::Min(Size.X - 1, FMath::FloorToInt((Center.X + RadiusCm - Bounds.Min.X) / CellCm));
	const int32 Y0 = FMath::Max(0, FMath::FloorToInt((Center.Y - RadiusCm - Bounds.Min.Y) / CellCm));
	const int32 Y1 = FMath::Min(Size.Y - 1, FMath::FloorToInt((Center.Y + RadiusCm - Bounds.Min.Y) / CellCm));
	const float RadiusSq = RadiusCm * RadiusCm;
	TArray<uint8>& Layer = Layers[Floor];
	int32 Added = 0;
	for (int32 Y = Y0; Y <= Y1; ++Y)
	{
		for (int32 X = X0; X <= X1; ++X)
		{
			const FVector2D CellCentre = Bounds.Min + FVector2D((X + 0.5f) * CellCm, (Y + 0.5f) * CellCm);
			if (FVector2D::DistSquared(CellCentre, Center) > RadiusSq)
			{
				continue;
			}
			const int32 Index = CellIndex(X, Y);
			uint8& Byte = Layer[Index >> 3];
			const uint8 Bit = static_cast<uint8>(1u << (Index & 7));
			if (!(Byte & Bit))
			{
				Byte |= Bit;
				++Added;
			}
		}
	}
	if (Added > 0)
	{
		RevealedCounts[Floor] += Added;
		++Revision;
	}
	return Added;
}

bool FMinimapFogGrid::IsCellRevealed(int32 Floor, int32 X, int32 Y) const
{
	if (!Layers.IsValidIndex(Floor) || X < 0 || Y < 0 || X >= Size.X || Y >= Size.Y)
	{
		return false;
	}
	const int32 Index = CellIndex(X, Y);
	return (Layers[Floor][Index >> 3] & (1u << (Index & 7))) != 0;
}

bool FMinimapFogGrid::IsRevealed(int32 Floor, const FVector2D& Point) const
{
	if (!IsValid())
	{
		return false;
	}
	return IsCellRevealed(Floor, FMath::FloorToInt((Point.X - Bounds.Min.X) / CellCm), FMath::FloorToInt((Point.Y - Bounds.Min.Y) / CellCm));
}

int32 FMinimapFogGrid::GetRevealedCount(int32 Floor) const
{
	return RevealedCounts.IsValidIndex(Floor) ? RevealedCounts[Floor] : 0;
}

FString FMinimapFogGrid::Encode(int32 Floor) const
{
	return Layers.IsValidIndex(Floor) ? FBase64::Encode(Layers[Floor]) : FString();
}

bool FMinimapFogGrid::Decode(int32 Floor, const FString& Data)
{
	if (!Layers.IsValidIndex(Floor))
	{
		return false;
	}
	TArray<uint8> Bytes;
	if (!FBase64::Decode(Data, Bytes) || Bytes.Num() != Layers[Floor].Num())
	{
		return false;
	}
	Layers[Floor] = MoveTemp(Bytes);
	int32 Count = 0;
	for (int32 Index = 0; Index < Size.X * Size.Y; ++Index)
	{
		Count += (Layers[Floor][Index >> 3] & (1u << (Index & 7))) ? 1 : 0;
	}
	RevealedCounts[Floor] = Count;
	++Revision;
	return true;
}
