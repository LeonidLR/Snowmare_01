// The PMC side of the tactical map: the commander's terrain terms and the squad's placement on the analysis spots
// (docs/design/TACTICAL_MAP.md P2, designer 2026-11-17). UE-only, no Godot reference.

#include "AI/PmcTacticalRules.h"

#include "Math/RandomStream.h"

namespace PmcTacticalRules
{
	namespace PmcTacticalLocal
	{
		inline float Clamp01(float Value)
		{
			return FMath::Clamp(Value, 0.f, 1.f);
		}

		/** A stable roll in [0, 1) per (seed, fighter index): the window share's draw. */
		float Roll(int32 Seed, int32 Index)
		{
			FRandomStream Stream(Seed * 977 + Index * 131 + 29);
			return Stream.FRand();
		}
	}

	const TCHAR* PostKindName(EPmcPostKind Kind)
	{
		switch (Kind)
		{
		case EPmcPostKind::Ambush: return TEXT("ambush");
		case EPmcPostKind::Overwatch: return TEXT("overwatch");
		case EPmcPostKind::Window: return TEXT("window");
		case EPmcPostKind::LadderRoof: return TEXT("ladder_roof");
		case EPmcPostKind::Drop: return TEXT("drop");
		default: return TEXT("none");
		}
	}

	float PositionFactor(float Danger, const FPmcWeights& Weights)
	{
		return 1.f - PmcTacticalLocal::Clamp01(Weights.TerrainDanger) * PmcTacticalLocal::Clamp01(Danger);
	}

	float LeaveFactor(float Danger, const FPmcWeights& Weights)
	{
		return 1.f + PmcTacticalLocal::Clamp01(Weights.TerrainDanger) * PmcTacticalLocal::Clamp01(Danger);
	}

	float PostKindFactor(EPmcPostKind Kind, const FPmcWeights& Weights)
	{
		switch (Kind)
		{
		case EPmcPostKind::Ambush: return 1.f + FMath::Max(Weights.AmbushExit, 0.f);
		case EPmcPostKind::Overwatch: return 1.f + FMath::Max(Weights.OverwatchRoof, 0.f);
		case EPmcPostKind::Window: return 1.f + FMath::Max(Weights.WindowPosition, 0.f);
		default: return 1.f;
		}
	}

	float CandidateScore(const FPmcPostCandidate& Candidate, const FPmcWeights& Weights)
	{
		return PmcTacticalLocal::Clamp01(Candidate.Score) * PostKindFactor(Candidate.Kind, Weights) * PositionFactor(Candidate.Danger, Weights);
	}

	float SlotTerrainFactor(float Danger, float RouteFacingValue, bool bNearAnalysisCover, const FPmcWeights& Weights)
	{
		return PositionFactor(Danger, Weights)
			* (1.f + FMath::Max(Weights.RouteCover, 0.f) * PmcTacticalLocal::Clamp01(RouteFacingValue) * (bNearAnalysisCover ? 1.f : 0.5f));
	}

	float RouteFacing(const FVector& Slot, const FVector& WallNormal, const FVector& RoutePoint)
	{
		const FVector ToRoute = (RoutePoint - Slot).GetSafeNormal2D();
		if (ToRoute.IsNearlyZero())
		{
			return 0.f;
		}
		return PmcTacticalLocal::Clamp01(FVector::DotProduct(-WallNormal.GetSafeNormal2D(), ToRoute));
	}

	FVector NearestOnRoute(TConstArrayView<FVector> Route, const FVector& Point)
	{
		if (Route.IsEmpty())
		{
			return Point;
		}
		if (Route.Num() == 1)
		{
			return Route[0];
		}
		FVector Best = Route[0];
		double BestDistSq = TNumericLimits<double>::Max();
		for (int32 Index = 0; Index + 1 < Route.Num(); ++Index)
		{
			const FVector A = Route[Index];
			const FVector B = Route[Index + 1];
			const FVector2D AB(B - A);
			const double LengthSq = AB.SizeSquared();
			const double T = LengthSq > KINDA_SMALL_NUMBER ? FMath::Clamp(FVector2D::DotProduct(FVector2D(Point - A), AB) / LengthSq, 0.0, 1.0) : 0.0;
			const FVector OnSegment = A + (B - A) * T;
			const double DistSq = FVector::DistSquared2D(OnSegment, Point);
			if (DistSq < BestDistSq)
			{
				BestDistSq = DistSq;
				Best = OnSegment;
			}
		}
		return Best;
	}

