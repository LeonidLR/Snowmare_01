#include "LevelGen/PropsLayoutView.h"
#include "CodexTactics.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#include "LevelGenJsonRead.h"

namespace PropsLayoutViewLocal
{
	using namespace LevelGenJson;

	const TCHAR* const GSchema = TEXT("codex.levelgen_layout/1");
	const TCHAR* const GGround = TEXT("ground");

	/** round(sin(d deg) x 16384) for d = 0..90: integer rotation of the covers (no floats in a decision; the TS port copies the table). */
	constexpr int32 GSinScale = 16384;
	const int32 GSinTable[91] = {
		0, 286, 572, 857, 1143, 1428, 1713, 1997, 2280, 2563, 2845, 3126, 3406,
		3686, 3964, 4240, 4516, 4790, 5063, 5334, 5604, 5872, 6138, 6402, 6664, 6924,
		7182, 7438, 7692, 7943, 8192, 8438, 8682, 8923, 9162, 9397, 9630, 9860, 10087,
		10311, 10531, 10749, 10963, 11174, 11381, 11585, 11786, 11982, 12176, 12365, 12551, 12733,
		12911, 13085, 13255, 13421, 13583, 13741, 13894, 14044, 14189, 14330, 14466, 14598, 14726,
		14849, 14968, 15082, 15191, 15296, 15396, 15491, 15582, 15668, 15749, 15826, 15897, 15964,
		16026, 16083, 16135, 16182, 16225, 16262, 16294, 16322, 16344, 16362, 16374, 16382, 16384,
	};

	int64 SinQ(int32 Deg)
	{
		Deg = ((Deg % 360) + 360) % 360;
		if (Deg <= 90) { return GSinTable[Deg]; }
		if (Deg <= 180) { return GSinTable[180 - Deg]; }
		if (Deg <= 270) { return -GSinTable[Deg - 180]; }
		return -GSinTable[360 - Deg];
	}

	int64 CosQ(int32 Deg)
	{
		return SinQ((Deg % 360) + 90);
	}

	/** Floor division by 2 for negative values too (JS Math.floor(v / 2)). */
	int32 FloorHalf(int64 V)
	{
		return static_cast<int32>(V >= 0 ? V / 2 : -((-V + 1) / 2));
	}

	int32 ClampRadius(int32 R)
	{
		return FMath::Clamp(R, 0, FPropsLayoutView::MaxRadiusCm);
	}

	/**
	 * Exact integer test: Den x dist(P, segment AB) <= Num (Den 1 or 2, so a half thickness stays integer). A degenerate segment is a
	 * point. The bounding-box reject first keeps every product inside int64 for segments <= MaxSegmentCm and Num / Den <= MaxRadiusCm.
	 */
	bool SegmentWithin(FIntPoint P, FIntPoint A, FIntPoint B, int64 Num, int64 Den)
	{
		if (Num < 0)
		{
			return false;
		}
		const int64 Reach = (Num + Den - 1) / Den;
		const int64 Px = P.X;
		const int64 Py = P.Y;
		if (Px < static_cast<int64>(FMath::Min(A.X, B.X)) - Reach || Px > static_cast<int64>(FMath::Max(A.X, B.X)) + Reach
			|| Py < static_cast<int64>(FMath::Min(A.Y, B.Y)) - Reach || Py > static_cast<int64>(FMath::Max(A.Y, B.Y)) + Reach)
		{
			return false;
		}
		const int64 ABx = static_cast<int64>(B.X) - A.X;
		const int64 ABy = static_cast<int64>(B.Y) - A.Y;
		const int64 APx = Px - A.X;
		const int64 APy = Py - A.Y;
		const int64 Num2 = Num * Num;
		const int64 Den2 = Den * Den;
		const int64 Dot = APx * ABx + APy * ABy;
		const int64 Len2 = ABx * ABx + ABy * ABy;
		if (Len2 == 0 || Dot <= 0)
		{
			return Den2 * (APx * APx + APy * APy) <= Num2;
		}
		if (Dot >= Len2)
		{
			const int64 BPx = Px - B.X;
			const int64 BPy = Py - B.Y;
			return Den2 * (BPx * BPx + BPy * BPy) <= Num2;
		}
		const int64 Cross = ABx * APy - ABy * APx;
		return Den2 * Cross * Cross <= Num2 * Len2;
	}

	/** Squared distance as a double: only an estimate for CeilDistance (never a decision). */
	double DistSqEstimate(FIntPoint P, FIntPoint A, FIntPoint B)
	{
		const double Dx = static_cast<double>(B.X) - A.X;
		const double Dy = static_cast<double>(B.Y) - A.Y;
		const double Len2 = Dx * Dx + Dy * Dy;
		double T = 0.0;
		if (Len2 > 0.0)
		{
			T = FMath::Clamp(((static_cast<double>(P.X) - A.X) * Dx + (static_cast<double>(P.Y) - A.Y) * Dy) / Len2, 0.0, 1.0);
		}
		const double Ex = P.X - (A.X + T * Dx);
		const double Ey = P.Y - (A.Y + T * Dy);
		return Ex * Ex + Ey * Ey;
	}

	/** Smallest integer D with dist(P, AB) <= D (exact up to MaxRadiusCm; farther distances are the rounded-up estimate). */
	int32 CeilDistance(FIntPoint P, FIntPoint A, FIntPoint B)
	{
		const double Estimate = FMath::Sqrt(DistSqEstimate(P, A, B));
		if (Estimate > FPropsLayoutView::MaxRadiusCm)
		{
			return static_cast<int32>(FMath::Min(FMath::CeilToDouble(Estimate), static_cast<double>(MAX_int32 - 1)));
		}
		int64 D = static_cast<int64>(FMath::CeilToDouble(Estimate));
		while (D > 0 && SegmentWithin(P, A, B, D - 1, 1))
		{
			--D;
		}
		while (!SegmentWithin(P, A, B, D, 1))
		{
			++D;
		}
		return static_cast<int32>(D);
	}

	/** Even-odd test in int64, half-open on y (a point on a bottom edge is in, on a top edge out - deterministic, not symmetric). */
	bool PointInPolygon(FIntPoint P, const TArray<FIntPoint>& Poly)
	{
		bool bInside = false;
		for (int32 I = 0, J = Poly.Num() - 1; I < Poly.Num(); J = I++)
		{
			const FIntPoint A = Poly[J];
			const FIntPoint B = Poly[I];
			if ((A.Y > P.Y) == (B.Y > P.Y))
			{
				continue;
			}
			const int64 Lhs = (static_cast<int64>(P.X) - A.X) * (static_cast<int64>(B.Y) - A.Y);
			const int64 Rhs = (static_cast<int64>(B.X) - A.X) * (static_cast<int64>(P.Y) - A.Y);
			if (B.Y > A.Y ? Lhs < Rhs : Lhs > Rhs)
			{
				bInside = !bInside;
			}
		}
		return bInside;
	}

	bool InRect(FIntPoint P, FIntPoint Min, FIntPoint Max)
	{
		return P.X >= Min.X && P.X <= Max.X && P.Y >= Min.Y && P.Y <= Max.Y;
	}

	/** Exact: distance from P to the filled rectangle <= R. */
	bool RectWithin(FIntPoint P, FIntPoint Min, FIntPoint Max, int64 R)
	{
		const int64 Dx = FMath::Max3(static_cast<int64>(Min.X) - P.X, static_cast<int64>(0), static_cast<int64>(P.X) - Max.X);
		const int64 Dy = FMath::Max3(static_cast<int64>(Min.Y) - P.Y, static_cast<int64>(0), static_cast<int64>(P.Y) - Max.Y);
		if (Dx > R || Dy > R)
		{
			return false;
		}
		return Dx * Dx + Dy * Dy <= R * R;
	}

	/** Exact (table rotation): P within Clearance of the cover's oriented box (size x / y centred on pos, rotated by yaw). */
	bool CoverWithin(FIntPoint P, FIntPoint Center, int32 Yaw, int32 SizeX, int32 SizeY, int64 Clearance)
	{
		const int64 Dx = static_cast<int64>(P.X) - Center.X;
		const int64 Dy = static_cast<int64>(P.Y) - Center.Y;
		const int64 Reach = (static_cast<int64>(SizeX) + SizeY) / 2 + Clearance + 2; // >= half diagonal + clearance (+ table error)
		if (FMath::Abs(Dx) > Reach || FMath::Abs(Dy) > Reach)
		{
			return false;
		}
		const int64 S = SinQ(Yaw);
		const int64 C = CosQ(Yaw);
		const int64 Lx = Dx * C + Dy * S;  // local x, scaled by GSinScale
		const int64 Ly = -Dx * S + Dy * C; // local y, scaled by GSinScale
		return 2 * FMath::Abs(Lx) <= (SizeX + 2 * Clearance) * GSinScale && 2 * FMath::Abs(Ly) <= (SizeY + 2 * Clearance) * GSinScale;
	}

	bool SegmentTooLong(FIntPoint A, FIntPoint B)
	{
		const int64 Dx = static_cast<int64>(B.X) - A.X;
		const int64 Dy = static_cast<int64>(B.Y) - A.Y;
		return Dx * Dx + Dy * Dy > static_cast<int64>(FPropsLayoutView::MaxSegmentCm) * FPropsLayoutView::MaxSegmentCm;
	}

