// SquadMantleRules: the squad's turn-taking climb's pure geometry / looks (R17-1, docs/design/MANTLING.md 6.4). UE-only (Godot had no mantling).

#include "Characters/SquadMantleRules.h"

#include "Formation/FormationSectorRules.h"

TArray<float> SquadMantleRules::LaneOffsets(int32 Count, float SpacingCm, float MinOffsetCm, float MaxOffsetCm)
{
	TArray<float> Out;
	if (Count <= 0)
	{
		return Out;
	}
	Out.Add(0.f);
	const float Spacing = FMath::Max(1.f, SpacingCm);
	for (int32 Step = 1; Out.Num() < Count; ++Step)
	{
		const float Right = Spacing * Step;
		const float Left = -Spacing * Step;
		const bool bRight = Right <= MaxOffsetCm + KINDA_SMALL_NUMBER;
		const bool bLeft = Left >= MinOffsetCm - KINDA_SMALL_NUMBER;
		if (!bRight && !bLeft)
		{
			break; // the face has no more room either side
		}
		if (bRight)
		{
			Out.Add(Right);
		}
		if (bLeft && Out.Num() < Count)
		{
			Out.Add(Left);
		}
	}
	return Out;
}

TArray<int32> SquadMantleRules::ClimbOrder(int32 LeaderIndex, const TArray<int32>& Slots)
{
	TArray<int32> Out;
	if (Slots.IsValidIndex(LeaderIndex))
	{
		Out.Add(LeaderIndex);
	}
	TArray<int32> Slotted;
	TArray<int32> Unslotted;
	for (int32 Index = 0; Index < Slots.Num(); ++Index)
	{
		if (Index == LeaderIndex)
		{
			continue;
		}
		(Slots[Index] == INDEX_NONE ? Unslotted : Slotted).Add(Index);
	}
	Slotted.StableSort([&Slots](int32 A, int32 B) { return Slots[A] < Slots[B]; });
	Out.Append(Slotted);
	Out.Append(Unslotted);
	return Out;
}

bool SquadMantleRules::IsThreatOnOpenSide(float OutwardYaw, float ThreatYaw)
{
	return FMath::Abs(FMath::FindDeltaAngleDegrees(OutwardYaw, ThreatYaw)) <= 90.f;
}

float SquadMantleRules::WaitLookYaw(float OutwardYaw, int32 Place, float FanDeg, float MaxOffDeg, const TOptional<float>& ThreatYaw)
{
	const float Base = ThreatYaw.IsSet() && IsThreatOnOpenSide(OutwardYaw, ThreatYaw.GetValue()) ? ThreatYaw.GetValue() : OutwardYaw;
	const int32 Step = (FMath::Max(0, Place) + 1) / 2;
	const float Sign = Place % 2 == 1 ? 1.f : -1.f;
	const float Look = Base + Sign * FanDeg * static_cast<float>(Step);
	const float Off = FMath::Clamp(FMath::FindDeltaAngleDegrees(OutwardYaw, Look), -FMath::Abs(MaxOffDeg), FMath::Abs(MaxOffDeg));
	return FRotator::NormalizeAxis(OutwardYaw + Off);
}

float SquadMantleRules::ClearWaitLook(const TArray<FFormationWall>& Walls, const FVector2D& Pos, float WantedYaw, float CurrentYaw, float OutwardYaw,
	float MaxOffDeg, const FFormationConfig& Config)
{
	FFormationLookLimit Limit;
	Limit.Yaw = OutwardYaw;
	Limit.Half = FMath::Min(FMath::Abs(MaxOffDeg), 180.f);
	return FRotator::NormalizeAxis(FormationSectorRules::ClearLook(Walls, Pos, WantedYaw, CurrentYaw, Config, TOptional<float>(), Limit));
}

float SquadMantleRules::UpLookYaw(float ClimbYaw, int32 UpIndex, const TOptional<float>& ThreatYaw)
{
	static const float Offsets[] = { 0.f, 90.f, -90.f, 180.f, 45.f, -45.f, 135.f, -135.f };
	if (UpIndex <= 0)
	{
		return FRotator::NormalizeAxis(ThreatYaw.IsSet() ? ThreatYaw.GetValue() : ClimbYaw);
	}
	return FRotator::NormalizeAxis(ClimbYaw + Offsets[UpIndex % UE_ARRAY_COUNT(Offsets)]);
}

int32 SquadMantleRules::LaneCandidateCount(int32 LanesPerFace)
{
	return LanesPerFace <= 0 ? 0 : LanesPerFace * 2 + 1;
}

bool SquadMantleRules::IsTurnOverTime(bool bInClip, float TurnSeconds, float TimeoutSeconds)
{
	return !bInClip && TurnSeconds > TimeoutSeconds;
}

bool SquadMantleRules::IsRetryDue(int32 Retries, float SinceOrderSeconds, float RetryWaitSeconds)
{
	return Retries <= 0 || SinceOrderSeconds >= RetryWaitSeconds;
}

int32 SquadMantleRules::WaitSpotPlace(bool bWaiting, bool bHolding, int32 LineIndex, int32 MemberIndex)
{
	if (bWaiting)
	{
		return LineIndex;
	}
	return bHolding && MemberIndex >= 0 ? MemberIndex : INDEX_NONE;
}

bool SquadMantleRules::ShouldWalkOn(const FWalkOnCheck& Check)
{
	return Check.bResumePath && Check.bFromPath && !Check.bTurnBased && Check.bResume && Check.bAlive && Check.bSameOrder && Check.DistToGoalCm > 60.f;
}
