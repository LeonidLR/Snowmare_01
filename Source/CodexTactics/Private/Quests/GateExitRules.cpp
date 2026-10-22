#include "Quests/GateExitRules.h"

bool GateExitRules::ParseSide(const FString& Text, bool& bOutEnabled, EGateExitSide& OutSide)
{
	const FString Value = Text.TrimStartAndEnd().ToLower();
	if (Value == TEXT("off") || Value.IsEmpty())
	{
		bOutEnabled = false;
		OutSide = EGateExitSide::Auto;
		return true;
	}
	bOutEnabled = true;
	if (Value == TEXT("auto") || Value == TEXT("on"))
	{
		OutSide = EGateExitSide::Auto;
		return true;
	}
	if (Value == TEXT("+x") || Value == TEXT("x"))
	{
		OutSide = EGateExitSide::PositiveX;
		return true;
	}
	if (Value == TEXT("-x"))
	{
		OutSide = EGateExitSide::NegativeX;
		return true;
	}
	bOutEnabled = false;
	return false;
}

FString GateExitRules::SideToString(bool bEnabled, EGateExitSide Side)
{
	if (!bEnabled)
	{
		return TEXT("off");
	}
	return Side == EGateExitSide::PositiveX ? TEXT("+x") : (Side == EGateExitSide::NegativeX ? TEXT("-x") : TEXT("auto"));
}

int32 GateExitRules::ResolveOutwardSign(EGateExitSide Side, const FTransform& GateTransform, const FVector* InsideReference)
{
	if (Side == EGateExitSide::PositiveX)
	{
		return 1;
	}
	if (Side == EGateExitSide::NegativeX)
	{
		return -1;
	}
	if (InsideReference)
	{
		const FVector Axis = GateTransform.GetUnitAxis(EAxis::X).GetSafeNormal2D();
		const FVector Offset = *InsideReference - GateTransform.GetLocation();
		const float Along = FVector::DotProduct(FVector(Offset.X, Offset.Y, 0.f), Axis);
		if (FMath::Abs(Along) > 1.f)
		{
			return Along > 0.f ? -1 : 1; // the reference stands inside: out is the other side
		}
	}
	return 1;
}

FGateExitLine GateExitRules::MakeLine(const FTransform& GateTransform, float HalfWidthCm, int32 OutwardSign)
{
	FGateExitLine Line;
	Line.Origin = GateTransform.GetLocation();
	FVector Axis = GateTransform.GetUnitAxis(EAxis::X).GetSafeNormal2D();
	if (Axis.IsNearlyZero())
	{
		Axis = FVector::ForwardVector;
	}
	Line.Outward = Axis * (OutwardSign < 0 ? -1.f : 1.f);
	Line.Along = FVector(-Line.Outward.Y, Line.Outward.X, 0.f);
	Line.HalfWidthCm = FMath::Max(0.f, HalfWidthCm);
	return Line;
}

float GateExitRules::SignedDistance(const FGateExitLine& Line, const FVector& Point)
{
	const FVector Offset(Point.X - Line.Origin.X, Point.Y - Line.Origin.Y, 0.f);
	return FVector::DotProduct(Offset, Line.Outward);
}

bool GateExitRules::IsInExitZone(const FGateExitLine& Line, const FGateExitConfig& Config, const FVector& Point)
{
	const float Depth = SignedDistance(Line, Point);
	const FVector Offset(Point.X - Line.Origin.X, Point.Y - Line.Origin.Y, 0.f);
	const float Lateral = FMath::Abs(FVector::DotProduct(Offset, Line.Along));
	return Depth >= Config.CrossDepthCm && Depth <= FMath::Max(Config.ZoneDepthCm, Config.CrossDepthCm) && Lateral <= Line.HalfWidthCm + Config.LateralSlackCm;
}

bool GateExitRules::UpdateCrossed(bool bWasCrossed, const FGateExitLine& Line, const FGateExitConfig& Config, const FVector& Point)
{
	if (SignedDistance(Line, Point) < 0.f)
	{
		return false; // back inside
	}
	return bWasCrossed || IsInExitZone(Line, Config, Point);
}

bool GateExitRules::IsArmed(bool bEnabled, bool bGateFullyOpen, bool bMissionFailed, bool bMissionComplete)
{
	return bEnabled && bGateFullyOpen && !bMissionFailed && !bMissionComplete;
}

int32 GateExitRules::CountRequired(TConstArrayView<FGateExitMember> Members)
{
	int32 Count = 0;
	for (const FGateExitMember& Member : Members)
	{
		Count += Member.bAlive ? 1 : 0;
	}
	return Count;
}

int32 GateExitRules::CountCrossed(TConstArrayView<FGateExitMember> Members)
{
	int32 Count = 0;
	for (const FGateExitMember& Member : Members)
	{
		Count += Member.bAlive && Member.bCrossed ? 1 : 0;
	}
	return Count;
}

bool GateExitRules::ShouldEndLevel(TConstArrayView<FGateExitMember> Members)
{
	const int32 Required = CountRequired(Members);
	return Required > 0 && CountCrossed(Members) == Required;
}

float GateExitRules::FadeAlpha(float ElapsedSeconds, const FGateExitConfig& Config)
{
	const float T = FMath::Clamp(ElapsedSeconds / FMath::Max(0.1f, Config.FadeSeconds), 0.f, 1.f);
	return FMath::SmoothStep(0.f, 1.f, T);
}

float GateExitRules::TitleAlpha(float ElapsedSeconds, const FGateExitConfig& Config)
{
	const float SinceBlack = ElapsedSeconds - FMath::Max(0.1f, Config.FadeSeconds);
	if (SinceBlack <= 0.f)
	{
		return 0.f;
	}
	return Config.TitleFadeSeconds <= 0.f ? 1.f : FMath::Clamp(SinceBlack / Config.TitleFadeSeconds, 0.f, 1.f);
}

bool GateExitRules::ShouldReturnToMenu(float ElapsedSeconds, const FGateExitConfig& Config, bool bHeadless)
{
	return !bHeadless && Config.ReturnToMenuSeconds > 0.f && ElapsedSeconds >= FMath::Max(0.1f, Config.FadeSeconds) + Config.ReturnToMenuSeconds;
}

bool GateExitRules::ShouldLeaveLevel(float ElapsedSeconds, const FGateExitConfig& Config, bool bHasNextLevel, bool bHeadless)
{
	if (!bHasNextLevel)
	{
		return ShouldReturnToMenu(ElapsedSeconds, Config, bHeadless);
	}
	return !bHeadless && Config.NextLevelSeconds > 0.f && ElapsedSeconds >= FMath::Max(0.1f, Config.FadeSeconds) + Config.NextLevelSeconds;
}
