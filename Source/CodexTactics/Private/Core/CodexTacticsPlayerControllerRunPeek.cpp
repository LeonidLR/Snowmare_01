// ACodexTacticsPlayerController: run-and-peek (#55, user request 2026-11-07: "the peek works only after hugging the wall first - that badly
// hurts the pace of the fight; I want it at once: the operative is running, gets the order to peek round a corner in front of him, runs
// straight to that spot and peeks immediately"). A click on the END of a peekable wall he is not in cover at (the manual corner peek's end
// region) = run to the corner slot on the face of that wall facing him and peek on arrival (AOperativeCharacter::OrderRunCornerPeek); the
// hover shows the peek ghost there. Solo first; a box selection: the member nearest that end takes it, the others run into a stack behind
// him (FormationWallLeadership::PlanGroupCornerPeek, the rule of the group peek in cover); formation: the group wall order at that corner
// with the squad's corner peek carried there (the bug_04 A carry; since the game designer's answer Q2 2026-11-08 it fires once the LEADER is at
// the wall). The tactical pause plans it (Q3: PlanRunCornerPeekClick, run on the release). Pure rules: RunCornerPeekRules.
// Godot reference: none (UE-only; docs/systems/cover-and-peek.md "Run-and-peek").

#include "Core/CodexTacticsPlayerController.h"
#include "Core/CodexTacticsPlayerControllerInternal.h"
#include "Camera/PlayerCameraManager.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/CombatFeedbackSubsystem.h"
#include "Combat/CombatTimeModeRules.h"
#include "Combat/EnemyGhostActor.h"
#include "Combat/HealthComponent.h"
#include "Engine/World.h"
#include "Formation/FormationWallLeadershipRules.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/InteractableActor.h"
#include "Misc/App.h"
#include "Tactics/CoverFacingRules.h"
#include "Tactics/CoverGhostActor.h"
#include "Tactics/CoverRules.h"
#include "Tactics/CoverTraceRules.h"
#include "Tactics/GroupCoverRules.h"
#include "Tactics/RunCornerPeekRules.h"
#include "Tactics/TurnBasedCombatSubsystem.h"
#include "Data/SquadWallTuning.h"
#include "Tutorial/TutorialSubsystem.h"
#include "UI/GameMessageSubsystem.h"
#include "Vehicles/VehicleSubsystem.h"

#define LOCTEXT_NAMESPACE "CodexTacticsPlayerController"

namespace ControllerRunPeekLocal
{
	TAutoConsoleVariable<int32> CVarRunCornerPeek(TEXT("Codex.Cover.RunCornerPeek"), 1,
		TEXT("#55 (user request 2026-11-07): 1 = a click on the END of a wall he is not in cover at sends the operative (a box selection: the man nearest that end, the others stack behind him) to that corner and he peeks round it on arrival, in one click; 0 = the old rule (hug the wall first, then click its end)"));

	/** The clicked wall's faces are probed from this far out round the click, cm. */
	constexpr float FaceReachCm = 300.f;
	/** A face hit is probed this far inside the face (inside the wall's body), cm. */
	constexpr float FaceInsetCm = 10.f;
	/** A top hit is probed this far under the top, cm. */
	constexpr float TopProbeBelowCm = 30.f;
	/** A hit counts as a face when its normal's |Z| is at most this (else a top / the ground). */
	constexpr float RunPeekFaceMaxNormalZ = 0.3f;
	/** A probe from direction D takes a face whose normal points at least this much towards D. */
	constexpr float FaceTowardProbeMinDot = 0.3f;
	/** A stack member within this of his place along the wall keeps it (as the group peek in cover), cm. */
	constexpr double StackKeepCm = 30.0;
	/** The cached faces hold while the cursor stays within this of the cached point, cm. */
	constexpr float CacheMoveCm = 5.f;

	FVector2D PlanarUnit(const FVector& Vector)
	{
		return FVector2D(Vector.X, Vector.Y).GetSafeNormal();
	}

	bool IsAbleToRun(const AOperativeCharacter* Member)
	{
		return Member && Member->HealthComponent && Member->HealthComponent->IsAlive() && !Member->IsKnockedDown() && !Member->IsPanicking() && !Member->IsRaging()
			&& !Member->IsClimbingLadder() && !VehicleQueries::IsMounted(Member);
	}
}

bool ACodexTacticsPlayerController::IsRunCornerPeekEnabled()
{
	return ControllerRunPeekLocal::CVarRunCornerPeek.GetValueOnGameThread() != 0;
}

FCoverSlot ACodexTacticsPlayerController::ResolveCornerStandSlot(const FCoverSlot& Base, ECoverFacing Side, float StandAlong) const
{
	const FVector Right = Base.RightTangent();
	FCoverSlot Corner;
	for (const float Back : { 0.f, 20.f, 40.f, 60.f })
	{
		// The wall's last few cm may not probe: a little further in.
		const float Along = StandAlong + (Side == ECoverFacing::Right ? -Back : Back);
		if (CoverTraceRules::FindShimmySlot(GetWorld(), Base, Base.WallPoint + Right * Along, Corner) && (!Base.WallActor.IsValid() || Corner.WallActor == Base.WallActor))
		{
			return Corner;
		}
	}
	Corner = Base;
	Corner.WorldLocation += Right * StandAlong;
	Corner.WallPoint += Right * StandAlong;
	return Corner;
}

