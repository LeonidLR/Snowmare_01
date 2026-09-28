#include "Interactables/InteractionSubsystem.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "Engine/World.h"
#include "Interactables/InteractableActor.h"

bool UInteractionSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UInteractionSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UInteractionSubsystem, STATGROUP_Tickables);
}

bool UInteractionSubsystem::RequestInteraction(AInteractableActor* Target)
{
	USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>();
	AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
	if (!Target || !Leader)
	{
		return false;
	}
	Pending = Target;
	if (!TryInteract())
	{
		Leader->OrderMoveTo(Target->GetApproachPoint(Leader->GetActorLocation()), false);
	}
	return true;
}

void UInteractionSubsystem::CancelInteraction()
{
	Pending.Reset();
}

void UInteractionSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (Pending.IsValid())
	{
		TryInteract();
	}
}

bool UInteractionSubsystem::TryInteract()
{
	AInteractableActor* Target = Pending.Get();
	const USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>();
	if (!Target || !Squad)
	{
		Pending.Reset();
		return false;
	}

	AOperativeCharacter* Nearest = nullptr;
	float NearestDistance = TNumericLimits<float>::Max();
	for (AOperativeCharacter* Member : Squad->GetMembers())
	{
		const float Distance = Target->GetDistanceTo(Member->GetActorLocation());
		if (Distance < NearestDistance)
		{
			NearestDistance = Distance;
			Nearest = Member;
		}
	}
	if (!Nearest || NearestDistance > Target->InteractionDistance)
	{
		return false;
	}
	Pending.Reset();
	Target->Interact(Nearest);
	return true;
}
