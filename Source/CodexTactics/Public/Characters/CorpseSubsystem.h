#pragma once

#include "CoreMinimal.h"
#include "Characters/CorpseRules.h"
#include "Subsystems/WorldSubsystem.h"
#include "CorpseSubsystem.generated.h"

class AActor;
class ICorpseBody;

/**
 * Corpses stay (game designer 2026-11-08: "corpses of ALL enemies, not only PMC, must stay on the ground"; 2026-11-09: "every enemy body
 * stays, ALL enemies, one cap"; UE-only). Every dead body that implements ICorpseBody registers at its death instead of the old SetLifeSpan
 * of the death decay delay: every monster (AEnemyCharacter::HandleDied - a body without a death pose too, laid on the ground) and every
 * hostile PMC fighter (AOperativeCharacter::HandleDied; never the player's own squad's KIA - CorpseRules::IsKeptUnderCap). Once its death
 * pose has settled and it lies on the ground (CorpseRules::ShouldFreeze: a body killed in the air / on a ladder falls first) the body is
 * frozen (ICorpseBody::FreezeCorpse: no AI controller, no actor / movement / anim tick, no collision, still rendered). Above the level's ONE
 * cap ("corpse_cap", default 150, monsters and PMC together) the oldest body farthest from the squad (out of the camera view when possible;
 * never one in view that still holds unsearched loot) is scaled down over FadeSeconds and destroyed (CorpseRules::PickCorpseToFade).
 * Bodies are not saved: a load starts without them (the save restores living enemies only). Console: Codex.Corpse.Persist / Cap /
 * FadeSeconds / OldestPool / Freeze, CodexTactics.Corpse.Status.
 */
UCLASS()
class CODEXTACTICS_API UCorpseSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** The rules in force (Codex.Corpse.* over the level's corpse_cap). */
	FCorpseConfig GetConfig() const;

	/**
	 * A body to keep (AEnemyCharacter / AOperativeCharacter::HandleDied); it must implement ICorpseBody. False when bodies do not stay
	 * (Codex.Corpse.Persist 0) or it is no ICorpseBody: the caller decides (an enemy sets its life span, a PMC body simply stays unmanaged).
	 */
	bool RegisterCorpse(AActor* Body);

	/** Bodies kept now (settling + frozen + fading). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Corpse")
	int32 GetCorpseCount() const { return Corpses.Num(); }

	/** Bodies frozen now. */
	int32 GetFrozenCount() const;
	/** Bodies fading out now. */
	int32 GetFadingCount() const;
	/** Bodies faded out (removed by the cap) in this world. */
	int32 GetFadedTotal() const { return FadedTotal; }
	/** The kept bodies (smokes). */
	TArray<AActor*> GetCorpses() const;
	/** The kept bodies of one class (smokes: the enemies, the PMC fighters). */
	template <typename T>
	TArray<T*> GetCorpsesOf() const
	{
		TArray<T*> Out;
		for (AActor* Body : GetCorpses())
		{
			if (T* Typed = Cast<T>(Body))
			{
				Out.Add(Typed);
			}
		}
		return Out;
	}
	/** A body is kept here / fading. */
	bool IsKept(const AActor* Body) const;
	bool IsFading(const AActor* Body) const;

private:
	struct FKeptCorpse
	{
		TWeakObjectPtr<AActor> Body;
		double DeathTime = 0.0;
		/** World time the death pose was first seen settled (-1 = not yet). */
		double SettledTime = -1.0;
		bool bFrozen = false;
		/** World time the fade began (-1 = not fading); the mesh's scale then. */
		double FadeStart = -1.0;
		FVector BaseScale = FVector::OneVector;
	};

	/** The body's corpse interface (null: gone / not one). */
	static ICorpseBody* AsCorpse(AActor* Body);
	static const ICorpseBody* AsCorpse(const AActor* Body);

	void UpdateSettling(double Now, const FCorpseConfig& Config);
	void ApplyCap(const FCorpseConfig& Config, double Now);
	void UpdateFades(double Now, const FCorpseConfig& Config);

	TArray<FKeptCorpse> Corpses;
	int32 FadedTotal = 0;
	float CapTimer = 0.f;
};