void ACodexTacticsPlayerController::GatherRunCornerPeekFaces(const AOperativeCharacter& Operative, const FHitResult& Hit, TArray<FRunCornerPeekFace>& OutFaces) const
{
	using namespace ControllerRunPeekLocal;
	OutFaces.Reset();
	UWorld* World = GetWorld();
	UPrimitiveComponent* Component = Hit.GetComponent();
	const AActor* HitActor = Hit.GetActor();
	if (!World || !Component || !HitActor)
	{
		return;
	}
	const bool bTop = Hit.ImpactNormal.Z > RunPeekFaceMaxNormalZ;
	// The first probe: the clicked face (a top hit: the face that looks at the camera).
	FVector2D Primary = bTop ? -PlanarUnit(PlayerCameraManager ? PlayerCameraManager->GetCameraRotation().Vector() : FVector::ForwardVector) : PlanarUnit(Hit.ImpactNormal);
	if (Primary.IsNearlyZero())
	{
		Primary = FVector2D(1.0, 0.0);
	}
	const FVector Probe = bTop ? FVector(Hit.ImpactPoint) - FVector(0.f, 0.f, TopProbeBelowCm) : FVector(Hit.ImpactPoint) - FVector(Primary, 0.f) * FaceInsetCm;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(RunCornerPeekFace), false);
	const FCoverFacingConfig Config = Operative.GetCoverFacingConfig();
	TArray<FVector2D> Known;
	for (const FVector2D& Toward : RunCornerPeekRules::ProbeDirections(Primary))
	{
		// The face of the wall seen from that side of the click: only the clicked component counts (LineTraceComponent).
		const FVector Direction(Toward, 0.f);
		FHitResult FaceHit;
		if (!Component->LineTraceComponent(FaceHit, Probe + Direction * FaceReachCm, Probe, Params) || FMath::Abs(FaceHit.ImpactNormal.Z) > RunPeekFaceMaxNormalZ)
		{
			continue;
		}
		const FVector2D Normal = PlanarUnit(FaceHit.ImpactNormal);
		if (FVector2D::DotProduct(Normal, Toward) < FaceTowardProbeMinDot || !RunCornerPeekRules::IsNewFace(Known, Normal))
		{
			continue;
		}
		Known.Add(Normal);
		FCoverSlot Base;
		if (!CoverTraceRules::FindCoverSlotAt(World, FaceHit.ImpactPoint, -FVector(Normal, 0.f), Base) || !Base.IsValid() || Base.Height == ECoverHeight::None
			|| (Base.WallActor.IsValid() && Base.WallActor.Get() != HitActor))
		{
			continue;
		}
		if (!RunCornerPeekRules::IsRunPeekCover(Base.Height))
		{
			return; // low cover (fire over it): never a run-and-peek - a plain cover click (a box selection: the group cover over nearby covers)
		}
		if (!RunCornerPeekRules::IsFaceWideEnough(Base.bLeftEdgeExposed, Base.LeftEdgeDistanceCm, Base.bRightEdgeExposed, Base.RightEdgeDistanceCm))
		{
			continue; // an end face / a thin post: its corners are the long faces' ends
		}
		FRunCornerPeekFace Face;
		Face.Base = Base;
		if (!FindCornerPeekForSlot(Base, Hit, Face.Side, Face.EdgeAlongCm))
		{
			if (Known.Num() == 1 && !bTop)
			{
				return; // the clicked face itself, the click in its middle: a cover click there (no more traces)
			}
			continue;
		}
		Face.EdgeOnFace = Base.WallPoint + Base.RightTangent() * Face.EdgeAlongCm;
		Face.bCrouched = Base.Height == ECoverHeight::LowCover || CoverRules::DefaultStanceFor(Base.Height) == EOperativeStance::Crouching;
		const float Stand = CoverFacingRules::CornerPeekStandAlong(Face.Side, Face.EdgeAlongCm, CoverFacingRules::CornerStandOff(Face.Side, Face.bCrouched, Config));
		Face.Corner = ResolveCornerStandSlot(Base, Face.Side, Stand);
		OutFaces.Add(Face);
	}
}

