#pragma once

// Shared helpers of the dev smoke commands (not shipped).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Interactables/GateActor.h"
#include "Engine/StaticMeshActor.h"

namespace SmokeUtils
{
	/**
	 * Transform of the L_MovementTest layout, from the checkpoint gate (else the "Floor" actor), created by
	 * create_movement_test_map.py. The user may move / rotate the whole layout in the editor; smokes express their
	 * points in the original design coordinates and map them through this transform. Identity when there is no Floor.
	 */
	inline FTransform LayoutTransform(const UWorld* World)
	{
		// Anchor on the checkpoint gate (design (0, -2000), yaw 90): the floor may be resized for play-testing
		// (e.g. stretched to a far PlayerStart), which moves its centre but not the gate.
		if (World)
		{
			for (TActorIterator<AGateActor> It(const_cast<UWorld*>(World)); It; ++It)
			{
				const float Yaw = It->GetActorRotation().Yaw - 90.f;
				const FRotator LayoutRotation(0.f, Yaw, 0.f);
				const FVector Gate = It->GetActorLocation();
				const FVector Origin = FVector(Gate.X, Gate.Y, 0.f) - LayoutRotation.RotateVector(FVector(0.f, -2000.f, 0.f));
				return FTransform(LayoutRotation, Origin);
			}
		}
		if (World)
		{
			for (TActorIterator<AStaticMeshActor> It(const_cast<UWorld*>(World)); It; ++It)
			{
#if WITH_EDITOR
				const bool bFloor = It->GetActorLabel() == TEXT("Floor");
#else
				const bool bFloor = It->GetName().StartsWith(TEXT("Floor"));
#endif
				if (bFloor)
				{
					const FVector Location = It->GetActorLocation();
					return FTransform(FRotator(0.f, It->GetActorRotation().Yaw, 0.f), FVector(Location.X, Location.Y, 0.f));
				}
			}
		}
		return FTransform::Identity;
	}

	/** Design-coordinate point of L_MovementTest (layout at the origin, yaw 0) -> world point of the current layout. */
	inline FVector LevelPoint(const UWorld* World, const FVector& DesignPoint)
	{
		return LayoutTransform(World).TransformPosition(DesignPoint);
	}

	/**
	 * Puts the squad at the test start of L_MovementTest (design origin, facing design +X, commander ahead of the other two), the
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
		const FTransform Layout = LayoutTransform(World);
		const FVector Offsets[] = { FVector(0.f, 0.f, 0.f), FVector(-280.f, -260.f, 0.f), FVector(-280.f, 260.f, 0.f) };
		for (int32 Index = 0; Index < Members.Num(); ++Index)
		{
			AOperativeCharacter* Member = Members[Index];
			Member->StopOperative();
			const FVector Offset = Offsets[FMath::Min(Index, 2)];
			Member->TeleportTo(Layout.TransformPosition(FVector(Offset.X, Offset.Y, 100.f)), Layout.Rotator(), false, true);
		}
		Squad->SetLeaderByIndex(0);
	}
}

#endif // !UE_BUILD_SHIPPING
