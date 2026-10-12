// Weather idle poses (user request 2026-10-12; UE-only, no Godot reference).

#include "Survival/WarmIdleRules.h"

#include "Algo/Count.h"
#include "Survival/TacticalHeatRules.h"

namespace WarmIdleRules
{
	bool IsExplorationPhase(ECodexGamePhase Phase, ECodexCombatMode CombatMode)
	{
		return (Phase == ECodexGamePhase::Exploration || Phase == ECodexGamePhase::PostCombat) && CombatMode == ECodexCombatMode::None;
	}

	bool IsCombatForWarming(ECodexGamePhase Phase)
	{
		return Phase == ECodexGamePhase::WaveCombat;
	}

	float GetHeatSourceWarmthMultiplier(const FTacticalHeatConfig& Config, bool bCombat)
	{
		return FMath::Max(0.f, bCombat ? Config.CombatWarmthMultiplier : Config.ExplorationWarmthMultiplier);
	}

	bool CanHoldWarmPose(const FWarmIdleInput& Input, const FTacticalHeatConfig& Config)
	{
		return Input.bExploration && Input.bAlive && !Input.bKnockedDown && !Input.bFrostbitten && !Input.bInCover && !Input.bCarrying
			&& !Input.bVaulting && !Input.bBusy && !Input.bMoving && !Input.bThreat && Input.Stance != EOperativeStance::Prone
			&& Input.IdleSeconds >= Config.WarmIdleDelaySeconds;
	}

	EWarmIdlePose ChooseSoloPose(const FWarmIdleInput& Input, const FTacticalHeatConfig& Config)
	{
		if (!CanHoldWarmPose(Input, Config))
		{
			return EWarmIdlePose::None;
		}
		switch (Input.Heat)
		{
		case EWarmHeatKind::Barrel:
		case EWarmHeatKind::Campfire:
		case EWarmHeatKind::Stove:
			return EWarmIdlePose::HandWarm;
		case EWarmHeatKind::Other:
			return EWarmIdlePose::None;
		default:
			break;
		}
		const float Threshold = Input.bInColdIdle ? Config.WarmIdleColdThreshold - Config.WarmIdleColdHysteresis : Config.WarmIdleColdThreshold;
		return Input.Stance == EOperativeStance::Standing && Input.Cold >= Threshold ? EWarmIdlePose::ColdIdle : EWarmIdlePose::None;
	}

	EWarmIdlePose PoseForFireRole(EWarmFireRole Role)
	{
		return Role == EWarmFireRole::Sit ? EWarmIdlePose::SitWarm : Role == EWarmFireRole::Stand ? EWarmIdlePose::HandWarm : EWarmIdlePose::None;
	}

	bool UsesWarmSpots(EWarmHeatKind Kind)
	{
		return Kind == EWarmHeatKind::Barrel || Kind == EWarmHeatKind::Campfire || Kind == EWarmHeatKind::Stove;
	}