bool ACodexTacticsPlayerController::FindRunCornerPeekForHit(const AOperativeCharacter* Operative, const FHitResult& Hit, FSquadCornerPeekTarget& OutTarget,
	FRunCornerPeekFace* OutFace) const
{
	using namespace ControllerRunPeekLocal;
	OutTarget = FSquadCornerPeekTarget();
	const AActor* HitActor = Hit.GetActor();
	if (!Operative || !GetWorld() || !HitActor || !Hit.GetComponent() || HitActor->IsA<APawn>() || HitActor->IsA<AInteractableActor>() || HitActor->IsA<AEnemyGhostActor>()
		|| HitActor->IsA<ACoverGhostActor>() || HitActor->ActorHasTag(FName(TEXT("Enemy"))))
	{
		return false;
	}
	// His own floor only (LadderQueueSmoke regression 2026-11-08: a roof click beyond a wall ladder's head ran a peek instead of the climb): never
	// a top surface (a wall's top, a roof, the ground), a face hit within his reach (RunCornerPeekRules::IsClickAtHisLevel).
	const float FeetZ = static_cast<float>(Operative->GetActorLocation().Z - Operative->GetSimpleCollisionHalfHeight());
	if (!RunCornerPeekRules::IsClickAtHisLevel(Hit.ImpactNormal.Z > RunPeekFaceMaxNormalZ, static_cast<float>(Hit.ImpactPoint.Z) - FeetZ))
	{
		return false;
	}
	if (Hit.GetComponent() != RunPeekCacheComponent.Get() || Operative != RunPeekCacheOperative.Get()
		|| FVector::DistSquared(Hit.ImpactPoint, RunPeekCachePoint) > FMath::Square(CacheMoveCm))
	{
		RunPeekCacheComponent = Hit.GetComponent();
		RunPeekCacheOperative = Operative;
		RunPeekCachePoint = Hit.ImpactPoint;
		GatherRunCornerPeekFaces(*Operative, Hit, RunPeekCacheFaces);
		bRunPeekCacheClickSlot = !RunPeekCacheFaces.IsEmpty() && FindCoverSlotForHit(Hit, RunPeekCacheClickSlot);
	}
	if (RunPeekCacheFaces.IsEmpty())
	{
		return false;
	}
	TArray<RunCornerPeekRules::FFace> Faces;
	for (const FRunCornerPeekFace& Each : RunPeekCacheFaces)
	{
		RunCornerPeekRules::FFace Face;
		Face.Normal = PlanarUnit(Each.Base.WallNormal);
		Face.Point = FVector2D(Each.Base.WallPoint);
		Faces.Add(Face);
	}
	// The face under the cursor decides (the game designer's PIE 2026-11-08: "I want to press to the face I point at"): any side of the object,
	// the far one too (he paths round to it); a hit on the edge between two faces: the one facing the camera. The hit face without a corner
	// of its own (a wall's narrow end face): the face round it that looks at him (decision 3, as before).
	const FVector2D TowardCamera = -PlanarUnit(PlayerCameraManager ? PlayerCameraManager->GetCameraRotation().Vector() : FVector::ForwardVector);
	int32 Near = RunCornerPeekRules::ChooseClickedFace(Faces, PlanarUnit(Hit.ImpactNormal), TowardCamera);
	if (Near == INDEX_NONE)
	{
		Near = RunCornerPeekRules::ChooseNearFace(Faces, FVector2D(Operative->GetActorLocation()));
	}
	if (!RunPeekCacheFaces.IsValidIndex(Near))
	{
		return false;
	}
	const FRunCornerPeekFace& Face = RunPeekCacheFaces[Near];
	if (!RunCornerPeekRules::IsCornerOnHisFloor(static_cast<float>(Face.Corner.WorldLocation.Z) - FeetZ))
	{
		return false; // that corner stands on another floor (a ladder / stairs away): a move there, not a run-and-peek
	}
	OutTarget.Peeker = const_cast<AOperativeCharacter*>(Operative);
	OutTarget.CornerSlot = Face.Corner;
	OutTarget.Side = Face.Side;
	OutTarget.EdgeOnFace = Face.EdgeOnFace;
	OutTarget.bCrouched = Face.bCrouched;
	if (OutFace)
	{
		*OutFace = Face;
	}
	return true;
}

