#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "OperativeAnimGraphLibrary.generated.h"

class UAnimBlueprint;
class UBlendSpace;

/**
 * Editor helper that authors the operatives' AnimGraph (called from Scripts/Editor/setup_operative_animation.py).
 * Builds a readable, editable graph the user can polish in the AnimBP editor:
 *
 *   stance (bIsProne / bIsCrouching) -> aim (bIsAiming) -> blend space players (Direction, speed axis, play rate)
 *   -> cached "Locomotion" -> Layered blend per bone (upper body from UpperBodyBone) with a montage Slot -> Output.
 *
 * The variables are UOperativeAnimInstance state (Direction, StandBlendSpeed / StandPlayRate for the 0..480 standing
 * blend space, SlowBlendSpeed / SlowPlayRate for the 0..120 crouch / aim ones, bIsAiming, bIsCrouching, bIsProne).
 * Godot reference: Scripts/components/locomotion_controller.gd (StandLocomotion / CrouchLocomotion / ProneIdle,
 * upper-body weapon layer split at the spine).
 */
UCLASS()
class UOperativeAnimGraphLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Replaces the AnimGraph of AnimBlueprint with the operative locomotion graph and compiles it.
	 * Prone uses ProneBlendSpace (the crouch one when there is no prone set yet).
	 * @return false (with the reason in OutReport) when the graph could not be built.
	 */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Editor")
	static bool BuildOperativeLocomotionGraph(UAnimBlueprint* AnimBlueprint, UBlendSpace* StandBlendSpace,
		UBlendSpace* StandAimBlendSpace, UBlendSpace* CrouchBlendSpace, UBlendSpace* CrouchAimBlendSpace,
		UBlendSpace* ProneBlendSpace, FName SlotName, FName UpperBodyBone, float StanceBlendTime, FString& OutReport);

	/**
	 * Replaces the AnimGraph of an enemy AnimBlueprint (parent UEnemyAnimInstance) and compiles it:
	 * idle / walk / run sequence players reading IdleAnimation / WalkAnimation / RunAnimation (set per enemy on the
	 * class defaults) with WalkPlayRate / RunPlayRate, switched by bIsRunning and bIsMoving, then a full-body Slot
	 * for the attacks, hits, pounce and death the anim instance plays. Godot reference: enemy_base.gd _play_anim.
	 */
	/** Number of nodes in the AnimGraph besides the Output Pose (0 = empty; the setup scripts only build empty graphs). */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Editor")
	static int32 CountAnimGraphNodes(UAnimBlueprint* AnimBlueprint);

	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Editor")
	static bool BuildEnemyLocomotionGraph(UAnimBlueprint* AnimBlueprint, FName SlotName, float BlendTime, FString& OutReport);
};
