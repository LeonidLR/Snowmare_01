#pragma once

#include "CoreMinimal.h"
#include "GateExitRules.generated.h"

/** Which side of the level's blast gate is "out" (level JSON "gate_exit"). */
UENUM(BlueprintType)
enum class EGateExitSide : uint8
{
	/** The side away from the level's CombatStart point (else away from the squad when the gate opens). */
	Auto,
	/** The gate actor's local +X (the leaves slide along Y; X runs through the doorway). */
	PositiveX,
	/** The gate actor's local -X. */
	NegativeX
};

/**
 * Level end through the fully opened blast gate (game designer 2026-10-22: "after the fight, if we go through the fully opened
 * gate, do a fade and that's the end of the level"). Level JSON "gate_exit": "off" (default) | "auto" | "+x" | "-x", or an
 * object {"outward": "auto", "fade_seconds": 2, "return_to_menu_seconds": 6}. UE-only (no Godot reference: the Godot gate
 * only let the squad into the checkpoint).
 */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FGateExitConfig
{
	GENERATED_BODY()

	/** The level ends when the squad leaves through the open gate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gate Exit")
	bool bEnabled = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gate Exit")
	EGateExitSide Side = EGateExitSide::Auto;

	/** An operative has crossed once his capsule centre is this far beyond the door plane, cm (leaf + capsule + margin). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gate Exit", meta = (ClampMin = "0"))
	float CrossDepthCm = 150.f;

	/** How deep the counted zone reaches out from the door plane, cm (the mouth of the gate on its outer side). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gate Exit", meta = (ClampMin = "0"))
	float ZoneDepthCm = 1500.f;

	/** The counted zone is the doorway's width plus this much on each side, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gate Exit", meta = (ClampMin = "0"))
	float LateralSlackCm = 300.f;

	/** Real seconds of the fade to black once the last living operative is out. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gate Exit", meta = (ClampMin = "0.1"))
	float FadeSeconds = 2.f;

	/** Real seconds the "MISSION COMPLETE" title takes to appear on the black screen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gate Exit", meta = (ClampMin = "0"))
	float TitleFadeSeconds = 1.f;

	/** Real seconds on the black screen before the main menu opens; 0 = stay (headless checks never leave). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gate Exit", meta = (ClampMin = "0"))
	float ReturnToMenuSeconds = 6.f;
};

/** The exit line of a gate on the ground plane: the door plane, its outward normal, the doorway's half width. */
struct CODEXTACTICS_API FGateExitLine
{
	/** The gate actor's origin (centre of the doorway at floor level). */
	FVector Origin = FVector::ZeroVector;
	/** Horizontal unit normal pointing OUT of the level through the doorway. */
	FVector Outward = FVector::ForwardVector;
	/** Horizontal unit vector along the doorway (the leaves' slide axis). */
	FVector Along = FVector::RightVector;
	/** Half the doorway's width, cm (one leaf's width: the two leaves meet at the origin). */
	float HalfWidthCm = 350.f;
};

/** One squad member for the exit check. */
struct CODEXTACTICS_API FGateExitMember
{
	/** Alive and in the squad (the killed in action are out of it). */
	bool bAlive = true;
	/** Has crossed the exit line and is still on its outer side (UpdateCrossed). */
	bool bCrossed = false;
};

/**
 * Pure rules of the level exit through the blast gate (ULevelExitSubsystem applies them): the exit line derived from the
 * gate's transform and DoorSize (no map actor needed), the crossing latch, the "everybody alive is out" test, the fade.
 */
namespace GateExitRules
{
	/** "off" / "auto" / "+x" / "-x" (any case; "on" = "auto"). False on anything else. */
	CODEXTACTICS_API bool ParseSide(const FString& Text, bool& bOutEnabled, EGateExitSide& OutSide);
	CODEXTACTICS_API FString SideToString(bool bEnabled, EGateExitSide Side);

	/**
	 * +1 = the gate's local +X is out, -1 = -X. Auto: away from InsideReference (CombatStart / the squad) when given and not on
	 * the door plane, else +1.
	 */
	CODEXTACTICS_API int32 ResolveOutwardSign(EGateExitSide Side, const FTransform& GateTransform, const FVector* InsideReference);

	/** The exit line of a gate (its transform; HalfWidthCm = AGateActor::DoorSize.Y: two leaves of that width meet at the origin). */
	CODEXTACTICS_API FGateExitLine MakeLine(const FTransform& GateTransform, float HalfWidthCm, int32 OutwardSign);

	/** Horizontal distance of Point beyond the door plane, cm (< 0 = inside). */
	CODEXTACTICS_API float SignedDistance(const FGateExitLine& Line, const FVector& Point);

	/** Point stands in the counted zone: CrossDepthCm..ZoneDepthCm beyond the plane, within the doorway + LateralSlackCm. */
	CODEXTACTICS_API bool IsInExitZone(const FGateExitLine& Line, const FGateExitConfig& Config, const FVector& Point);

	/**
	 * The crossing latch of one operative: set when he stands in the exit zone, kept while he stays beyond the door plane
	 * (he may walk on along the road), cleared when he is back inside.
	 */
	CODEXTACTICS_API bool UpdateCrossed(bool bWasCrossed, const FGateExitLine& Line, const FGateExitConfig& Config, const FVector& Point);

	/** The exit counts now: enabled on the level, the gate fully open (not still sliding), the mission neither failed nor over. */
	CODEXTACTICS_API bool IsArmed(bool bEnabled, bool bGateFullyOpen, bool bMissionFailed, bool bMissionComplete);

	/** Living members (the dead never have to cross). */
	CODEXTACTICS_API int32 CountRequired(TConstArrayView<FGateExitMember> Members);

	/** Living members that have crossed. */
	CODEXTACTICS_API int32 CountCrossed(TConstArrayView<FGateExitMember> Members);

	/** The level ends: at least one living member and every living one has crossed (no fade while anyone alive is inside). */
	CODEXTACTICS_API bool ShouldEndLevel(TConstArrayView<FGateExitMember> Members);

	/** Black overlay alpha ElapsedSeconds after the fade began (0 -> 1 over FadeSeconds, smooth). */
	CODEXTACTICS_API float FadeAlpha(float ElapsedSeconds, const FGateExitConfig& Config);

	/** The "MISSION COMPLETE" title's alpha (0 until the screen is black, then up over TitleFadeSeconds). */
	CODEXTACTICS_API float TitleAlpha(float ElapsedSeconds, const FGateExitConfig& Config);

	/** The main menu opens now (ReturnToMenuSeconds after the black screen; never when 0 or in a headless check). */
	CODEXTACTICS_API bool ShouldReturnToMenu(float ElapsedSeconds, const FGateExitConfig& Config, bool bHeadless);
}
