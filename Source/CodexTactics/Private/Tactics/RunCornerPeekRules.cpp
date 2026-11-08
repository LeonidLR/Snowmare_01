// RunCornerPeekRules: the run-and-peek click (#55, user request 2026-11-07) - when a click on a wall's end sends an operative from the open
// straight into the corner peek, which face of the wall he takes, when his cover entry is that peek's arrival, and who carries it out.
// Godot reference: none (UE-only; docs/systems/cover-and-peek.md "Run-and-peek").

#include "Tactics/RunCornerPeekRules.h"

namespace RunCornerPeekRules
{
	bool MayRunToPeek(const FClickFacts& Facts)
	{
		// Q3 (2026-11-08): the tactical pause no longer refuses it - it plans it (DecideTiming).
		return Facts.bEnabled && !Facts.bTurnBased && !Facts.bSquadWide && !Facts.bTutorialLocked && !Facts.bHitPawn
			&& !Facts.bHitObject && !Facts.bInCoverAtClickedObject && !Facts.bCoverSwitchTarget;
	}

	bool IsClickAtHisLevel(bool bTopSurface, float HitAboveFeetCm, float MaxFaceAboveCm, float MaxBelowCm)
	{
		return !bTopSurface && HitAboveFeetCm <= MaxFaceAboveCm && HitAboveFeetCm >= -MaxBelowCm;
	}

	bool IsCornerOnHisFloor(float CornerFloorAboveFeetCm, float MaxGapCm)
	{
		return FMath::Abs(CornerFloorAboveFeetCm) <= MaxGapCm;
	}

	bool IsRunPeekCover(ECoverHeight Height)
	{
		return Height == ECoverHeight::HighCover;
	}

	bool IsStackPlaceCovered(bool bSlotOnFace, float FromCornerSlotCm, float MinFromCornerCm)
	{
		return bSlotOnFace && FromCornerSlotCm >= MinFromCornerCm;
	}

	bool IsFaceWideEnough(bool bLeftExposed, float LeftCm, bool bRightExposed, float RightCm, float MinWidthCm)
	{
		if (!bLeftExposed || !bRightExposed)
		{
			return true; // a closed side: the wall goes on past the corner probes
		}
		return FMath::Max(LeftCm, 0.f) + FMath::Max(RightCm, 0.f) >= MinWidthCm;
	}

	bool IsNewFace(const TArray<FVector2D>& Known, const FVector2D& Normal)
	{
		const FVector2D Unit = Normal.GetSafeNormal();
		if (Unit.IsNearlyZero())
		{
			return false;
		}
		return !Known.ContainsByPredicate([&Unit](const FVector2D& Each) { return FVector2D::DotProduct(Each.GetSafeNormal(), Unit) >= SameFaceDot; });
	}

	int32 ChooseNearFace(const TArray<FFace>& Faces, const FVector2D& Operative)
	{
		int32 Best = INDEX_NONE;
		double BestScore = 0.0;
		double BestDistance = 0.0;
		for (int32 Index = 0; Index < Faces.Num(); ++Index)
		{
			const FFace& Face = Faces[Index];
			const FVector2D ToHim = Operative - Face.Point;
			const double Distance = ToHim.Size();
			const double Score = Distance > KINDA_SMALL_NUMBER ? FVector2D::DotProduct(Face.Normal.GetSafeNormal(), ToHim / Distance) : 0.0;
			const bool bBetter = Best == INDEX_NONE || Score > BestScore + 0.01 || (Score >= BestScore - 0.01 && Distance < BestDistance);
			if (bBetter)
			{
				Best = Index;
				BestScore = Score;
				BestDistance = Distance;
			}
		}
		return Best;
	}

	int32 ChooseClickedFace(const TArray<FFace>& Faces, const FVector2D& HitNormal, const FVector2D& TowardCamera, float MinDot, float TieDot)
	{
		const FVector2D Hit = HitNormal.GetSafeNormal();
		const FVector2D Camera = TowardCamera.GetSafeNormal();
		double Best = -2.0;
		for (const FFace& Face : Faces)
		{
			Best = FMath::Max(Best, FVector2D::DotProduct(Face.Normal.GetSafeNormal(), Hit));
		}
		if (Hit.IsNearlyZero() || Best < MinDot)
		{
			return INDEX_NONE;
		}
		int32 Pick = INDEX_NONE;
		double PickCamera = -2.0;
		for (int32 Index = 0; Index < Faces.Num(); ++Index)
		{
			const FVector2D Normal = Faces[Index].Normal.GetSafeNormal();
			if (FVector2D::DotProduct(Normal, Hit) < Best - TieDot)
			{
				continue;
			}
			const double ToCamera = FVector2D::DotProduct(Normal, Camera);
			if (Pick == INDEX_NONE || ToCamera > PickCamera)
			{
				Pick = Index;
				PickCamera = ToCamera;
			}
		}
		return Pick;
	}