bool ACodexTacticsPlayerController::FindRunCornerPeekClickTarget(AOperativeCharacter* Operative, const FHitResult& Hit, bool bSquadWide,
	const UTurnBasedCombatSubsystem* TurnBased, FRunCornerPeekFace& OutFace, FSquadCornerPeekTarget& OutTarget) const
{
	UWorld* World = GetWorld();
	const AActor* HitActor = Hit.GetActor();
	if (!Operative || !World || !HitActor)
	{
		return false;
	}
	const UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
	const UTutorialSubsystem* Tutorial = World->GetSubsystem<UTutorialSubsystem>();
	const FCoverSlot& Own = Operative->GetCoverSlot();
	RunCornerPeekRules::FClickFacts Facts;
	Facts.bEnabled = IsRunCornerPeekEnabled();
	Facts.bTurnBased = TurnBased != nullptr || GetActiveTurnBased() != nullptr;
	Facts.bPausePlanning = Flow && FCombatTimeModeRules::GetOrderDispatch(Flow->GetPhase(), Flow->GetCombatMode(),
		OrderLock::CVarRealTimeOrders.GetValueOnGameThread() != 0) == ECombatOrderDispatch::Queue;
	Facts.bSquadWide = bSquadWide;
	Facts.bTutorialLocked = Tutorial && (Tutorial->IsCommandLocked(PlayerCommands::CoverCornerPeek) || Tutorial->IsCommandLocked(PlayerCommands::CoverPreview));
	Facts.bHitPawn = HitActor->IsA<APawn>();
	Facts.bHitObject = HitActor->IsA<AInteractableActor>() || HitActor->IsA<AEnemyGhostActor>() || HitActor->IsA<ACoverGhostActor>() || HitActor->ActorHasTag(FName(TEXT("Enemy")));
	// In cover at the clicked object: decided once the face is known - his own face keeps the in-cover rules, another face of the same block
	// is a run-and-peek to its corner (RunCornerPeekRules::IsOwnCoverFace; the game designer 2026-11-08, the far face towards the threat).
	const bool bInCoverAtObject = Operative->bInCover && (Own.WallComponent.IsValid() ? Hit.GetComponent() == Own.WallComponent.Get() : HitActor == Own.WallActor.Get());
	if (!RunCornerPeekRules::MayRunToPeek(Facts) || !FindRunCornerPeekForHit(Operative, Hit, OutTarget, &OutFace))
	{
		return false;
	}
	Facts.bInCoverAtClickedObject = RunCornerPeekRules::IsOwnCoverFace(bInCoverAtObject, CoverTraceRules::IsSameWall(Own, OutFace.Base));
	if (!RunCornerPeekRules::MayRunToPeek(Facts))
	{
		OutTarget = FSquadCornerPeekTarget();
		return false;
	}
	// From a wall edge another cover in switch range keeps the wall-edge cover switch (its ghost, its double-click dash) - since the face rule
	// (2026-11-08) only where the switch goes itself: the protected-side face at its end nearer to him (RunCornerPeekRules::KeepsCoverSwitch);
	// the other faces / the far end are the run-and-peek to the face under the cursor.
	const bool bSwitchVerdict = Operative->bInCover && !(Tutorial && Tutorial->IsCommandLocked(PlayerCommands::CoverSwitch))
		&& (Operative->EvaluateCoverSwitch(OutFace.Base) == ECoverSwitchVerdict::Switch
			|| (bRunPeekCacheClickSlot && Operative->EvaluateCoverSwitch(RunPeekCacheClickSlot) == ECoverSwitchVerdict::Switch));
	const FVector OtherEnd = OutFace.Base.WallPoint + OutFace.Base.RightTangent()
		* (OutFace.Side == ECoverFacing::Right ? -OutFace.Base.LeftEdgeDistanceCm : OutFace.Base.RightEdgeDistanceCm);
	Facts.bCoverSwitchTarget = RunCornerPeekRules::KeepsCoverSwitch(bSwitchVerdict, ControllerRunPeekLocal::PlanarUnit(OutFace.Base.WallNormal),
		ControllerRunPeekLocal::PlanarUnit(Own.WallNormal), static_cast<float>(FVector::Dist2D(Operative->GetActorLocation(), OutFace.EdgeOnFace)),
		static_cast<float>(FVector::Dist2D(Operative->GetActorLocation(), OtherEnd)));
	if (!RunCornerPeekRules::MayRunToPeek(Facts))
	{
		OutTarget = FSquadCornerPeekTarget();
		return false;
	}
	// A box selection: the ghost / the order is the man who takes the corner.
	AOperativeCharacter* GroupPeeker = nullptr;
	const USquadSubsystem* Squad = GetSquad();
	if (Squad && Squad->HasMultiSelection() && PlanRunGroupCornerPeek(Operative, OutFace, GroupPeeker, nullptr) && GroupPeeker)
	{
		OutTarget.Peeker = GroupPeeker;
	}
	return true;
}

bool ACodexTacticsPlayerController::PlanRunGroupCornerPeek(AOperativeCharacter* Operative, const FRunCornerPeekFace& Face, AOperativeCharacter*& OutPeeker,
	TArray<TPair<AOperativeCharacter*, FCoverSlot>>* OutStack, TArray<AOperativeCharacter*>* OutUnplaced) const
{
	using namespace ControllerRunPeekLocal;
	OutPeeker = nullptr;
	if (OutStack)
	{
		OutStack->Reset();
	}
	if (OutUnplaced)
	{
		OutUnplaced->Reset();
	}
	const USquadSubsystem* Squad = GetSquad();
	UWorld* World = GetWorld();
	const IConsoleVariable* GroupPeek = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Squad.GroupCornerPeek"));
	if (!Squad || !World || !Operative || !Squad->HasMultiSelection() || !Squad->IsGroupSelected(Operative) || (GroupPeek && GroupPeek->GetInt() == 0))
	{
		return false;
	}
	// Every selected member's place along that face (from its slot at the click, as the edge), wherever he stands now.
	const FCoverSlot& Base = Face.Base;
	TArray<AOperativeCharacter*> Who;
	TArray<double> Along;
	TArray<bool> Able;
	for (AOperativeCharacter* Member : Squad->GetSelectedGroup())
	{
		if (!Member)
		{
			continue;
		}
		Who.Add(Member);
		Along.Add(CoverTraceRules::AlongWallDistance(Base, Member->GetActorLocation()));
		Able.Add(IsAbleToRun(Member));
	}
	if (Who.Num() < 2)
	{
		return false;
	}
	const float StandOff = CoverFacingRules::CornerStandOff(Face.Side, Face.bCrouched, Operative->GetCoverFacingConfig());
	const FGroupCoverConfig GroupConfig;
	const FormationWallLeadership::FGroupCornerPeekPlan Plan = FormationWallLeadership::PlanGroupCornerPeek(Along, Able, Face.EdgeAlongCm,
		CoverFacingRules::CornerPeekStandAlong(Face.Side, Face.EdgeAlongCm, StandOff), GroupConfig.SlotSpacingCm, StackKeepCm);
	if (!Who.IsValidIndex(Plan.Peeker))
	{
		return false;
	}
	OutPeeker = Who[Plan.Peeker];
	if (OutStack)
	{
		for (int32 Position = 0; Position < Plan.Order.Num() && Position < Plan.PlaceAlong.Num(); ++Position)
		{
			AOperativeCharacter* Member = Who[Plan.Order[Position]];
			const bool bInPlace = Member->bInCover && CoverTraceRules::IsSameWall(Base, Member->GetCoverSlot())
				&& FMath::Abs(Plan.PlaceAlong[Position] - Along[Plan.Order[Position]]) <= 1.0;
			FCoverSlot Target;
			const bool bFound = !bInPlace && CoverTraceRules::FindShimmySlot(World, Base, Base.WallPoint + Base.RightTangent() * static_cast<float>(Plan.PlaceAlong[Position]), Target)
				&& (!Base.WallActor.IsValid() || Target.WallActor == Base.WallActor);
			if (bInPlace)
			{
				continue;
			}
			// The game designer's PIE 2026-11-08: a block too short for the stack - no covered place behind the corner man: a nearby cover instead.
			if (RunCornerPeekRules::IsStackPlaceCovered(bFound, bFound ? static_cast<float>(FVector::Dist2D(Target.WorldLocation, Face.Corner.WorldLocation)) : 0.f))
			{
				OutStack->Add(TPair<AOperativeCharacter*, FCoverSlot>(Member, Target));
			}
			else if (OutUnplaced)
			{
				OutUnplaced->Add(Member);
			}
		}
	}
	return true;
}

