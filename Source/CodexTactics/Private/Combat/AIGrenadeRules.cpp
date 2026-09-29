#include "Combat/AIGrenadeRules.h"
#include "Combat/GrenadeRules.h"
#include "Data/GodotBalanceAsset.h"

FAIGrenadeConfig AIGrenadeRules::ConfigFromBalance(const UGodotBalanceAsset* Balance)
{
	FAIGrenadeConfig Config;
	if (Balance)
	{
		Config.MinCluster = Balance->GetInt(TEXT("ai_grenade_min_cluster"), Config.MinCluster);
		Config.Cooldown = Balance->GetNumber(TEXT("ai_grenade_cooldown"), Config.Cooldown);
		Config.MinThrowerDistance = Balance->GetNumber(TEXT("ai_grenade_min_thrower_dist"), Config.MinThrowerDistance / 100.f) * 100.f;
		Config.FriendlyFireRadius = Balance->GetNumber(TEXT("ai_grenade_friendly_fire_radius"), Config.FriendlyFireRadius / 100.f) * 100.f;
	}
	return Config;
}

FAIGrenadeOpportunity AIGrenadeRules::Evaluate(const FAIGrenadeConfig& Config, const FVector& Thrower, EOperativeStance Stance,
	float ThrowRange, float BlastRadius, const TArray<FVector>& Enemies, const TArray<FVector>& Allies)
{
	FAIGrenadeOpportunity Best;
	const float MaxRange = GrenadeRules::EffectiveRange(ThrowRange > 0.f ? ThrowRange : 1200.f, Stance);
	const float Blast = BlastRadius > 0.f ? BlastRadius : 400.f;
	for (const FVector& Candidate : Enemies)
	{
		const float Distance = FVector::Dist(Thrower, Candidate);
		if (Distance > MaxRange || Distance < Config.MinThrowerDistance)
		{
			continue;
		}
		if (Allies.ContainsByPredicate([&](const FVector& Ally) { return FVector::Dist(Ally, Candidate) < Config.FriendlyFireRadius; }))
		{
			continue;
		}
		int32 Cluster = 0;
		for (const FVector& Other : Enemies)
		{
			Cluster += FVector::Dist(Candidate, Other) <= Blast ? 1 : 0;
		}
		if (Cluster >= Config.MinCluster && Cluster > Best.EnemyCount)
		{
			Best.bCanThrow = true;
			Best.Target = Candidate;
			Best.EnemyCount = Cluster;
		}
	}
	return Best;
}
