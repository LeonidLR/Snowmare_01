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

	EWeaponLayerMask ResolveMask(bool bEligible, bool bFullBodyMontage, bool bUpperBodyMontage)
	{
		// The layer drives the upper body only, so an upper-body montage (which owns the torso) leaves nothing for it to drive.
		return bEligible && !bFullBodyMontage && !bUpperBodyMontage ? EWeaponLayerMask::Full : EWeaponLayerMask::Off;
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
}