	bool KeepsCoverSwitch(bool bSwitchVerdict, const FVector2D& ClickedFaceNormal, const FVector2D& OwnWallNormal, float ToClickedEndCm, float ToOtherEndCm)
	{
		const FVector2D Face = ClickedFaceNormal.GetSafeNormal();
		const FVector2D Own = OwnWallNormal.GetSafeNormal();
		const bool bProtectedSide = !Face.IsNearlyZero() && !Own.IsNearlyZero() && FVector2D::DotProduct(Face, Own) >= 0.5f;
		return bSwitchVerdict && bProtectedSide && ToClickedEndCm <= ToOtherEndCm;
	}

	TArray<FVector2D> ProbeDirections(const FVector2D& Primary, int32 Count)
	{
		TArray<FVector2D> Result;
		FVector2D First = Primary.GetSafeNormal();
		if (First.IsNearlyZero())
		{
			First = FVector2D(1.0, 0.0);
		}
		const int32 Num = FMath::Max(Count, 1);
		for (int32 Index = 0; Index < Num; ++Index)
		{
			const double Angle = 2.0 * UE_DOUBLE_PI * Index / Num;
			const double Cos = FMath::Cos(Angle);
			const double Sin = FMath::Sin(Angle);
			Result.Add(FVector2D(First.X * Cos - First.Y * Sin, First.X * Sin + First.Y * Cos));
		}
		return Result;
	}

	bool IsPeekArrival(bool bArmed, bool bSameWall, float DistanceFromOrderedCm, float ToleranceCm)
	{
		return bArmed && bSameWall && DistanceFromOrderedCm <= ToleranceCm;
	}

	EGroup DecideGroup(bool bMultiSelection, bool bOperativeSelected, bool bGroupCornerPeekOn, bool bSoloMode, int32 GroupCoverOthers)
	{
		if (bMultiSelection && bOperativeSelected)
		{
			return bGroupCornerPeekOn ? EGroup::BoxSelection : EGroup::Single;
		}
		if (!bSoloMode && GroupCoverOthers > 0)
		{
			return EGroup::Formation;
		}
		return EGroup::Single;
	}

	bool IsSprintClick(double SecondsSinceFirst, double MaxSeconds, float PixelsApart, float MaxPixels, float EdgeCmApart, float MaxEdgeCm)
	{
		return SecondsSinceFirst >= 0.0 && SecondsSinceFirst <= MaxSeconds && PixelsApart <= MaxPixels && EdgeCmApart <= MaxEdgeCm;
	}

	ETiming DecideTiming(const FClickFacts& Facts)
	{
		return Facts.bPausePlanning ? ETiming::OnPauseRelease : ETiming::Now;
	}

	FormationPeekCarry::EPeekCarryStep DecideFormationCarry(const FormationPeekCarry::FCarryProgressFacts& Facts, bool bRunPeek, bool bLeaderAtWall)
	{
		using namespace FormationPeekCarry;
		const EPeekCarryStep Base = Decide(Facts);
		if (!bRunPeek || Base != EPeekCarryStep::Wait)
		{
			return Base; // the group wall order's carry as before; Drop / Fire stand for both
		}
		if (!Facts.bLeaderMoved && Facts.Seconds < StartGraceSeconds)
		{
			return EPeekCarryStep::Wait; // he has not set off yet (standing still is not arriving)
		}
		return bLeaderAtWall && Facts.LeaderStillSeconds >= LeaderStillSeconds ? EPeekCarryStep::Fire : EPeekCarryStep::Wait;
	}

	bool ShouldPreTurn(bool bArmedForThisSlot, float DistanceToSlotCm, float SpeedCmS, float PreTurnDistanceCm, float MinSpeedCmS)
	{
		return bArmedForThisSlot && DistanceToSlotCm >= 0.f && DistanceToSlotCm <= PreTurnDistanceCm && SpeedCmS > MinSpeedCmS;
	}

	float HeldTurnSameWayAsBlend(float HeldDeg, float BlendTurnDeg)
	{
		float Best = HeldDeg;
		for (const float Candidate : { HeldDeg - 360.f, HeldDeg + 360.f })
		{
			if (FMath::Abs(Candidate - BlendTurnDeg) < FMath::Abs(Best - BlendTurnDeg))
			{
				Best = Candidate;
			}
		}
		return Best;
	}

