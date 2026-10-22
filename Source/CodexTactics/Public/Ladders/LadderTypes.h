#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "LadderTypes.generated.h"

class UMaterialInterface;
class UStaticMesh;

/**
 * Ladder system types (docs/design/LADDERS.md, user decisions 2026-10-22). UE-only system: the Godot archive has no ladders.
 */

/** One climb state machine (LADDERS.md section 5.1; LadderRules::Transition is the table). */
UENUM(BlueprintType)
enum class ELadderClimbState : uint8
{
	None UMETA(DisplayName = "None"),
	/** The ladder is leased by another climber (or he stands up first): holds at the link start. */
	Queue UMETA(DisplayName = "Queue"),
	/** Code glide from the link start onto the hang line at the floor, turning to the wall. */
	AlignBottom UMETA(DisplayName = "Align Bottom"),
	/** Code glide on the platform to the top link point, turning to face the drop. */
	AlignTop UMETA(DisplayName = "Align Top"),
	/** IntoFromTop clip (blend-in 0), own collision off, warped onto the hang line. */
	EnterTop UMETA(DisplayName = "Enter Top"),
	ClimbUp UMETA(DisplayName = "Climb Up"),
	ClimbDown UMETA(DisplayName = "Climb Down"),
	/** ClimbOutTop clip, own collision off, warped onto the top exit point. */
	ExitTop UMETA(DisplayName = "Exit Top"),
	/** Blend out of the loop pose at the floor. */
	ExitBottom UMETA(DisplayName = "Exit Bottom"),
	/** Knocked down / killed on the ladder: falling to the bottom (LADDERS.md 7.7). */
	Dropping UMETA(DisplayName = "Dropping")
};

/** What happened to the climb state machine (LadderRules::Transition). */
enum class ELadderClimbEvent : uint8
{
	LinkReachedBottom,
	LinkReachedTop,
	MustWait,
	AlignDone,
	EnterDone,
	TargetReached,
	ExitDone,
	Abort,
	Drop,
	Landed
};

/** Why a climb ended early (the one interrupt path, ULadderClimbComponent::AbortClimb). */
UENUM(BlueprintType)
enum class ELadderAbortReason : uint8
{
	None,
	MontageInterrupted,
	Death,
	Knockdown,
	SaveLoad,
	EndPlay,
	LeaseLost,
	Timeout,
	Teleported
};

/** Why a climb order was refused at the link (LADDERS.md 5.2). */
UENUM(BlueprintType)
enum class ELadderRefusal : uint8
{
	None,
	TurnBased,
	Carrying,
	Mounted,
	KnockedDown,
	Dead,
	/** Fewer than MinClimbableRungs rungs or no top exit: a decoration ladder. */
	NotClimbable,
	/** Leased by another climber: not a refusal, he queues. */
	Leased,
	/** Not standing: not a refusal, he stands up first. */
	MustStand,
	/** No clips for this body (a climbing class without its clip set). */
	NoClips
};

/** A limb that grips the rungs (rung IK, LADDERS.md 6.2). */
enum class ELadderLimb : uint8
{
	HandL,
	HandR,
	FootL,
	FootR,
	Count
};

/**
 * Look of one ladder type (rows of a DT_LadderTypes table; LADDERS.md 3.2). The defaults are the engine's basic shapes (a graybox
 * steel ladder) so a ladder draws without any imported art; the course meshes (SM_LadderPole / SM_LadderRung / ...) go into rows.
 */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FLadderMeshData : public FTableRowBase
{
	GENERATED_BODY()

	/** Rail segment, tiled along the rail without stretching (its own bounds height = one segment). Pivot read from the bounds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ladder")
	TSoftObjectPtr<UStaticMesh> RailMesh;

	/** Rung, its length along local Z (the basic cylinder), scaled to the ladder width. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ladder")
	TSoftObjectPtr<UStaticMesh> RungMesh;

	/** Wall bracket (a cube scaled from the rail back to the wall). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ladder")
	TSoftObjectPtr<UStaticMesh> BracketMesh;

	/** Material with a "Color" vector parameter (BasicShapeMaterial); empty = the mesh's own. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ladder")
	TSoftObjectPtr<UMaterialInterface> Material;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ladder")
	FLinearColor RailColor = FLinearColor(0.16f, 0.17f, 0.19f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ladder")
	FLinearColor RungColor = FLinearColor(0.85f, 0.55f, 0.08f);

	/** Rail / rung diameters, cm (the basic shapes are scaled to them). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ladder", meta = (ClampMin = "1"))
	float RailDiameterCm = 6.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ladder", meta = (ClampMin = "1"))
	float RungDiameterCm = 4.f;
};
