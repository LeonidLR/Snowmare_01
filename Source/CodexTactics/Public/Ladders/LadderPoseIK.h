#pragma once

#include "CoreMinimal.h"
#include "Ladders/LadderClimbComponent.h"

struct FPoseContext;

/**
 * Rung IK on an evaluated pose (docs/design/LADDERS.md 6.2): two-bone IK of the hands / feet onto their rung grips, in component space,
 * after the whole anim graph - no ABP node. Shared by the operatives' (FOperativeAnimInstanceProxy) and the climbing enemies'
 * (FEnemyAnimInstanceProxy) anim proxies; bone names of the UE4 mannequin / UE5 Manny (upperarm / lowerarm / hand, thigh / calf / foot /
 * ball). Worker-thread safe (reads only the pose and the frame). UE-only system (no Godot reference).
 */
namespace LadderPoseIK
{
	/**
	 * Moves the gripping limbs of Output onto Frame's rung grips (each by its alpha). OutKneeOffset[0 / 1] = how far the IK moved the
	 * left / right knee (component space; zero = untouched).
	 */
	CODEXTACTICS_API void Apply(FPoseContext& Output, const FTransform& ComponentTransform, const FLadderIKFrame& Frame, FVector OutKneeOffset[2]);
}