	bool PolyTooLong(const TArray<FIntPoint>& Poly, bool bClosed)
	{
		const int32 Edges = bClosed ? Poly.Num() : Poly.Num() - 1;
		for (int32 I = 0; I < Edges; ++I)
		{
			if (SegmentTooLong(Poly[I], Poly[(I + 1) % Poly.Num()]))
			{
				return true;
			}
		}
		return false;
	}

	/** Wall id without the trailing "_<digits>": F_compound_N_0 -> F_compound_N; W_deadend_w and F_south stay as they are. */
	FString StripIndex(const FString& Id)
	{
		int32 Idx = INDEX_NONE;
		if (!Id.FindLastChar(TEXT('_'), Idx) || Idx + 1 >= Id.Len())
		{
			return Id;
		}
		for (int32 I = Idx + 1; I < Id.Len(); ++I)
		{
			if (!FChar::IsDigit(Id[I]))
			{
				return Id;
			}
		}
		return Id.Left(Idx);
	}

	bool IsGateKind(const FString& Kind)
	{
		return Kind == TEXT("fence") || Kind == TEXT("wall");
	}

	/** A required field: false + "missing" when absent (a wrong type was already reported by the reader). */
	bool Need(FFieldReader& R, const TCHAR* Key, bool bRead)
	{
		if (!bRead && !R.Has(Key))
		{
			R.Fail(FString::Printf(TEXT("missing '%s'"), Key));
		}
		return bRead;
	}

	bool NumberToInt(const TSharedPtr<FJsonValue>& V, int32& Out)
	{
		return !IsNullValue(V) && V->Type == EJson::Number && ToInt32(V->AsNumber(), Out);
	}

	/** An array of [x, y] under Key; any bad entry fails the reader. False when absent or bad. */
	bool ReadPoly(FFieldReader& R, const TCHAR* Key, TArray<FIntPoint>& Out)
	{
		Out.Reset();
		const TArray<TSharedPtr<FJsonValue>>* Items = R.Array(Key);
		if (!Items)
		{
			return false;
		}
		for (int32 I = 0; I < Items->Num(); ++I)
		{
			FIntPoint P;
			if (!ReadPosValue((*Items)[I], P))
			{
				R.Fail(FString::Printf(TEXT("'%s'[%d] must be [x, y] in cm"), Key, I));
				Out.Reset();
				return false;
			}
			Out.Add(P);
		}
		return true;
	}

	/** Calls Fn(object, ctx) for each element of the array under Key; a non-object element is an error. */
	template <typename FuncType>
	void ForEachObject(FFieldReader& Parent, const TCHAR* Key, FuncType&& Fn)
	{
		const TArray<TSharedPtr<FJsonValue>>* Items = Parent.Array(Key);
		if (!Items)
		{
			return;
		}
		for (int32 Index = 0; Index < Items->Num(); ++Index)
		{
			const FString Ctx = FString::Printf(TEXT("%s.%s[%d]"), *Parent.Ctx, Key, Index);
			const TSharedPtr<FJsonObject> Object = AsObject((*Items)[Index]);
			if (!Object.IsValid())
			{
				Parent.Errors.Add(Ctx + TEXT(": not an object"));
				continue;
			}
			Fn(*Object, Ctx);
		}
	}
}

// ---- FPropsExclusion ---------------------------------------------------------------------------------------------------------

bool FPropsExclusion::Contains(FIntPoint P, const FString& Layer) const
{
	using namespace PropsLayoutViewLocal;
	if ((!WalkLayer.IsEmpty() && WalkLayer != Layer) || Points.Num() == 0)
	{
		return false;
	}
	const int32 R = ClampRadius(RadiusCm);
	if (bArea && Points.Num() >= 3 && PointInPolygon(P, Points))
	{
		return true;
	}
	// the edges with the radius (also R 0: a point exactly on an area's edge is in, whatever the half-open polygon test says)
	if (Points.Num() == 1)
	{
		return SegmentWithin(P, Points[0], Points[0], R, 1);
	}
	const int32 Edges = (bArea && Points.Num() >= 3) ? Points.Num() : Points.Num() - 1;
	for (int32 I = 0; I < Edges; ++I)
	{
		if (SegmentWithin(P, Points[I], Points[(I + 1) % Points.Num()], R, 1))
		{
			return true;
		}
	}
	return false;
}

// ---- build -------------------------------------------------------------------------------------------------------------------

bool FPropsLayoutView::Build(const FString& Json, FPropsLayoutView& Out, TArray<FString>& Errors, const FPropsLayoutConfig& InConfig)
{
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root.IsValid())
	{
		Out = FPropsLayoutView();
		Errors.Add(TEXT("layout: invalid JSON (the root must be an object)"));
		UE_LOG(LogCodexTactics, Warning, TEXT("Props layout view: %s"), *Errors.Last());
		return false;
	}
	return Build(*Root, Out, Errors, InConfig);
}

