// R17-3 unified building click: the pure decisions (ClickPreviewRules.h). UE-only (Godot reference: none).

#include "Tactics/ClickPreviewRules.h"

namespace ClickPreviewRules
{
	EClickPreviewAction DecideClick(const FClickPreviewClickFacts& Facts)
	{
		if (!Facts.bEnabled)
		{
			return EClickPreviewAction::PassThrough;
		}
		if (Facts.bBypass)
		{
			return Facts.bArmed ? EClickPreviewAction::CancelAndPass : EClickPreviewAction::PassThrough;
		}
		if (Facts.bArmed && (Facts.bOnArmedStructure || Facts.bOnGhost))
		{
			return EClickPreviewAction::Execute;
		}
		if (Facts.bSurface)
		{
			return EClickPreviewAction::Arm; // the first click, or another building while a ghost is up: its ghost instead
		}
		return Facts.bArmed ? EClickPreviewAction::CancelAndPass : EClickPreviewAction::PassThrough;
	}

	bool IsDoubleClick(double SinceArmSeconds, float MouseDistancePx, float MaxSeconds, float MaxPixels)
	{
		return SinceArmSeconds >= 0.0 && SinceArmSeconds <= MaxSeconds && MouseDistancePx <= MaxPixels;
	}

	bool IsGroundPiece(const FClickPreviewPiece& Piece)
	{
		if (!Piece.Bounds.IsValid)
		{
			return false;
		}
		const FVector Size = Piece.Bounds.GetSize();
		return Size.X > MaxPieceExtentCm || Size.Y > MaxPieceExtentCm;
	}

	bool IsOnStructure(const FClickPreviewStructure& Structure, const FClickPreviewPiece& Piece)
	{
		if (Structure.IsEmpty())
		{
			return false;
		}
		if (Piece.ComponentId != 0 && Structure.Components.Contains(Piece.ComponentId))
		{
			return true; // a piece the ghost stood on (its face, a wall's own top, the obstacle's top)
		}
		if (Piece.bFloorLike || IsGroundPiece(Piece))
		{
			return false; // the floor beside / inside a building is a move
		}
		if (Piece.ActorId != 0 && Structure.Actors.Contains(Piece.ActorId))
		{
			return true;
		}
		if (Piece.Building != NAME_None && Structure.Buildings.Contains(Piece.Building))
		{
			return true;
		}
		return Structure.Bounds.IsValid && Piece.Bounds.IsValid && Structure.Bounds.ExpandBy(TouchToleranceCm).Intersect(Piece.Bounds);
	}

	void AddPiece(FClickPreviewStructure& Structure, const FClickPreviewPiece& Piece)
	{
		if (Piece.ComponentId != 0)
		{
			Structure.Components.Add(Piece.ComponentId);
		}
		if (IsGroundPiece(Piece))
		{
			return; // its bounds would touch every wall standing on it
		}
		if (Piece.ActorId != 0)
		{
			Structure.Actors.Add(Piece.ActorId);
		}
		if (Piece.Building != NAME_None)
		{
			Structure.Buildings.Add(Piece.Building);
		}
		if (Piece.Bounds.IsValid)
		{
			Structure.Bounds += Piece.Bounds;
		}
	}

	bool IsClickOnGhost(const FVector& Click, bool bClickFloorLike, const FVector& GhostFeet)
	{
		return bClickFloorLike && FVector::Dist2D(Click, GhostFeet) <= GhostClickRadiusCm && FMath::Abs(Click.Z - GhostFeet.Z) <= GhostClickHeightCm;
	}

	bool ShouldLowerWallHit(float HitZ, float NormalZ, float FeetZ)
	{
		return FMath::Abs(NormalZ) < 0.3f && HitZ - FeetZ > HighHitAboveFeetCm;
	}

	bool IsSameFaceLower(const FVector2D& HighNormal, const FVector2D& LowNormal, float LowNormalZ)
	{
		if (FMath::Abs(LowNormalZ) >= 0.3f)
		{
			return false;
		}
		const FVector2D High = HighNormal.GetSafeNormal();
		const FVector2D Low = LowNormal.GetSafeNormal();
		return !High.IsNearlyZero() && !Low.IsNearlyZero() && FVector2D::DotProduct(High, Low) >= LowHitSameFaceDot;
	}

	bool IsAtPortalPlane(const FVector& HitPoint, const FVector& Center, const FVector& Outward, float DepthSlackCm)
	{
		const FVector Normal = FVector(Outward.X, Outward.Y, 0.f).GetSafeNormal();
		return !Normal.IsNearlyZero() && FMath::Abs(FVector::DotProduct(HitPoint - Center, Normal)) <= DepthSlackCm;
	}

	bool RayThroughPortal(const FVector& From, const FVector& To, const FVector& Center, const FVector& Tangent, const FVector& Outward,
		float WidthCm, float HeightCm, float SlackCm, float DepthSlackCm, FVector& OutPoint)
	{
		const FVector Normal = FVector(Outward.X, Outward.Y, 0.f).GetSafeNormal();
		const FVector Along = FVector(Tangent.X, Tangent.Y, 0.f).GetSafeNormal();
		if (Normal.IsNearlyZero() || Along.IsNearlyZero() || WidthCm <= 0.f)
		{
			return false;
		}
		const double S0 = FVector::DotProduct(From - Center, Normal);
		const double S1 = FVector::DotProduct(To - Center, Normal);
		FVector Point;
		if (FMath::Abs(S1) <= DepthSlackCm)
		{
			Point = To - Normal * S1; // the hit itself lies at the portal (the threshold, a jamb face): its foot on the plane
		}
		else if ((S0 > 0.0) != (S1 > 0.0))
		{
			const double T = S0 / (S0 - S1);
			Point = From + (To - From) * T; // the ray crosses the plane before its hit (through the opening onto the floor / a wall inside)
		}
		else
		{
			return false;
		}
		const double Lateral = FMath::Abs(FVector::DotProduct(Point - Center, Along));
		const double Up = Point.Z - Center.Z;
		if (Lateral > WidthCm * 0.5 + SlackCm || Up < -30.0 || Up > HeightCm)
		{
			return false;
		}
		OutPoint = Point;
		return true;
	}
}
