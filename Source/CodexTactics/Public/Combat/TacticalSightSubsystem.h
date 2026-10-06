#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "TacticalSightSubsystem.generated.h"

class AActor;
class AEnemyCharacter;
class AEnemyGhostActor;
class AOperativeCharacter;
class UMaterialInterface;
class USkinnedMeshComponent;
enum class EVisibilityBasedAnimTickOption : uint8;

/** What the sight system did so far (smokes, the summary log). */
struct CODEXTACTICS_API FTacticalSightStats
{
	int32 Hidden = 0;
	int32 Revealed = 0;
	int32 GhostsSpawned = 0;
	int32 HeardGhosts = 0;
	int32 Demasks = 0;
	int32 PackAlerts = 0;
	int32 Forgotten = 0;
};

/**
 * Tactical line of sight (Sprint 08, TANDEM «SPRINT 08 DIRECTIVE»; UE-only, no Godot reference). In a wave fight
 * (real time and the tactical pause, not the turn-based grid fight, which keeps its own rules) every 0.2 s:
 * - squad -> enemies: an enemy is shown while any operative sees it — a ECC_Visibility trace from his eyes (stance
 *   height, SightRules) to the enemy's profile, pawns ignored, so a 60 cm barricade hides a prone target — or while it
 *   demasked itself by attacking (2 s). Otherwise its actor is hidden and a stasis silhouette (AEnemyGhostActor) stays
 *   at the last confirmed spot; within 12 m an unseen enemy is heard and its silhouette follows the sound.
 * - enemies -> squad (symmetric): an enemy perceives an operative it sees (same heights), hears (12 m, a crawling one
 *   5 m) or that demasked himself by firing; perceived positions are shared with pack mates within 15 m. An enemy
 *   pursues an unperceived operative's last known spot, searches there 5 s and forgets him (or after 20 s) — user decision
 *   2026-10-05. A new enemy knows where the squad stood when it appeared (the wave is sent at them).
 * Outside the fight, in the turn-based fight or with Codex.Sight 0 everything is shown and every enemy knows everything.
 */
UCLASS()
class CODEXTACTICS_API UTacticalSightSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual void Deinitialize() override;

	/** The occlusion rules apply now (wave fight, not turn-based, Codex.Sight 1). */
	bool IsActive() const;

	/** Runs an update now (smokes; the tick runs one every UpdateInterval). */
	void Refresh();

	/** The squad sees this enemy (always true while inactive). */
	bool IsVisibleToSquad(const AActor* Enemy) const;

	/**
	 * Where Enemy believes Operative is: his real position while it perceives him (or while the rules are inactive),
	 * else the last known spot. False when it does not know about him.
	 */
	bool GetBelief(const AEnemyCharacter* Enemy, const AActor* Operative, FVector& OutLocation, bool* bOutPerceived = nullptr) const;

	/** A shot / blow: the shooter is visible to everybody for SightRules::DemaskSeconds. */
	void NotifyFired(AActor* Shooter);
	bool IsDemasked(const AActor* Actor) const;

	/** The silhouette of Enemy, if it is hidden and has one. */
	AEnemyGhostActor* GetGhost(const AActor* Enemy) const;
	/** The silhouette closest to Point within RadiusCm (planar), for a click. */
	AEnemyGhostActor* FindGhostNear(const FVector& Point, float RadiusCm) const;

	/** Sight trace between two points, pawns ignored: true when nothing blocks. */
	bool HasClearSight(const FVector& From, const FVector& To) const;

	/** Observer eye / target profile points of a character (stance heights; enemies by their size or the marksman's stance). */
	static FVector EyePoint(const AActor& Actor);
	static FVector ProfilePoint(const AActor& Actor);

	const FTacticalSightStats& GetStats() const { return Stats; }

	/** Authored tick options of the meshes kept animating while their enemy is hidden. */
	using FSavedAnimTickOptions = TMap<TWeakObjectPtr<USkinnedMeshComponent>, EVisibilityBasedAnimTickOption>;

	/**
	 * Keeps a hidden enemy's mesh animating (user report 2026-10-06: enemies slid in the T-pose as silhouettes and
	 * only animated once seen). A hidden actor is not rendered, and a skinned mesh refreshes its bones only while
	 * rendered unless its option is AlwaysTickPoseAndRefreshBones (USkinnedMeshComponent::ShouldUpdateTransform), so a
	 * hidden enemy kept its last pose — its reference (T) pose if it was never on screen — which its silhouette copied
	 * and which it showed for a moment when it appeared. Hidden: the authored option is saved in Saved and the mesh
	 * evaluates its pose every frame; shown: the authored option is restored (it is rendered again, the pose stays
	 * current, and the cheaper option saves the bone work for enemies off screen).
	 */
	static void KeepPoseWhileHidden(USkinnedMeshComponent& Mesh, bool bHidden, FSavedAnimTickOptions& Saved);

	static constexpr float UpdateInterval = 0.2f;
	/** No sight beyond this, cm. */
	static constexpr float MaxSightCm = 6000.f;

private:
	struct FEnemyState
	{
		bool bHidden = false;
		bool bEverSeen = false;
		TWeakObjectPtr<AEnemyGhostActor> Ghost;
	};

	struct FIntel
	{
		FVector Location = FVector::ZeroVector;
		double Time = 0.0;
		/** When it reached the last known spot (< 0: not there yet). */
		double ArrivedTime = -1.0;
		bool bPerceived = false;
	};

	void Update();
	void RestoreAll();
	void SetEnemyHidden(AEnemyCharacter& Enemy, bool bHidden);
	AEnemyGhostActor* PlaceGhost(AEnemyCharacter& Enemy, FEnemyState& State);
	void DropGhost(FEnemyState& State);
	UMaterialInterface* GetGhostMaterial();

	TMap<TWeakObjectPtr<AEnemyCharacter>, FEnemyState> EnemyStates;
	TMap<TWeakObjectPtr<AEnemyCharacter>, TMap<TWeakObjectPtr<AOperativeCharacter>, FIntel>> Intel;
	TMap<TWeakObjectPtr<AActor>, double> DemaskUntil;
	/** Pawns ignored by the sight traces (rebuilt every update). */
	TArray<TWeakObjectPtr<AActor>> IgnoredPawns;
	FSavedAnimTickOptions SavedAnimTickOptions;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> GhostMaterial;

	FTacticalSightStats Stats;
	float Timer = 0.f;
	float SummaryTimer = 0.f;
	bool bWasActive = false;
};
