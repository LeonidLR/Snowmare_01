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
		if (!bEligible || bFullBodyMontage)
		{
			return EWeaponLayerMask::Off;
		}
		return bUpperBodyMontage ? EWeaponLayerMask::LowerBody : EWeaponLayerMask::Full;
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

	float ShellReloadRate(float ClipLength, float ShellStart, float ShellEnd, float StepSeconds, float& OutStart, float& OutEnd)
	{
		OutStart = 0.f;
		OutEnd = FMath::Max(0.f, ClipLength);
		if (ShellEnd > ShellStart && ShellStart >= 0.f && ShellEnd <= ClipLength + KINDA_SMALL_NUMBER)
		{
			OutStart = ShellStart;
			OutEnd = ShellEnd;
		}
		const float Section = OutEnd - OutStart;
		return Section > KINDA_SMALL_NUMBER && StepSeconds > KINDA_SMALL_NUMBER ? Section / StepSeconds : 1.f;
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
}