bool FPropsLayoutView::Build(const FJsonObject& Root, FPropsLayoutView& Out, TArray<FString>& Errors, const FPropsLayoutConfig& InConfig)
{
	using namespace PropsLayoutViewLocal;

	Out = FPropsLayoutView();
	Out.Config = InConfig;
	const int32 FirstError = Errors.Num();
	const auto Finish = [&Errors, FirstError]()
	{
		for (int32 I = FirstError; I < Errors.Num(); ++I)
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("Props layout view: %s"), *Errors[I]);
		}
		return Errors.Num() == FirstError;
	};

	FFieldReader R(Root, TEXT("layout"), Errors);
	FString Schema;
	if (!R.Str(TEXT("schema"), Schema) || Schema != GSchema)
	{
		Errors.Add(FString::Printf(TEXT("layout: schema '%s' is not %s; refused"), *Schema, GSchema));
		return Finish();
	}
	R.Str(TEXT("name"), Out.Name);
	R.Int(TEXT("ground_z"), Out.GroundZ);
	if (const TSharedPtr<FJsonObject> Bounds = R.Object(TEXT("bounds")))
	{
		FFieldReader BR(*Bounds, TEXT("layout.bounds"), Errors);
		Need(BR, TEXT("min"), BR.Point(TEXT("min"), Out.BoundsMin));
		Need(BR, TEXT("max"), BR.Point(TEXT("max"), Out.BoundsMax));
	}
	else if (!R.Has(TEXT("bounds")))
	{
		R.Fail(TEXT("missing 'bounds'"));
	}
	if (const TSharedPtr<FJsonObject> Spawn = R.Object(TEXT("spawn")))
	{
		FFieldReader SR(*Spawn, TEXT("layout.spawn"), Errors);
		Need(SR, TEXT("pos"), SR.Point(TEXT("pos"), Out.SpawnPos));
	}
	else if (!R.Has(TEXT("spawn")))
	{
		R.Fail(TEXT("missing 'spawn'"));
	}
	Need(R, TEXT("end"), R.Point(TEXT("end"), Out.EndPos));

	ForEachObject(R, TEXT("walls"), [&](const FJsonObject& J, const FString& Ctx)
	{
		FFieldReader WR(J, Ctx, Errors);
		FWall W;
		Need(WR, TEXT("id"), WR.Str(TEXT("id"), W.Id));
		WR.Ctx = FString::Printf(TEXT("%s (%s)"), *Ctx, *W.Id);
		Need(WR, TEXT("a"), WR.Point(TEXT("a"), W.A));
		Need(WR, TEXT("b"), WR.Point(TEXT("b"), W.B));
		WR.Int(TEXT("thickness"), W.Thickness);
		WR.Str(TEXT("kind"), W.Kind);
		WR.Str(TEXT("building"), W.Building);
		WR.Int(TEXT("floor"), W.Floor);
		WR.Int(TEXT("height"), W.Height);
		if (WR.bOk && SegmentTooLong(W.A, W.B))
		{
			WR.Fail(FString::Printf(TEXT("longer than %d cm (exact geometry limit)"), MaxSegmentCm));
		}
		if (WR.bOk && (W.Thickness < 0 || W.Thickness > MaxWallThicknessCm))
		{
			WR.Fail(FString::Printf(TEXT("'thickness' must be 0..%d cm"), MaxWallThicknessCm));
		}
		if (WR.bOk)
		{
			Out.Walls.Add(MoveTemp(W));
		}
	});

	ForEachObject(R, TEXT("buildings"), [&](const FJsonObject& J, const FString& Ctx)
	{
		FFieldReader BR(J, Ctx, Errors);
		FBuilding B;
		Need(BR, TEXT("id"), BR.Str(TEXT("id"), B.Id));
		BR.Ctx = FString::Printf(TEXT("%s (%s)"), *Ctx, *B.Id);
		TArray<FIntPoint> Foot;
		if (Need(BR, TEXT("footprint"), ReadPoly(BR, TEXT("footprint"), Foot)) && Foot.Num() != 2)
		{
			BR.Fail(TEXT("'footprint' must be [[x, y], [x, y]]"));
		}
		if (Foot.Num() == 2)
		{
			B.Min = FIntPoint(FMath::Min(Foot[0].X, Foot[1].X), FMath::Min(Foot[0].Y, Foot[1].Y));
			B.Max = FIntPoint(FMath::Max(Foot[0].X, Foot[1].X), FMath::Max(Foot[0].Y, Foot[1].Y));
			if (SegmentTooLong(B.Min, FIntPoint(B.Max.X, B.Min.Y)) || SegmentTooLong(B.Min, FIntPoint(B.Min.X, B.Max.Y)))
			{
				BR.Fail(FString::Printf(TEXT("footprint side longer than %d cm (exact geometry limit)"), MaxSegmentCm));
			}
		}
		BR.Int(TEXT("floors"), B.Floors);
		B.Floors = FMath::Max(1, B.Floors);
		BR.Bool(TEXT("hollow"), B.bHollow);
		BR.Bool(TEXT("enterable"), B.bEnterable);
		BR.Bool(TEXT("roof_walkable"), B.bRoofWalkable);
		ForEachObject(BR, TEXT("doors"), [&](const FJsonObject& D, const FString& DCtx)
		{
			FFieldReader DR(D, DCtx, Errors);
			FBuildingDoor Door;
			Need(DR, TEXT("pos"), DR.Point(TEXT("pos"), Door.Pos));
			DR.Int(TEXT("floor"), Door.Floor);
			if (DR.bOk)
			{
				B.Doors.Add(Door);
			}
		});
		ForEachObject(BR, TEXT("stairs"), [&](const FJsonObject& S, const FString& SCtx)
		{
			FFieldReader SR(S, SCtx, Errors);
			FStair Stair;
			Need(SR, TEXT("id"), SR.Str(TEXT("id"), Stair.Id));
			Need(SR, TEXT("pos"), SR.Point(TEXT("pos"), Stair.Pos));
			Need(SR, TEXT("top"), SR.Point(TEXT("top"), Stair.Top));
			Stair.bHasMid = SR.Point(TEXT("mid"), Stair.Mid);
			if (SR.bOk && (SegmentTooLong(Stair.Pos, Stair.bHasMid ? Stair.Mid : Stair.Top) || (Stair.bHasMid && SegmentTooLong(Stair.Mid, Stair.Top))))
			{
				SR.Fail(FString::Printf(TEXT("longer than %d cm (exact geometry limit)"), MaxSegmentCm));
			}
			if (SR.bOk)
			{
				B.Stairs.Add(Stair);
			}
		});
		TArray<FIntPoint> RoofHoles;
		if (ReadPoly(BR, TEXT("roof_holes"), RoofHoles))
		{
			B.RoofHoles = MoveTemp(RoofHoles);
		}
		ForEachObject(BR, TEXT("floor_holes"), [&](const FJsonObject& H, const FString& HCtx)
		{
			FFieldReader HR(H, HCtx, Errors);
			FFloorHole Hole;
			Need(HR, TEXT("pos"), HR.Point(TEXT("pos"), Hole.Pos));
			HR.Int(TEXT("floor"), Hole.Floor);
			if (HR.bOk)
			{
				B.FloorHoles.Add(Hole);
			}
		});
		ForEachObject(BR, TEXT("openings"), [&](const FJsonObject& O, const FString& OCtx)
		{
			FFieldReader OR(O, OCtx, Errors);
			FBuildingOpening Op;
			OR.Str(TEXT("kind"), Op.Kind);
			Need(OR, TEXT("pos"), OR.Point(TEXT("pos"), Op.Pos));
			OR.Int(TEXT("floor"), Op.Floor);
			OR.Int(TEXT("width"), Op.WidthCm);
			OR.Int(TEXT("sill_height"), Op.SillHeightCm);
			OR.Int(TEXT("top_height"), Op.TopHeightCm);
			if (OR.bOk)
			{
				B.Openings.Add(MoveTemp(Op));
			}
		});
		ForEachObject(BR, TEXT("floors_detail"), [&](const FJsonObject& FD, const FString& FDCtx)
		{
			FFieldReader FDR(FD, FDCtx, Errors);
			int32 DetailFloor = 0;
			FDR.Int(TEXT("floor"), DetailFloor);
			ForEachObject(FDR, TEXT("gaps"), [&](const FJsonObject& G, const FString& GCtx)
			{
				FFieldReader GR(G, GCtx, Errors);
				FFloorGapRect Gap;
				Gap.Floor = DetailFloor;
				GR.Str(TEXT("id"), Gap.Id);
				TArray<FIntPoint> Poly;
				if (ReadPoly(GR, TEXT("poly"), Poly) && Poly.Num() >= 2 && GR.bOk)
				{
					Gap.Min = Poly[0];
					Gap.Max = Poly[0];
					for (const FIntPoint& Pt : Poly)
					{
						Gap.Min.X = FMath::Min(Gap.Min.X, Pt.X);
						Gap.Min.Y = FMath::Min(Gap.Min.Y, Pt.Y);
						Gap.Max.X = FMath::Max(Gap.Max.X, Pt.X);
						Gap.Max.Y = FMath::Max(Gap.Max.Y, Pt.Y);
					}
					B.FloorGaps.Add(MoveTemp(Gap));
				}
			});
		});
		ForEachObject(BR, TEXT("hatches"), [&](const FJsonObject& H, const FString& HCtx)
		{
			FFieldReader HR(H, HCtx, Errors);
			FBuildingHatch Hatch;
			HR.Str(TEXT("id"), Hatch.Id);
			Need(HR, TEXT("pos"), HR.Point(TEXT("pos"), Hatch.Pos));
			HR.Int(TEXT("floor"), Hatch.Floor);
			FIntPoint Size(100, 100);
			if (HR.Point(TEXT("size"), Size))
			{
				Hatch.SizeX = FMath::Max(10, Size.X);
				Hatch.SizeY = FMath::Max(10, Size.Y);
			}
			if (HR.bOk)
			{
				B.Hatches.Add(MoveTemp(Hatch));
			}
		});
		ForEachObject(BR, TEXT("parapets"), [&](const FJsonObject& P, const FString& PCtx)
		{
			FFieldReader PR(P, PCtx, Errors);
			FBuildingParapet Parapet;
			Need(PR, TEXT("start"), PR.Point(TEXT("start"), Parapet.Start));
			Need(PR, TEXT("end"), PR.Point(TEXT("end"), Parapet.End));
			Parapet.ThicknessCm = 20;
			PR.Int(TEXT("thickness_cm"), Parapet.ThicknessCm);
			if (PR.bOk)
			{
				B.Parapets.Add(Parapet);
			}
		});
		if (BR.bOk)
		{
			Out.Buildings.Add(MoveTemp(B));
		}
	});

	if (const TSharedPtr<FJsonObject> Cut = R.Object(TEXT("cut")))
	{
		FFieldReader CR(*Cut, TEXT("layout.cut"), Errors);
		Out.bHasCut = true;
		ReadPoly(CR, TEXT("poly"), Out.CutPoly);
		if (PolyTooLong(Out.CutPoly, true))
		{
			CR.Fail(FString::Printf(TEXT("'poly' edge longer than %d cm (exact geometry limit)"), MaxSegmentCm));
			Out.CutPoly.Reset();
		}
		CR.Int(TEXT("level"), Out.CutLevel);
		Out.bHasCutFloorZ = CR.Int(TEXT("floor_z"), Out.CutFloorZ);
		ForEachObject(CR, TEXT("crossings"), [&](const FJsonObject& J, const FString& Ctx)
		{
			FFieldReader XR(J, Ctx, Errors);
			FCrossing X;
			Need(XR, TEXT("id"), XR.Str(TEXT("id"), X.Id));
			Need(XR, TEXT("poly"), ReadPoly(XR, TEXT("poly"), X.Poly));
			if (XR.bOk)
			{
				Out.Crossings.Add(MoveTemp(X));
			}
		});
	}

	ForEachObject(R, TEXT("ladders"), [&](const FJsonObject& J, const FString& Ctx)
	{
		FFieldReader LR(J, Ctx, Errors);
		FLadder L;
		Need(LR, TEXT("id"), LR.Str(TEXT("id"), L.Id));
		Need(LR, TEXT("foot"), LR.Point(TEXT("foot"), L.Foot));
		Need(LR, TEXT("top"), LR.Point(TEXT("top"), L.Top));
		if (!LR.Point(TEXT("pos"), L.Pos))
		{
			L.Pos = L.Foot;
		}
		LR.Str(TEXT("from"), L.From);
		LR.Str(TEXT("to"), L.To);
		if (LR.bOk)
		{
			Out.Ladders.Add(MoveTemp(L));
		}
	});

	ForEachObject(R, TEXT("covers"), [&](const FJsonObject& J, const FString& Ctx)
	{
		FFieldReader CR(J, Ctx, Errors);
		FPropsCover C;
		Need(CR, TEXT("id"), CR.Str(TEXT("id"), C.Id));
		Need(CR, TEXT("pos"), CR.Point(TEXT("pos"), C.Pos));
		CR.Int(TEXT("yaw"), C.Yaw);
		const TArray<TSharedPtr<FJsonValue>>* Size = CR.Array(TEXT("size"));
		if (Need(CR, TEXT("size"), Size != nullptr)
			&& (Size->Num() < 2 || !NumberToInt((*Size)[0], C.SizeX) || !NumberToInt((*Size)[1], C.SizeY) || C.SizeX < 0 || C.SizeY < 0))
		{
			CR.Fail(TEXT("'size' must be [x, y, h] in cm"));
		}
		if (Size && Size->Num() >= 3 && !NumberToInt((*Size)[2], C.HeightCm))
		{
			CR.Fail(TEXT("'size'[2] (height) must be a number in cm"));
		}
		if (CR.bOk && (C.SizeX > MaxSegmentCm || C.SizeY > MaxSegmentCm))
		{
			CR.Fail(FString::Printf(TEXT("'size' above %d cm (exact geometry limit)"), MaxSegmentCm));
		}
		if (CR.bOk)
		{
			Out.Covers.Add(MoveTemp(C));
		}
	});

	ForEachObject(R, TEXT("zones"), [&](const FJsonObject& J, const FString& Ctx)
	{
		FFieldReader ZR(J, Ctx, Errors);
		FPropsZone Z;
		Need(ZR, TEXT("id"), ZR.Str(TEXT("id"), Z.Id));
		Need(ZR, TEXT("poly"), ReadPoly(ZR, TEXT("poly"), Z.Poly));
		ZR.Str(TEXT("kind"), Z.Kind);
		ZR.Str(TEXT("set_piece"), Z.SetPiece);
		ZR.Str(TEXT("intent"), Z.Intent);
		if (ZR.bOk)
		{
			Out.Zones.Add(MoveTemp(Z));
		}
	});

	ForEachObject(R, TEXT("ambush_points"), [&](const FJsonObject& J, const FString& Ctx)
	{
		FFieldReader AR(J, Ctx, Errors);
		FPropsAnchorPoint A;
		Need(AR, TEXT("id"), AR.Str(TEXT("id"), A.Id));
		Need(AR, TEXT("pos"), AR.Point(TEXT("pos"), A.Pos));
		if (AR.bOk)
		{
			Out.AmbushPoints.Add(MoveTemp(A));
		}
	});

	ForEachObject(R, TEXT("routes"), [&](const FJsonObject& J, const FString& Ctx)
	{
		FFieldReader RR(J, Ctx, Errors);
		FPropsRoute Route;
		Need(RR, TEXT("id"), RR.Str(TEXT("id"), Route.Id));
		RR.Ctx = FString::Printf(TEXT("%s (%s)"), *Ctx, *Route.Id);
		Need(RR, TEXT("points"), ReadPoly(RR, TEXT("points"), Route.Points));
		RR.Bool(TEXT("mandatory"), Route.bMandatory);
		if (const TArray<TSharedPtr<FJsonValue>>* Names = RR.Array(TEXT("layers")))
		{
			for (int32 I = 0; I < Names->Num(); ++I)
			{
				const TSharedPtr<FJsonValue>& N = (*Names)[I];
				if (IsNullValue(N) || N->Type != EJson::String)
				{
					RR.Fail(FString::Printf(TEXT("'layers'[%d] must be a layer name"), I));
					break;
				}
				Route.Layers.Add(N->AsString());
			}
			if (RR.bOk && Route.Layers.Num() != Route.Points.Num())
			{
				RR.Fail(FString::Printf(TEXT("'layers' must have one entry per point (%d vs %d)"), Route.Layers.Num(), Route.Points.Num()));
			}
		}
		else if (!RR.Has(TEXT("layers")))
		{
			Route.Layers.Init(FString(GGround), Route.Points.Num()); // schema.ts: default all ground
		}
		if (RR.bOk && PolyTooLong(Route.Points, false))
		{
			RR.Fail(FString::Printf(TEXT("a leg longer than %d cm (exact geometry limit)"), MaxSegmentCm));
		}
		if (RR.bOk)
		{
			if (Route.bMandatory)
			{
				Out.MandatoryRouteIds.Add(Route.Id);
			}
			Out.Routes.Add(MoveTemp(Route));
		}
	});

	for (const FBuilding& B : Out.Buildings)
	{
		FPropsBuildingSummary& S = Out.BuildingSummaries.AddDefaulted_GetRef();
		S.Id = B.Id;
		S.Min = B.Min;
		S.Max = B.Max;
		S.Floors = B.Floors;
		S.bHollow = B.bHollow;
		S.bEnterable = B.bEnterable;
		S.bRoofWalkable = B.bRoofWalkable;
	}
	Out.BuildLayers();
	Out.FindGaps();
	Out.BuildDoorways();
	return Finish();
}