bool ACodexTacticsPlayerController::TryHandleRunCornerPeekClick(AOperativeCharacter* Operative, const FHitResult& Hit, bool bSquadWide,
	const UTurnBasedCombatSubsystem* TurnBased)
{
	using namespace ControllerRunPeekLocal;
	FRunCornerPeekFace Face;
	FSquadCornerPeekTarget Target;
	if (!FindRunCornerPeekClickTarget(Operative, Hit, bSquadWide, TurnBased, Face, Target))
	{
		return false;
	}
	UWorld* World = GetWorld();
	USquadSubsystem* Squad = GetSquad();
	if (Squad)
	{
		Squad->HandOverLeadForOrder(Operative, TEXT("a run-and-peek order")); // bug 2026-11-08: the next order after a switch mid-move
	}
	HideCoverPreview();
	UpdateCornerPeekHover(Operative, nullptr);
	// A double-click on the same corner: the same order at a sprint (as the ground's double-click).
	FVector2D Mouse = FVector2D::ZeroVector; // stays zero without a viewport (headless smokes)
	GetMousePosition(Mouse.X, Mouse.Y);
	const double Now = FApp::GetCurrentTime();
	// Q4 (the game designer 2026-11-08, kept): the normal run; a sprint only on a double-click on the same corner.
	const bool bDouble = RunCornerPeekRules::IsSprintClick(RunPeekClickTime >= 0.0 ? Now - RunPeekClickTime : -1.0, DoubleClickSeconds,
		static_cast<float>(FVector2D::Distance(Mouse, RunPeekClickPosition)), DoubleClickPixels, static_cast<float>(FVector::Dist2D(Face.EdgeOnFace, RunPeekClickEdge)));
	RunPeekClickTime = bDouble ? -1.0 : Now;
	RunPeekClickPosition = Mouse;
	RunPeekClickEdge = Face.EdgeOnFace;

	const IConsoleVariable* GroupPeek = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Squad.GroupCornerPeek"));
	const bool bMulti = Squad && Squad->HasMultiSelection();
	const TArray<AOperativeCharacter*> Others = Squad && !bMulti ? Squad->GetGroupCoverMembers(Operative) : TArray<AOperativeCharacter*>();
	RunCornerPeekRules::EGroup Group = RunCornerPeekRules::DecideGroup(bMulti, Squad && Squad->IsGroupSelected(Operative), !GroupPeek || GroupPeek->GetInt() != 0,
		!Squad || Squad->IsSoloMode(), Others.Num());
	if (UVehicleSubsystem* Vehicles = World->GetSubsystem<UVehicleSubsystem>())
	{
		Vehicles->CancelBoarding(Operative);
	}
	AOperativeCharacter* PeekOperative = Operative;
	EOperativeOrderResult Result = EOperativeOrderResult::Refused;
	FString StackLine;
	bool bOrdered = false;
	// Q3 (the game designer 2026-11-08): in the tactical pause the run-and-peek is planned - it runs when the pause ends.
	RunCornerPeekRules::FClickFacts TimingFacts;
	TimingFacts.bPausePlanning = IsPausePlanningOrders();
	if (RunCornerPeekRules::DecideTiming(TimingFacts) == RunCornerPeekRules::ETiming::OnPauseRelease)
	{
		return PlanRunCornerPeekClick(Operative, Face, Group, bDouble, Hit);
	}
	if (Group == RunCornerPeekRules::EGroup::BoxSelection)
	{
		AOperativeCharacter* Peeker = nullptr;
		TArray<TPair<AOperativeCharacter*, FCoverSlot>> Stack;
		TArray<AOperativeCharacter*> Unplaced;
		if (PlanRunGroupCornerPeek(Operative, Face, Peeker, &Stack, &Unplaced) && Peeker)
		{
			PeekOperative = Peeker;
			Result = Peeker->OrderRunCornerPeek(Face.Corner, Face.EdgeOnFace, bDouble);
			bOrdered = true;
			if (Result == EOperativeOrderResult::Accepted)
			{
				for (const TPair<AOperativeCharacter*, FCoverSlot>& Each : Stack)
				{
					const EOperativeOrderResult Step = Each.Key->OrderTakeCover(Each.Value, bDouble);
					StackLine += FString::Printf(TEXT(" %s -> (%.0f, %.0f) %d;"), *Each.Key->DisplayName.ToString(), Each.Value.WorldLocation.X, Each.Value.WorldLocation.Y,
						static_cast<int32>(Step));
				}
				StackLine += OrderUnplacedStackToNearbyCover(Face, Stack, Unplaced, bDouble);
			}
		}
		else
		{
			Group = RunCornerPeekRules::EGroup::Single;
		}
	}
	else if (Group == RunCornerPeekRules::EGroup::Formation)
	{
		if (bDouble && PeekCarry.bActive)
		{
			return true; // the second click of a double-click: the squad's wall order at that corner already runs
		}
		const UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		if (OrderGroupCover(Operative, Face.Corner, false, Flow ? Flow->GetPauseOrderRadius() : 1200.f))
		{
			bOrdered = true;
			Result = EOperativeOrderResult::Accepted;
			if (Squad->HasFormationWallOrder())
			{
				// The squad's wall layout at that corner; Q2 (the game designer 2026-11-08): once the LEADER is at that wall, the squad's corner
				// peek round it (the man nearest it takes it), the others keep forming up behind.
				StartSquadPeekCarry(*Squad, *Operative, Face.Corner, true, true);
				StackLine = PeekCarry.bActive ? TEXT(" the formation wall order there, the squad's corner peek once the leader is at it") : TEXT(" the formation wall order there (no corner to carry the peek to)");
				// #55 fix (the game designer's video 2026-11-08): the leader's own wall place IS that corner - his run there is the run-and-peek
				// (pre-turned, straight into the peek stance on arrival); the carry then finds the corner man peeking.
				const FFormationSlot* LeaderPlace = Squad->GetFormationWallSlots().FindByPredicate([](const FFormationSlot& Each) { return Each.Role == EFormationRole::Commander; });
				if (PeekCarry.bActive && AOperativeCharacter::IsRunPeekStanceEnabled() && LeaderPlace
					&& FVector2D::Distance(LeaderPlace->Position, FVector2D(Face.Corner.WorldLocation)) <= RunCornerPeekRules::LeaderPlaceAtCornerCm)
				{
					Operative->SetPendingCover(Face.Corner);
					if (Operative->ArmRunCornerPeek(Face.EdgeOnFace))
					{
						Operative->MarkCornerPeekArrival();
						StackLine += TEXT("; his own place is the corner - he peeks on arrival");
					}
				}
			}
			else
			{
				// The group cover beside him: his own entry into the corner slot turns into the peek.
				StackLine = Operative->ArmRunCornerPeek(Face.EdgeOnFace) ? TEXT(" the group cover there, he peeks on arrival") : TEXT(" the group cover there");
			}
		}
		else
		{
			Group = RunCornerPeekRules::EGroup::Single;
		}
	}
	if (!bOrdered)
	{
		Result = Operative->OrderRunCornerPeek(Face.Corner, Face.EdgeOnFace, bDouble);
	}
	const TCHAR* GroupName = Group == RunCornerPeekRules::EGroup::BoxSelection ? TEXT("box selection") : Group == RunCornerPeekRules::EGroup::Formation ? TEXT("formation")
		: TEXT("single");
	UE_LOG(LogCodexTactics, Display, TEXT("[Cover] run-and-peek click at (%.0f, %.0f, %.0f) on %s (%s%s): %s runs to the %s corner at (%.0f, %.0f) of the face looking (%.0f, %.0f) -> %d;%s"),
		Hit.ImpactPoint.X, Hit.ImpactPoint.Y, Hit.ImpactPoint.Z, *GetNameSafe(Hit.GetActor()), GroupName, bDouble ? TEXT(", double-click: at a sprint") : TEXT(""),
		*PeekOperative->DisplayName.ToString(), Face.Side == ECoverFacing::Left ? TEXT("left") : TEXT("right"), Face.Corner.WorldLocation.X, Face.Corner.WorldLocation.Y,
		Face.Base.WallNormal.X, Face.Base.WallNormal.Y, static_cast<int32>(Result), StackLine.IsEmpty() ? TEXT(" alone") : *StackLine);
	UGameMessageSubsystem* Messages = World->GetSubsystem<UGameMessageSubsystem>();
	if (Result == EOperativeOrderResult::Accepted)
	{
		ControllerCommands::Report(this, PlayerCommands::CoverCornerPeek, nullptr, false, Group != RunCornerPeekRules::EGroup::Single, PeekOperative, Hit.ImpactPoint);
		if (UCombatFeedbackSubsystem* Feedback = World->GetSubsystem<UCombatFeedbackSubsystem>())
		{
			Feedback->SpawnMovePing(Face.Corner.WorldLocation, true);
		}
		if (Messages && Group != RunCornerPeekRules::EGroup::Formation)
		{
			Messages->PostMessage(PeekOperative->DisplayName, Group == RunCornerPeekRules::EGroup::BoxSelection
				? LOCTEXT("RunGroupCornerPeek", "🏃 To that corner - covering it, the others stack up behind me.") : LOCTEXT("RunCornerPeek", "🏃 To that corner - covering it."));
		}
	}
	else if (Messages)
	{
		Messages->PostMessage(LOCTEXT("HQ", "HQ"), LOCTEXT("CornerPeekRefused", "⚠️ Can't get to that corner."));
	}
	return true;
}

