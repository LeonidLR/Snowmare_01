#include "Characters/WeaponAnimLayerRules.h"

namespace WeaponAnimLayerRules
{
	float DirectionAngle(EWeaponLayerDirection Direction)
	{
		const float Angle = 45.f * static_cast<float>(static_cast<uint8>(Direction));
		return Angle > 180.f ? Angle - 360.f : Angle;
	}

	FWeaponLayerDirectionBlend BlendDirections(float AngleDeg, const TStaticArray<bool, 8>& Available)
	{
		FWeaponLayerDirectionBlend Out;
		const float Angle = FMath::Fmod(FMath::Fmod(AngleDeg, 360.f) + 360.f, 360.f); // [0, 360)
		// The nearest available direction at or counter-clockwise of the angle (A) and at or clockwise of it (B).
		int32 Below = INDEX_NONE;
		float BelowGap = 0.f;
		int32 Above = INDEX_NONE;
		float AboveGap = 0.f;
		for (int32 Step = 0; Step < 8; ++Step)
		{
			const int32 Index = Step;
			if (!Available[Index])
			{
				continue;
			}
			const float DirAngle = 45.f * Index;
			const float Ccw = FMath::Fmod(Angle - DirAngle + 360.f, 360.f); // how far the angle is clockwise of this direction
			const float Cw = FMath::Fmod(DirAngle - Angle + 360.f, 360.f);
			if (Below == INDEX_NONE || Ccw < BelowGap)
			{
				Below = Index;
				BelowGap = Ccw;
			}
			if (Above == INDEX_NONE || Cw < AboveGap)
			{
				Above = Index;
				AboveGap = Cw;
			}
		}
		if (Below == INDEX_NONE)
		{
			return Out;
		}
		Out.A = static_cast<EWeaponLayerDirection>(Below);
		Out.B = static_cast<EWeaponLayerDirection>(Above);
		const float Span = BelowGap + AboveGap;
		Out.Alpha = Below == Above || Span <= KINDA_SMALL_NUMBER ? 0.f : BelowGap / Span;
		return Out;
	}

	EWeaponLayerMask ResolveMask(bool bEligible, bool bFullBodyMontage, bool bUpperBodyMontage, bool bFullBodySet)
	{
		if (!bEligible || bFullBodyMontage)
		{
			return EWeaponLayerMask::Off;
		}
		// An upper-body set leaves nothing to drive under an upper-body montage (it owns the torso); a full-body set keeps the legs.
		if (bUpperBodyMontage)
		{
			return bFullBodySet ? EWeaponLayerMask::LowerBody : EWeaponLayerMask::Off;
		}
		return EWeaponLayerMask::Full;
	}

	float BlendedNominalSpeed(const FWeaponLayerDirectionBlend& Blend, const TStaticArray<float, 8>& Speeds, float Fallback)
	{
		const float A = Speeds[static_cast<int32>(Blend.A)] > 0.f ? Speeds[static_cast<int32>(Blend.A)] : Fallback;
		const float B = Speeds[static_cast<int32>(Blend.B)] > 0.f ? Speeds[static_cast<int32>(Blend.B)] : Fallback;
		return FMath::Lerp(A, B, FMath::Clamp(Blend.Alpha, 0.f, 1.f));
	}

	float SyncedClipTime(float Phase, float Offset, float Length)
	{
		if (Length <= KINDA_SMALL_NUMBER)
		{
			return 0.f;
		}
		const float Wrapped = FMath::Fmod(FMath::Fmod(Phase + Offset, 1.f) + 1.f, 1.f);
		return FMath::Min(Wrapped * Length, Length - KINDA_SMALL_NUMBER);
	}

	float TurnClipAdvance(float YawDeltaDeg, float ClipTurnDeg, float ClipLength)
	{
		if (FMath::Abs(ClipTurnDeg) <= KINDA_SMALL_NUMBER || ClipLength <= KINDA_SMALL_NUMBER)
		{
			return 0.f;
		}
		return FMath::Abs(YawDeltaDeg) / FMath::Abs(ClipTurnDeg) * ClipLength;
	}

	float MoveWeight(float Speed, float StartSpeed, float FullSpeed)
	{
		if (FullSpeed <= StartSpeed)
		{
			return Speed > StartSpeed ? 1.f : 0.f;
		}
		return FMath::Clamp((Speed - StartSpeed) / (FullSpeed - StartSpeed), 0.f, 1.f);
	}