FString FPropsLayoutView::LayerNameFor(const FString& Building, int32 Floor)
{
	return Floor <= 0 ? FString(PropsLayoutViewLocal::GGround) : FString::Printf(TEXT("floor:%s:%d"), *Building, Floor);
}

void FPropsLayoutView::BuildLayers()
{
	Layers.Reset();
	LayerWalls.Reset();
	const auto AddRect = [](TArray<FLayerWall>& Out, FIntPoint Min, FIntPoint Max, int32 Thickness)
	{
		const FIntPoint C[4] = { Min, FIntPoint(Max.X, Min.Y), Max, FIntPoint(Min.X, Max.Y) };
		for (int32 I = 0; I < 4; ++I)
		{
			Out.Add({ C[I], C[(I + 1) % 4], Thickness });
		}
	};

	// ground: every wall standing on floor 0 (schema.ts layerWalls 'ground')
	FPropsWalkLayer& Ground = Layers.AddDefaulted_GetRef();
	Ground.Name = PropsLayoutViewLocal::GGround;
	Ground.ZCm = GroundZ;
	TArray<FLayerWall>& GroundWalls = LayerWalls.AddDefaulted_GetRef();
	for (const FWall& W : Walls)
	{
		if (W.Floor == 0)
		{
			GroundWalls.Add({ W.A, W.B, W.Thickness });
		}
	}

	// cut: listed whenever the layout has a cut block (schema.ts layersOf); its walls are the trench polygon's edges
	if (bHasCut)
	{
		FPropsWalkLayer& Cut = Layers.AddDefaulted_GetRef();
		Cut.Name = TEXT("cut");
		Cut.Kind = EPropsWalkLayerKind::Cut;
		Cut.Level = CutLevel;
		Cut.ZCm = GroundZ + (bHasCutFloorZ ? CutFloorZ : CutLevel * Config.LevelHeightCm);
		TArray<FLayerWall>& CutWalls = LayerWalls.AddDefaulted_GetRef();
		if (CutPoly.Num() >= 3)
		{
			for (int32 I = 0; I < CutPoly.Num(); ++I)
			{
				CutWalls.Add({ CutPoly[I], CutPoly[(I + 1) % CutPoly.Num()], Config.CutEdgeThicknessCm });
			}
		}
	}

	for (int32 Index = 0; Index < Buildings.Num(); ++Index)
	{
		const FBuilding& B = Buildings[Index];
		if (!B.bHollow)
		{
			for (int32 N = 1; N < B.Floors; ++N)
			{
				FPropsWalkLayer& L = Layers.AddDefaulted_GetRef();
				L.Name = LayerNameFor(B.Id, N);
				L.Kind = EPropsWalkLayerKind::Floor;
				L.Building = B.Id;
				L.Level = N;
				L.BuildingIndex = Index;
				L.ZCm = GroundZ + N * Config.LevelHeightCm;
				TArray<FLayerWall>& FloorWalls = LayerWalls.AddDefaulted_GetRef();
				AddRect(FloorWalls, B.Min, B.Max, Config.FloorPerimeterThicknessCm);
				for (const FWall& W : Walls)
				{
					if (W.Building == B.Id && W.Floor == N)
					{
						FloorWalls.Add({ W.A, W.B, W.Thickness });
					}
				}
			}
		}
		if (B.bRoofWalkable)
		{
			FPropsWalkLayer& L = Layers.AddDefaulted_GetRef();
			L.Name = TEXT("roof:") + B.Id;
			L.Kind = EPropsWalkLayerKind::Roof;
			L.Building = B.Id;
			L.Level = B.Floors;
			L.BuildingIndex = Index;
			L.ZCm = GroundZ + B.Floors * Config.LevelHeightCm;
			TArray<FLayerWall>& RoofWalls = LayerWalls.AddDefaulted_GetRef();
			if (B.Parapets.Num() > 0)
			{
				for (const FBuildingParapet& P : B.Parapets)
				{
					RoofWalls.Add({ P.Start, P.End, P.ThicknessCm });
				}
			}
			else
			{
				AddRect(RoofWalls, B.Min, B.Max, Config.ParapetThicknessCm);
			}
		}
	}
}

void FPropsLayoutView::FindGaps()
{
	using namespace PropsLayoutViewLocal;
	struct FInterval
	{
		int32 Lo = 0;
		int32 Hi = 0;
		int32 Fixed = 0;
		bool bAlongY = false;
		int32 WallIndex = INDEX_NONE;
	};
	TMap<FString, TArray<FInterval>> Groups;
	for (int32 Index = 0; Index < Walls.Num(); ++Index)
	{
		const FWall& W = Walls[Index];
		const bool bAlongY = W.A.X == W.B.X;
		if (bAlongY == (W.A.Y == W.B.Y))
		{
			continue; // diagonal or zero length: no collinear gaps
		}
		FInterval I;
		I.Fixed = bAlongY ? W.A.X : W.A.Y;
		I.Lo = bAlongY ? FMath::Min(W.A.Y, W.B.Y) : FMath::Min(W.A.X, W.B.X);
		I.Hi = bAlongY ? FMath::Max(W.A.Y, W.B.Y) : FMath::Max(W.A.X, W.B.X);
		I.bAlongY = bAlongY;
		I.WallIndex = Index;
		Groups.FindOrAdd(FString::Printf(TEXT("%s|%c|%d"), *StripIndex(W.Id), bAlongY ? TEXT('Y') : TEXT('X'), I.Fixed)).Add(I);
	}
	TArray<FString> Keys;
	Groups.GetKeys(Keys);
	SortKeysOrdinal(Keys);

	WallGaps.Reset();
	for (const FString& Key : Keys)
	{
		TArray<FInterval>& Items = Groups[Key];
		Items.Sort([](const FInterval& A, const FInterval& B) { return A.Lo != B.Lo ? A.Lo < B.Lo : A.Hi < B.Hi; });
		int32 MaxHi = Items[0].Hi;
		for (int32 I = 1; I < Items.Num(); ++I)
		{
			const FInterval& Item = Items[I];
			if (Item.Lo > MaxHi)
			{
				const FWall& W = Walls[Item.WallIndex];
				const int32 Mid = FloorHalf(static_cast<int64>(MaxHi) + Item.Lo);
				FPropsWallGap& Gap = WallGaps.AddDefaulted_GetRef();
				Gap.Group = StripIndex(W.Id);
				Gap.WallKind = W.Kind;
				Gap.Building = W.Building;
				Gap.WidthCm = Item.Lo - MaxHi;
				Gap.bAlongY = Item.bAlongY;
				Gap.Center = Item.bAlongY ? FIntPoint(Item.Fixed, Mid) : FIntPoint(Mid, Item.Fixed);
			}
			MaxHi = FMath::Max(MaxHi, Item.Hi);
		}
	}
}