FString ACodexTacticsPlayerController::OrderUnplacedStackToNearbyCover(const FRunCornerPeekFace& Face, const TArray<TPair<AOperativeCharacter*, FCoverSlot>>& Stack,
	const TArray<AOperativeCharacter*>& Unplaced, bool bSprint)
{
	UWorld* World = GetWorld();
	if (!World || Unplaced.IsEmpty())
	{
		return FString();
	}
	// The group cover order's nearby covers (squad_walls.json follower_cover_radius_m / _side_deg) round the corner man's slot, none taken.
	TArray<FCoverSlot> Claimed{ Face.Corner };
	for (const TPair<AOperativeCharacter*, FCoverSlot>& Each : Stack)
	{
		Claimed.Add(Each.Value);
	}
	const USquadSubsystem* Squad = GetSquad();
	const auto IsTaken = [Squad](const FVector& Point)
	{
		if (!Squad)
		{
			return false;
		}
		for (const AOperativeCharacter* Member : Squad->GetMembers())
		{
			if (Member && ((Member->bInCover && FVector::Dist2D(Member->GetCoverSlot().WorldLocation, Point) < 60.f)
				|| (Member->HasPendingCover() && FVector::Dist2D(Member->GetPendingCoverSlot().WorldLocation, Point) < 60.f)))
			{
				return true;
			}
		}
		return false;
	};
	const FSquadWallTuning& Tuning = SquadWallTuning::Get();
	TArray<FCoverSlot> Nearby = ControllerGroupCover::FindNearbyGroupCoverSlots(World, Face.Corner, Claimed, Unplaced.Num(), Tuning.FollowerCoverRadiusCm,
		Tuning.FollowerCoverSideDeg, IsTaken);
	FString Line = TEXT(" no room behind him:");
	TArray<AOperativeCharacter*> Left = Unplaced;
	// Nearest man to each cover (a short greedy pass; at most a few men).
	for (const FCoverSlot& Slot : Nearby)
	{
		int32 Best = INDEX_NONE;
		double BestDistance = 0.0;
		for (int32 Index = 0; Index < Left.Num(); ++Index)
		{
			const double Distance = FVector::Dist2D(Left[Index]->GetActorLocation(), Slot.WorldLocation);
			if (Best == INDEX_NONE || Distance < BestDistance)
			{
				Best = Index;
				BestDistance = Distance;
			}
		}
		if (Best == INDEX_NONE)
		{
			break;
		}
		AOperativeCharacter* Member = Left[Best];
		Left.RemoveAt(Best);
		const EOperativeOrderResult Step = Member->OrderTakeCover(Slot, bSprint);
		Line += FString::Printf(TEXT(" %s -> a nearby cover (%.0f, %.0f) %d;"), *Member->DisplayName.ToString(), Slot.WorldLocation.X, Slot.WorldLocation.Y,
			static_cast<int32>(Step));
	}
	// No cover within reach: prone behind the corner man (the group cover's last resort), off his corner.
	const FVector Behind = Face.Corner.WorldLocation + Face.Base.WallNormal.GetSafeNormal2D() * 120.f;
	for (int32 Index = 0; Index < Left.Num(); ++Index)
	{
		AOperativeCharacter* Member = Left[Index];
		const FVector Spot = Behind - Face.Base.RightTangent() * (Face.Side == ECoverFacing::Right ? 1.f : -1.f) * (80.f * static_cast<float>(Index + 1));
		Member->OrderMoveTo(Spot, bSprint);
		Member->SetStance(EOperativeStance::Prone);
		Line += FString::Printf(TEXT(" %s -> prone behind him (%.0f, %.0f);"), *Member->DisplayName.ToString(), Spot.X, Spot.Y);
	}
	return Line;
}