	float DistanceToRoute(TConstArrayView<FVector> Route, const FVector& Point)
	{
		return Route.IsEmpty() ? TNumericLimits<float>::Max() : static_cast<float>(FVector::Dist2D(NearestOnRoute(Route, Point), Point));
	}

	TArray<float> FallBackAngles()
	{
		return { 0.f, 35.f, -35.f, 70.f, -70.f };
	}

	int32 PickFallBack(TConstArrayView<float> AnglesDeg, TConstArrayView<float> Dangers, const FPmcWeights& Weights)
	{
		int32 Best = INDEX_NONE;
		float BestScore = -1.f;
		for (int32 Index = 0; Index < FMath::Min(AnglesDeg.Num(), Dangers.Num()); ++Index)
		{
			const float Away = 0.5f + 0.5f * FMath::Cos(FMath::DegreesToRadians(AnglesDeg[Index]));
			const float Score = Away * (1.f - PmcTacticalLocal::Clamp01(Weights.ChokepointAvoid) * PmcTacticalLocal::Clamp01(Dangers[Index]));
			// Strictly better only: the earlier (straighter) candidate keeps a tie.
			if (Score > BestScore + 1e-4f)
			{
				BestScore = Score;
				Best = Index;
			}
		}
		return Best;
	}

	void MarkMarksmen(TArray<FPmcPlacementFighter>& Fighters)
	{
		int32 Longest = INDEX_NONE;
		for (int32 Index = 0; Index < Fighters.Num(); ++Index)
		{
			if (Fighters[Index].bMarksman)
			{
				return;
			}
			if (Longest == INDEX_NONE || Fighters[Index].BandMaxCm > Fighters[Longest].BandMaxCm)
			{
				Longest = Index;
			}
		}
		if (Longest != INDEX_NONE)
		{
			Fighters[Longest].bMarksman = true;
		}
	}

	bool CanJump(const FString& Archetype, TConstArrayView<FString> Jumpers)
	{
		for (const FString& Jumper : Jumpers)
		{
			if (Jumper.Equals(Archetype, ESearchCase::IgnoreCase) || Jumper.Equals(TEXT("any"), ESearchCase::IgnoreCase))
			{
				return true;
			}
		}
		return false;
	}