void FPropsLayoutView::BuildDoorways()
{
	using namespace PropsLayoutViewLocal;
	Doorways.Reset();

	for (const FBuilding& B : Buildings)
	{
		for (int32 Index = 0; Index < B.Doors.Num(); ++Index)
		{
			const FBuildingDoor& D = B.Doors[Index];
			FPropsDoorway& W = Doorways.AddDefaulted_GetRef();
			W.Id = FString::Printf(TEXT("%s#%d"), *B.Id, Index);
			W.Kind = EPropsDoorwayKind::BuildingDoor;
			W.Building = B.Id;
			W.Pos = D.Pos;
			W.Floor = D.Floor;
			W.WalkLayer = LayerNameFor(B.Id, D.Floor);

			// side read from the footprint: the nearest edge (ties: -x, +x, -y, +y)
			const int64 Dist[4] = { FMath::Abs(static_cast<int64>(D.Pos.X) - B.Min.X), FMath::Abs(static_cast<int64>(D.Pos.X) - B.Max.X),
				FMath::Abs(static_cast<int64>(D.Pos.Y) - B.Min.Y), FMath::Abs(static_cast<int64>(D.Pos.Y) - B.Max.Y) };
			const FIntPoint Dir[4] = { FIntPoint(-1, 0), FIntPoint(1, 0), FIntPoint(0, -1), FIntPoint(0, 1) };
			int32 Best = 0;
			for (int32 I = 1; I < 4; ++I)
			{
				Best = Dist[I] < Dist[Best] ? I : Best;
			}
			W.Outward = Dir[Best];
			W.Along = W.Outward.X != 0 ? FIntPoint(0, 1) : FIntPoint(1, 0);

			for (const FPropsWallGap& Gap : WallGaps)
			{
				if (Gap.WallKind == TEXT("building") && Gap.Building == B.Id
					&& FMath::Abs(Gap.Center.X - D.Pos.X) <= Config.DoorGapMatchCm && FMath::Abs(Gap.Center.Y - D.Pos.Y) <= Config.DoorGapMatchCm)
				{
					W.WidthCm = Gap.WidthCm;
					W.WallGroup = Gap.Group;
					break;
				}
			}
		}
	}

	TMap<FString, int32> GateCounter;
	for (const FPropsWallGap& Gap : WallGaps)
	{
		if (!IsGateKind(Gap.WallKind) || Gap.WidthCm < Config.GateMinGapCm || Gap.WidthCm > Config.GateMaxGapCm)
		{
			continue; // building gaps are their buildings' doors; interior gaps are not doorways; cut_wall gaps are crossings
		}
		int32& Counter = GateCounter.FindOrAdd(Gap.Group);
		FPropsDoorway& W = Doorways.AddDefaulted_GetRef();
		W.Id = FString::Printf(TEXT("%s#g%d"), *Gap.Group, Counter++);
		W.Kind = EPropsDoorwayKind::Gate;
		W.WallGroup = Gap.Group;
		W.Pos = Gap.Center;
		W.Along = Gap.bAlongY ? FIntPoint(0, 1) : FIntPoint(1, 0);
		W.WidthCm = Gap.WidthCm;
	}

	for (FPropsDoorway& W : Doorways)
	{
		W.MandatoryRouteDistCm = DistanceToMandatoryRouteCm(W.Pos, W.WalkLayer);
		const int64 Reach = static_cast<int64>((W.WidthCm > 0 ? W.WidthCm : Config.DoorFallbackWidthCm) / 2) + Config.DoorThroughSlackCm;
		// the wall line through the opening is axis aligned: Across = the axis perpendicular to Along
		const bool bLineAlongY = W.Along.Y != 0;
		for (const FPropsRoute& Route : Routes)
		{
			if (!Route.bMandatory)
			{
				continue;
			}
			const int32 N = Route.Points.Num();
			for (int32 I = 0; I < N; ++I)
			{
				const FIntPoint V = Route.Points[I];
				// THROUGH: leg I -> I + 1 crosses / touches the wall line within Reach of the opening's centre
				if (I + 1 < N && Route.Layers[I] == W.WalkLayer && Route.Layers[I + 1] == W.WalkLayer)
				{
					const FIntPoint B = Route.Points[I + 1];
					const int64 S0 = bLineAlongY ? static_cast<int64>(V.X) - W.Pos.X : static_cast<int64>(V.Y) - W.Pos.Y;
					const int64 S1 = bLineAlongY ? static_cast<int64>(B.X) - W.Pos.X : static_cast<int64>(B.Y) - W.Pos.Y;
					const bool bCrosses = !((S0 > 0 && S1 > 0) || (S0 < 0 && S1 < 0) || (S0 == 0 && S1 == 0));
					if (bCrosses)
					{
						// lateral offset of the crossing point x |S1 - S0|, exact
						const int64 Den = S1 - S0;
						const int64 L0 = bLineAlongY ? static_cast<int64>(V.Y) - W.Pos.Y : static_cast<int64>(V.X) - W.Pos.X;
						const int64 L1 = bLineAlongY ? static_cast<int64>(B.Y) - W.Pos.Y : static_cast<int64>(B.X) - W.Pos.X;
						const int64 Num = L0 * Den - S0 * (L1 - L0);
						if (FMath::Abs(Num) <= Reach * FMath::Abs(Den))
						{
							W.bRouteThrough = true;
						}
					}
				}
				// TURNS AT: a reversal / end vertex in front of the doorway
				if (Route.Layers[I] != W.WalkLayer)
				{
					continue;
				}
				const int64 Rx = static_cast<int64>(V.X) - W.Pos.X;
				const int64 Ry = static_cast<int64>(V.Y) - W.Pos.Y;
				const int64 Lateral = FMath::Abs(Rx * W.Along.X + Ry * W.Along.Y);
				bool bInFront = false;
				if (W.Outward == FIntPoint::ZeroValue)
				{
					bInFront = FMath::Abs(bLineAlongY ? Rx : Ry) <= Config.DoorTurnReachCm;
				}
				else
				{
					const int64 Depth = Rx * W.Outward.X + Ry * W.Outward.Y;
					bInFront = Depth >= 0 && Depth <= Config.DoorTurnReachCm;
				}
				if (!bInFront || Lateral > Reach)
				{
					continue;
				}
				bool bTurn = I == 0 || I == N - 1;
				if (!bTurn)
				{
					const FIntPoint P = Route.Points[I - 1];
					const FIntPoint Q = Route.Points[I + 1];
					const int64 Dot = (static_cast<int64>(V.X) - P.X) * (static_cast<int64>(Q.X) - V.X)
						+ (static_cast<int64>(V.Y) - P.Y) * (static_cast<int64>(Q.Y) - V.Y);
					bTurn = Dot < 0;
				}
				W.bRouteTurnsAt |= bTurn;
			}
		}
		W.bOnMandatoryRoute = W.bRouteThrough || W.bRouteTurnsAt;
	}
	Doorways.Sort([](const FPropsDoorway& A, const FPropsDoorway& B) { return A.Id.Compare(B.Id, ESearchCase::CaseSensitive) < 0; });
}

// ---- queries -----------------------------------------------------------------------------------------------------------------

const FPropsWalkLayer* FPropsLayoutView::FindLayer(const FString& LayerName) const
{
	return Layers.FindByPredicate([&LayerName](const FPropsWalkLayer& L) { return L.Name.Equals(LayerName, ESearchCase::CaseSensitive); });
}

const FPropsDoorway* FPropsLayoutView::FindDoorway(const FString& Id) const
{
	return Doorways.FindByPredicate([&Id](const FPropsDoorway& D) { return D.Id.Equals(Id, ESearchCase::CaseSensitive); });
}

const FPropsZone* FPropsLayoutView::FindZone(const FString& Id) const
{
	return Zones.FindByPredicate([&Id](const FPropsZone& Z) { return Z.Id.Equals(Id, ESearchCase::CaseSensitive); });
}

bool FPropsLayoutView::IsInBounds(FIntPoint P) const
{
	return PropsLayoutViewLocal::InRect(P, BoundsMin, BoundsMax);
}

