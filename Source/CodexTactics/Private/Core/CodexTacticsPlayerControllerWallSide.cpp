// ACodexTacticsPlayerController: the far side of a wall (the game designer's play-test 2026-11-01 on L_FormationLab, bug_02, approved fix a):
// the wall face a click means by the cursor (ResolveWallSideSlot, WallSideRules). Fix b (the tight pick at a wall) lives in FindClickedMember
// (Selection), the formation's route round the wall (d / e) in USquadSubsystem::ApplyFormationWallRoute. Fix c (the occluder fade: a wall
// hiding the cursor's target went see-through) was removed on the game designer's decision 2026-11-01 (bug_05: "a failed solution" - walls never
// fade). Godot reference: none (UE-only).

#include "Core/CodexTacticsPlayerController.h"
#include "Core/CodexTacticsPlayerControllerInternal.h"
#include "Camera/PlayerCameraManager.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/EnemyGhostActor.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/InteractableActor.h"
#include "Tactics/CoverGhostActor.h"
#include "Tactics/CoverTraceRules.h"
#include "Tactics/WallSideRules.h"

#define LOCTEXT_NAMESPACE "CodexTacticsPlayerController"

namespace ControllerWallSideLocal
{
	TAutoConsoleVariable<int32> CVarCursorWallSide(TEXT("Codex.Cursor.WallSide"), 1,
		TEXT("Game designer 2026-11-01 (fix a): 1 = the wall side a click means is decided by the cursor - a high wall's top beyond its middle / the ground right behind it = the FAR face, the top's near half = the near face; 0 = the old rule (a top click is a move, the ghost on the face the camera sees)"));

	/** The faces of a top hit are probed this far under it (inside the wall's height), cm. */
	constexpr float FaceProbeBelowTopCm = 30.f;
	/** The ground rule looks back towards the camera at this height over the clicked ground (a man's chest), cm. */
	constexpr float GroundProbeChestCm = 110.f;
	/** A face counts when its normal's Z is at most this (a wall, not a slope). */
	constexpr float FaceMaxNormalZ = 0.3f;
	/** The wall's top is looked for this high over the clicked ground, cm. */
	constexpr float TopProbeAboveCm = 800.f;

	FVector PlanarView(const APlayerCameraManager* Camera)
	{
		FVector View = Camera ? Camera->GetCameraRotation().Vector() : FVector::ForwardVector;
		View.Z = 0.f;
		return View.Normalize() ? View : FVector::ForwardVector;
	}

	FVector Planar(const FVector& Vector)
	{
		return FVector(Vector.X, Vector.Y, 0.f).GetSafeNormal();
	}
}

