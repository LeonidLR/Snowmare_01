#include "Characters/HitReactionRules.h"

bool HitReactionRules::ShouldReact(const FHitReactionSettings& Settings, const FHitReactionContext& Context)
{
	if (Context.bBusy || Context.Damage < Settings.MinDamage || Context.SecondsSinceLast < Settings.MinIntervalSeconds)
	{
		return false;
	}
	return !Context.bInCover || Settings.bAllowInCover;
}

bool HitReactionRules::IsFromBehind(const FVector& ActorForward, const FVector& ActorLocation, const FVector& SourceLocation)
{
	const FVector Forward = ActorForward.GetSafeNormal2D();
	const FVector ToSource = (SourceLocation - ActorLocation).GetSafeNormal2D();
	if (Forward.IsNearlyZero() || ToSource.IsNearlyZero())
	{
		return false;
	}
	return FVector::DotProduct(Forward, ToSource) < 0.f;
}
