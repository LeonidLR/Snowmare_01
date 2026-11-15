#pragma once

#include "CoreMinimal.h"
#include "Characters/OperativeMovementRules.h"
#include "Tactics/CoverTypes.h"
#include "DoorwayBreachRules.generated.h"

class AOperativeCharacter;

/**
 * Tactical doorway breach CQB roles (docs/design/DOORWAY_BREACH.md).
 * Step 1: Pure geometry and slot logic.
 */
UENUM(BlueprintType)
enum class EDoorwayBreachSlotRole : uint8
{
	/** Left door jamb: hugs exterior wall in cover, leans past left frame, covers deep right interior. */
	LeftJamb,
	/** Right door jamb: hugs exterior wall in cover, leans past right frame, covers deep left interior. */
	RightJamb,
	/** Center point: offset 2.2 m back on outward normal, crouched low-ready, covers center opening. */
	CenterPoint,
	/** Rear security: offset 3.5 m back on outward normal, 180 deg reverse facing, watches rear. */
	RearSecurity
};

/** Tunables for doorway breach positioning and tolerances. */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FDoorwayBreachConfig
{
	GENERATED_BODY()

	/** Standoff along exterior wall face from opening edge to operative body center, cm (default 30). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DoorwayBreach")
	float CornerStandoffCm = 30.f;

	/** Standoff out from wall surface along exterior normal, cm (default 45, FCoverTraceConfig::SlotOffsetCm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DoorwayBreach")
	float WallOffsetCm = 45.f;

	/** Distance back from doorway threshold along outward normal for center point operative, cm (default 220 = 2.2 m). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DoorwayBreach")
	float CenterStandoffCm = 220.f;

	/** Distance back from doorway threshold along outward normal for rear security operative, cm (default 350 = 3.5 m). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DoorwayBreach")
	float RearStandoffCm = 350.f;

	/** In-line stack spacing when one jamb is obstructed and operative stacks behind the open jamb, cm (default 60). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DoorwayBreach")
	float FallbackStackSpacingCm = 60.f;

	/** Minimum opening width for standard breach triad, cm (narrow doorways convert opposite jamb to stack). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DoorwayBreach")
	float MinDoorWidthCm = 60.f;

	/** Maximum opening width for doorway / gate breach, cm (designer answer #3: gates up to 600 cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DoorwayBreach")
	float MaxGateWidthCm = 600.f;

	/** Default cross-coverage aim angle into room, degrees (default 45 deg). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DoorwayBreach")
	float AimCrossAngleDeg = 45.f;

	/** Portal cursor click slack along opening width, cm (within opening + 40 cm triggers breach vs wall cover). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DoorwayBreach")
	float PortalSlackCm = 40.f;

	/** Portal cursor click depth tolerance across threshold line, cm (default 50 cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DoorwayBreach")
	float PortalDepthSlackCm = 50.f;
};

/** One calculated slot in a doorway breach formation. */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FDoorwayBreachSlot
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DoorwayBreach")
	EDoorwayBreachSlotRole Role = EDoorwayBreachSlotRole::LeftJamb;

	/** World location for operative's feet. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DoorwayBreach")
	FVector Location = FVector::ZeroVector;

	/** World orientation (actor yaw). For jambs: back to wall (faces along outward normal). For center: facing into opening. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DoorwayBreach")
	FRotator Rotation = FRotator::ZeroRotator;

	/** Normalized vector indicating primary aim / sector coverage direction. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DoorwayBreach")
	FVector AimDirection = FVector::ForwardVector;

	/** Tactical cover slot data (for jambs). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DoorwayBreach")
	FCoverSlot CoverSlot;

	/** True if this slot requires hugging wall cover (jambs); false for free-standing center / rear overwatch. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DoorwayBreach")
	bool bRequiresCover = true;

	/** Which side the operative peeks / leans past the jamb frame. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DoorwayBreach")
	ECoverFacing PeekFacing = ECoverFacing::Right;

	/** Stance for operative in this slot (default Crouching for low-ready / stealth profile). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DoorwayBreach")
	EOperativeStance Stance = EOperativeStance::Crouching;

	/** True if obstructed by geometry, missing navmesh, or user-disabled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DoorwayBreach")
	bool bBlocked = false;

	bool IsValid() const { return !Location.IsZero() && !bBlocked; }
};

/** Complete breach constellation (Left Jamb, Right Jamb, Center Point, and optional Rear Security). */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FDoorwayBreachTriad
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DoorwayBreach")
	FDoorwayBreachSlot LeftJamb;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DoorwayBreach")
	FDoorwayBreachSlot RightJamb;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DoorwayBreach")
	FDoorwayBreachSlot CenterPoint;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DoorwayBreach")
	FDoorwayBreachSlot RearSecurity;

	/** Threshold center on the opening line. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DoorwayBreach")
	FVector DoorwayCenter = FVector::ZeroVector;

	/** Unit normal pointing outward from interior to exterior approach side. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DoorwayBreach")
	FVector OutwardNormal = FVector::ForwardVector;

	/** Unit vector pointing along the wall opening from left to right jamb. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DoorwayBreach")
	FVector WallTangent = FVector::RightVector;

	/** Opening width in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DoorwayBreach")
	float OpeningWidthCm = 120.f;

	/** Unique doorway or gate ID if from layout. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DoorwayBreach")
	FString DoorwayId;

	/** Whether a 4th slot (rear security) is populated. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DoorwayBreach")
	bool bHasRearSecurity = true;

	bool IsValid() const { return OpeningWidthCm > 0.f && !DoorwayCenter.IsZero(); }

	const FDoorwayBreachSlot* GetSlotByRole(EDoorwayBreachSlotRole Role) const;
	FDoorwayBreachSlot* GetSlotByRole(EDoorwayBreachSlotRole Role);
	TArray<FDoorwayBreachSlot> GetAllSlots() const;
};

/** Assignment of an operative to a specific doorway breach slot. */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FDoorwaySlotAssignment
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DoorwayBreach")
	int32 OperativeIndex = INDEX_NONE;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DoorwayBreach")
	FDoorwayBreachSlot Slot;
};

