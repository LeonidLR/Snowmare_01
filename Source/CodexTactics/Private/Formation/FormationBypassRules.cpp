#include "Formation/FormationBypassRules.h"

namespace FormationBypassRules
{
	/** Winding number of the closed polygon Poly round P (rules.ts winding). */
	static int32 BypassWinding(const TArray<FVector2D>& Poly, const FVector2D& P)
	{
		int32 Winding = 0;
		for (int32 Index = 0; Index < Poly.Num(); ++Index)
		{
			const FVector2D& A = Poly[Index];
			const FVector2D& B = Poly[(Index + 1) % Poly.Num()];
			const double Side = (B.X - A.X) * (P.Y - A.Y) - (P.X - A.X) * (B.Y - A.Y);
			if (A.Y <= P.Y)
			{
				if (B.Y > P.Y && Side > 0.0)
				{
					++Winding;
				}
			}
			else if (B.Y <= P.Y && Side < 0.0)
			{
				--Winding;
			}
		}
		return Winding;
	}

	static double BypassLength(const TArray<FVector2D>& Path)
	{
		double Length = 0.0;
		for (int32 Index = 1; Index < Path.Num(); ++Index)
		{
			Length += FVector2D::Distance(Path[Index - 1], Path[Index]);
		}
		return Length;
	}

	static TArray<FVector2D> BypassSamples(const TArray<FVector2D>& Path, double Step)
	{
		TArray<FVector2D> Out;
		Out.Add(Path[0]);
		for (int32 Index = 1; Index < Path.Num(); ++Index)
		{
			const double Length = FVector2D::Distance(Path[Index - 1], Path[Index]);
			for (double S = Step; S < Length; S += Step)
			{
				Out.Add(Path[Index - 1] + (Path[Index] - Path[Index - 1]) * (S / Length));
			}
			Out.Add(Path[Index]);
		}
		return Out;
	}

	EFormationWallSide WallSideOf(const TArray<FVector2D>& Path, const FFormationWall& Wall)
	{
		const FVector2D P = Path[0];
		const FVector2D D = Path.Last();
		const FVector2D Line = D - P;
		const auto Off = [&Line, &P](const FVector2D& Q) { return Line.X * (Q.Y - P.Y) - Line.Y * (Q.X - P.X); };
		const FVector2D R = FMath::Abs(Off(Wall.A)) >= FMath::Abs(Off(Wall.B)) ? Wall.A : Wall.B;
		// UE frame (x forward, y right): a positive cross (Line x (R - P)) puts R on the right of the line.
		const EFormationWallSide Straight = Off(R) > 0.0 ? EFormationWallSide::Right : EFormationWallSide::Left;
		if (BypassWinding(Path, R) == 0)
		{
			return Straight;
		}
		return Straight == EFormationWallSide::Right ? EFormationWallSide::Left : EFormationWallSide::Right;
	}