	float JogWeight(float Speed, float WalkSpeed, float JogSpeed)
	{
		return MoveWeight(Speed, WalkSpeed, JogSpeed);
	}

	float LoopPlayRate(float Speed, float NominalSpeed, float MinRate, float MaxRate)
	{
		if (NominalSpeed <= KINDA_SMALL_NUMBER)
		{
			return 1.f;
		}
		return FMath::Clamp(Speed / NominalSpeed, MinRate, MaxRate);
	}


	FString RetargetedClipPath(const FString& PackPath, const FString& Folder)
	{
		FString Package = PackPath.TrimStartAndEnd();
		FString Root = Folder.TrimStartAndEnd();
		Root.RemoveFromEnd(TEXT("/"));
		int32 Dot = INDEX_NONE;
		if (Package.FindLastChar(TEXT('.'), Dot))
		{
			Package.LeftInline(Dot);
		}
		int32 Slash = INDEX_NONE;
		if (Root.IsEmpty() || !Package.FindLastChar(TEXT('/'), Slash) || Slash + 1 >= Package.Len())
		{
			return FString();
		}
		const FString Name = Package.Mid(Slash + 1);
		return FString::Printf(TEXT("%s/%s.%s"), *Root, *Name, *Name);
	}

	float ShellLoopRate(float LoopStart, float LoopEnd, float StepSeconds, float MinRate, float MaxRate)
	{
		const float Segment = LoopEnd - LoopStart;
		if (Segment <= KINDA_SMALL_NUMBER || StepSeconds <= KINDA_SMALL_NUMBER)
		{
			return 1.f;
		}
		return FMath::Clamp(Segment / StepSeconds, MinRate, MaxRate);
	}

	FShellReloadClip SanitizeShellReloadClip(const FShellReloadClip& Clip)
	{
		FShellReloadClip Out = Clip;
		Out.Length = FMath::Max(0.f, Clip.Length);
		Out.LoopRate = Clip.LoopRate > KINDA_SMALL_NUMBER ? Clip.LoopRate : 1.f;
		if (!(Clip.LoopStart >= 0.f && Clip.LoopEnd > Clip.LoopStart + 0.02f && Clip.LoopEnd <= Out.Length + KINDA_SMALL_NUMBER))
		{
			Out.LoopStart = 0.f;
			Out.LoopEnd = Out.Length;
			Out.OutroStart = 0.f;
		}
		if (Out.OutroStart >= Out.Length || Out.OutroStart < Out.LoopEnd - KINDA_SMALL_NUMBER)
		{
			Out.OutroStart = 0.f; // no outro (it must start at or after the loop's end)
		}
		return Out;
	}

	void AdvanceShellReload(FShellReloadState& State, const FShellReloadClip& InClip, float DeltaSeconds, bool bReloading)
	{
		const FShellReloadClip Clip = SanitizeShellReloadClip(InClip);
		const float Segment = Clip.LoopEnd - Clip.LoopStart;
		if (Clip.Length <= KINDA_SMALL_NUMBER || Segment <= KINDA_SMALL_NUMBER)
		{
			State = FShellReloadState();
			return;
		}
		const float Dt = FMath::Max(0.f, DeltaSeconds);
		switch (State.Phase)
		{
		case EShellReloadPhase::None:
			if (!bReloading)
			{
				return;
			}
			State = FShellReloadState();
			State.Phase = Clip.LoopStart > KINDA_SMALL_NUMBER ? EShellReloadPhase::Intro : EShellReloadPhase::Loop;
			State.Time = Clip.LoopStart > KINDA_SMALL_NUMBER ? 0.f : Clip.LoopStart;
			AdvanceShellReload(State, InClip, Dt, bReloading);
			return;
		case EShellReloadPhase::Intro:
			State.Time += Dt;
			if (State.Time >= Clip.LoopStart)
			{
				const float Over = (State.Time - Clip.LoopStart) * Clip.LoopRate;
				State.Phase = bReloading ? EShellReloadPhase::Loop : EShellReloadPhase::Finishing;
				State.Time = Clip.LoopStart;
				AdvanceShellReload(State, InClip, Over / Clip.LoopRate, bReloading);
			}
			return;
		case EShellReloadPhase::Loop:
		case EShellReloadPhase::Finishing:
		{
			if (State.Phase == EShellReloadPhase::Loop && !bReloading)
			{
				State.Phase = EShellReloadPhase::Finishing;
			}
			else if (State.Phase == EShellReloadPhase::Finishing && bReloading)
			{
				State.Phase = EShellReloadPhase::Loop;
			}
			State.Time += Dt * Clip.LoopRate;
			if (State.Time < Clip.LoopEnd)
			{
				return;
			}
			const float Over = State.Time - Clip.LoopEnd;
			if (State.Phase == EShellReloadPhase::Loop)
			{
				++State.Loops;
				State.Time = Clip.LoopStart + FMath::Fmod(Over, Segment);
				return;
			}
			if (Clip.OutroStart <= 0.f)
			{
				State = FShellReloadState();
				return;
			}
			State.Phase = EShellReloadPhase::Outro;
			State.Time = Clip.OutroStart;
			AdvanceShellReload(State, InClip, Over / Clip.LoopRate, bReloading);
			return;
		}
		case EShellReloadPhase::Outro:
			if (bReloading)
			{
				State = FShellReloadState();
				AdvanceShellReload(State, InClip, Dt, bReloading);
				return;
			}
			State.Time += Dt;
			if (State.Time >= Clip.Length)
			{
				State = FShellReloadState();
			}
			return;
		default:
			State = FShellReloadState();
			return;
		}
	}

