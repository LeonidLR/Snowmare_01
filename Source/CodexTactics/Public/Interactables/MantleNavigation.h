#pragma once

#include "CoreMinimal.h"
#include "Characters/MantleRules.h"
#include "GameFramework/Actor.h"
#include "Interactables/TripwireActor.h"
#include "Level/CrawlGapActor.h"
#include "NavAreas/NavArea.h"
#include "NavFilters/NavigationQueryFilter.h"
#include "NavLinkCustomComponent.h"
#include "Subsystems/WorldSubsystem.h"
#include "MantleNavigation.generated.h"

class UAnimSequenceBase;
struct FNavigationPath;

/**
 * Mantling slice 1 - navigation (docs/design/MANTLING.md 11.4 step 5, designer answers 11.6; docs/port/MANTLING_SLICE1_REPORT.md).
 * Directed path links over the eligible obstacles (crates, wrecks, boilers, the cut's parapets, 135 cm fences, marked edges): an order path to a
 * clicked point that runs over one mantles by itself (trigger C, "like the vault"). Their own areas - the vault's cost-3 area
 * (VaultNavigation.cpp) is untouched. UE-only (Godot had no mantling).
 */

/** A climb link up onto a top / over a fence: open only for the operative the player commands in Solo (every other move closes it). */
UCLASS()
class CODEXTACTICS_API UNavArea_Mantle : public UNavArea
{
	GENERATED_BODY()

public:
	UNavArea_Mantle();
};

/** The hop back down off a low top (a crate / wreck / boiler under links.down_below_cm): open to every operative (nobody is stranded on top). */
UCLASS()
class CODEXTACTICS_API UNavArea_MantleDown : public UNavArea
{
	GENERATED_BODY()

public:
	UNavArea_MantleDown();
};

/** Operative moves with the mantle closed (formation moves, follows, the autonomy; MantleNavigation::CloseMantleArea). */
UCLASS()
class CODEXTACTICS_API UNavFilter_OperativeNoMantle : public UNavigationQueryFilter
{
	GENERATED_BODY()

public:
	UNavFilter_OperativeNoMantle();
};

/** UNavFilter_NoCrawl with the mantle closed. */
UCLASS()
class CODEXTACTICS_API UNavFilter_NoCrawlNoMantle : public UNavFilter_NoCrawl
{
	GENERATED_BODY()

public:
	UNavFilter_NoCrawlNoMantle();
};

/** UNavFilter_TrapLiteral with the mantle closed. */
UCLASS()
class CODEXTACTICS_API UNavFilter_TrapLiteralNoMantle : public UNavFilter_TrapLiteral
{
	GENERATED_BODY()

public:
	UNavFilter_TrapLiteralNoMantle();
};

/** UNavFilter_NoCrawlTrapLiteral with the mantle closed. */
UCLASS()
class CODEXTACTICS_API UNavFilter_NoCrawlTrapLiteralNoMantle : public UNavFilter_NoCrawlTrapLiteral
{
	GENERATED_BODY()

public:
	UNavFilter_NoCrawlTrapLiteralNoMantle();
};

/**
 * Formation moves with the window sills closed too (designer 2026-11-22: in formation the men follow their commander through the doors and
 * never jump through a window; Solo / a box selection may - faster, loud). MantleNavigation::NoWindowVariant.
 */
UCLASS()
class CODEXTACTICS_API UNavFilter_OperativeNoMantleNoWindow : public UNavFilter_OperativeNoMantle
{
	GENERATED_BODY()

public:
	UNavFilter_OperativeNoMantleNoWindow();
};

/** UNavFilter_NoCrawlNoMantle with the window sills closed (formation). */
UCLASS()
class CODEXTACTICS_API UNavFilter_NoCrawlNoMantleNoWindow : public UNavFilter_NoCrawlNoMantle
{
	GENERATED_BODY()

public:
	UNavFilter_NoCrawlNoMantleNoWindow();
};

/** UNavFilter_TrapLiteralNoMantle with the window sills closed (formation). */
UCLASS()
class CODEXTACTICS_API UNavFilter_TrapLiteralNoMantleNoWindow : public UNavFilter_TrapLiteralNoMantle
{
	GENERATED_BODY()

public:
	UNavFilter_TrapLiteralNoMantleNoWindow();
};