	FFormationBypassChoice ChooseSide(const FFormationWall& Wall, const FVector2D& LeaderPos, const FVector2D& Destination,
		const TArray<FFormationThreat>& Threats, const FFormationConfig& Config)
	{
		const double WallLength = FVector2D::Distance(Wall.A, Wall.B);
		const FVector2D U = WallLength > 1e-6 ? (Wall.B - Wall.A) * (1.0 / WallLength) : FVector2D(1.0, 0.0);
		const FVector2D N(-U.Y, U.X);
		const double M = 0.5 * Wall.Thickness + BypassRadius;
		const FVector2D Corners[4] = { Wall.A - U * M + N * M, Wall.A - U * M - N * M, Wall.B + U * M + N * M, Wall.B + U * M - N * M };
		const TArray<FFormationWall> Only = { Wall };
		// A leg keeps BypassRadius off the body (less only as close as its own end - the leader / the destination - already is).
		const auto Body = [&Wall](const FVector2D& P) { return FMath::Max(0.0, FormationMath::DistToWall(P, Wall) - 0.5 * Wall.Thickness); };
		const auto Clear = [&Only, &Body](const FVector2D& P, const FVector2D& Q)
		{
			return !FormationMath::SegmentBlocked(P, Q, Only, FMath::Max(0.0, FMath::Min3(BypassRadius, Body(P), Body(Q)) - 1.0));
		};
		TArray<TArray<FVector2D>> Candidates;
		Candidates.Add({ LeaderPos, Destination });
		for (int32 First = 0; First < 4; ++First)
		{
			Candidates.Add({ LeaderPos, Corners[First], Destination });
			for (int32 Second = 0; Second < 4; ++Second)
			{
				if (Second != First)
				{
					Candidates.Add({ LeaderPos, Corners[First], Corners[Second], Destination });
				}
			}
		}
		// Best path per side: [0] = left, [1] = right.
		TOptional<TArray<FVector2D>> BestPath[2];
		double BestLength[2] = { 0.0, 0.0 };
		for (const TArray<FVector2D>& Path : Candidates)
		{
			bool bClear = true;
			for (int32 Index = 1; Index < Path.Num() && bClear; ++Index)
			{
				bClear = Clear(Path[Index - 1], Path[Index]);
			}
			if (!bClear)
			{
				continue;
			}
			const int32 Side = WallSideOf(Path, Wall) == EFormationWallSide::Right ? 1 : 0;
			const double Length = BypassLength(Path);
			if (!BestPath[Side].IsSet() || Length < BestLength[Side] - 1e-6)
			{
				BestPath[Side] = Path;
				BestLength[Side] = Length;
			}
		}
		const auto SideOf = [](int32 Index) { return Index == 1 ? EFormationWallSide::Right : EFormationWallSide::Left; };
		if (!BestPath[0].IsSet() && !BestPath[1].IsSet())
		{
			FFormationBypassChoice None;
			None.Path = { LeaderPos, Destination };
			return None;
		}
		int32 Shortest = BestPath[0].IsSet() ? 0 : 1;
		if (BestPath[0].IsSet() && BestPath[1].IsSet() && BestLength[1] < BestLength[0] - 1e-6)
		{
			Shortest = 1;
		}
		const double MinLength = BestLength[Shortest];
		const auto Pick = [&](int32 Side, EFormationBypassReason Reason)
		{
			FFormationBypassChoice Choice;
			Choice.WallOn = SideOf(Side);
			Choice.Detour = static_cast<float>(MinLength > 0.0 ? BestLength[Side] / MinLength - 1.0 : 0.0);
			Choice.Shortest = SideOf(Shortest);
			Choice.Reason = Reason;
			Choice.Path = BestPath[Side].GetValue();
			return Choice;
		};
		TArray<FFormationThreat> Known;
		for (const FFormationThreat& Threat : Threats)
		{
			if (Threat.AgeS >= 0.f && Threat.AgeS <= Config.BypassThreatMemoryS)
			{
				Known.Add(Threat);
			}
		}
		if (Known.Num() == 0 || !BestPath[0].IsSet() || !BestPath[1].IsSet())
		{
			return Pick(Shortest, EFormationBypassReason::Shortest);
		}
		const auto Hidden = [&](int32 Side)
		{
			const TArray<FVector2D> Samples = BypassSamples(BestPath[Side].GetValue(), BypassSample);
			int32 Count = 0;
			for (const FVector2D& Sample : Samples)
			{
				bool bAll = true;
				for (const FFormationThreat& Threat : Known)
				{
					bAll &= FormationMath::SegmentBlocked(Sample, Threat.Pos, Only, 0.0);
				}
				Count += bAll ? 1 : 0;
			}
			return static_cast<double>(Count) / static_cast<double>(Samples.Num());
		};
		const int32 Other = 1 - Shortest;
		if (Hidden(Other) <= Hidden(Shortest))
		{
			return Pick(Shortest, EFormationBypassReason::Shortest);
		}
		return BestLength[Other] <= (1.0 + Config.BypassMaxDetour) * MinLength + 1e-6 ? Pick(Other, EFormationBypassReason::Shielded)
			: Pick(Shortest, EFormationBypassReason::TooLong);
	}
}
