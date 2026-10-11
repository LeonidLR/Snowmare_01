#include "Characters/OperativeHitRules.h"

bool OperativeHitRules::CanDodge(EOperativeHitKind Kind)
{
	return Kind != EOperativeHitKind::Unavoidable;
}

bool OperativeHitRules::UsesCover(EOperativeHitKind Kind)
{
	return Kind == EOperativeHitKind::Attack;
}

bool OperativeHitRules::UsesStanceDefense(EOperativeHitKind Kind)
{
	return Kind == EOperativeHitKind::Attack;
}

bool OperativeHitRules::UsesFortitude(EOperativeHitKind Kind)
{
	return Kind != EOperativeHitKind::Unavoidable;
}

bool OperativeHitRules::AllowsHeavyHitKnockdown(EOperativeHitKind Kind)
{
	return Kind == EOperativeHitKind::Attack;
}

bool OperativeHitRules::IsDodged(EOperativeHitKind Kind, float DodgeRoll, float Luck)
{
	return CanDodge(Kind) && DodgeRoll < Luck * 0.4f;
}

float OperativeHitRules::FortitudeCut(float Fortitude)
{
	return FMath::Clamp(Fortitude * 0.015f, 0.f, 0.5f);
}

float OperativeHitRules::FinalDamage(EOperativeHitKind Kind, float AmountAfterCover, float StanceDefense, float Fortitude, float KnockdownScale)
{
	if (Kind == EOperativeHitKind::Unavoidable)
	{
		// Whole numbers stay whole on the grid path (KnockdownRules::DownedDamageMultiplier, user decision 2026-10-11).
		return FMath::Max(1.f, KnockdownScale != 1.f ? FMath::RoundToFloat(AmountAfterCover * KnockdownScale) : AmountAfterCover);
	}
	const float Stance = UsesStanceDefense(Kind) ? StanceDefense : 1.f;
	const float Fort = UsesFortitude(Kind) ? 1.f - FortitudeCut(Fortitude) : 1.f;
	return FMath::Max(1.f, AmountAfterCover * Stance * Fort * KnockdownScale);
}