/** UNavFilter_NoCrawlTrapLiteralNoMantle with the window sills closed (formation). */
UCLASS()
class CODEXTACTICS_API UNavFilter_NoCrawlTrapLiteralNoMantleNoWindow : public UNavFilter_NoCrawlTrapLiteralNoMantle
{
	GENERATED_BODY()

public:
	UNavFilter_NoCrawlTrapLiteralNoMantleNoWindow();
};

/** the plain operative filter with the window sills closed - the formation LEADER (his mantle stays open; designer 2026-11-22: the formation keeps together). */
UCLASS()
class CODEXTACTICS_API UNavFilter_OperativeNoWindow : public UNavigationQueryFilter
{
	GENERATED_BODY()

public:
	UNavFilter_OperativeNoWindow();
};

/** UNavFilter_NoCrawl with the window sills closed - the formation LEADER (his mantle stays open; designer 2026-11-22: the formation keeps together). */
UCLASS()
class CODEXTACTICS_API UNavFilter_NoCrawlNoWindow : public UNavFilter_NoCrawl
{
	GENERATED_BODY()

public:
	UNavFilter_NoCrawlNoWindow();
};

/** UNavFilter_TrapLiteral with the window sills closed - the formation LEADER (his mantle stays open; designer 2026-11-22: the formation keeps together). */
UCLASS()
class CODEXTACTICS_API UNavFilter_TrapLiteralNoWindow : public UNavFilter_TrapLiteral
{
	GENERATED_BODY()

public:
	UNavFilter_TrapLiteralNoWindow();
};

/** UNavFilter_NoCrawlTrapLiteral with the window sills closed - the formation LEADER (his mantle stays open; designer 2026-11-22: the formation keeps together). */
UCLASS()
class CODEXTACTICS_API UNavFilter_NoCrawlTrapLiteralNoWindow : public UNavFilter_NoCrawlTrapLiteral
{
	GENERATED_BODY()

public:
	UNavFilter_NoCrawlTrapLiteralNoWindow();
};

/** UNavFilter_OperativeNoMantle with the window sills free to enter - a formation FOLLOWER on a window order goes through the window behind his commander. */
UCLASS()
class CODEXTACTICS_API UNavFilter_OperativeNoMantleWindowFree : public UNavFilter_OperativeNoMantle
{
	GENERATED_BODY()

public:
	UNavFilter_OperativeNoMantleWindowFree();
};

/** UNavFilter_NoCrawlNoMantle with the window sills free to enter - a formation FOLLOWER on a window order goes through the window behind his commander. */
UCLASS()
class CODEXTACTICS_API UNavFilter_NoCrawlNoMantleWindowFree : public UNavFilter_NoCrawlNoMantle
{
	GENERATED_BODY()

public:
	UNavFilter_NoCrawlNoMantleWindowFree();
};

/** UNavFilter_TrapLiteralNoMantle with the window sills free to enter - a formation FOLLOWER on a window order goes through the window behind his commander. */
UCLASS()
class CODEXTACTICS_API UNavFilter_TrapLiteralNoMantleWindowFree : public UNavFilter_TrapLiteralNoMantle
{
	GENERATED_BODY()

public:
	UNavFilter_TrapLiteralNoMantleWindowFree();
};

/** UNavFilter_NoCrawlTrapLiteralNoMantle with the window sills free to enter - a formation FOLLOWER on a window order goes through the window behind his commander. */
UCLASS()
class CODEXTACTICS_API UNavFilter_NoCrawlTrapLiteralNoMantleWindowFree : public UNavFilter_NoCrawlTrapLiteralNoMantle
{
	GENERATED_BODY()

public:
	UNavFilter_NoCrawlTrapLiteralNoMantleWindowFree();
};

/** One measured mantle site (MantleNavigation::ProbeSite): the probe for the rules plus the geometry the climb needs. */
struct FMantleSite
{
	FMantleProbe Probe;
	TWeakObjectPtr<AActor> Obstacle;
	/** Where it was measured from (the feet = capsule bottom). */
	FVector Feet = FVector::ZeroVector;
	/** The face hit and its outward normal (horizontal unit). */
	FVector FacePoint = FVector::ZeroVector;
	FVector Normal = FVector::ForwardVector;
	/** The climb's direction (into the obstacle: -Normal). */
	FVector Dir = FVector::ForwardVector;
	/** The top's height (world Z) and the top edge point over the face. */
	float TopZ = 0.f;
	FVector EdgeTop = FVector::ZeroVector;
	/** The planned end of the climb (feet): on the top past the edge, or on the far side's ground. */
	FVector Landing = FVector::ZeroVector;
	EMantleDestination Destination = EMantleDestination::Top;
	/** The obstacle is vaultable (the vault takes it; the probe refuses). */
	bool bVaultable = false;
	bool bValid = false;
};

