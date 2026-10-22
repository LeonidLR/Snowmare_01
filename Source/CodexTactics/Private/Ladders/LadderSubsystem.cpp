#include "Ladders/LadderSubsystem.h"

#include "Ladders/LadderActor.h"

void ULadderSubsystem::Register(ALadderActor* Ladder)
{
	if (Ladder)
	{
		Ladders.RemoveAll([](const TWeakObjectPtr<ALadderActor>& Entry) { return !Entry.IsValid(); });
		Ladders.AddUnique(Ladder);
	}
}

void ULadderSubsystem::Unregister(ALadderActor* Ladder)
{
	Ladders.RemoveAll([Ladder](const TWeakObjectPtr<ALadderActor>& Entry) { return !Entry.IsValid() || Entry.Get() == Ladder; });
}

TArray<ALadderActor*> ULadderSubsystem::GetLadders() const
{
	TArray<ALadderActor*> Result;
	for (const TWeakObjectPtr<ALadderActor>& Entry : Ladders)
	{
		if (ALadderActor* Ladder = Entry.Get())
		{
			Result.Add(Ladder);
		}
	}
	return Result;
}

ALadderActor* ULadderSubsystem::FindLinkEndNear(const FVector& Location, float RadiusCm, bool& bOutTop) const
{
	ALadderActor* Best = nullptr;
	float BestDistance = RadiusCm;
	for (const TWeakObjectPtr<ALadderActor>& Entry : Ladders)
	{
		ALadderActor* Ladder = Entry.Get();
		if (!Ladder || !Ladder->IsClimbable())
		{
			continue;
		}
		for (const bool bTop : { false, true })
		{
			const FVector End = bTop ? Ladder->GetTopLinkWorld() : Ladder->GetBottomLinkWorld();
			const float Distance = FVector::Dist2D(End, Location);
			if (Distance <= BestDistance && FMath::Abs(End.Z - Location.Z) <= 80.f)
			{
				Best = Ladder;
				BestDistance = Distance;
				bOutTop = bTop;
			}
		}
	}
	return Best;
}