bool FPropsLayoutView::IsInZone(FIntPoint P, const FString& ZoneId) const
{
	const FPropsZone* Zone = FindZone(ZoneId);
	return Zone && PropsLayoutViewLocal::PointInPolygon(P, Zone->Poly);
}

bool FPropsLayoutView::IsWalkable(FIntPoint P, const FString& WalkLayer) const
{
	using namespace PropsLayoutViewLocal;
	const FPropsWalkLayer* L = FindLayer(WalkLayer);
	if (!L)
	{
		return false;
	}
	switch (L->Kind)
	{
	case EPropsWalkLayerKind::Ground:
	{
		if (!IsInBounds(P))
		{
			return false;
		}
		if (CutPoly.Num() >= 3 && PointInPolygon(P, CutPoly))
		{
			for (const FCrossing& X : Crossings)
			{
				if (PointInPolygon(P, X.Poly))
				{
					return true;
				}
			}
			return false;
		}
		return true;
	}
	case EPropsWalkLayerKind::Cut:
		return CutPoly.Num() >= 3 && PointInPolygon(P, CutPoly);
	case EPropsWalkLayerKind::Roof:
	{
		const FBuilding& B = Buildings[L->BuildingIndex];
		const FIntPoint Inset(Config.RoofInsetCm, Config.RoofInsetCm);
		if (!InRect(P, B.Min + Inset, B.Max - Inset))
		{
			return false;
		}
		for (const FIntPoint& H : B.RoofHoles)
		{
			if (FMath::Abs(P.X - H.X) <= Config.HoleHalfSideCm && FMath::Abs(P.Y - H.Y) <= Config.HoleHalfSideCm)
			{
				return false;
			}
		}
		return true;
	}
	case EPropsWalkLayerKind::Floor:
	{
		const FBuilding& B = Buildings[L->BuildingIndex];
		if (!InRect(P, B.Min, B.Max))
		{
			return false;
		}
		for (const FFloorHole& H : B.FloorHoles)
		{
			if (H.Floor == L->Level && FMath::Abs(P.X - H.Pos.X) <= Config.HoleHalfSideCm && FMath::Abs(P.Y - H.Pos.Y) <= Config.HoleHalfSideCm)
			{
				return false;
			}
		}
		return true;
	}
	}
	return false;
}

bool FPropsLayoutView::IsBlocked(FIntPoint P, const FString& WalkLayer, int32 ClearanceCm) const
{
	using namespace PropsLayoutViewLocal;
	const int32 LayerIndex = Layers.IndexOfByPredicate([&WalkLayer](const FPropsWalkLayer& L) { return L.Name.Equals(WalkLayer, ESearchCase::CaseSensitive); });
	if (LayerIndex == INDEX_NONE)
	{
		return false;
	}
	const int64 Clearance = ClampRadius(ClearanceCm);
	for (const FLayerWall& W : LayerWalls[LayerIndex])
	{
		// 2 x dist <= thickness + 2 x clearance  <=>  dist <= thickness / 2 + clearance, exact for odd thicknesses
		if (SegmentWithin(P, W.A, W.B, static_cast<int64>(W.Thickness) + 2 * Clearance, 2))
		{
			return true;
		}
	}
	if (Layers[LayerIndex].Kind != EPropsWalkLayerKind::Ground)
	{
		return false;
	}
	for (const FBuilding& B : Buildings)
	{
		if (!B.bEnterable && RectWithin(P, B.Min, B.Max, Clearance))
		{
			return true;
		}
	}
	for (const FPropsCover& C : Covers)
	{
		if (CoverWithin(P, C.Pos, C.Yaw, C.SizeX, C.SizeY, Clearance))
		{
			return true;
		}
	}
	return false;
}

bool FPropsLayoutView::IsFree(FIntPoint P, const FString& WalkLayer, int32 ClearanceCm) const
{
	return IsWalkable(P, WalkLayer) && !IsBlocked(P, WalkLayer, ClearanceCm);
}

int32 FPropsLayoutView::DistanceToMandatoryRouteCm(FIntPoint P, const FString& WalkLayer) const
{
	int32 Best = MAX_int32;
	for (const FPropsRoute& Route : Routes)
	{
		if (!Route.bMandatory)
		{
			continue;
		}
		for (int32 I = 0; I + 1 < Route.Points.Num(); ++I)
		{
			if (Route.Layers[I] == WalkLayer && Route.Layers[I + 1] == WalkLayer)
			{
				Best = FMath::Min(Best, PropsLayoutViewLocal::CeilDistance(P, Route.Points[I], Route.Points[I + 1]));
			}
		}
	}
	return Best;
}

bool FPropsLayoutView::IsNearMandatoryRoute(FIntPoint P, const FString& WalkLayer, int32 RadiusCm) const
{
	const int32 Radius = PropsLayoutViewLocal::ClampRadius(RadiusCm);
	for (const FPropsRoute& Route : Routes)
	{
		if (!Route.bMandatory)
		{
			continue;
		}
		for (int32 I = 0; I + 1 < Route.Points.Num(); ++I)
		{
			if (Route.Layers[I] == WalkLayer && Route.Layers[I + 1] == WalkLayer
				&& PropsLayoutViewLocal::SegmentWithin(P, Route.Points[I], Route.Points[I + 1], Radius, 1))
			{
				return true;
			}
		}
	}
	return false;
}

TArray<FPropsExclusion> FPropsLayoutView::BuildExclusions(const FPropsExclusionParams& Params) const
{
	using namespace PropsLayoutViewLocal;
	TArray<FPropsExclusion> Out;
	const auto Shape = [&Out](EPropsExclusionKind Kind, const FString& Id, const FString& Layer, TArray<FIntPoint> Points, int32 Radius, bool bArea)
	{
		FPropsExclusion& S = Out.AddDefaulted_GetRef();
		S.Kind = Kind;
		S.SourceId = Id;
		S.WalkLayer = Layer;
		S.Points = MoveTemp(Points);
		S.RadiusCm = ClampRadius(Radius);
		S.bArea = bArea;
	};
	const auto Square = [](FIntPoint C, int32 Half)
	{
		return TArray<FIntPoint>{ FIntPoint(C.X - Half, C.Y - Half), FIntPoint(C.X + Half, C.Y - Half), FIntPoint(C.X + Half, C.Y + Half),
			FIntPoint(C.X - Half, C.Y + Half) };
	};

	// spawn / end: every layer (a roof prop 10 m above the spawn is as close to the squad as a ground one)
	if (Params.SpawnCm > 0)
	{
		Shape(EPropsExclusionKind::Spawn, TEXT("spawn"), FString(), { SpawnPos }, Params.SpawnCm, false);
	}
	if (Params.EndCm > 0)
	{
		Shape(EPropsExclusionKind::End, TEXT("end"), FString(), { EndPos }, Params.EndCm, false);
	}
	if (Params.LadderCm > 0)
	{
		for (const FLadder& L : Ladders)
		{
			// the foot: a capsule from the rungs (pos) to the arrival point (foot) on the lower layer; the top: a disk on the upper one
			Shape(EPropsExclusionKind::LadderFoot, L.Id, L.From, { L.Pos, L.Foot }, Params.LadderCm, false);
			Shape(EPropsExclusionKind::LadderTop, L.Id, L.To, { L.Top }, Params.LadderCm, false);
		}
	}
	if (Params.StairCm > 0)
	{
		for (const FBuilding& B : Buildings)
		{
			for (const FStair& St : B.Stairs)
			{
				TArray<FIntPoint> Points = { St.Pos };
				if (St.bHasMid)
				{
					Points.Add(St.Mid);
				}
				Points.Add(St.Top);
				// every layer: a stair joins several (and its slab opening sits on the upper one)
				Shape(EPropsExclusionKind::Stair, St.Id, FString(), MoveTemp(Points), Config.StairHalfWidthCm + Params.StairCm, false);
			}
		}
	}
	if (Params.DoorCm > 0)
	{
		for (const FPropsDoorway& D : Doorways)
		{
			Shape(EPropsExclusionKind::Door, D.Id, D.WalkLayer, { D.Pos }, Params.DoorCm, false);
		}
	}
	for (const FPropsRoute& Route : Routes)
	{
		const int32 Radius = Route.bMandatory ? Params.RouteMandatoryCm : Params.RouteVariantsCm;
		if (Radius <= 0)
		{
			continue;
		}
		// one corridor per run of points on one layer; the layer-changing legs (ladders, stairs, holes) have their own shapes
		int32 Start = 0;
		for (int32 I = 1; I <= Route.Points.Num(); ++I)
		{
			if (I == Route.Points.Num() || Route.Layers[I] != Route.Layers[Start])
			{
				Shape(EPropsExclusionKind::Route, Route.Id, Route.Layers[Start], TArray<FIntPoint>(&Route.Points[Start], I - Start), Radius, false);
				Start = I;
			}
		}
	}
	if (Params.bCrossing)
	{
		for (const FCrossing& X : Crossings)
		{
			Shape(EPropsExclusionKind::Crossing, X.Id, FString(), X.Poly, 0, true);
		}
	}
	if (Params.HoleRimCm > 0)
	{
		const int32 Half = Config.HoleHalfSideCm + Params.HoleRimCm;
		const auto RectPoly = [](FIntPoint MinPt, FIntPoint MaxPt, int32 Pad)
		{
			return TArray<FIntPoint>{
				FIntPoint(MinPt.X - Pad, MinPt.Y - Pad),
				FIntPoint(MaxPt.X + Pad, MinPt.Y - Pad),
				FIntPoint(MaxPt.X + Pad, MaxPt.Y + Pad),
				FIntPoint(MinPt.X - Pad, MaxPt.Y + Pad)
			};
		};
		for (const FBuilding& B : Buildings)
		{
			for (int32 I = 0; I < B.RoofHoles.Num(); ++I)
			{
				Shape(EPropsExclusionKind::HoleRim, FString::Printf(TEXT("%s:roof_hole:%d"), *B.Id, I), TEXT("roof:") + B.Id, Square(B.RoofHoles[I], Half), 0, true);
			}
			for (int32 I = 0; I < B.FloorHoles.Num(); ++I)
			{
				Shape(EPropsExclusionKind::HoleRim, FString::Printf(TEXT("%s:floor_hole:%d"), *B.Id, I), LayerNameFor(B.Id, B.FloorHoles[I].Floor),
					Square(B.FloorHoles[I].Pos, Half), 0, true);
			}
			for (int32 I = 0; I < B.FloorGaps.Num(); ++I)
			{
				const FFloorGapRect& Gap = B.FloorGaps[I];
				Shape(EPropsExclusionKind::HoleRim, Gap.Id.IsEmpty() ? FString::Printf(TEXT("%s:gap:%d"), *B.Id, I) : Gap.Id,
					LayerNameFor(B.Id, Gap.Floor), RectPoly(Gap.Min, Gap.Max, Params.HoleRimCm), 0, true);
			}
			for (int32 I = 0; I < B.Hatches.Num(); ++I)
			{
				const FBuildingHatch& Hatch = B.Hatches[I];
				const FIntPoint HMin(Hatch.Pos.X - Hatch.SizeX / 2, Hatch.Pos.Y - Hatch.SizeY / 2);
				const FIntPoint HMax(Hatch.Pos.X + Hatch.SizeX / 2, Hatch.Pos.Y + Hatch.SizeY / 2);
				const TArray<FIntPoint> Poly = RectPoly(HMin, HMax, Params.HoleRimCm);
				const FString HatchLabel = Hatch.Id.IsEmpty() ? FString::Printf(TEXT("%s:hatch:%d"), *B.Id, I) : Hatch.Id;
				for (int32 Fl = Hatch.Floor + 1; Fl <= B.Floors; ++Fl)
				{
					if (Fl < B.Floors && !B.bHollow)
					{
						Shape(EPropsExclusionKind::HoleRim, HatchLabel, LayerNameFor(B.Id, Fl), Poly, 0, true);
					}
					else if (Fl == B.Floors && B.bRoofWalkable)
					{
						Shape(EPropsExclusionKind::HoleRim, HatchLabel, TEXT("roof:") + B.Id, Poly, 0, true);
					}
				}
			}
		}
	}
	return Out;
}