	TArray<FPmcTacticalPost> PlanPlacement(const TArray<FPmcPlacementFighter>& Fighters, const TArray<FPmcPostCandidate>& Candidates,
		const FPmcWeights& Weights, int32 Seed, float MinSpacingCm)
	{
		TArray<FPmcTacticalPost> Posts;
		Posts.SetNum(Fighters.Num());
		// Candidates by effective score, best first (stable: the gather order breaks ties).
		TArray<int32> Order;
		for (int32 Index = 0; Index < Candidates.Num(); ++Index)
		{
			Order.Add(Index);
		}
		Order.StableSort([&Candidates, &Weights](int32 Left, int32 Right)
		{
			return CandidateScore(Candidates[Left], Weights) > CandidateScore(Candidates[Right], Weights);
		});
		TSet<FString> UsedGroups;
		TArray<FVector> Taken;
		auto IsFree = [&](const FPmcPostCandidate& Candidate)
		{
			if (!Candidate.Group.IsEmpty() && UsedGroups.Contains(Candidate.Group))
			{
				return false;
			}
			for (const FVector& Other : Taken)
			{
				if (FVector::Dist(Other, Candidate.Location) < MinSpacingCm)
				{
					return false;
				}
			}
			return true;
		};
		// The best free candidate of a kind (an optional filter); INDEX_NONE when none.
		auto Best = [&](EPmcPostKind Kind, TFunctionRef<bool(const FPmcPostCandidate&)> Accept)
		{
			for (const int32 Index : Order)
			{
				const FPmcPostCandidate& Candidate = Candidates[Index];
				if (Candidate.Kind == Kind && IsFree(Candidate) && Accept(Candidate))
				{
					return Index;
				}
			}
			return static_cast<int32>(INDEX_NONE);
		};
		auto Assign = [&](int32 Fighter, int32 Index)
		{
			const FPmcPostCandidate& Candidate = Candidates[Index];
			FPmcTacticalPost& Post = Posts[Fighter];
			Post.Kind = Candidate.Kind;
			Post.Location = Candidate.Location;
			Post.Yaw = Candidate.Yaw;
			Post.Score = CandidateScore(Candidate, Weights);
			Post.Danger = Candidate.Danger;
			Post.Group = Candidate.Group;
			Post.SourceIndex = Candidate.SourceIndex;
			Post.SpotIndex = Candidate.SpotIndex;
			if (!Candidate.Group.IsEmpty())
			{
				UsedGroups.Add(Candidate.Group);
			}
			Taken.Add(Candidate.Location);
		};
		auto Any = [](const FPmcPostCandidate&) { return true; };
		// 1. Marksmen on the overwatch roofs (one man per roof).
		for (int32 Fighter = 0; Fighter < Fighters.Num(); ++Fighter)
		{
			if (Fighters[Fighter].bMarksman)
			{
				const int32 Index = Best(EPmcPostKind::Overwatch, Any);
				if (Index != INDEX_NONE)
				{
					Assign(Fighter, Index);
				}
			}
		}
		// 2. Jumpers above the drop spots (only an archetype the spot names).
		for (int32 Fighter = 0; Fighter < Fighters.Num(); ++Fighter)
		{
			if (!Posts[Fighter].IsValid())
			{
				const FString& Archetype = Fighters[Fighter].Archetype;
				const int32 Index = Best(EPmcPostKind::Drop, [&Archetype](const FPmcPostCandidate& Candidate) { return CanJump(Archetype, Candidate.Jumpers); });
				if (Index != INDEX_NONE)
				{
					Assign(Fighter, Index);
				}
			}
		}
		// 3. Up to LadderRoofMen men on the picked ladder-only roofs (the last non-marksmen first: the squad's lead keeps the ambush).
		int32 LadderMen = 0;
		for (int32 Fighter = Fighters.Num() - 1; Fighter >= 0 && LadderMen < Weights.LadderRoofMen; --Fighter)
		{
			if (!Posts[Fighter].IsValid() && !Fighters[Fighter].bMarksman)
			{
				const int32 Index = Best(EPmcPostKind::LadderRoof, Any);
				if (Index == INDEX_NONE)
				{
					break;
				}
				Assign(Fighter, Index);
				++LadderMen;
			}
		}
		// 4. The rest: the first of them always at the best ambush spot (the chokepoint exit: the squad's ambush), then a seeded share at
		// windows, the others at ambush spots (each falls back to the other kind, then any post).
		bool bAmbusherPlaced = false;
		for (int32 Fighter = 0; Fighter < Fighters.Num(); ++Fighter)
		{
			if (Posts[Fighter].IsValid())
			{
				continue;
			}
			const bool bWindowFirst = bAmbusherPlaced && PmcTacticalLocal::Roll(Seed, Fighter) < PmcTacticalLocal::Clamp01(Weights.WindowShare);
			bAmbusherPlaced = true;
			const EPmcPostKind First = bWindowFirst ? EPmcPostKind::Window : EPmcPostKind::Ambush;
			const EPmcPostKind Second = bWindowFirst ? EPmcPostKind::Ambush : EPmcPostKind::Window;
			int32 Index = Best(First, Any);
			if (Index == INDEX_NONE)
			{
				Index = Best(Second, Any);
			}
			if (Index == INDEX_NONE)
			{
				Index = Best(EPmcPostKind::Overwatch, Any);
			}
			if (Index != INDEX_NONE)
			{
				Assign(Fighter, Index);
			}
		}
		return Posts;
	}

	bool IsAtPost(const FVector& Feet, const FVector& Post, float ArriveCm, float LevelToleranceCm)
	{
		return FVector::Dist2D(Feet, Post) <= ArriveCm && (LevelToleranceCm <= 0.f || FMath::Abs(Feet.Z - Post.Z) <= LevelToleranceCm);
	}
}
