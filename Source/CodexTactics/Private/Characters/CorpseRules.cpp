#include "Characters/CorpseRules.h"

namespace CorpseRules
{
	bool ShouldFreeze(float SinceDeathSeconds, float SinceSettledSeconds, bool bFalling, bool bOnLadder, const FCorpseConfig& Config)
	{
		if (bFalling || bOnLadder)
		{
			return SinceDeathSeconds >= Config.MaxSettleSeconds * 2.f; // stuck in the fall: freeze where it hangs rather than tick forever
		}
		if (SinceDeathSeconds >= Config.MaxSettleSeconds)
		{
			return true;
		}
		return SinceDeathSeconds >= Config.MinSettleSeconds && SinceSettledSeconds >= Config.SettleHoldSeconds;
	}

	int32 CountOverCap(const TArray<FCorpseCandidate>& Corpses, int32 Cap)
	{
		int32 Kept = 0;
		for (const FCorpseCandidate& Corpse : Corpses)
		{
			Kept += Corpse.bFading ? 0 : 1;
		}
		return FMath::Max(0, Kept - FMath::Max(Cap, 1));
	}

	int32 PickCorpseToFade(const TArray<FCorpseCandidate>& Corpses, const FCorpseConfig& Config)
	{
		TArray<int32> Pool;
		bool bAnyHidden = false;
		for (int32 Index = 0; Index < Corpses.Num(); ++Index)
		{
			const FCorpseCandidate& Corpse = Corpses[Index];
			if (Corpse.bFrozen && !Corpse.bFading)
			{
				Pool.Add(Index);
				bAnyHidden |= !Corpse.bInView;
			}
		}
		if (bAnyHidden)
		{
			Pool.RemoveAll([&Corpses](int32 Index) { return Corpses[Index].bInView; }); // never in the camera view if avoidable
		}
		if (Pool.IsEmpty())
		{
			return INDEX_NONE;
		}
		Pool.StableSort([&Corpses](int32 A, int32 B) { return Corpses[A].DeathTime < Corpses[B].DeathTime; });
		const int32 Oldest = FMath::Clamp(Config.OldestPool, 1, Pool.Num());
		int32 Best = Pool[0];
		for (int32 Rank = 1; Rank < Oldest; ++Rank)
		{
			if (Corpses[Pool[Rank]].SquadDistanceCm > Corpses[Best].SquadDistanceCm)
			{
				Best = Pool[Rank]; // strictly farther: a tie keeps the older
			}
		}
		return Best;
	}

	float FadeScale(float ElapsedSeconds, float FadeSeconds)
	{
		if (FadeSeconds <= 0.f || ElapsedSeconds >= FadeSeconds)
		{
			return 0.f;
		}
		const float Alpha = FMath::Clamp(ElapsedSeconds / FadeSeconds, 0.f, 1.f);
		return 1.f - FMath::SmoothStep(0.f, 1.f, Alpha);
	}

	int32 ResolveCap(int32 LevelCap, int32 ConsoleCap)
	{
		return ConsoleCap >= 1 ? ConsoleCap : FMath::Max(LevelCap, 1);
	}
}