	TStaticArray<float, 9> AimPoseWeights(float YawDeg, float PitchDeg, float YawRangeDeg, float PitchRangeDeg, const TStaticArray<bool, 9>& Available)
	{
		TStaticArray<float, 9> Out;
		for (int32 Index = 0; Index < 9; ++Index)
		{
			Out[Index] = 0.f;
		}
		const float Y = YawRangeDeg > KINDA_SMALL_NUMBER ? FMath::Clamp(YawDeg / YawRangeDeg, -1.f, 1.f) : 0.f;
		const float P = PitchRangeDeg > KINDA_SMALL_NUMBER ? FMath::Clamp(PitchDeg / PitchRangeDeg, -1.f, 1.f) : 0.f;
		const float Cols[3] = { FMath::Max(0.f, -Y), 1.f - FMath::Abs(Y), FMath::Max(0.f, Y) };   // left, centre, right
		const float Rows[3] = { FMath::Max(0.f, P), 1.f - FMath::Abs(P), FMath::Max(0.f, -P) };   // up, level, down
		for (int32 Row = 0; Row < 3; ++Row)
		{
			for (int32 Col = 0; Col < 3; ++Col)
			{
				const int32 Index = Row * 3 + Col;
				const float Weight = Rows[Row] * Cols[Col];
				if (Weight <= 0.f)
				{
					continue;
				}
				if (Available[Index] || Index == static_cast<int32>(EWeaponAimPose::Center))
				{
					Out[Index] += Weight;
					continue;
				}
				// A missing corner = its row's edge pose + its column's edge pose (additive deltas sum up to the diagonal).
				if (Row != 1 && Col != 1)
				{
					const int32 RowEdge = Row * 3 + 1;
					const int32 ColEdge = 3 + Col;
					if (Available[RowEdge])
					{
						Out[RowEdge] += Weight;
					}
					if (Available[ColEdge])
					{
						Out[ColEdge] += Weight;
					}
				}
			}
		}
		return Out;
	}

	bool LauncherRoundVisible(bool bRoundInTube, bool bReloading, float ReloadProgress01, float InsertAt01)
	{
		if (bRoundInTube)
		{
			return true;
		}
		return bReloading && ReloadProgress01 >= FMath::Clamp(InsertAt01, 0.f, 1.f);
	}

	float LauncherReloadClipTime(float ElapsedSeconds, float TotalSeconds, float ClipLength, float LeadInSeconds)
	{
		if (ClipLength <= KINDA_SMALL_NUMBER || TotalSeconds <= KINDA_SMALL_NUMBER)
		{
			return -1.f;
		}
		const float LeadIn = FMath::Clamp(LeadInSeconds, 0.f, TotalSeconds * 0.5f);
		if (ElapsedSeconds < LeadIn)
		{
			return -1.f;
		}
		const float Rate = ClipLength / FMath::Max(0.05f, TotalSeconds - LeadIn);
		return FMath::Clamp((ElapsedSeconds - LeadIn) * Rate, 0.f, ClipLength);
	}