namespace DoorwayBreachRules
{
	/** Global default config. */
	CODEXTACTICS_API const FDoorwayBreachConfig& GetConfig();

	/**
	 * Computes the 4-slot doorway breach constellation from door geometry.
	 * @param DoorwayCenter Center of doorway opening on threshold.
	 * @param OpeningWidthCm Width of the opening in cm.
	 * @param WallTangent Direction vector along the wall opening.
	 * @param OutwardNormal Unit normal pointing outward to exterior approach side.
	 * @param Config Tunables for standoffs and angles.
	 */
	CODEXTACTICS_API FDoorwayBreachTriad CalculateBreachSlots(
		const FVector& DoorwayCenter,
		float OpeningWidthCm,
		const FVector& WallTangent,
		const FVector& OutwardNormal,
		const FDoorwayBreachConfig& Config = GetConfig());

	/**
	 * Adjusts triad when one jamb is obstructed (Designer Answer #2: open jamb is kept, center covers,
	 * 3rd operative stacks in-line behind the open jamb).
	 */
	CODEXTACTICS_API void ApplyJambObstruction(
		FDoorwayBreachTriad& Triad,
		bool bLeftBlocked,
		bool bRightBlocked,
		const FDoorwayBreachConfig& Config = GetConfig());

	/**
	 * Assigns operatives to breach slots via minimal Euclidean travel distance (avoiding path crossings).
	 * If bSoloMode is true, assigns ONLY the selected operative to the nearest valid jamb.
	 * If 2 operatives: assigns to both jambs (or open jamb + center if one jamb is blocked).
	 * If 3 operatives: assigns to Left, Right, Center (or open jamb + center + in-line stack).
	 * If 4 operatives: assigns to Left, Right, Center, Rear Security.
	 */
	CODEXTACTICS_API TArray<FDoorwaySlotAssignment> AssignSlots(
		const TArray<FVector>& OperativePositions,
		const FDoorwayBreachTriad& Triad,
		bool bSoloMode = false,
		int32 SelectedIndex = 0,
		const FDoorwayBreachConfig& Config = GetConfig());

	/**
	 * Tests whether a world point (e.g. cursor trace hit) is within the doorway portal interaction zone.
	 * Within opening width + PortalSlackCm laterally, and within PortalDepthSlackCm across threshold line.
	 */
	CODEXTACTICS_API bool IsPointInDoorwayPortal(
		const FVector& Point,
		const FVector& DoorwayCenter,
		float OpeningWidthCm,
		const FVector& WallTangent,
		const FVector& OutwardNormal,
		float LateralSlackCm = 40.f,
		float DepthSlackCm = 50.f);
}
