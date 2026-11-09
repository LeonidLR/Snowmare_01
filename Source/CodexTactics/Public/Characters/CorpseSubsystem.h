#pragma once

#include "CoreMinimal.h"
#include "Characters/CorpseRules.h"
#include "Subsystems/WorldSubsystem.h"
#include "CorpseSubsystem.generated.h"

class AEnemyCharacter;

/**
 * Corpses stay (game designer 2026-11-08: "corpses of ALL enemies, not only PMC, must stay on the ground"; UE-only). Every enemy body with
 * a death pose registers at its death (AEnemyCharacter::HandleDied, instead of the old SetLifeSpan of the death decay delay); once its death
 * clip has settled and it lies on the ground (CorpseRules::ShouldFreeze: a body killed in the air / on a ladder falls first) the body is
 * frozen (AEnemyCharacter::FreezeCorpse: no AI controller, no actor / movement / anim tick, out of the enemy registry, no collision, still
 * rendered). Above the level's cap ("corpse_cap", default 150) the oldest body farthest from the squad (out of the camera view when
 * possible) is scaled down over FadeSeconds and destroyed (CorpseRules::PickCorpseToFade). Hostile PMC fighters' bodies are
 * AOperativeCharacter corpses (their own death pose) and are not managed here. Bodies are not saved: a load starts without them (the
 * save restores living enemies only). Console: Codex.Corpse.Persist / Cap / FadeSeconds / OldestPool, CodexTactics.Corpse.Status.
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

	/** A body to keep (AEnemyCharacter::HandleDied). False when bodies do not stay (Codex.Corpse.Persist 0): the caller sets its life span. */
	bool RegisterCorpse(AEnemyCharacter* Body);

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
	TArray<AEnemyCharacter*> GetCorpses() const;
	/** A body is kept here / frozen / fading. */
	bool IsKept(const AEnemyCharacter* Body) const;
	bool IsFading(const AEnemyCharacter* Body) const;

private:
	struct FKeptCorpse
	{
		TWeakObjectPtr<AEnemyCharacter> Body;
		double DeathTime = 0.0;
		/** World time the death pose was first seen settled (-1 = not yet). */
		double SettledTime = -1.0;
		bool bFrozen = false;
		/** World time the fade began (-1 = not fading); the mesh's scale then. */
		double FadeStart = -1.0;
		FVector BaseScale = FVector::OneVector;
	};

	void UpdateSettling(double Now, const FCorpseConfig& Config);
	void ApplyCap(const FCorpseConfig& Config, double Now);
	void UpdateFades(double Now, const FCorpseConfig& Config);

	TArray<FKeptCorpse> Corpses;
	int32 FadedTotal = 0;
	float CapTimer = 0.f;
};
