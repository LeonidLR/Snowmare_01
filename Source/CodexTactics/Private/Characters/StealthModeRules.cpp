#include "Characters/StealthModeRules.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace StealthModeRules
{
	bool EnableForSelection(int32 SelectedCount, int32 StealthedCount)
	{
		return SelectedCount > 0 && StealthedCount < SelectedCount;
	}

	bool IsBlockedByFormation(bool bSoloOrHolding, bool bFormationActive, bool bGuarding)
	{
		return !bSoloOrHolding && bFormationActive && !bGuarding;
	}

	FText GetFormationBlockedTooltip()
	{
		return NSLOCTEXT("StealthModeRules", "FormationBlockedTip", "Stealth is only available in Solo (or while guarding / holding position).");
	}

	bool ShouldBreak(bool bActive, bool bFired, bool bSprinting, const FStealthModeConfig& Config)
	{
		return bActive && (bSprinting || (bFired && Config.bBreakOnFire));
	}

	float DetectionRangeScale(bool bActive, const FStealthModeConfig& Config)
	{
		return DetectionRangeScale(bActive, false, Config);
	}

	float DetectionRangeScale(bool bActive, bool bFast, const FStealthModeConfig& Config)
	{
		if (!bActive || !Config.bEnabled)
		{
			return 1.f;
		}
		const float Raw = bFast ? Config.FastDetectionRangeMultiplier : Config.DetectionRangeMultiplier;
		return FMath::Clamp(Raw, 0.f, 1.f);
	}

	float DetectionBuildupScale(bool bActive, const FStealthModeConfig& Config)
	{
		return DetectionBuildupScale(bActive, false, Config);
	}

	float DetectionBuildupScale(bool bActive, bool bFast, const FStealthModeConfig& Config)
	{
		if (!bActive || !Config.bEnabled)
		{
			return 1.f;
		}
		const float Raw = bFast ? Config.FastDetectionBuildupMultiplier : Config.DetectionBuildupMultiplier;
		return FMath::Clamp(Raw, 0.f, 1.f);
	}

	float FootstepNoiseScale(bool bActive, const FStealthModeConfig& Config)
	{
		return FootstepNoiseScale(bActive, false, Config);
	}

	float FootstepNoiseScale(bool bActive, bool bFast, const FStealthModeConfig& Config)
	{
		if (!bActive || !Config.bEnabled || bFast)
		{
			return 1.f;
		}
		return FMath::Clamp(Config.FootstepNoiseMultiplier, 0.f, 1.f);
	}

	bool IsInLookLockFov(const FVector& ObserverLocation, float HeadingYawDeg, const FVector& CandidateLocation,
		float FovDeg, float MinDistanceCm)
	{
		const FVector2D Delta(CandidateLocation.X - ObserverLocation.X, CandidateLocation.Y - ObserverLocation.Y);
		const float MinDistSq = FMath::Square(FMath::Max(0.f, MinDistanceCm));
		if (Delta.SizeSquared() <= MinDistSq)
		{
			return false;
		}
		const float TargetYaw = FMath::RadiansToDegrees(FMath::Atan2(Delta.Y, Delta.X));
		const float AngleError = FMath::Abs(FRotator::NormalizeAxis(TargetYaw - HeadingYawDeg));
		return AngleError <= FMath::Clamp(FovDeg, 0.f, 180.f);
	}

	void UpdateLookLock(FStealthLookLockState& State, const FVector& ObserverLocation, float HeadingYawDeg,
		TArrayView<const FStealthLookLockCandidate> Candidates, float DeltaSeconds, const FStealthModeConfig& Config,
		float MinDistanceCm)
	{
		if (!Config.bEnabled)
		{
			State.Reset();
			return;
		}
		const FStealthLookLockCandidate* Current = nullptr;
		const FStealthLookLockCandidate* BestVisibleInFov = nullptr;
		float BestVisibleDist = TNumericLimits<float>::Max();
		for (const FStealthLookLockCandidate& Candidate : Candidates)
		{
			if (Candidate.TargetId == 0)
			{
				continue;
			}
			if (Candidate.TargetId == State.TargetId)
			{
				Current = &Candidate;
			}
			if (Candidate.bVisible && IsInLookLockFov(ObserverLocation, HeadingYawDeg, Candidate.Location, Config.LookLockFovDeg, MinDistanceCm))
			{
				const float Dist = FVector::Dist2D(ObserverLocation, Candidate.Location);
				if (Dist < BestVisibleDist)
				{
					BestVisibleDist = Dist;
					BestVisibleInFov = &Candidate;
				}
			}
		}

		const float SwitchRatio = FMath::Clamp(Config.LookLockSwitchRatio, 0.f, 1.f);
		if (Current && Current->bVisible && IsInLookLockFov(ObserverLocation, HeadingYawDeg, Current->Location, Config.LookLockFovDeg, MinDistanceCm))
		{
			State.LastKnownLocation = Current->Location;
			State.TimeSinceSeenSeconds = 0.f;
			const float CurrentDist = FVector::Dist2D(ObserverLocation, Current->Location);
			if (BestVisibleInFov && BestVisibleInFov->TargetId != State.TargetId && BestVisibleDist < CurrentDist * SwitchRatio)
			{
				State.TargetId = BestVisibleInFov->TargetId;
				State.LastKnownLocation = BestVisibleInFov->Location;
			}
			return;
		}

		if (State.TargetId != 0)
		{
			State.TimeSinceSeenSeconds += FMath::Max(0.f, DeltaSeconds);
			if (Current && State.TimeSinceSeenSeconds <= FMath::Max(0.f, Config.LookLockKeepSeconds))
			{
				const float LastKnownDist = FVector::Dist2D(ObserverLocation, State.LastKnownLocation);
				if (BestVisibleInFov && BestVisibleInFov->TargetId != State.TargetId && BestVisibleDist < LastKnownDist * SwitchRatio)
				{
					State.TargetId = BestVisibleInFov->TargetId;
					State.LastKnownLocation = BestVisibleInFov->Location;
					State.TimeSinceSeenSeconds = 0.f;
				}
				return;
			}
			State.Reset();
		}

		if (BestVisibleInFov)
		{
			State.TargetId = BestVisibleInFov->TargetId;
			State.LastKnownLocation = BestVisibleInFov->Location;
			State.TimeSinceSeenSeconds = 0.f;
		}
	}

	FStealthModeConfig FromJson(const FJsonObject& Json, const FStealthModeConfig& Base)
	{
		FStealthModeConfig Config = Base;
		Json.TryGetBoolField(TEXT("enabled"), Config.bEnabled);
		Json.TryGetBoolField(TEXT("stealth_break_on_fire"), Config.bBreakOnFire);
		const auto ReadClamped = [&Json](const TCHAR* Key, float& Value, float MinVal, float MaxVal)
		{
			double Read = 0.0;
			if (Json.TryGetNumberField(Key, Read) && FMath::IsFinite(Read))
			{
				Value = FMath::Clamp(static_cast<float>(Read), MinVal, MaxVal);
			}
		};
		ReadClamped(TEXT("detection_range_multiplier"), Config.DetectionRangeMultiplier, 0.f, 1.f);
		ReadClamped(TEXT("detection_buildup_multiplier"), Config.DetectionBuildupMultiplier, 0.f, 1.f);
		ReadClamped(TEXT("footstep_noise_multiplier"), Config.FootstepNoiseMultiplier, 0.f, 1.f);
		ReadClamped(TEXT("visual_opacity"), Config.VisualOpacity, 0.f, 1.f);
		ReadClamped(TEXT("visual_gray"), Config.VisualGray, 0.f, 1.f);
		ReadClamped(TEXT("fast_speed"), Config.FastSpeedCm, 0.f, 2000.f);
		ReadClamped(TEXT("fast_detection_range_mult"), Config.FastDetectionRangeMultiplier, 0.f, 1.f);
		ReadClamped(TEXT("fast_buildup_mult"), Config.FastDetectionBuildupMultiplier, 0.f, 1.f);
		ReadClamped(TEXT("look_lock_keep_s"), Config.LookLockKeepSeconds, 0.f, 30.f);
		ReadClamped(TEXT("look_lock_fov_deg"), Config.LookLockFovDeg, 0.f, 180.f);
		ReadClamped(TEXT("look_lock_switch_ratio"), Config.LookLockSwitchRatio, 0.f, 1.f);
		return Config;
	}

	FString GetDefaultPath()
	{
		return FPaths::ProjectContentDir() / TEXT("Data/AI/stealth_mode.json");
	}

	const FStealthModeConfig& GetConfig()
	{
		static const FStealthModeConfig Config = []
		{
			FString Text;
			TSharedPtr<FJsonObject> Json;
			return FFileHelper::LoadFileToString(Text, *GetDefaultPath())
				&& FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) && Json.IsValid()
				? FromJson(*Json) : FStealthModeConfig();
		}();
		return Config;
	}
}