/** One generated link (world space; the holder stands at the origin). */
struct FMantleLink
{
	FVector Start = FVector::ZeroVector;
	FVector End = FVector::ZeroVector;
	/** Into the obstacle (2D unit) - the climb's direction (down links: out over the edge). */
	FVector Dir = FVector::ForwardVector;
	/** The hop down off a low top (else a climb up / over). */
	bool bDown = false;
	float HeightCm = 0.f;
	FName ClassId;
	TWeakObjectPtr<AActor> Obstacle;
	TWeakObjectPtr<UNavLinkCustomComponent> Link;
	FNavLinkId LinkId;
};

/** The holder of the generated mantle links (transient, at the world origin, no collision). */
UCLASS(NotBlueprintable, NotPlaceable, Transient)
class CODEXTACTICS_API AMantleLinkActor : public AActor
{
	GENERATED_BODY()

public:
	AMantleLinkActor();

	/** A one-way smart link Start -> End (world = actor space) in Area, on the infantry navmesh only (agent 0). */
	UNavLinkCustomComponent* AddLink(const FVector& Start, const FVector& End, TSubclassOf<UNavArea> Area);
};

/**
 * The mantle's world state: the generated path links (after the navmesh and the drop links are ready; FDropLinkReach keeps only links whose
 * top has a way back to the main area), the exclusive landing reservations (one climber per landing spot) and the loaded clips.
 * Console: CodexTactics.MantleLinks [clear] regenerates; Codex.Mantle.Links 0 = no path links (the click order still works).
 */
UCLASS()
class CODEXTACTICS_API UMantleSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	/** Measures every eligible obstacle and spawns the links (replacing earlier ones). Returns the link count; -1 = no infantry navmesh. */
	int32 Generate();
	void ClearLinks();
	bool HasGenerated() const { return bGenerated; }
	/**
	 * The world preload's call (UGameplayPreloadSubsystem, under the loading cover - world timers do not run while it pauses the world): generates
	 * now when the level-start poll still waits, the navmesh is built and the drop links are done. True = nothing left to wait for; false = not
	 * ready yet. Keeps the ~0.12 s generation out of the first gameplay frames (HitchProbeSmoke frame #984 141.6 ms, 2026-11-18).
	 */
	bool TryGenerateForPreload();
	/** The last generation stopped at the link cap (links.max_total): obstacles after it got no links (smokes, batch 2 item 9). */
	bool WasLinkCapReached() const { return bLinkCapReached; }

	const TArray<FMantleLink>& GetLinks() const { return Links; }
	const FMantleLink* GetLink(int32 Index) const { return Links.IsValidIndex(Index) ? &Links[Index] : nullptr; }
	int32 FindLinkByComponent(const UNavLinkCustomComponent* Component) const;
	int32 FindLinkById(FNavLinkId LinkId) const;
	/** The mantle links a path runs through, in path order (its corridor's link polys and its custom-link points). */
	void FindLinksOnPath(const FNavigationPath& Path, TArray<int32>& OutLinks) const;
	/** A link that failed in play: its area off - no path takes it again. */
	void DisableLink(int32 Index, const TCHAR* Why);

	/** Exclusive landing: Who holds Landing (no other climber's landing within reserve.radius_cm). False when another man holds it. */
	bool TryReserve(AActor* Who, const FVector& Landing);
	void Release(const AActor* Who);
	bool IsReservedByOther(const FVector& Landing, const AActor* Who) const;
	int32 GetReservationCount() const { return Reservations.Num(); }

	/** The clip of the config's table (loaded once; null when missing). */
	UAnimSequenceBase* GetClip(int32 ClipIndex);

	/** A clip's sampled motion (AOperativeCharacter::GetMantleClipTrack fills it once per clip: each clip plays on one body only). */
	const FMantleClipTrack* FindClipTrack(int32 ClipIndex) const { return ClipTracks.Find(ClipIndex); }
	void StoreClipTrack(int32 ClipIndex, FMantleClipTrack&& Track) { ClipTracks.Add(ClipIndex, MoveTemp(Track)); }

	/** Generation stats (logs / smokes). */
	int32 GetSampledCount() const { return Sampled; }
	double GetGenerationMs() const { return GenerationMs; }