	TArray<EWarmFireRole> AssignFireRoles(const TArray<EWarmFireRole>& Current, const TArray<float>& Colds, int32 MinSitters,
		float ExtraSitterCold, int32 MaxSitters)
	{
		const int32 Count = FMath::Min(Current.Num(), Colds.Num());
		TArray<EWarmFireRole> Roles = Current;
		Roles.SetNum(Count);
		const int32 Wanted = FMath::Min(FMath::Max(0, MinSitters), Count);
		const int32 Cap = FMath::Max(0, MaxSitters);
		auto CountSitters = [&Roles]()
		{
			return static_cast<int32>(Algo::CountIf(Roles, [](EWarmFireRole Role) { return Role == EWarmFireRole::Sit; }));
		};
		// Coldest first; equal cold keeps the input order (stable).
		TArray<int32> Order;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Order.Add(Index);
		}
		Order.StableSort([&Colds](int32 A, int32 B) { return Colds[A] > Colds[B]; });
		for (const int32 Index : Order)
		{
			if (Roles[Index] != EWarmFireRole::Unassigned)
			{
				continue;
			}
			const int32 Sitters = CountSitters();
			Roles[Index] = Sitters < Cap && (Sitters < Wanted || Colds[Index] >= ExtraSitterCold) ? EWarmFireRole::Sit : EWarmFireRole::Stand;
		}
		// Someone left: the coldest standers sit down until the minimum holds again.
		for (const int32 Index : Order)
		{
			const int32 Sitters = CountSitters();
			if (Sitters >= Wanted || Sitters >= Cap)
			{
				break;
			}
			if (Roles[Index] == EWarmFireRole::Stand)
			{
				Roles[Index] = EWarmFireRole::Sit;
			}
		}
		return Roles;
	}

	bool ShouldPullFollower(const FWarmPullInput& Input, const FTacticalHeatConfig& Config)
	{
		return Config.bWarmPullFollowers && Input.bExploration && Input.bInFormation && !Input.bOrdered && Input.bLeaderAtFire
			&& Input.bFollowerFree && (Input.bPulling || Input.FollowerToSource <= Config.WarmPullMaxDistance);
	}

	int32 RingCapacity(float Radius, float Spacing)
	{
		if (Radius <= 0.f || Spacing <= 0.f)
		{
			return 1;
		}
		return FMath::Max(1, FMath::FloorToInt32(2.f * UE_PI * Radius / Spacing));
	}

	TArray<FVector> ComputeRing(const FVector& Center, float Radius, int32 Count, float StartYawDeg)
	{
		TArray<FVector> Spots;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const float Yaw = FMath::DegreesToRadians(StartYawDeg + 360.f * Index / FMath::Max(1, Count));
			Spots.Add(Center + FVector(FMath::Cos(Yaw), FMath::Sin(Yaw), 0.f) * Radius);
		}
		return Spots;
	}

	int32 PickSlot(const TArray<FVector>& Slots, const TArray<bool>& Taken, const FVector& Center, const FVector& From, float WalkWeightPerCm)
	{
		auto Bearing = [&Center](const FVector& Point) { return FMath::RadiansToDegrees(FMath::Atan2(Point.Y - Center.Y, Point.X - Center.X)); };
		bool bAnyTaken = false;
		for (int32 Index = 0; Index < Slots.Num(); ++Index)
		{
			bAnyTaken |= Taken.IsValidIndex(Index) && Taken[Index];
		}
		int32 Best = INDEX_NONE;
		float BestScore = -TNumericLimits<float>::Max();
		for (int32 Index = 0; Index < Slots.Num(); ++Index)
		{
			if (Taken.IsValidIndex(Index) && Taken[Index])
			{
				continue;
			}
			const float Walk = FVector::Dist2D(From, Slots[Index]);
			float Score = -Walk;
			if (bAnyTaken)
			{
				float Gap = 360.f;
				for (int32 Other = 0; Other < Slots.Num(); ++Other)
				{
					if (Taken.IsValidIndex(Other) && Taken[Other])
					{
						Gap = FMath::Min(Gap, FMath::Abs(FMath::FindDeltaAngleDegrees(Bearing(Slots[Index]), Bearing(Slots[Other]))));
					}
				}
				Score = Gap - Walk * WalkWeightPerCm;
			}
			if (Score > BestScore + KINDA_SMALL_NUMBER)
			{
				BestScore = Score;
				Best = Index;
			}
		}
		return Best;
	}

	float FaceYaw(const FVector& From, const FVector& To)
	{
		return FMath::RadiansToDegrees(FMath::Atan2(To.Y - From.Y, To.X - From.X));
	}

	bool UsesFemaleClips(const FString& MeshName, const FString& SkeletonName)
	{
		return MeshName.Contains(TEXT("Female")) || SkeletonName.Contains(TEXT("Female"));
	}

	EWarmClipStep NextClipStep(EWarmClipStep Current, int32 LoopsDone, int32 LoopsBeforeSwitch, bool bHasLoopB)
	{
		const bool bSwitch = bHasLoopB && LoopsDone >= FMath::Max(1, LoopsBeforeSwitch);
		switch (Current)
		{
		case EWarmClipStep::Start:
			return EWarmClipStep::LoopA;
		case EWarmClipStep::LoopA:
			return bSwitch ? EWarmClipStep::TransitAB : EWarmClipStep::LoopA;
		case EWarmClipStep::TransitAB:
			return EWarmClipStep::LoopB;
		case EWarmClipStep::LoopB:
			return LoopsDone >= FMath::Max(1, LoopsBeforeSwitch) ? EWarmClipStep::TransitBA : EWarmClipStep::LoopB;
		case EWarmClipStep::TransitBA:
			return EWarmClipStep::LoopA;
		default:
			return EWarmClipStep::None;
		}
	}

	EWarmExitStyle GetExitStyle(EWarmExitReason Reason, bool bHasEndClip)
	{
		switch (Reason)
		{
		case EWarmExitReason::Moved:
		case EWarmExitReason::Threat:
			return EWarmExitStyle::QuickBlend;
		case EWarmExitReason::BodyTaken:
			return EWarmExitStyle::Cut;
		default:
			return bHasEndClip ? EWarmExitStyle::PlayEnd : EWarmExitStyle::QuickBlend;
		}
	}
}
