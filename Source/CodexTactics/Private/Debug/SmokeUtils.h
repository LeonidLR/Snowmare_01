#pragma once

// Shared helpers of the dev smoke commands (not shipped).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "Engine/World.h"

namespace SmokeUtils
{
	/**
	 * Puts the squad at the test start of L_MovementTest (origin, facing +X, commander ahead of the other two), the
	 * layout the checks were written for (Scripts/Editor/create_movement_test_map.py). The level's PlayerStart may be
	 * moved for play-testing; smokes that rely on distances to map features call this first.
	 */
	inline void PlaceSquadAtTestStart(UWorld* World)
	{
		USquadSubsystem* Squad = World ? World->GetSubsystem<USquadSubsystem>() : nullptr;
		if (!Squad)
		{
			return;
		}
		TArray<AOperativeCharacter*> Members = Squad->GetMembers();
		Members.Sort([](const AOperativeCharacter& A, const AOperativeCharacter& B) { return A.SquadIndex < B.SquadIndex; });
		const FVector Offsets[] = { FVector(0.f, 0.f, 0.f), FVector(-280.f, -260.f, 0.f), FVector(-280.f, 260.f, 0.f) };
		for (int32 Index = 0; Index < Members.Num(); ++Index)
		{
			AOperativeCharacter* Member = Members[Index];
			Member->StopOperative();
			const FVector Offset = Offsets[FMath::Min(Index, 2)];
			Member->TeleportTo(FVector(Offset.X, Offset.Y, 100.f), FRotator::ZeroRotator, false, true);
		}
		Squad->SetLeaderByIndex(0);
	}
}

#endif // !UE_BUILD_SHIPPING