bool ACodexTacticsPlayerController::ResolveWallSideSlot(const FHitResult& Hit, FCoverSlot& OutSlot, EWallClickSide& OutSide) const
{
	using namespace ControllerWallSideLocal;
	OutSlot = FCoverSlot();
	OutSide = EWallClickSide::None;
	UWorld* World = GetWorld();
	const USquadSubsystem* Squad = GetSquad();
	const AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
	AActor* HitActor = Hit.GetActor();
	UPrimitiveComponent* HitComponent = Hit.GetComponent();
	if (CVarCursorWallSide.GetValueOnGameThread() == 0 || !World || !Leader || !HitActor || !HitComponent || HitActor->IsA<APawn>()
		|| HitActor->IsA<AInteractableActor>() || HitActor->IsA<AEnemyGhostActor>() || HitActor->IsA<ACoverGhostActor>() || HitActor->ActorHasTag(FName(TEXT("Enemy")))
		|| Hit.ImpactNormal.Z < WallSideRules::TopNormalZ)
	{
		return false; // a face hit keeps the face that was hit (FindCoverSlotForHit); bodies / objects keep their meaning
	}
	// The cursor ray's own planar direction (off the screen centre it slants sideways from the camera's forward); the camera's forward when
	// the hit carries no ray (a constructed hit).
	FVector View = Planar(FVector(Hit.ImpactPoint) - Hit.TraceStart);
	if (View.IsNearlyZero() || Hit.TraceStart.Equals(Hit.TraceEnd))
	{
		View = PlanarView(PlayerCameraManager);
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(WallSideTrace), false);
	if (Squad)
	{
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			Params.AddIgnoredActor(Member);
		}
	}
	const auto FaceAlong = [&View](const FHitResult& Face) { return static_cast<float>(FVector::DotProduct(Planar(Face.ImpactNormal), View)); };

	// 1. The top of a wall: its two faces along the ray just under the hit, on the same object (LineTraceComponent: nothing in between counts).
	{
		const FVector Under = Hit.ImpactPoint - FVector(0.f, 0.f, FaceProbeBelowTopCm);
		const float Reach = WallSideRules::MaxWallThicknessCm + 50.f;
		FCollisionQueryParams FaceParams(SCENE_QUERY_STAT(WallSideFace), false);
		FHitResult NearFace;
		FHitResult FarFace;
		const bool bNear = HitComponent->LineTraceComponent(NearFace, Under - View * Reach, Under, FaceParams) && FMath::Abs(NearFace.ImpactNormal.Z) <= FaceMaxNormalZ
			&& FaceAlong(NearFace) <= -WallSideRules::FaceAlongViewMin;
		const bool bFar = HitComponent->LineTraceComponent(FarFace, Under + View * Reach, Under, FaceParams) && FMath::Abs(FarFace.ImpactNormal.Z) <= FaceMaxNormalZ
			&& FaceAlong(FarFace) >= WallSideRules::FaceAlongViewMin;
		if (bNear && bFar)
		{
			const EWallClickSide Side = WallSideRules::SideOfTopHit(static_cast<float>(FVector::DotProduct(Hit.ImpactPoint, View)),
				static_cast<float>(FVector::DotProduct(NearFace.ImpactPoint, View)), static_cast<float>(FVector::DotProduct(FarFace.ImpactPoint, View)));
			if (Side == EWallClickSide::None)
			{
				return false;
			}
			const FHitResult& Face = Side == EWallClickSide::Far ? FarFace : NearFace;
			FCoverSlot Slot;
			// The slot on that face at the click, traced from the face's own side into it.
			if (CoverTraceRules::FindCoverSlotAt(World, Face.ImpactPoint, -Planar(Face.ImpactNormal), Slot) && Slot.Height == ECoverHeight::HighCover)
			{
				OutSlot = Slot;
				OutSide = Side;
				return true;
			}
			return false; // a low wall: its top click keeps its old meaning (a move, a vault)
		}
	}

	// 2. The ground right behind a high wall (the band it hides from the camera), the leader on its other side. Not his own cover object
	// while he is in cover: the ground round it is his wrap along it (TryHandleCoverWrapClick), as before.
	const FVector Chest = Hit.ImpactPoint + FVector(0.f, 0.f, GroundProbeChestCm);
	FHitResult Back;
	if (!World->LineTraceSingleByChannel(Back, Chest, Chest - View * WallSideRules::FarGroundReachMaxCm, ECC_Visibility, Params)
		|| FMath::Abs(Back.ImpactNormal.Z) > FaceMaxNormalZ || !Back.GetActor() || Back.GetActor()->IsA<APawn>() || Back.GetActor()->IsA<AInteractableActor>())
	{
		return false;
	}
	if (Leader->bInCover && (Leader->GetCoverSlot().WallComponent.IsValid() ? Back.GetComponent() == Leader->GetCoverSlot().WallComponent.Get()
		: Back.GetActor() == Leader->GetCoverSlot().WallActor.Get()))
	{
		return false;
	}
	const FVector Normal = Planar(Back.ImpactNormal);
	// The wall's height over the clicked ground: its top straight above the face, just inside it.
	float WallHeight = 0.f;
	{
		const FVector Inside = Back.ImpactPoint - Normal * 10.f;
		FHitResult Top;
		FCollisionQueryParams TopParams(SCENE_QUERY_STAT(WallSideTop), false);
		if (Back.GetComponent() && Back.GetComponent()->LineTraceComponent(Top, FVector(Inside.X, Inside.Y, Hit.ImpactPoint.Z + TopProbeAboveCm),
			FVector(Inside.X, Inside.Y, Hit.ImpactPoint.Z), TopParams))
		{
			WallHeight = static_cast<float>(Top.ImpactPoint.Z - Hit.ImpactPoint.Z);
		}
	}
	// The band the wall hides along THIS ray (its own depression - flatter than the camera's centre pitch for a point far up the screen).
	float Pitch = PlayerCameraManager ? static_cast<float>(PlayerCameraManager->GetCameraRotation().Pitch) : -35.f;
	if (const double Run = FVector::Dist2D(Hit.TraceStart, Hit.ImpactPoint); Run > 1.0 && Hit.TraceStart.Z > Hit.ImpactPoint.Z)
	{
		Pitch = -static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(Hit.TraceStart.Z - Hit.ImpactPoint.Z, Run)));
	}
	const float BandCm = WallSideRules::FarGroundReachCm(WallHeight, Pitch);
	const bool bLeaderOtherSide = FVector::DotProduct(Leader->GetActorLocation() - Back.ImpactPoint, Normal) < 0.f;
	FCoverSlot Slot;
	const bool bSlot = CoverTraceRules::FindCoverSlotAt(World, Back.ImpactPoint, -Normal, Slot);
	if (bSlot && WallSideRules::IsGroundBehindWall(static_cast<float>(FVector::Dist2D(Back.ImpactPoint, Hit.ImpactPoint)), BandCm,
		static_cast<float>(FVector::DotProduct(Normal, View)), Slot.Height == ECoverHeight::HighCover, bLeaderOtherSide))
	{
		OutSlot = Slot;
		OutSide = EWallClickSide::Far;
		return true;
	}
	return false;
}

#undef LOCTEXT_NAMESPACE
