#pragma once

#include "CoreMinimal.h"
#include "Combat/VisionConeRules.h"
#include "ProceduralMeshComponent.h"
#include "VisionConeComponent.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;

/**
 * Ground vision cone (user request 2026-10-11; Godot Scenes/movements/player.gd _setup_vision_cone,
 * _rebuild_vision_cone_mesh, _update_vision_cone). A flat sector (fill) plus an outline band, a few cm above the owner's
 * feet, turning with the owner. The owner drives it every tick through UpdateCone (shape from its perception / weapon,
 * colour from its state, visibility from its bShowVisionCone and the console variable Codex.Debug.VisionCones).
 * Geometry and colours: VisionConeRules (tests CodexTactics.Combat.VisionCone.*).
 *
 * Look: ConeMaterial (Gemini's slot; TANDEM request) — a translucent unlit material with a vector parameter
 * ColorParameterName (rgb = colour, a = opacity) and optionally a scalar OpacityParameterName. Without one an engine
 * translucent material is used (FallbackMaterial), tinted through the same parameters and the vertex colours.
 */
UCLASS(ClassGroup = (CodexTactics), meta = (BlueprintSpawnableComponent))
class CODEXTACTICS_API UVisionConeComponent : public UProceduralMeshComponent
{
	GENERATED_BODY()

public:
	UVisionConeComponent(const FObjectInitializer& ObjectInitializer);

	/** The cone's material (fill and outline). Empty: FallbackMaterial. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Cone")
	TObjectPtr<UMaterialInterface> ConeMaterial;

	/** Engine material used while ConeMaterial is empty. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Cone")
	TSoftObjectPtr<UMaterialInterface> FallbackMaterial;

	/** Vector parameter that receives the colour (rgb) and the opacity (a). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Cone")
	FName ColorParameterName = TEXT("Color");

	/** Scalar parameter that receives the opacity (none = skip). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Cone")
	FName OpacityParameterName = TEXT("Opacity");

	/** Arc segments (Godot 32). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Cone", meta = (ClampMin = "3", ClampMax = "128"))
	int32 Segments = VisionConeRules::GodotSegments;

	/** Height of the cone above the owner's feet, cm (Godot 8). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Cone", meta = (ClampMin = "0"))
	float HeightAboveFeetCm = VisionConeRules::GodotHeightCm;

	/** Fill opacity (Godot 0.22). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Cone", meta = (ClampMin = "0", ClampMax = "1"))
	float FillAlpha = VisionConeRules::GodotFillAlpha;

	/** Draw the outline band (Godot line strip). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Cone")
	bool bDrawOutline = true;

	/** Outline opacity (Godot 0.85). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Cone", meta = (ClampMin = "0", ClampMax = "1"))
	float OutlineAlpha = VisionConeRules::GodotLineAlpha;

	/** Outline band width, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Cone", meta = (ClampMin = "1"))
	float OutlineWidthCm = 8.f;

	/** Colour change speed (Godot 14). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Cone", meta = (ClampMin = "0"))
	float ColorLerpSpeed = VisionConeRules::GodotColorLerpSpeed;

	/**
	 * Called by the owner every tick: shows / hides the cone, rebuilds the mesh when the shape changed, eases the colour
	 * towards TargetColor and keeps the cone at the feet.
	 */
	void UpdateCone(bool bShow, float RangeCm, float HalfAngleDeg, const FLinearColor& TargetColor, float DeltaSeconds);

	UFUNCTION(BlueprintPure, Category = "Vision Cone")
	bool IsConeShown() const { return bShown; }
	UFUNCTION(BlueprintPure, Category = "Vision Cone")
	float GetConeRangeCm() const { return BuiltRange; }
	UFUNCTION(BlueprintPure, Category = "Vision Cone")
	float GetConeHalfAngleDeg() const { return BuiltHalfAngle; }
	UFUNCTION(BlueprintPure, Category = "Vision Cone")
	FLinearColor GetShownColor() const { return ShownColor; }
	/** Vertices of the fill section (smokes). */
	int32 GetFillVertexCount() const;

	/** Console Codex.Debug.VisionCones: 0 all off, 1 all on, -1 (default) each actor's bShowVisionCone. */
	static int32 GetGlobalMode();

private:
	void Rebuild(float RangeCm, float HalfAngleDeg);
	void ApplyColor();
	void PlaceAtFeet();

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> FillMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> LineMaterial;

	float BuiltRange = -1.f;
	float BuiltHalfAngle = -1.f;
	bool bShown = false;
	bool bColorInitialised = false;
	FLinearColor ShownColor = FLinearColor::White;
};