bool FPropsLayoutView::IsExcluded(const TArray<FPropsExclusion>& Shapes, FIntPoint P, const FString& WalkLayer)
{
	for (const FPropsExclusion& S : Shapes)
	{
		if (S.Contains(P, WalkLayer))
		{
			return true;
		}
	}
	return false;
}

bool FPropsLayoutView::GetLayerExtent(const FString& WalkLayer, FIntPoint& OutMin, FIntPoint& OutMax) const
{
	const FPropsWalkLayer* L = FindLayer(WalkLayer);
	if (!L)
	{
		return false;
	}
	switch (L->Kind)
	{
	case EPropsWalkLayerKind::Ground:
		OutMin = BoundsMin;
		OutMax = BoundsMax;
		return true;
	case EPropsWalkLayerKind::Cut:
	{
		if (CutPoly.Num() < 3)
		{
			return false;
		}
		OutMin = CutPoly[0];
		OutMax = CutPoly[0];
		for (const FIntPoint& P : CutPoly)
		{
			OutMin = FIntPoint(FMath::Min(OutMin.X, P.X), FMath::Min(OutMin.Y, P.Y));
			OutMax = FIntPoint(FMath::Max(OutMax.X, P.X), FMath::Max(OutMax.Y, P.Y));
		}
		return true;
	}
	default:
		OutMin = Buildings[L->BuildingIndex].Min;
		OutMax = Buildings[L->BuildingIndex].Max;
		return true;
	}
}

bool FPropsLayoutView::IsNearWall(FIntPoint P, const FString& WalkLayer, int32 ClearanceCm) const
{
	bool bAlongX = false;
	return FindNearestWallAxis(P, WalkLayer, ClearanceCm, bAlongX);
}

bool FPropsLayoutView::IsNearCover(FIntPoint P, const FString& WalkLayer, int32 ClearanceCm) const
{
	using namespace PropsLayoutViewLocal;
	const FPropsWalkLayer* L = FindLayer(WalkLayer);
	if (!L || L->Kind != EPropsWalkLayerKind::Ground)
	{
		return false;
	}
	const int64 Clearance = ClampRadius(ClearanceCm);
	for (const FPropsCover& C : Covers)
	{
		if (CoverWithin(P, C.Pos, C.Yaw, C.SizeX, C.SizeY, Clearance))
		{
			return true;
		}
	}
	return false;
}

bool FPropsLayoutView::FindNearestWallAxis(FIntPoint P, const FString& WalkLayer, int32 ClearanceCm, bool& bOutAlongX) const
{
	using namespace PropsLayoutViewLocal;
	const int32 LayerIndex = Layers.IndexOfByPredicate([&WalkLayer](const FPropsWalkLayer& L) { return L.Name.Equals(WalkLayer, ESearchCase::CaseSensitive); });
	if (LayerIndex == INDEX_NONE)
	{
		return false;
	}
	const int64 Clearance = ClampRadius(ClearanceCm);
	int32 BestDist = MAX_int32;
	const FLayerWall* Best = nullptr;
	for (const FLayerWall& W : LayerWalls[LayerIndex])
	{
		if (!SegmentWithin(P, W.A, W.B, static_cast<int64>(W.Thickness) + 2 * Clearance, 2))
		{
			continue;
		}
		const int32 D = CeilDistance(P, W.A, W.B);
		if (D < BestDist)
		{
			BestDist = D;
			Best = &W;
		}
	}
	if (!Best)
	{
		return false;
	}
	bOutAlongX = FMath::Abs(static_cast<int64>(Best->B.X) - Best->A.X) >= FMath::Abs(static_cast<int64>(Best->B.Y) - Best->A.Y);
	return true;
}

bool FPropsLayoutView::ReadConfig(const FJsonObject& Balance, FPropsLayoutConfig& Out, TArray<FString>& Errors)
{
	using namespace LevelGenJson;
	const int32 FirstError = Errors.Num();
	FFieldReader R(Balance, TEXT("props_balance"), Errors);
	const TSharedPtr<FJsonObject> Block = R.Object(TEXT("layout_view"));
	if (Block.IsValid())
	{
		FFieldReader B(*Block, TEXT("props_balance.layout_view"), Errors);
		const auto Read = [&B](const TCHAR* Key, int32& Value)
		{
			int32 V = Value;
			if (B.Int(Key, V))
			{
				if (V < 0)
				{
					B.Fail(FString::Printf(TEXT("'%s' must be >= 0"), Key));
				}
				else
				{
					Value = V;
				}
			}
		};
		Read(TEXT("gate_min_gap_cm"), Out.GateMinGapCm);
		Read(TEXT("gate_max_gap_cm"), Out.GateMaxGapCm);
		Read(TEXT("door_through_slack_cm"), Out.DoorThroughSlackCm);
		Read(TEXT("door_turn_reach_cm"), Out.DoorTurnReachCm);
		Read(TEXT("door_fallback_width_cm"), Out.DoorFallbackWidthCm);
		Read(TEXT("tall_cover_min_cm"), Out.TallCoverMinCm);
		Read(TEXT("eye_height_cm"), Out.EyeHeightCm);
		Read(TEXT("roof_edge_inset_cm"), Out.RoofEdgeInsetCm);
	}
	for (int32 I = FirstError; I < Errors.Num(); ++I)
	{
		UE_LOG(LogCodexTactics, Warning, TEXT("Props layout config: %s"), *Errors[I]);
	}
	return Errors.Num() == FirstError;
}

// ---- C4: exact helpers, routes, sight ------------------------------------------------------------------------------------------

int64 PropsGeometry::SinQ(int32 Deg)
{
	return PropsLayoutViewLocal::SinQ(Deg);
}

int64 PropsGeometry::CosQ(int32 Deg)
{
	return PropsLayoutViewLocal::CosQ(Deg);
}

int64 PropsGeometry::ISqrt(int64 V)
{
	if (V <= 0)
	{
		return 0;
	}
	int64 R = static_cast<int64>(FMath::Sqrt(static_cast<double>(V))); // an estimate only; corrected exactly below
	while (R > 0 && R * R > V)
	{
		--R;
	}
	while ((R + 1) * (R + 1) <= V)
	{
		++R;
	}
	return R;
}

int64 PropsGeometry::FloorDiv(int64 A, int64 B)
{
	const int64 Q = A / B;
	return (A % B != 0 && ((A < 0) != (B < 0))) ? Q - 1 : Q;
}

const FPropsRoute* FPropsLayoutView::FindRoute(const FString& Id) const
{
	return Routes.FindByPredicate([&Id](const FPropsRoute& R) { return R.Id.Equals(Id, ESearchCase::CaseSensitive); });
}

