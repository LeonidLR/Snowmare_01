#pragma once

#include "CoreMinimal.h"
#include "Characters/CorpseRules.h"
#include "Combat/DeathRagdollTypes.h"
#include "Data/CombatTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "CorpseSubsystem.generated.h"

class AActor;
class ACharacter;
class ICorpseBody;
class UProceduralMeshComponent;
class USkeletalMeshComponent;

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
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** Loads death_ragdoll.json configuration once per world */
	void LoadRagdollConfig();

	/** Returns cached death ragdoll configuration */
	const FDeathRagdollConfig& GetRagdollConfig() const { return RagdollConfig; }

	/** True if ragdoll config loaded successfully and is enabled */
	bool IsRagdollEnabled() const;

	/** Number of currently simulating ragdoll corpses */
	int32 GetActiveRagdollCount() const;

	/** Attempts to activate death ragdoll on a dying character */
	bool TryActivateDeathRagdoll(
		ACharacter* Character,
		const FVector& HitDirection,
		float DamageAmount,
		EDamageType DamageType,
		FName ArchetypeId);

	/**
	 * The ragdoll's pelvis bone: 'pelvis' when the skeleton has it, else the physics-asset body nearest the root (the hound's
	 * skeleton has no 'pelvis'). NAME_None without a physics asset body.
	 */
	static FName ResolvePelvisBone(const USkeletalMeshComponent* Mesh);

	/**
	 * World Z of the lowest point of the pelvis collision body (physics-asset shapes at the bone's current transform, so it also
	 * works on a frozen corpse whose bodies are gone). False without that body.
	 */
	static bool GetPelvisBodyBottomZ(const USkeletalMeshComponent* Mesh, FName PelvisBone, float& OutBottomZ);

	/**
	 * Floor under a corpse point: the first WorldStatic / WorldDynamic surface a ragdoll can rest on (physics collision, blocks
	 * PhysicsBody; never a pawn capsule, another ragdoll or an overlap volume) that FDeathRagdollRules::IsCorpseFloorHit accepts,
	 * traced FloorTraceUpCm above to FloorTraceDownCm below From.
	 */
	static bool TraceCorpseFloorZ(const UWorld* World, const FVector& From, const AActor* Ignore, const FDeathRagdollConfig& Config, float& OutFloorZ);

	/** Registers an actor whose ragdoll is active */
	bool RegisterRagdollCorpse(AActor* Body, EDamageType DamageType = EDamageType::Kinetic);

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
		bool bIsRagdoll = false;
		bool bPlayerTeam = false;
		EDamageType DamageType = EDamageType::Kinetic;
		/**
		 * Settled ragdoll pose copied once into a procedural mesh. The skeletal mesh stays registered and visible
		 * (persist / pelvis queries) but stops drawing, so the game thread does not skin it every frame.
		 */
		TWeakObjectPtr<UProceduralMeshComponent> PoseMesh;
	};

	/** The body's corpse interface (null: gone / not one). */
	static ICorpseBody* AsCorpse(AActor* Body);
	static const ICorpseBody* AsCorpse(const AActor* Body);

	void UpdateSettling(double Now, const FCorpseConfig& Config);
	void ApplyCap(const FCorpseConfig& Config, double Now);
	void UpdateFades(double Now, const FCorpseConfig& Config);

	TArray<FKeptCorpse> Corpses;
	FDeathRagdollConfig RagdollConfig;
	bool bRagdollConfigLoaded = false;
	bool bRagdollConfigAttempted = false;
	int32 FadedTotal = 0;
	float CapTimer = 0.f;
};
