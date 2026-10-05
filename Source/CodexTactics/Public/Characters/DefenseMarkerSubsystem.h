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
};

/**
 * Sprint 10 «Рубеж обороны» visuals (user request 2026-10-05; UE-only): every object / point an operative holds at all
 * costs gets a translucent green fresnel overlay (M_TargetFresnel, the material of the red target highlight), a shield
 * icon with «РУБЕЖ» drawn by the HUD, and a green ground ring at the intercept radius that grows and fades in softly,
 * breathes gently and fades out when the line is lifted (a new move order, the object destroyed). Runs in the pause too
 * (real time, not the dilated world time). Reads the operatives' FTacticalAnchor::Defense; no gameplay effect.
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
		TWeakObjectPtr<ARadiusRingActor> Ring;
		TWeakObjectPtr<UMaterialInstanceDynamic> Overlay;
		TArray<TWeakObjectPtr<UMeshComponent>> Meshes;
		TArray<TWeakObjectPtr<UMaterialInterface>> PreviousOverlays;
	};

	FMarker& FindOrAddMarker(AActor* Actor, const FVector& Location);
	void ApplyOverlay(FMarker& Marker);
	void RemoveMarker(FMarker& Marker);

	TArray<FMarker> Markers;
	/** Keeps the overlay materials alive. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> OverlayMaterials;
	float Clock = 0.f;
};