	bool HeldTurnGoesTheOtherWay(float HeldDeg, float ObservedTurnDeg, float MinSeenDeg)
	{
		return FMath::Abs(ObservedTurnDeg) >= MinSeenDeg && FMath::Abs(HeldDeg) > MinSeenDeg && HeldDeg * ObservedTurnDeg < 0.f;
	}

	float PeekStanceYawBlendSeconds(float LeftoverDeg, float MinSeconds, float MaxSeconds)
	{
		const float Deg = FMath::Abs(FRotator::NormalizeAxis(LeftoverDeg));
		const float Alpha = FMath::Clamp((Deg - 30.f) / 150.f, 0.f, 1.f);
		return FMath::Lerp(MinSeconds, FMath::Max(MaxSeconds, MinSeconds), Alpha);
	}

	float PreTurnStartCm(float SpeedCmS, float EntryLeadCm, float MinCm, float LeadSeconds, float MaxCm)
	{
		const float Stretch = FMath::Clamp(FMath::Max(SpeedCmS, 0.f) * LeadSeconds, MinCm, FMath::Max(MaxCm, MinCm));
		return Stretch + FMath::Max(EntryLeadCm, 0.f);
	}

	float PreTurnStepYaw(float CurrentYaw, float TargetYaw, float DistanceToEntryCm, float SpeedCmS, float DeltaSeconds, float FinishEarlySeconds)
	{
		if (DeltaSeconds <= 0.f)
		{
			return FRotator::NormalizeAxis(CurrentYaw);
		}
		const float Left = FRotator::NormalizeAxis(TargetYaw - CurrentYaw);
		const float SecondsToEntry = SpeedCmS > 1.f ? FMath::Max(DistanceToEntryCm, 0.f) / SpeedCmS : 0.f;
		const float SecondsLeft = SecondsToEntry - FinishEarlySeconds;
		const float Alpha = SecondsLeft <= DeltaSeconds ? 1.f : DeltaSeconds / SecondsLeft;
		return FRotator::NormalizeAxis(CurrentYaw + Left * Alpha);
	}

	float OneWayStepYaw(float CurrentYaw, float SteppedYaw, int32& InOutSign)
	{
		const float Step = static_cast<float>(FRotator::NormalizeAxis(SteppedYaw - CurrentYaw));
		if (InOutSign == 0)
		{
			if (FMath::Abs(Step) > 0.05f)
			{
				InOutSign = Step > 0.f ? 1 : -1;
			}
			return SteppedYaw;
		}
		return Step * static_cast<float>(InOutSign) < 0.f ? CurrentYaw : SteppedYaw;
	}

	float BodyTargetActorYaw(float FacingYaw, float FinalBodyYawCs, float RunBodyYawCs)
	{
		return FRotator::NormalizeAxis(FacingYaw + FRotator::NormalizeAxis(FinalBodyYawCs - RunBodyYawCs));
	}

	float BodyLineYaw(const FVector2D& Left, const FVector2D& Right)
	{
		const FVector2D Line = Right - Left;
		return FMath::RadiansToDegrees(FMath::Atan2(-Line.X, Line.Y));
	}

	float MixBodyYaw(float ShoulderYaw, float HipYaw, float ShoulderWeight)
	{
		return FRotator::NormalizeAxis(HipYaw + FMath::Clamp(ShoulderWeight, 0.f, 1.f) * FRotator::NormalizeAxis(ShoulderYaw - HipYaw));
	}

	float LockTargetYaw(float EntryYaw, float FinalYaw, float Seconds, float PathSeconds)
	{
		const float Alpha = PathSeconds > 0.f ? FMath::Clamp(Seconds / PathSeconds, 0.f, 1.f) : 1.f;
		return FRotator::NormalizeAxis(EntryYaw + FRotator::NormalizeAxis(FinalYaw - EntryYaw) * FMath::SmoothStep(0.f, 1.f, Alpha));
	}

	float LockAlpha(float Seconds, float PathSeconds, float FadeSeconds)
	{
		if (Seconds <= PathSeconds)
		{
			return 1.f;
		}
		return FadeSeconds > 0.f ? FMath::Clamp(1.f - (Seconds - PathSeconds) / FadeSeconds, 0.f, 1.f) : 0.f;
	}

	float LockCorrectionDeg(float TargetYaw, float PoseBodyYaw, float Alpha)
	{
		return FRotator::NormalizeAxis(TargetYaw - PoseBodyYaw) * FMath::Clamp(Alpha, 0.f, 1.f);
	}
}