	float LauncherInsertFraction(float InsertSeconds, float ClipLength, float TotalSeconds, float LeadInSeconds)
	{
		if (ClipLength <= KINDA_SMALL_NUMBER || TotalSeconds <= KINDA_SMALL_NUMBER)
		{
			return 0.5f;
		}
		const float LeadIn = FMath::Clamp(LeadInSeconds, 0.f, TotalSeconds * 0.5f);
		const float Insert = FMath::Clamp(InsertSeconds, 0.f, ClipLength);
		return FMath::Clamp((LeadIn + Insert * (TotalSeconds - LeadIn) / ClipLength) / TotalSeconds, 0.f, 1.f);
	}

	float ForwardConeAngle(float AngleDeg, float ConeDeg)
	{
		const float Angle = FRotator::NormalizeAxis(AngleDeg);
		const float Cone = FMath::Min(ConeDeg, 89.f);
		const float Abs = FMath::Abs(Angle);
		if (Cone <= 0.f || Abs >= 90.f)
		{
			return Angle;
		}
		return FMath::Sign(Angle) * FMath::Max(0.f, Abs - Cone) * 90.f / (90.f - Cone);
	}

	ELauncherAction ResolveLauncherAction(float ShotSeconds, float ShotLength, float ReloadClipTime)
	{
		if (ShotSeconds >= 0.f && ShotSeconds < ShotLength)
		{
			return ELauncherAction::Shot;
		}
		return ReloadClipTime >= 0.f ? ELauncherAction::Reload : ELauncherAction::None;
	}

	float TurnInPlaceShare(float YawRateDegPerSec, float Speed, float MoveSpeed, float MinRate, float FullRate)
	{
		if (Speed >= MoveSpeed)
		{
			return 0.f;
		}
		const float Rate = FMath::Abs(YawRateDegPerSec);
		if (FullRate <= MinRate)
		{
			return Rate >= MinRate ? 1.f : 0.f;
		}
		return FMath::Clamp((Rate - MinRate) / (FullRate - MinRate), 0.f, 1.f);
	}

	float ForwardShare(const FWeaponLayerDirectionBlend& Blend)
	{
		const float Alpha = FMath::Clamp(Blend.Alpha, 0.f, 1.f);
		return (Blend.A == EWeaponLayerDirection::Forward ? 1.f - Alpha : 0.f) + (Blend.B == EWeaponLayerDirection::Forward ? Alpha : 0.f);
	}

	FWeaponLegsAbpGaits ParseLegsAbpGaits(const TArray<FString>& Names, TArray<FString>* OutUnknown)
	{
		FWeaponLegsAbpGaits Gaits;
		for (const FString& Raw : Names)
		{
			const FString Name = Raw.TrimStartAndEnd();
			if (Name.Equals(TEXT("run"), ESearchCase::IgnoreCase))
			{
				Gaits.bRun = true;
			}
			else if (Name.Equals(TEXT("sprint"), ESearchCase::IgnoreCase))
			{
				Gaits.bSprint = true;
			}
			else if (Name.Equals(TEXT("walk"), ESearchCase::IgnoreCase))
			{
				Gaits.bWalk = true;
			}
			else if (Name.Equals(TEXT("walk_forward"), ESearchCase::IgnoreCase))
			{
				Gaits.bWalkForward = true;
			}
			else if (OutUnknown)
			{
				OutUnknown->Add(Raw);
			}
		}
		return Gaits;
	}

	float LegsAbpWeight(const FWeaponLegsAbpGaits& Gaits, const FWeaponLegsAbpShares& Shares)
	{
		if (!Gaits.Any())
		{
			return 0.f;
		}
		auto Unit = [](float Value) { return FMath::Clamp(Value, 0.f, 1.f); };
		auto Walk = [&Gaits, &Unit](float Forward) { return Gaits.bWalk ? 1.f : (Gaits.bWalkForward ? Unit(Forward) : 0.f); };
		const float Sprint = Unit(Shares.Sprint);
		const float Aim = Unit(Shares.AimMove);
		const float Jog = Unit(Shares.Jog);
		const float Upright = (1.f - Aim) * (Jog * (Gaits.bRun ? 1.f : 0.f) + (1.f - Jog) * Walk(Shares.WalkForward)) + Aim * Walk(Shares.AimWalkForward);
		const float Gait = Sprint * (Gaits.bSprint ? 1.f : 0.f) + (1.f - Sprint) * Upright;
		return Unit(Unit(Shares.Move) * (1.f - Unit(Shares.Crouch)) * Gait);
	}
}