int32 FPropsLayoutView::DistanceToRouteCm(FIntPoint P, const FPropsRoute& Route, const FString* Layer) const
{
	int32 Best = MAX_int32;
	for (int32 I = 0; I + 1 < Route.Points.Num(); ++I)
	{
		if (!Layer || (Route.Layers[I] == *Layer && Route.Layers[I + 1] == *Layer))
		{
			Best = FMath::Min(Best, PropsLayoutViewLocal::CeilDistance(P, Route.Points[I], Route.Points[I + 1]));
		}
	}
	return Best;
}

int32 FPropsLayoutView::DistanceToMandatoryRouteAnyLayerCm(FIntPoint P) const
{
	int32 Best = MAX_int32;
	for (const FPropsRoute& Route : Routes)
	{
		if (Route.bMandatory)
		{
			Best = FMath::Min(Best, DistanceToRouteCm(P, Route, nullptr));
		}
	}
	return Best;
}

void FPropsLayoutView::GetSightBlockers(const FString& EyeLayer, TArray<FPropsSightSegment>& Out) const
{
	using namespace PropsLayoutViewLocal;
	Out.Reset();
	const int32 LayerIndex = Layers.IndexOfByPredicate([&EyeLayer](const FPropsWalkLayer& L) { return L.Name.Equals(EyeLayer, ESearchCase::CaseSensitive); });
	if (LayerIndex == INDEX_NONE)
	{
		return;
	}
	const int64 K = PropsGeometry::SightScale;
	const auto AddWall = [&Out, K](FIntPoint A, FIntPoint B)
	{
		Out.Add({ A.X * K, A.Y * K, B.X * K, B.Y * K });
	};
	const auto AddCover = [&Out, K](const FPropsCover& C)
	{
		const int64 Cs = CosQ(C.Yaw);
		const int64 Sn = SinQ(C.Yaw);
		// corner(sx, sy) x SightScale = pos x K + (cos, sin) x SizeX x sx + (-sin, cos) x SizeY x sy  (K x half size = 16384 x size)
		const int32 Signs[4][2] = { { 1, 1 }, { -1, 1 }, { -1, -1 }, { 1, -1 } };
		int64 X[4];
		int64 Y[4];
		for (int32 I = 0; I < 4; ++I)
		{
			const int64 Lx = static_cast<int64>(C.SizeX) * Signs[I][0];
			const int64 Ly = static_cast<int64>(C.SizeY) * Signs[I][1];
			X[I] = C.Pos.X * K + Cs * Lx - Sn * Ly;
			Y[I] = C.Pos.Y * K + Sn * Lx + Cs * Ly;
		}
		for (int32 I = 0; I < 4; ++I)
		{
			Out.Add({ X[I], Y[I], X[(I + 1) % 4], Y[(I + 1) % 4] });
		}
	};
	const FPropsWalkLayer& L = Layers[LayerIndex];
	if (L.Kind == EPropsWalkLayerKind::Roof)
	{
		// the elevated rule: only what reaches the eye blocks (levels are relative to the ground, like schema.ts levelZ)
		const int64 Eye = static_cast<int64>(L.Level) * Config.LevelHeightCm + Config.EyeHeightCm;
		for (const FWall& W : Walls)
		{
			if (static_cast<int64>(W.Floor) * Config.LevelHeightCm + W.Height >= Eye)
			{
				AddWall(W.A, W.B);
			}
		}
		for (const FPropsCover& C : Covers)
		{
			if (C.HeightCm >= Eye)
			{
				AddCover(C);
			}
		}
		return;
	}
	TArray<const FBuildingOpening*> Windows;
	for (const FBuilding& B : Buildings)
	{
		if (!L.Building.IsEmpty() && B.Id != L.Building)
		{
			continue;
		}
		for (const FBuildingOpening& Op : B.Openings)
		{
			if (Op.Kind == TEXT("window") && Op.Floor == L.Level && Op.SillHeightCm < Config.EyeHeightCm && Op.TopHeightCm > Config.EyeHeightCm && Op.WidthCm > 0)
			{
				Windows.Add(&Op);
			}
		}
	}
	for (const FLayerWall& W : LayerWalls[LayerIndex])
	{
		const bool bAlongX = W.A.Y == W.B.Y && W.A.X != W.B.X;
		const bool bAlongY = W.A.X == W.B.X && W.A.Y != W.B.Y;
		if (Windows.IsEmpty() || (!bAlongX && !bAlongY))
		{
			AddWall(W.A, W.B);
			continue;
		}
		const int32 Fixed = bAlongX ? W.A.Y : W.A.X;
		const int32 Lo = bAlongX ? FMath::Min(W.A.X, W.B.X) : FMath::Min(W.A.Y, W.B.Y);
		const int32 Hi = bAlongX ? FMath::Max(W.A.X, W.B.X) : FMath::Max(W.A.Y, W.B.Y);
		struct FSpan { int32 A; int32 B; };
		TArray<FSpan> Cuts;
		for (const FBuildingOpening* Op : Windows)
		{
			const int32 OpFixed = bAlongX ? Op->Pos.Y : Op->Pos.X;
			if (FMath::Abs(OpFixed - Fixed) > 20)
			{
				continue;
			}
			const int32 Center = bAlongX ? Op->Pos.X : Op->Pos.Y;
			const int32 HalfW = Op->WidthCm / 2;
			const int32 C0 = FMath::Max(Lo, Center - HalfW);
			const int32 C1 = FMath::Min(Hi, Center + HalfW);
			if (C1 > C0)
			{
				Cuts.Add({ C0, C1 });
			}
		}
		if (Cuts.IsEmpty())
		{
			AddWall(W.A, W.B);
			continue;
		}
		Cuts.Sort([](const FSpan& Lhs, const FSpan& Rhs) { return Lhs.A < Rhs.A; });
		int32 Cur = Lo;
		for (const FSpan& Cut : Cuts)
		{
			if (Cut.A > Cur)
			{
				AddWall(bAlongX ? FIntPoint(Cur, Fixed) : FIntPoint(Fixed, Cur), bAlongX ? FIntPoint(Cut.A, Fixed) : FIntPoint(Fixed, Cut.A));
			}
			Cur = FMath::Max(Cur, Cut.B);
		}
		if (Hi > Cur)
		{
			AddWall(bAlongX ? FIntPoint(Cur, Fixed) : FIntPoint(Fixed, Cur), bAlongX ? FIntPoint(Hi, Fixed) : FIntPoint(Fixed, Hi));
		}
	}
	if (L.Kind == EPropsWalkLayerKind::Ground)
	{
		for (const FPropsCover& C : Covers)
		{
			if (C.HeightCm >= Config.TallCoverMinCm)
			{
				AddCover(C);
			}
		}
	}
}

bool FPropsLayoutView::IsSightBlocked(FIntPoint From, FIntPoint To, const TArray<FPropsSightSegment>& Blockers)
{
	const int64 K = PropsGeometry::SightScale;
	const int64 PX = From.X * K;
	const int64 PY = From.Y * K;
	const int64 DX = (static_cast<int64>(To.X) - From.X) * K;
	const int64 DY = (static_cast<int64>(To.Y) - From.Y) * K;
	const int64 MinX = FMath::Min<int64>(0, DX);
	const int64 MaxX = FMath::Max<int64>(0, DX);
	const int64 MinY = FMath::Min<int64>(0, DY);
	const int64 MaxY = FMath::Max<int64>(0, DY);
	// everything relative to From: after the bounding-box reject every cross product stays inside int64 for blockers up to
	// MaxSegmentCm long and sight lines up to MaxRadiusCm
	const auto Orient = [](int64 AX, int64 AY, int64 BX, int64 BY, int64 CX, int64 CY)
	{
		const int64 C = (BX - AX) * (CY - AY) - (BY - AY) * (CX - AX);
		return C > 0 ? 1 : (C < 0 ? -1 : 0);
	};
	const auto OnSeg = [](int64 AX, int64 AY, int64 BX, int64 BY, int64 CX, int64 CY)
	{
		return CX >= FMath::Min(AX, BX) && CX <= FMath::Max(AX, BX) && CY >= FMath::Min(AY, BY) && CY <= FMath::Max(AY, BY);
	};
	for (const FPropsSightSegment& S : Blockers)
	{
		const int64 AX = S.AX - PX;
		const int64 AY = S.AY - PY;
		const int64 BX = S.BX - PX;
		const int64 BY = S.BY - PY;
		if (FMath::Max(AX, BX) < MinX || FMath::Min(AX, BX) > MaxX || FMath::Max(AY, BY) < MinY || FMath::Min(AY, BY) > MaxY)
		{
			continue;
		}
		const int32 O1 = Orient(0, 0, DX, DY, AX, AY);
		const int32 O2 = Orient(0, 0, DX, DY, BX, BY);
		const int32 O3 = Orient(AX, AY, BX, BY, 0, 0);
		const int32 O4 = Orient(AX, AY, BX, BY, DX, DY);
		if (O1 * O2 < 0 && O3 * O4 < 0)
		{
			return true;
		}
		if ((O1 == 0 && OnSeg(0, 0, DX, DY, AX, AY)) || (O2 == 0 && OnSeg(0, 0, DX, DY, BX, BY))
			|| (O3 == 0 && OnSeg(AX, AY, BX, BY, 0, 0)) || (O4 == 0 && OnSeg(AX, AY, BX, BY, DX, DY)))
		{
			return true;
		}
	}
	return false;
}