void ACodexTacticsPlayerController::PlanRunCornerPeek(AOperativeCharacter& Operative, const FCoverSlot& CornerSlot, const FVector& EdgeOnFace, bool bSprint, float Radius,
	FVector& OutPlanned)
{
	// As the planned cover click: the slot pending (entered on arrival), the walk planned; the peek on that entry armed (SetPendingCover
	// clears both arrival flags - they are set again after it). The release's walk to the pending slot keeps them (OrderMoveTo).
	Operative.SetPendingCover(CornerSlot);
	Operative.ArmRunCornerPeek(EdgeOnFace);
	Operative.MarkCornerPeekArrival();
	USquadSubsystem* Squad = GetSquad();
	OutPlanned = Squad ? Squad->PlanMove(&Operative, CornerSlot.WorldLocation, bSprint, Radius) : CornerSlot.WorldLocation;
	++PlannedRunCornerPeeks;
}

bool ACodexTacticsPlayerController::PlanRunCornerPeekClick(AOperativeCharacter* Operative, const FRunCornerPeekFace& Face, RunCornerPeekRules::EGroup Group, bool bSprint,
	const FHitResult& Hit)
{
	UWorld* World = GetWorld();
	USquadSubsystem* Squad = GetSquad();
	const UGameFlowSubsystem* Flow = World ? World->GetSubsystem<UGameFlowSubsystem>() : nullptr;
	const float Radius = Flow ? Flow->GetPauseOrderRadius() : 1200.f;
	UCombatFeedbackSubsystem* Feedback = World ? World->GetSubsystem<UCombatFeedbackSubsystem>() : nullptr;
	UGameMessageSubsystem* Messages = World ? World->GetSubsystem<UGameMessageSubsystem>() : nullptr;
	if (!Operative || !Squad)
	{
		return false;
	}
	AOperativeCharacter* PeekOperative = Operative;
	FString Line;
	FVector Planned = Face.Corner.WorldLocation;
	bool bPlanned = false;
	if (Group == RunCornerPeekRules::EGroup::BoxSelection)
	{
		AOperativeCharacter* Peeker = nullptr;
		TArray<TPair<AOperativeCharacter*, FCoverSlot>> Stack;
		if (PlanRunGroupCornerPeek(Operative, Face, Peeker, &Stack) && Peeker)
		{
			PeekOperative = Peeker;
			PlanRunCornerPeek(*Peeker, Face.Corner, Face.EdgeOnFace, bSprint, Radius, Planned);
			for (const TPair<AOperativeCharacter*, FCoverSlot>& Each : Stack)
			{
				Each.Key->SetPendingCover(Each.Value);
				const FVector EachPlanned = Squad->PlanMove(Each.Key, Each.Value.WorldLocation, bSprint, Radius);
				Line += FString::Printf(TEXT(" %s -> (%.0f, %.0f);"), *Each.Key->DisplayName.ToString(), EachPlanned.X, EachPlanned.Y);
			}
			bPlanned = true;
		}
		else
		{
			Group = RunCornerPeekRules::EGroup::Single;
		}
	}
	else if (Group == RunCornerPeekRules::EGroup::Formation)
	{
		// The squad's wall order there is planned (PlanFormationWall, ordered on the release); its carried peek starts then (TickSquadPeekCarry).
		if (OrderGroupCover(Operative, Face.Corner, true, Radius))
		{
			bPlanned = true;
			if (Squad->HasPlannedFormationWall())
			{
				PlannedRunPeekCarry.bActive = true;
				PlannedRunPeekCarry.Leader = Operative;
				PlannedRunPeekCarry.Slot = Face.Corner;
				++PlannedRunCornerPeeks;
				Line = TEXT(" the formation wall order there planned, the squad's corner peek once the leader is at it");
			}
			else
			{
				// The legacy group cover beside him (planned walks to pending slots): his own entry into the corner slot turns into the peek.
				Operative->ArmRunCornerPeek(Face.EdgeOnFace);
				Operative->MarkCornerPeekArrival();
				++PlannedRunCornerPeeks;
				Line = TEXT(" the group cover there planned, he peeks on arrival");
			}
		}
		else
		{
			Group = RunCornerPeekRules::EGroup::Single;
		}
	}
	if (!bPlanned)
	{
		PlanRunCornerPeek(*Operative, Face.Corner, Face.EdgeOnFace, bSprint, Radius, Planned);
	}
	if (Feedback && Group != RunCornerPeekRules::EGroup::Formation)
	{
		Feedback->SpawnWaypointMarker(Planned);
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[Cover] run-and-peek PLANNED in the tactical pause at (%.0f, %.0f, %.0f) (%s%s): %s - to the %s corner at (%.0f, %.0f), planned to (%.0f, %.0f), the peek on arrival once the pause ends;%s"),
		Hit.ImpactPoint.X, Hit.ImpactPoint.Y, Hit.ImpactPoint.Z, Group == RunCornerPeekRules::EGroup::BoxSelection ? TEXT("box selection")
		: Group == RunCornerPeekRules::EGroup::Formation ? TEXT("formation") : TEXT("single"), bSprint ? TEXT(", at a sprint") : TEXT(""), *PeekOperative->DisplayName.ToString(),
		Face.Side == ECoverFacing::Left ? TEXT("left") : TEXT("right"), Face.Corner.WorldLocation.X, Face.Corner.WorldLocation.Y, Planned.X, Planned.Y,
		Line.IsEmpty() ? TEXT(" alone") : *Line);
	ControllerCommands::Report(this, PlayerCommands::CoverCornerPeek, nullptr, true, Group != RunCornerPeekRules::EGroup::Single, PeekOperative, Hit.ImpactPoint);
	if (Messages && Group != RunCornerPeekRules::EGroup::Formation)
	{
		Messages->PostMessage(PeekOperative->DisplayName, LOCTEXT("RunCornerPeekPlanned", "📋 [PLAN] Run to that corner and cover it!"));
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
