#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Subsystems/WorldSubsystem.h"
#include "NavLinkCustomComponent.h"
#include "Falls/FallRules.h"
#include "DropLinkSubsystem.generated.h"

class ARecastNavMesh;
class UPathFollowingComponent;
struct FNavigationPath;

/** One generated drop link (world space; the link actor stands at the origin). */
struct FDropLink
{
	/** The navmesh point at the upper edge (the link start) and the landing point on the lower surface's navmesh (the link end). */
	FVector Start = FVector::ZeroVector;
	FVector End = FVector::ZeroVector;
	/** Out over the edge (2D unit). */
	FVector Dir = FVector::ForwardVector;
	/** Horizontally from Start along Dir: the edge, the lower surface's near / far border (FarCm <= 0 = open); the drop height. */
	float EdgeCm = 0.f;
	float NearCm = 0.f;
	float FarCm = 0.f;
	float DropCm = 0.f;
	/** FallRules::IsHurtingDrop: the dearer area and the red marker. */
	bool bHurt = false;
	TWeakObjectPtr<UNavLinkCustomComponent> Link;
	FNavLinkId LinkId;
	/** Who is dropping here (one at a time) and until when the landing stays taken after he landed (world seconds; 0 = while he holds it). */
	TWeakObjectPtr<AActor> LeaseHolder;
	double LeaseUntil = 0.0;
};

/** A drop on a squad member's order path (the HUD's marker; smokes). */
struct FDropMarker
{
	int32 LinkIndex = INDEX_NONE;
	TWeakObjectPtr<AActor> Who;
	/** The link start, the edge point and the landing (world). */
	FVector Start = FVector::ZeroVector;
	FVector Edge = FVector::ZeroVector;
	FVector End = FVector::ZeroVector;
	/** FallRules::DropMarkerLabel. */
	FString Label;
	/** Hurts, kills or falls short: drawn red. */
	bool bDanger = false;
	/** He is dropping off it now (else it lies ahead on his path). */
	bool bActive = false;
};

/** The holder of the generated drop links (transient, at the world origin, no collision). */
UCLASS(NotBlueprintable, NotPlaceable, Transient)
class CODEXTACTICS_API ADropLinkActor : public AActor
{
	GENERATED_BODY()

public:
	ADropLinkActor();

	/** A one-way smart link Start -> End (world = actor space) in Area, on the infantry navmesh only (agent 0). */
	UNavLinkCustomComponent* AddLink(const FVector& Start, const FVector& End, TSubclassOf<class UNavArea> Area);
};

/**
 * Generated DROP nav links (game designer items 6 / 10 / 11a, 2026-11-03; docs/systems/stances-and-movement.md "Falls and drops"). Once the
 * infantry navmesh is built, every exterior navmesh edge is sampled (FallRules::EdgeSampleFractions, link.spacing_cm); a sample with no
 * parapet / wall at the edge, a walkable lower surface 1.5-6.5 m below at most 2.5 m beyond it (FallRules::AnalyseProfile), a landing on the
 * navmesh that a run-up at link.reference_speed_cms reaches (FallRules::PlanTakeoff) and a clear flight arc gets a one-way smart link
 * (UNavArea_Drop / UNavArea_DropHurt) - so a move order may path off a roof / bridge edge; the operative's ULedgeFallComponent takes the link
 * over (run-up, the fall keeping his speed, landing, fall damage, the path goes on). One at a time per landing spot (TryLease).
 * Which maps: Codex.Fall.DropLinks 0 off / 1 the level JSON's "drop_links": true / 2 (default, user decision 2026-11-03) every map unless its
 * level JSON says "drop_links": false; the level JSON's "drop_link_exclusions" boxes suppress links locally. The enemies take them per type
 * (FallRules::EnemyDropUse, DropLinkNavigation::EnemyFilterClass).
 * UE-only (no Godot reference).
 */
UCLASS()
class CODEXTACTICS_API UDropLinkSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	/** Drop links on this map (the console / the level JSON). */
	bool IsEnabledForWorld() const;

	/** Samples the navmesh edges and spawns the links (replacing earlier ones). Returns the link count; -1 = no infantry navmesh. */
	int32 Generate();

	/** A link that failed in play (a run-up that never left its edge): its area off - no path takes it again. */
	void DisableLink(int32 Index, const TCHAR* Why);

	/** Removes every generated link. */
	void ClearLinks();

	/** Generated (Generate ran on this world's navmesh). */
	bool HasGenerated() const { return bGenerated; }

	const TArray<FDropLink>& GetLinks() const { return Links; }
	const FDropLink* GetLink(int32 Index) const { return Links.IsValidIndex(Index) ? &Links[Index] : nullptr; }

	/** The link of a nav link id / its smart link component; INDEX_NONE when none. */
	int32 FindLinkById(FNavLinkId LinkId) const;
	int32 FindLinkByComponent(const UNavLinkCustomComponent* Component) const;

	/** The link whose start is within RadiusCm (2D, +-60 cm in height) of Feet, nearest first; INDEX_NONE when none. */
	int32 FindLinkStartNear(const FVector& Feet, float RadiusCm) const;

	/** The drop links a path runs through, in path order (its corridor's link polys and its custom-link points). */
	void FindLinksOnPath(const FNavigationPath& Path, TArray<int32>& OutLinks) const;

	/** One at a time: takes the link's landing spot for Who; false when another man's running drop lands within lease.radius_cm of it. */
	bool TryLease(int32 Index, AActor* Who);
	/** Who landed: the spot stays taken for HoldSeconds more (0 = free now). */
	void ReleaseLease(int32 Index, const AActor* Who, float HoldSeconds);
	/** Another man's drop holds this link's landing spot now. */
	bool IsLeasedByOther(int32 Index, const AActor* Who) const;

	/**
	 * The drops on the squad's order paths now (each link once, the first member's view of it): links ahead on a member's running path and the
	 * one a member is dropping off / waiting at. Danger = the drop hurts or his run-up now would fall short (ULedgeFallComponent::PredictDrop).
	 */
	void GatherSquadMarkers(TArray<FDropMarker>& OutMarkers) const;

	/** Generation stats (logs / smokes): candidates sampled, links made, ms. */
	int32 GetSampledCount() const { return Sampled; }
	double GetGenerationMs() const { return GenerationMs; }

private:
	void PollNavigationReady();
	/** The level config of this map (the game mode's), null without. */
	const class ULevelConfigAsset* GetLevelConfig() const;
	void HandleLinkReached(UNavLinkCustomComponent* ThisComp, UObject* PathComp, const FVector& DestPoint);
	/** One edge sample -> a link (filled in OutLink) or a verdict. */
	bool BuildLinkAt(const FVector& EdgePoint, const FVector& EdgeNormal, const FallRules::FFallConfig& Config, const FCollisionQueryParams& Params,
		FDropLink& OutLink, FallRules::EProfileVerdict& OutVerdict) const;

	TArray<FDropLink> Links;
	UPROPERTY(Transient)
	TObjectPtr<ADropLinkActor> LinkActor;
	FTimerHandle PollTimer;
	float WaitedSeconds = 0.f;
	bool bGenerated = false;
	int32 Sampled = 0;
	double GenerationMs = 0.0;
};
