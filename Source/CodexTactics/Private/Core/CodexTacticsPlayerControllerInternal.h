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
#include "Interactables/WindowBreakage.h"
#include "Tactics/CoverTraceRules.h"
#include "Tactics/WindowCoverRules.h"

namespace ControllerWindowCover
{
	/**
	 * 144: True when a click on a window sill should target cover / shimmy / wall-slide on the operative's wall-hug side
	 * rather than climbing through to the far side.
	 */
	inline bool ShouldTakeWindowClickAsCover(UWorld* World, const USquadSubsystem* Squad, const AOperativeCharacter* Leader,
		const FHitResult& Hit, bool bAlt, AActor*& OutSill, FVector& OutSideNormal, bool& bOutFormationSlide)
	{
		OutSill = nullptr;
		OutSideNormal = FVector::ForwardVector;
		bOutFormationSlide = false;
		if (!World || !Leader || !Hit.bBlockingHit)
		{
			return false;
		}
		AActor* Sill = WindowBreakage::FindWindowSillAt(*World, Hit);
		if (!Sill)
		{
			return false;
		}
		const FBox SillBox = Sill->GetComponentsBoundingBox(true);
		if (!SillBox.IsValid)
		{
			return false;
		}
		const FVector SillCenter = SillBox.GetCenter();
		const float Limit = WindowBreakage::FaceMarginCm + WindowBreakage::OpeningPadCm;
		if (Leader->bInCover)
		{
			const FCoverSlot& Own = Leader->GetCoverSlot();
			const bool bSameWall = WindowCoverRules::OnSameWallPlane(SillCenter, Own.WallPoint, Own.WallNormal, Limit);
			const bool bSameSide = FVector::DotProduct(Leader->GetActorLocation() - SillCenter, Own.WallNormal) >= -WindowBreakage::FaceMarginCm;
			if (WindowCoverRules::CanClickWindowAsCover(bSameWall, bAlt, bSameSide))
			{
				OutSill = Sill;
				OutSideNormal = Own.WallNormal.GetSafeNormal2D();
				return true;
			}
		}
		float WallYaw = 0.f;
		if (Squad && Squad->HasFormationWallOrder() && Squad->GetFormationWallNormalYaw(WallYaw))
		{
			const FVector WallNormal = FRotator(0.f, WallYaw, 0.f).Vector().GetSafeNormal2D();
			const bool bSameWall = WindowCoverRules::OnSameWallPlane(SillCenter, Leader->GetActorLocation() - WallNormal * 45.f, WallNormal, Limit);
			if (WindowCoverRules::CanClickWindowAsCover(bSameWall, bAlt, bSameWall))
			{
				OutSill = Sill;
				OutSideNormal = WallNormal;
				bOutFormationSlide = !bAlt;
				return true;
			}
		}
		if (bAlt)
		{
			const FVector Extent = SillBox.GetExtent();
			FVector Normal = Extent.X <= Extent.Y ? FVector(1.f, 0.f, 0.f) : FVector(0.f, 1.f, 0.f);
			if (FVector::DotProduct(Leader->GetActorLocation() - SillCenter, Normal) < 0.f)
			{
				Normal = -Normal;
			}
			OutSill = Sill;
			OutSideNormal = Normal;
			return true;
		}
		return false;
	}

	/** 144: Probes the cover slot on SideNormal's face of Sill (even if the cursor hit the glass or the floor past the opening). */
	inline bool ResolveWindowCoverSlot(UWorld* World, const AActor& Sill, const FVector& SideNormal, FCoverSlot& OutSlot)
	{
		OutSlot = FCoverSlot();
		const FBox SillBox = Sill.GetComponentsBoundingBox(true);
		const FVector Normal = SideNormal.GetSafeNormal2D();
		if (!World || !SillBox.IsValid || Normal.IsNearlyZero())
		{
			return false;
		}
		const FVector Center = SillBox.GetCenter();
		const float Half = static_cast<float>(FMath::Abs(FVector::DotProduct(SillBox.GetExtent(), Normal)));
		const FVector Probe(Center.X + Normal.X * (Half + 15.f), Center.Y + Normal.Y * (Half + 15.f), SillBox.Max.Z - 20.f);
		return CoverTraceRules::FindCoverSlotAt(World, Probe, -Normal, OutSlot) && OutSlot.IsValid();
	}
}

namespace ControllerGroupCover
{
	/**
	 * Free cover slots of other objects round LeaderSlot (CodexTacticsPlayerControllerCover.cpp): traces from the squad's side towards the
	 * threat side on two rows across RadiusCm either way along it; a face looking the same way (GroupCoverRules::IsNearbyCoverUsable, within
	 * SideDeg) gives a slot; up to Count, nearest first, one man per spot (GroupCoverRules::ChooseNearbyCovers), none in Claimed / IsTaken.
	 * The group cover order's item 3 (2026-11-03) and the run-and-peek's stack that does not fit behind the corner man (2026-11-08).
	 */
	TArray<FCoverSlot> FindNearbyGroupCoverSlots(UWorld* World, const FCoverSlot& LeaderSlot, const TArray<FCoverSlot>& Claimed, int32 Count, float RadiusCm,
		float SideDeg, TFunctionRef<bool(const FVector&)> IsTaken);
}

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

	/**
	 * The feed line of a selection: "Operative selected: X"; bug 2026-11-08 - while the formation finishes the old leader's order
	 * (USquadSubsystem::SelectLeader made X the pending leader) "... - leads from the next order (Y finishes the squad's move)".
	 */
	inline FText SelectionLine(const USquadSubsystem& Squad, const AOperativeCharacter& Picked)
	{
		const AOperativeCharacter* Formation = Squad.GetFormationLeader();
		if (Squad.GetPendingLeader() == &Picked && Formation && Formation != &Picked)
		{
			return FText::Format(NSLOCTEXT("CodexTacticsPlayerController", "UnitSelectedPending",
				"👤 Operative selected: {0} - leads from the next order ({1} finishes the squad's move)"), Picked.DisplayName, Formation->DisplayName);
		}
		return FText::Format(NSLOCTEXT("CodexTacticsPlayerController", "UnitSelected", "👤 Operative selected: {0}"), Picked.DisplayName);
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
