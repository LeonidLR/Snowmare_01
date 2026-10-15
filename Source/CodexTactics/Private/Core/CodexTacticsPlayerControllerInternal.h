// Helpers shared by the translation units of ACodexTacticsPlayerController (CodexTacticsPlayerController*.cpp; UE-only).
// Private to the module. Moved verbatim from CodexTacticsPlayerController.cpp (cleanup 2026-10-15,
// docs/port/REFACTOR_CONTROLLER_2026-10.md); named namespaces so unity builds see no duplicate file-local helpers.

#pragma once

#include "CoreMinimal.h"
#include "HAL/IConsoleManager.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/OperativeMovementRules.h"
#include "Characters/SquadSubsystem.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Core/PlayerCommands.h"
#include "Engine/World.h"
#include "Interactables/DeployableRules.h"

namespace ControllerCommands
{
	/**
	 * Demo 01 command choke point: reports a logical player command (PlayerCommands::Report) with the leader (or the
	 * given operative) as the actor. Pure notification - the handler's behaviour is unchanged.
	 */
	inline void Report(const ACodexTacticsPlayerController* Controller, FName CommandId, AActor* Target = nullptr, bool bPlanned = false,
		bool bSquadWide = false, AActor* Operative = nullptr, const FVector& Location = FVector::ZeroVector)
	{
		if (!Controller)
		{
			return;
		}
		const UWorld* World = Controller->GetWorld();
		const USquadSubsystem* Squad = World ? World->GetSubsystem<USquadSubsystem>() : nullptr;
		FPlayerCommandContext Context;
		Context.Operative = Operative ? Operative : (Squad ? Squad->GetLeader() : nullptr);
		Context.Target = Target;
		Context.bPlanned = bPlanned;
		Context.bSquadWide = bSquadWide;
		Context.Location = !Location.IsZero() ? Location : (Target ? Target->GetActorLocation() : FVector::ZeroVector);
		PlayerCommands::Report(Controller, CommandId, Context);
	}

	inline FName ForStance(EOperativeStance Stance, bool bSquadWide)
	{
		switch (Stance)
		{
		case EOperativeStance::Crouching: return bSquadWide ? PlayerCommands::SquadStanceCrouch : PlayerCommands::StanceCrouch;
		case EOperativeStance::Prone: return bSquadWide ? PlayerCommands::SquadStanceProne : PlayerCommands::StanceProne;
		default: return bSquadWide ? PlayerCommands::SquadStanceStand : PlayerCommands::StanceStand;
		}
	}

	inline FName ForDeployable(EDeployableType Type)
	{
		switch (Type)
		{
		case EDeployableType::Mine: return PlayerCommands::DeployMine;
		case EDeployableType::Barricade: return PlayerCommands::DeployBarricade;
		case EDeployableType::Turret: return PlayerCommands::DeployTurret;
		default: return PlayerCommands::DeployOther;
		}
	}
}

namespace OrderLock
{
	/**
	 * Codex.RealTimeOrders (1: orders run at once in a real-time wave fight, 0: only in the tactical pause). Defined once in
	 * CodexTacticsPlayerControllerOrders.cpp; read by HandleWorldHit / IsRealTimeOrderLocked there and by ConfirmCoverPreview.
	 */
	extern TAutoConsoleVariable<int32> CVarRealTimeOrders;
}
