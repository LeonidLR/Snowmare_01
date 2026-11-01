#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "OccluderFadeSubsystem.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPrimitiveComponent;
class UStaticMeshComponent;

/** Tuning of the occluder fade (defaults; CVar Codex.OccluderFade 0 switches it off). */
struct FOccluderFadeConfig
{
	/** One pass every this many seconds (real time), s. */
	float UpdateIntervalSeconds = 0.1f;
	/** A piece counts as an occluder only when its top rises at least this far above the target's feet, cm (a kerb, a low cover never fades). */
	float MinRiseAboveFeetCm = 100.f;
	/** A hit closer than this to the target is the target's own ground / wall face, not an occluder, cm. */
	float TargetMarginCm = 40.f;
	/** A piece whose footprint is wider than this on BOTH axes is a floor slab / terrain block, never faded, cm. */
	float MaxShortSideCm = 3000.f;
	/** The faded look: the fade material's Opacity at full fade. */
	float FadedOpacity = 0.45f;
	/** Fade in / out time (real time), s. */
	float FadeSeconds = 0.2f;
	/** A faded piece no target is behind any more comes back this long after it last hid one (hysteresis), s. */
	float RestoreDelaySeconds = 0.3f;
	/** Pieces faded at most at once (performance: a target hides behind a wall or two). */
	int32 MaxFaded = 6;
	/** The fade material (Masked, dithered opacity: no translucency sorting). Created by Scripts/Editor/create_occluder_fade_material.py. */
	const TCHAR* MaterialPath = TEXT("/Game/UI/Materials/M_OccluderFade.M_OccluderFade");
};

/** Pure rules of the occluder fade (tested in CodexTactics.Camera.OccluderFade.*). */
namespace OccluderFadeRules
{
	/** Actor tag: never fade this actor. */
	CODEXTACTICS_API FName OptOutTag();

	/** What is known about a hit piece and its owner. */
	struct FOccluderTraits
	{
		bool bPawn = false;
		bool bTerrain = false;
		bool bFoliage = false;
		/** A plain static mesh (not instanced: one proxy per piece). */
		bool bStaticMesh = false;
		bool bInstanced = false;
		bool bVisible = true;
		bool bOptOut = false;
		/** An interactable object (barricade, crate, barrel): it has its own click meaning and moves. */
		bool bInteractable = false;
	};

	/** The piece may be faded at all (a visible, plain static mesh of no pawn / terrain / foliage / interactable, not opted out). */
	CODEXTACTICS_API bool IsFadeableKind(const FOccluderTraits& Traits);

	/**
	 * The piece hides the target: it stands between the camera and the target (HitDistance < TargetDistance - TargetMarginCm along the
	 * view ray), rises MinRiseAboveFeetCm above the target's feet (PieceTopZ) and is no floor slab (its short side <= MaxShortSideCm).
	 */
	CODEXTACTICS_API bool HidesTarget(float HitDistance, float TargetDistance, float PieceTopZ, float TargetFeetZ, float PieceShortSideCm,
		const FOccluderFadeConfig& Config);

	/** The fade material's opacity Elapsed seconds into a fade in (bFadingIn) / out: 1 -> FadedOpacity -> 1 over FadeSeconds. */
	CODEXTACTICS_API float Opacity(float Elapsed, bool bFadingIn, const FOccluderFadeConfig& Config);

	/** A faded piece no target hides behind any more starts to come back RestoreDelaySeconds after it last did. */
	CODEXTACTICS_API bool ShouldRestore(double LastHidSeconds, double NowSeconds, const FOccluderFadeConfig& Config);
}

/**
 * A wall that hides the cursor's target fades (the game designer's play-test 2026-11-01, approved fix c: "like the roof cutaway"; UE-only,
 * Godot reference: none). ~10 times a second (real time) the player controller's targets - the cover ghost of the click, the far-side cover
 * spot under the cursor (ACodexTacticsPlayerController::ResolveWallSideSlot) - are traced from the camera; every visible plain static
 * mesh in front of a target that rises a metre above its feet fades: it is no longer drawn in the main / depth pass (as the roof cutaway:
 * its shadow and its collision stay - sight, cover and AI traces are unchanged; only the player's cursor looks through it to the ground
 * behind, ACodexTacticsPlayerController::ThroughFadedOccluders) and a see-through proxy with the dithered
 * fade material (M_OccluderFade, Masked: no translucency sorting) stands in its place, its opacity eased 1 -> 0.45 in 0.2 s. A piece no target
 * hides behind any more comes back 0.3 s later. At most 6 pieces at once; pawns, terrain, foliage, instanced meshes, interactables and
 * actors tagged NoOccluderFade never fade. Missing material: the piece is cut like a roof (no proxy).
 */
UCLASS()
class CODEXTACTICS_API UOccluderFadeSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual void Deinitialize() override;

	/** The points the player means right now (world; a body centre): every tick from the player controller. Empty = nothing to show. */
	void SetTargets(const TArray<FVector>& InTargets);
	const TArray<FVector>& GetTargets() const { return Targets; }

	/** Piece is faded now (fading in or held; not while fading back). */
	bool IsFaded(const UPrimitiveComponent* Piece) const;
	/** The pieces faded now (fading in or held): the cursor looks through them (ACodexTacticsPlayerController::ThroughFadedOccluders). */
	TArray<UPrimitiveComponent*> GetFadedPieces() const;
	/** Pieces faded now. */
	int32 GetFadedCount() const;
	/** Fades that started since the level began (smokes). */
	int32 GetFadeCount() const { return FadeCount; }
	/** The see-through proxy standing in for Piece (null when none). */
	UStaticMeshComponent* GetProxy(const UPrimitiveComponent* Piece) const;

	/** One pass now (smokes; the tick runs it every UpdateIntervalSeconds). */
	void UpdateNow();
	/** Every faded piece back at once (switching it off, the world going away). */
	void RestoreAll();

	FOccluderFadeConfig Config;

private:
	struct FFaded
	{
		TWeakObjectPtr<UPrimitiveComponent> Piece;
		TWeakObjectPtr<UStaticMeshComponent> Proxy;
		TArray<TWeakObjectPtr<UMaterialInstanceDynamic>> Materials;
		bool bRenderInMainPass = true;
		bool bRenderInDepthPass = true;
		/** Its overlay material (the weather's snow / wet overlay draws even with the main pass off): taken off while faded. */
		TWeakObjectPtr<UMaterialInterface> Overlay;
		double LastHidSeconds = 0.0;
		double StateSeconds = 0.0;
		bool bFadingOut = false;
	};

	static OccluderFadeRules::FOccluderTraits GetTraits(const UPrimitiveComponent& Piece);
	/** The pieces hiding Target from the camera at CameraLocation. */
	void CollectOccluders(const FVector& CameraLocation, const FVector& Target, TSet<UPrimitiveComponent*>& OutPieces) const;
	void Fade(UPrimitiveComponent& Piece, double Now);
	void Restore(FFaded& Faded);
	void ApplyOpacity(FFaded& Faded, float Opacity);
	void Animate(double Now);

	TArray<FVector> Targets;
	TArray<FFaded> FadedPieces;
	TWeakObjectPtr<UMaterialInterface> FadeMaterial;
	bool bMaterialLoaded = false;
	float UpdateAccumulator = 0.f;
	int32 FadeCount = 0;
};
