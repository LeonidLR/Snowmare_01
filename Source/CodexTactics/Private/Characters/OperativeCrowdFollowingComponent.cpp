// UE-only (no Godot reference). See the header: the crowd's overshoot finish limited to the path's end.

#include "Characters/OperativeCrowdFollowingComponent.h"

#include "CodexTactics.h"
#include "Characters/OperativeMovementRules.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "NavigationData.h"

UOperativeCrowdFollowingComponent::UOperativeCrowdFollowingComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UOperativeCrowdFollowingComponent::ApplyCrowdAgentVelocity(const FVector& NewVelocity, const FVector& DestPathCorner, bool bTraversingLink,
	bool bIsNearEndOfPath)
{
	Super::ApplyCrowdAgentVelocity(NewVelocity, DestPathCorner, bTraversingLink, bIsNearEndOfPath); // re-arms the test near the end
	LimitOvershootTest();
}

void UOperativeCrowdFollowingComponent::SetMoveSegment(int32 SegmentStartIndex)
{
	Super::SetMoveSegment(SegmentStartIndex);
	LimitOvershootTest(); // a new path: Reset() armed the test with the previous walk's direction
}

void UOperativeCrowdFollowingComponent::LimitOvershootTest()
{
	if (!bCanCheckMovingTooFar || !Path.IsValid() || !Path->IsValid() || Path->GetPathPoints().Num() == 0)
	{
		return;
	}
	const AController* Controller = Cast<AController>(GetOwner());
	const APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (!Pawn)
	{
		return;
	}
	const float ToEnd = static_cast<float>(FVector::Dist2D(Pawn->GetActorLocation(), Path->GetEndLocation()));
	if (OperativeMovementRules::AllowsCrowdOvershootFinish(ToEnd))
	{
		return;
	}
	bCanCheckMovingTooFar = false;
	static const IConsoleVariable* MoveTrace = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Debug.MoveTrace"));
	if (MoveTrace && MoveTrace->GetInt() != 0 && !CrowdAgentMoveDirection.IsNearlyZero()
		&& FVector::DotProduct(Path->GetEndLocation() - Pawn->GetActorLocation(), CrowdAgentMoveDirection) < 0.0)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[MoveTrace] %s: crowd overshoot finish suppressed %.0f cm from the path's end (direction away from the goal)"),
			*Pawn->GetName(), ToEnd);
	}
}
