#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DefenseMarkerSubsystem.generated.h"

class ARadiusRingActor;
class UMaterialInstanceDynamic;
class UMeshComponent;
class UMaterialInterface;

/** One defended object / point as the HUD draws it (shield icon). */
struct CODEXTACTICS_API FDefenseMarkerView
{
	/** World point above the object for the shield icon. */
	FVector IconLocation = FVector::ZeroVector;
	/** 0..1 fade (in when the line is set, out when it is lifted). */
	float Alpha = 0.f;
	int32 Defenders = 0;
	/** A SUPPRESS order's marker (red; user decision 2026-10-26) instead of a hold-line guard (green). */
	bool bSuppress = false;
	/** The marker's colour (green guard / red suppress). */
	FLinearColor Color = FLinearColor::Green;
};

/**
 * Sprint 10 "Hold line" visuals (user request 2026-10-05; UE-only): every object / point an operative holds at all
 * costs gets a translucent green fresnel overlay (M_TargetFresnel, the material of the red target highlight), a shield
 * icon with "HOLD LINE" drawn by the HUD, and a green ground ring at the intercept radius that grows and fades in softly,
 * breathes gently and fades out when the line is lifted (a new move order, the object destroyed). Runs in the pause too
 * (real time, not the dilated world time). Reads the operatives' FTacticalAnchor::Defense; no gameplay effect.
 * Suppressive fire (user decision 2026-10-26): the same marker in RED over every target / spot of a SUPPRESS order (Shift + click on
 * enemies; running or planned) - the enemy's overlay and a red ring of SuppressionRules order.marker_radius_cm, "SUPPRESS" on the HUD.
 */
UCLASS()
class CODEXTACTICS_API UDefenseMarkerSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickableWhenPaused() const override { return true; }
	virtual void Deinitialize() override;

	/** The markers for the HUD's shield icons. */
	TArray<FDefenseMarkerView> GetMarkers() const;

	/** Live markers (smokes). */
	int32 GetMarkerCount() const { return Markers.Num(); }
	/** Fade of the marker of Actor (-1: none). */
	float GetMarkerAlpha(const AActor* Actor) const;
	/** Red SUPPRESS markers wanted now (not the ones fading out; smokes). */
	int32 GetSuppressMarkerCount() const;
	/** A wanted red SUPPRESS marker on Actor, or (Actor null) one within ToleranceCm of Point (smokes). */
	bool HasSuppressMarker(const AActor* Actor, const FVector& Point, float ToleranceCm = 150.f) const;
	/** The ring colour of a marker kind (green guard / red suppress). */
	static FLinearColor GetRingColor(bool bSuppress);

	static constexpr float FadeInSeconds = 0.6f;
	static constexpr float FadeOutSeconds = 0.9f;

private:
	struct FMarker
	{
		TWeakObjectPtr<AActor> Actor;
		FVector Ground = FVector::ZeroVector;
		float TopZ = 0.f;
		float RadiusCm = 1200.f;
		float Alpha = 0.f;
		bool bWanted = false;
		int32 Defenders = 0;
		bool bSuppress = false;
		TWeakObjectPtr<ARadiusRingActor> Ring;
		TWeakObjectPtr<UMaterialInstanceDynamic> Overlay;
		TArray<TWeakObjectPtr<UMeshComponent>> Meshes;
		TArray<TWeakObjectPtr<UMaterialInterface>> PreviousOverlays;
	};

	FMarker& FindOrAddMarker(AActor* Actor, const FVector& Location, bool bSuppress = false);
	void ApplyOverlay(FMarker& Marker);
	void RemoveMarker(FMarker& Marker);

	TArray<FMarker> Markers;
	/** Keeps the overlay materials alive. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> OverlayMaterials;
	float Clock = 0.f;
};