private:
	void PollReady();
	/** The infantry navmesh exists, no navigation build runs and the drop links are done (or waited out). */
	bool IsReadyToGenerate() const;
	void HandleLinkReached(UNavLinkCustomComponent* ThisComp, UObject* PathComp, const FVector& DestPoint);

	TArray<FMantleLink> Links;
	UPROPERTY(Transient)
	TObjectPtr<AMantleLinkActor> LinkActor;
	struct FReservation
	{
		TWeakObjectPtr<AActor> Who;
		FVector Landing = FVector::ZeroVector;
	};
	TArray<FReservation> Reservations;
	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<UAnimSequenceBase>> ClipCache;
	TSet<int32> MissingClips;
	TMap<int32, FMantleClipTrack> ClipTracks;
	FTimerHandle PollTimer;
	float WaitedSeconds = 0.f;
	bool bGenerated = false;
	bool bLinkCapReached = false;
	int32 Sampled = 0;
	double GenerationMs = 0.0;
};

namespace MantleNavigation
{
	/** Adds the mantle area's exclusion to a filter's area list (UNavFilter_NoVault for the enemies; the operatives' NoMantle filters). */
	CODEXTACTICS_API void CloseMantleArea(TArray<struct FNavigationFilterArea>& Areas, bool bAlsoDown);

	/** The operative filter with the mantle closed for a filter class ChooseMoveFilter would use (null = the default filter). */
	/**
	 * Formation without a window order (designer 2026-11-22: the formation keeps together - nobody, the leader included, paths through a
	 * window): Filter (a base filter when bMantleOpen, else a NoMantle one; or null) with the window sills closed as well.
	 */
	CODEXTACTICS_API TSubclassOf<UNavigationQueryFilter> NoWindowVariant(TSubclassOf<UNavigationQueryFilter> Filter, bool bMantleOpen);
	/** Formation on a window order: a follower's NoMantle filter (or null) with the window sills free to enter - he follows through the window. */
	CODEXTACTICS_API TSubclassOf<UNavigationQueryFilter> WindowFreeVariant(TSubclassOf<UNavigationQueryFilter> Filter);
	CODEXTACTICS_API TSubclassOf<UNavigationQueryFilter> NoMantleVariant(TSubclassOf<UNavigationQueryFilter> Filter);

	/** A hit on geometry that blocks a walking capsule (no pawns; overlap-only volumes left out - docs/port/TRAPS.md). */
	CODEXTACTICS_API bool IsBlockingHit(const FHitResult& Hit);

	/**
	 * Measures the obstacle in front of a capsule standing at Feet (its bottom) along Dir (horizontal): the face within MaxReachCm at knee
	 * height, its top (a wall if solid above the ceiling), the top's depth, eligibility (tags) and the landing (top inset / far side) with its
	 * footprint and headroom. False when no face is found (Out.Probe.HeightCm = 0).
	 */
	CODEXTACTICS_API bool ProbeSite(UWorld& World, const FVector& Feet, const FVector& Dir, float MaxReachCm, float CapsuleRadius, float CapsuleHalfHeight,
		const FCollisionQueryParams& Params, const FMantleConfig& Config, FMantleSite& Out);

	/**
	 * A click on an obstacle's top (TopHit, normal up) seen from a capsule at FromFeet: the obstacle's face nearest to him (its oriented
	 * bounds) - the base point in front of it (on his level) and the climb direction. False when the hit is no eligible top.
	 */
	CODEXTACTICS_API bool PlanTopClick(UWorld& World, const FHitResult& TopHit, const FVector& FromFeet, float CapsuleRadius, FVector& OutBaseFeet, FVector& OutDir);

	/**
	 * A click on an eligible obstacle's SIDE face (|normal z| < 0.3; batch 2 item 3 - the isometric camera shows the faces): the top point just
	 * inside that face (order.face_inset_cm) as a top hit on the same obstacle. False when it is no such face or the top is not under it.
	 */
	CODEXTACTICS_API bool TopHitFromFace(UWorld& World, const FHitResult& FaceHit, FHitResult& OutTopHit);

	/**
	 * Batch 2 (docs/port/MANTLING_BATCH2_REPORT.md; console Codex.Mantle.Batch2): 1 (default) = the wall guard, the clip's exit at its settled
	 * pose, the walk-off descent over the face nearest the goal, the face double click and the hover ghost; 0 = the behaviour before it (A/B for
	 * the designer and the smokes' "before" runs).
	 */
	CODEXTACTICS_API bool IsBatch2Enabled();
}
