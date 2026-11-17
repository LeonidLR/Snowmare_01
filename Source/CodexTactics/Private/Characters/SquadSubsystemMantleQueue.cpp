// USquadSubsystem: the squad climbs an obstacle in turns (R17-1, designer 2026-11-17 TASK_BOARD: "the characters climb onto obstacles and
// surfaces the same way as on ladders now: one climbs, the others cover; he is up, the next climbs while the rest cover"; docs/design/MANTLING.md
// 6.4, docs/design/FORMATION_CONTRACT.md 22).
// The ladder's queue is reused (TTraversalQueue: one holder at a time, the FIFO line, the spot rule out from the start): a group order that needs
// a climb - a click on a climbable obstacle for the formation / a box selection, or the formation leader's path over a mantle link - puts the
// first man (the leader) on the obstacle and the others in line at the base, in formation / selection order, each on his own lane of the face;
// the waiters cover outwards (the threat side first), the man up covers the far side; the next one climbs when the one before him is up.
// The climb itself is AOperativeCharacter's (OperativeCharacterMantle.cpp: OrderMantleTop per lane). Pure rules: Characters/SquadMantleRules.
// UE-only (Godot had no mantling).

#include "Characters/SquadSubsystem.h"

#include "Characters/MantleRules.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadMantleRules.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "Formation/FormationSectorRules.h"
#include "Formation/FormationTuning.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/MantleNavigation.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Vehicles/VehicleSubsystem.h"

namespace SquadMantleLocal
{
	TAutoConsoleVariable<int32> CVarMantleSquad(TEXT("Codex.Mantle.Squad"), 1,
		TEXT("R17-1 (designer 2026-11-17): 1 = a group order that needs a climb (formation: the leader's click / path over a mantle link; Solo: the box ")
		TEXT("selection's click) climbs in turns like a ladder - one climbs, the others wait at the base covering; 0 = before R17-1 (a Solo single ")
		TEXT("climbs by click, formation moves never take the mantle links)."));

	/** A waiter counts as on his spot within this (2D), cm. */
	constexpr float SpotArriveCm = 60.f;
	/** The world walls for the waiters' looks are gathered this far round the base, cm. */
	constexpr float WallsHalfExtentCm = 700.f;
	/** The face's ends are kept this far clear of a lane (on top of the capsule radius), cm. */
	constexpr float LaneEndMarginCm = 10.f;
	/** A turn whose climb order was refused is asked again after this long (twice), s. */
	constexpr float TurnRetrySeconds = 1.f;
	/** He stands at the face without a climb order or a walk this long: the climb was refused there, s. */
	constexpr float TurnStallSeconds = 1.f;
	/** The base point off the face (MantleNavigation's click base), cm. */
	constexpr float BaseOffFaceCm = 80.f;

	const TCHAR* StateName(ESquadMantleState State)
	{
		switch (State)
		{
		case ESquadMantleState::Waiting: return TEXT("waiting");
		case ESquadMantleState::Turn: return TEXT("his turn");
		case ESquadMantleState::Climbing: return TEXT("climbing");
		case ESquadMantleState::Up: return TEXT("up");
		case ESquadMantleState::Hold: return TEXT("holding at the base");
		default: return TEXT("out");
		}
	}

	/** Able to take part in a climb now (alive, on his feet, not on a ladder / in a vehicle / on guard / carrying / panicking). */
	bool CanJoin(const AOperativeCharacter& Operative)
	{
		return Operative.HealthComponent && Operative.HealthComponent->IsAlive() && !Operative.IsKnockedDown() && !Operative.IsPanicking()
			&& !Operative.IsClimbingLadder() && !VehicleQueries::IsMounted(&Operative) && !Operative.bGuarding && !Operative.bCarrying;
	}

	FVector FeetOf(const AOperativeCharacter& Operative)
	{
		return Operative.GetActorLocation() - FVector(0.f, 0.f, Operative.GetSimpleCollisionHalfHeight());
	}

	/** A full navmesh path (no mantle links) from where he stands to Point, ending within ReachCm of it. */
	bool IsReachable(UWorld* World, AOperativeCharacter& Operative, const FVector& Point, float ReachCm = 60.f)
	{
		UNavigationSystemV1* Nav = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
		if (!Nav)
		{
			return true; // no navmesh to ask (the order itself will tell)
		}
		const UNavigationPath* Path = Nav->FindPathToLocationSynchronously(World, FeetOf(Operative), Point, &Operative,
			MantleNavigation::NoMantleVariant(nullptr));
		return Path && Path->IsValid() && !Path->IsPartial() && Path->PathPoints.Num() > 0 && FVector::Dist2D(Path->PathPoints.Last(), Point) <= ReachCm;
	}
}

bool USquadSubsystem::IsSquadMantleEnabled()
{
	return SquadMantleLocal::CVarMantleSquad.GetValueOnGameThread() != 0;
}

TArray<AOperativeCharacter*> USquadSubsystem::GetSquadMantleGroup(AOperativeCharacter& Climber) const
{
	using namespace SquadMantleLocal;
	TArray<AOperativeCharacter*> Group;
	if (!IsSquadMantleEnabled())
	{
		return Group;
	}
	TArray<AOperativeCharacter*> Candidates;
	if (!bIsSoloMode)
	{
		// The formation: its leader leads the climb, his followers come after him in formation order (their slots).
		if (Leader.Get() != &Climber && GetLeader() != &Climber)
		{
			return Group;
		}
		if (AOperativeCharacter* FormationLeader = Leader.Get(); FormationLeader && FormationLeader != &Climber)
		{
			Candidates.Add(FormationLeader); // a pending leader's climb: the old leader is one of his men now
		}
		for (const FFollowerState& Follower : Followers)
		{
			if (AOperativeCharacter* Operative = Follower.Operative.Get())
			{
				Candidates.AddUnique(Operative);
			}
		}
	}
	else if (HasMultiSelection() && IsGroupSelected(&Climber))
	{
		Candidates = GetSelectedGroup(); // Solo: the box selection, in its order
	}
	else
	{
		return Group;
	}
	TArray<AOperativeCharacter*> Pool = { &Climber };
	TArray<int32> Slots = { INDEX_NONE };
	const float Radius = MantleRules::GetConfig().SquadJoinRadiusCm;
	for (AOperativeCharacter* Operative : Candidates)
	{
		if (Operative && Operative != &Climber && CanJoin(*Operative) && !Operative->IsMantling()
			&& FVector::Dist2D(Operative->GetActorLocation(), Climber.GetActorLocation()) <= Radius)
		{
			Pool.AddUnique(Operative);
			Slots.Add(bIsSoloMode ? INDEX_NONE : GetFormationSlot(Operative));
		}
	}
	if (Pool.Num() < 2)
	{
		return Group;
	}
	for (const int32 Index : SquadMantleRules::ClimbOrder(0, Slots))
	{
		Group.Add(Pool[Index]);
	}
	return Group;
}

FHitResult USquadSubsystem::SquadMantleLaneHit(const FSquadMantleLane& Lane) const
{
	// Over the lane, a little inside the top (a fence: the middle of its thin top) - PlanTopClick takes the lane from the point along the face.
	const float In = SquadMantle.bOver ? FMath::Max(1.f, Lane.DepthCm * 0.5f) : FMath::Clamp(Lane.DepthCm * 0.5f, 10.f, 40.f);
	FVector Point = Lane.EdgeTop + Lane.Dir * In;
	Point.Z = Lane.EdgeTop.Z;
	FHitResult Hit(const_cast<AActor*>(SquadMantle.Obstacle.Get()), nullptr, Point, FVector::UpVector);
	Hit.Location = Point;
	Hit.ImpactPoint = Point;
	Hit.Normal = FVector::UpVector;
	Hit.ImpactNormal = FVector::UpVector;
	Hit.bBlockingHit = true;
	return Hit;
}

TArray<USquadSubsystem::FSquadMantleLane> USquadSubsystem::MakeSquadMantleLanes(int32 PerFace, float CapsuleRadius) const
{
	using namespace SquadMantleLocal;
	const FSquadMantle& Op = SquadMantle;
	const FMantleConfig& Config = MantleRules::GetConfig();
	TArray<FSquadMantleLane> Lanes;
	const AActor* Obstacle = Op.Obstacle.Get();
	const FBox Local = Obstacle ? Obstacle->CalculateComponentsBoundingBoxInLocalSpace(false) : FBox(ForceInit);
	const float Margin = CapsuleRadius + LaneEndMarginCm;
	auto AddFace = [&](int32 Face, const FVector& FacePoint, const FVector& Dir, const FVector& EdgeTop, const FVector& Base, float DepthCm, float MinOffset, float MaxOffset)
	{
		const FVector Tangent = FVector::CrossProduct(FVector::UpVector, Dir).GetSafeNormal2D();
		// Candidates at half the lane spacing (an uneven top - a wreck's bonnet - may refuse a lane, its neighbour may do); the landing gap keeps
		// the lanes given squad.min_landing_gap_cm apart.
		for (const float Offset : SquadMantleRules::LaneOffsets(PerFace, Config.SquadLaneSpacingCm * 0.5f, MinOffset, MaxOffset))
		{
			FSquadMantleLane& Lane = Lanes.AddDefaulted_GetRef();
			Lane.Face = Face;
			Lane.OffsetCm = Offset;
			Lane.Dir = Dir;
			Lane.Tangent = Tangent;
			Lane.FacePoint = FacePoint + Tangent * Offset;
			Lane.EdgeTop = EdgeTop + Tangent * Offset;
			Lane.Base = Base + Tangent * Offset;
			Lane.DepthCm = DepthCm;
		}
	};
	if (!Local.IsValid)
	{
		AddFace(0, Op.FacePoint, Op.Dir, Op.EdgeTop, Op.Base, Op.DepthCm, 0.f, 0.f);
		return Lanes;
	}
	const FTransform Transform = Obstacle->GetActorTransform();
	const FVector Scale = Transform.GetScale3D().GetAbs();
	const FVector Centre = Local.GetCenter();
	const FVector Extent = Local.GetExtent();
	// The clicked face: the room either side of the first lane (the box on the face's axis, the ends kept clear).
	{
		float Lo = TNumericLimits<float>::Max();
		float Hi = -TNumericLimits<float>::Max();
		for (int32 Corner = 0; Corner < 4; ++Corner)
		{
			const FVector LocalCorner((Corner & 1) ? Local.Max.X : Local.Min.X, (Corner & 2) ? Local.Max.Y : Local.Min.Y, Local.Min.Z);
			const float Along = static_cast<float>(FVector::DotProduct(Transform.TransformPosition(LocalCorner) - Op.FacePoint, Op.Tangent));
			Lo = FMath::Min(Lo, Along);
			Hi = FMath::Max(Hi, Along);
		}
		AddFace(0, Op.FacePoint, Op.Dir, Op.EdgeTop, Op.Base, Op.DepthCm, FMath::Min(0.f, Lo + Margin), FMath::Max(0.f, Hi - Margin));
	}
	if (Op.bOver)
	{
		return Lanes; // a fence is climbed over from its near side only
	}
	// A top: its other faces too (a wreck clicked at its short end), nearest to the clicked base first, each from its middle.
	struct FFace
	{
		FVector Point;
		FVector Dir;
		float Half = 0.f;
		float Depth = 0.f;
		double Dist = 0.0;
	};
	TArray<FFace> Faces;
	for (int32 Axis = 0; Axis < 2; ++Axis)
	{
		for (const float Sign : { 1.f, -1.f })
		{
			FVector NormalLocal = FVector::ZeroVector;
			NormalLocal[Axis] = Sign;
			const FVector Out = Transform.TransformVectorNoScale(NormalLocal).GetSafeNormal2D();
			if (Out.IsNearlyZero() || FVector::DotProduct(Out, -Op.Dir) > 0.9f)
			{
				continue; // the clicked face
			}
			FVector PointLocal = Centre;
			PointLocal[Axis] += Sign * Extent[Axis];
			PointLocal.Z = Local.Min.Z;
			FFace& Face = Faces.AddDefaulted_GetRef();
			Face.Point = Transform.TransformPosition(PointLocal);
			Face.Dir = -Out;
			Face.Half = static_cast<float>(Extent[1 - Axis] * Scale[1 - Axis]);
			Face.Depth = static_cast<float>(2.0 * Extent[Axis] * Scale[Axis]);
			Face.Dist = FVector::Dist2D(Face.Point, Op.Base);
		}
	}
	Faces.Sort([](const FFace& A, const FFace& B) { return A.Dist < B.Dist; });
	for (int32 Index = 0; Index < Faces.Num(); ++Index)
	{
		const FFace& Face = Faces[Index];
		const FVector FacePoint(Face.Point.X, Face.Point.Y, Op.FacePoint.Z);
		const FVector Base = FVector(Face.Point.X, Face.Point.Y, Op.Base.Z) - Face.Dir * BaseOffFaceCm;
		const FVector EdgeTop(Face.Point.X, Face.Point.Y, Op.EdgeTop.Z);
		const float Room = FMath::Max(0.f, Face.Half - Margin);
		AddFace(Index + 1, FacePoint, Face.Dir, EdgeTop, Base, Face.Depth, -Room, Room);
	}
	return Lanes;
}

bool USquadSubsystem::IsSquadMantleLaneFor(const AOperativeCharacter& Operative, const FSquadMantleLane& Lane, const TArray<FVector>& Taken, FVector& OutLanding,
	FString* OutWhy) const
{
	const float Gap = MantleRules::GetConfig().SquadMinLandingGapCm;
	const FVector Approx = ApproxSquadMantleLanding(Lane);
	for (const FVector& Other : Taken)
	{
		if (FVector::Dist2D(Other, Approx) < Gap * 0.75f && FMath::Abs(Other.Z - Approx.Z) < 100.f)
		{
			if (OutWhy)
			{
				*OutWhy = FString::Printf(TEXT("its landing ~%.0f cm from a man / lane up there"), FVector::Dist2D(Other, Approx));
			}
			return false;
		}
	}
	FMantleClickPlan Plan;
	const FVector Base = Lane.Base;
	FString Refusal;
	if (!Operative.PlanMantleClick(SquadMantleLaneHit(Lane), Plan, &Refusal, &Base) || Plan.Obstacle != SquadMantle.Obstacle.Get()
		|| FVector::DotProduct(Plan.Dir.GetSafeNormal2D(), Lane.Dir) < 0.9f)
	{
		if (OutWhy)
		{
			*OutWhy = Refusal.IsEmpty() ? FString(TEXT("another face from its base")) : Refusal;
		}
		return false;
	}
	for (const FVector& Other : Taken)
	{
		if (FVector::Dist2D(Other, Plan.Landing) < Gap && FMath::Abs(Other.Z - Plan.Landing.Z) < 100.f)
		{
			if (OutWhy)
			{
				*OutWhy = FString::Printf(TEXT("its landing %.0f cm from a man / lane up there"), FVector::Dist2D(Other, Plan.Landing));
			}
			return false;
		}
	}
	if (!SquadMantleLocal::IsReachable(GetWorld(), const_cast<AOperativeCharacter&>(Operative), Plan.Base))
	{
		if (OutWhy)
		{
			*OutWhy = TEXT("its base is not reachable from him");
		}
		return false;
	}
	OutLanding = Plan.Landing;
	return true;
}

FVector USquadSubsystem::ApproxSquadMantleLanding(const FSquadMantleLane& Lane) const
{
	const FMantleConfig& Config = MantleRules::GetConfig();
	if (SquadMantle.bOver)
	{
		return FVector(Lane.FacePoint.X, Lane.FacePoint.Y, Lane.Base.Z) + Lane.Dir * (Lane.DepthCm + 100.f);
	}
	return Lane.EdgeTop + Lane.Dir * FMath::Min(Config.TopInsetCm, FMath::Max(1.f, Lane.DepthCm * 0.5f));
}

TArray<FVector> USquadSubsystem::SquadMantleTakenLandings(const FSquadMantleMember* Except) const
{
	TArray<FVector> Taken;
	for (const FSquadMantleMember& Member : SquadMantle.Members)
	{
		const AOperativeCharacter* Operative = Member.Operative.Get();
		if (&Member == Except || !Operative)
		{
			continue;
		}
		if (Member.State == ESquadMantleState::Up || Member.State == ESquadMantleState::Climbing)
		{
			Taken.Add(SquadMantleLocal::FeetOf(*Operative));
			Taken.Add(Member.Lane.Landing);
		}
		else if (Member.State == ESquadMantleState::Waiting || Member.State == ESquadMantleState::Turn)
		{
			Taken.Add(Member.Lane.Landing);
		}
	}
	return Taken;
}

bool USquadSubsystem::OrderSquadMantle(const TArray<AOperativeCharacter*>& Group, const FHitResult& Hit, bool bRun, FString* OutWhy)
{
	AOperativeCharacter* First = Group.IsEmpty() ? nullptr : Group[0];
	if (!First)
	{
		return false;
	}
	if (Group.Num() < 2 || !IsSquadMantleEnabled())
	{
		return First->OrderMantleTop(Hit, bRun, OutWhy);
	}
	FMantleClickPlan Plan;
	if (!First->PlanMantleClick(Hit, Plan, OutWhy))
	{
		return false;
	}
	if (SquadMantle.bActive)
	{
		EndSquadMantle(TEXT("a new climb order"));
	}
	const int32 Before = First->GetMantleCount();
	if (!First->OrderMantleTop(Hit, bRun, OutWhy))
	{
		return false;
	}
	SquadMantle = FSquadMantle();
	FSquadMantle& Op = SquadMantle;
	Op.Obstacle = Plan.Obstacle;
	Op.Dir = Plan.Dir.GetSafeNormal2D();
	Op.Base = Plan.Base;
	Op.bOver = !Plan.bTop;
	Op.DepthCm = Plan.DepthCm;
	// Batch 2 off has no measured face: the click base's face (MantleNavigation's 80 cm) and the clicked top's height.
	Op.FacePoint = Plan.FacePoint.IsNearlyZero() ? Plan.Base + Op.Dir * SquadMantleLocal::BaseOffFaceCm : Plan.FacePoint;
	Op.EdgeTop = Plan.EdgeTop.IsNearlyZero() ? FVector(Op.FacePoint.X, Op.FacePoint.Y, Plan.TopHit.ImpactPoint.Z) : Plan.EdgeTop;
	SetUpSquadMantle(Group, /*bFromPath*/ false, Plan.Landing);
	FSquadMantleMember& Lead = Op.Members[0];
	Lead.State = ESquadMantleState::Turn;
	Lead.bOrdered = true;
	Lead.MantlesAtTurn = Before;
	Lead.OrderSerial = First->GetMoveOrderSerial();
	return true;
}

bool USquadSubsystem::BeginSquadMantleFromPath(AOperativeCharacter& Climber, const FMantleSite& Site, const FVector& ResumeTarget)
{
	if (!IsSquadMantleEnabled() || SquadMantle.bActive || !Site.Obstacle.IsValid())
	{
		return false;
	}
	const TArray<AOperativeCharacter*> Group = GetSquadMantleGroup(Climber);
	if (Group.Num() < 2)
	{
		return false;
	}
	SquadMantle = FSquadMantle();
	FSquadMantle& Op = SquadMantle;
	Op.Obstacle = Site.Obstacle.Get();
	Op.Dir = Site.Dir.GetSafeNormal2D();
	Op.FacePoint = Site.FacePoint;
	Op.EdgeTop = Site.EdgeTop;
	Op.DepthCm = Site.Probe.DepthCm;
	Op.bOver = Site.Destination == EMantleDestination::Over;
	Op.Base = FVector(Site.FacePoint.X, Site.FacePoint.Y, Site.Feet.Z) - Op.Dir * SquadMantleLocal::BaseOffFaceCm;
	SetUpSquadMantle(Group, /*bFromPath*/ true, Site.Landing);
	FSquadMantleMember& Lead = Op.Members[0];
	Lead.State = ESquadMantleState::Climbing;
	Lead.bOrdered = true;
	Lead.MantlesAtTurn = Climber.GetMantleCount() - 1; // this climb is his turn's
	Lead.OrderSerial = Climber.GetMoveOrderSerial();
	Lead.bResume = true;
	Lead.ResumeTarget = ResumeTarget;
	return true;
}

void USquadSubsystem::SetUpSquadMantle(const TArray<AOperativeCharacter*>& Group, bool bFromPath, const FVector& FirstLanding)
{
	using namespace SquadMantleLocal;
	FSquadMantle& Op = SquadMantle;
	const FMantleConfig& Config = MantleRules::GetConfig();
	Op.bActive = true;
	Op.bFromPath = bFromPath;
	Op.bSolo = bIsSoloMode;
	Op.Tangent = FVector::CrossProduct(FVector::UpVector, Op.Dir).GetSafeNormal2D();
	const AOperativeCharacter* Any = Group.IsEmpty() ? nullptr : Group[0];
	const float Radius = Any && Any->GetCapsuleComponent() ? Any->GetCapsuleComponent()->GetScaledCapsuleRadius() : 35.f;
	// Candidate lanes: the group + 2 per face. No traces / path queries here (a frame spike): each man's lane is tentative (its geometry keeps
	// the landings apart) and is checked with his own clip and a path at his turn (StartSquadMantleTurn: else the first spare that is free).
	TArray<FSquadMantleLane> Lanes = MakeSquadMantleLanes(Group.Num() + 2, Radius);
	TArray<FVector> Landings;
	FString Line;
	for (int32 Index = 0; Index < Group.Num(); ++Index)
	{
		AOperativeCharacter* Operative = Group[Index];
		FSquadMantleMember& Member = Op.Members.AddDefaulted_GetRef();
		Member.Operative = Operative;
		Member.OrderSerial = Operative ? Operative->GetMoveOrderSerial() : 0;
		if (!Operative)
		{
			Member.State = ESquadMantleState::Out;
			continue;
		}
		if (Index == 0)
		{
			// The first lane is his (the clicked spot / the link he climbs).
			Member.Lane = Lanes.IsEmpty() ? FSquadMantleLane() : Lanes[0];
			Member.Lane.Landing = FirstLanding.IsNearlyZero() ? Op.EdgeTop + Op.Dir * Config.TopInsetCm : FirstLanding;
			Landings.Add(Member.Lane.Landing);
			if (!Lanes.IsEmpty())
			{
				Lanes.RemoveAt(0);
			}
			Op.Queue.TryLease(Operative, [](const AOperativeCharacter*) { return false; });
			Line += FString::Printf(TEXT("%s (lane 0)"), *Operative->DisplayName.ToString());
			continue;
		}
		// His tentative lane: the first candidate whose estimated landing keeps clear of the lanes given before.
		int32 Chosen = INDEX_NONE;
		for (int32 Lane = 0; Lane < Lanes.Num() && Chosen == INDEX_NONE; ++Lane)
		{
			const FVector Landing = ApproxSquadMantleLanding(Lanes[Lane]);
			if (!Landings.ContainsByPredicate([&](const FVector& Other) { return FVector::Dist2D(Other, Landing) < Config.SquadMinLandingGapCm; }))
			{
				Chosen = Lane;
				Lanes[Lane].Landing = Landing;
				Landings.Add(Landing);
			}
		}
		if (Chosen == INDEX_NONE)
		{
			Member.State = ESquadMantleState::Hold; // the obstacle has no lane left for him: he holds at the base, covering
			Member.Lane = Op.Members[0].Lane;
			Line += FString::Printf(TEXT(", %s (no lane: holds at the base)"), *Operative->DisplayName.ToString());
		}
		else
		{
			Member.Lane = Lanes[Chosen];
			Lanes.RemoveAt(Chosen);
			Member.State = ESquadMantleState::Waiting;
			Op.Queue.Join(Operative); // the ladder's line, in climb order
			Line += FString::Printf(TEXT(", %s (face %d lane %+.0f)"), *Operative->DisplayName.ToString(), Member.Lane.Face, Member.Lane.OffsetCm);
		}
		// A box-selected man with an order of his own walks on to it once all are over (a path climb); formation followers follow again.
		if (bFromPath && bIsSoloMode && Operative->HasOwnMoveOrder())
		{
			Member.bResume = true;
			Member.ResumeTarget = Operative->GetLastMoveDestination();
		}
	}
	Op.SpareLanes = MoveTemp(Lanes);
	Op.Walls = GatherWallsAround(FVector2D(Op.Base), WallsHalfExtentCm, static_cast<float>(Op.Base.Z));
	++SquadMantleCount;
	UE_LOG(LogCodexTactics, Display, TEXT("[SquadMantle] %s %s %s (%s, %d spare lanes): in turns - %s"), bFromPath ? TEXT("a path climb") : TEXT("a climb order"),
		Op.bOver ? TEXT("over") : TEXT("onto"), *GetNameSafe(Op.Obstacle.Get()), bIsSoloMode ? TEXT("box selection") : TEXT("formation"), Op.SpareLanes.Num(), *Line);
}

void USquadSubsystem::StartSquadMantleTurn(FSquadMantleMember& Member)
{
	AOperativeCharacter* Operative = Member.Operative.Get();
	if (!Operative || Member.bOrdered)
	{
		return;
	}
	// Prone / a stance clip playing: he gets up first (the climb order waits for him).
	if (Operative->GetStance() == EOperativeStance::Prone)
	{
		Operative->SetStance(EOperativeStance::Standing);
		return;
	}
	if (Operative->GetMoveHoldSeconds() > 0.f || Operative->IsKnockedDown())
	{
		return;
	}
	if (Member.Retries > 0 && Member.SinceOrder < SquadMantleLocal::TurnRetrySeconds)
	{
		return;
	}
	// His lane against where the men up really stand (a run climb lands off its lane): blocked -> the first spare lane that is free for him.
	{
		const TArray<FVector> Taken = SquadMantleTakenLandings(&Member);
		FVector Landing;
		FString Why;
		if (!IsSquadMantleLaneFor(*Operative, Member.Lane, Taken, Landing, &Why))
		{
			TArray<int32> Order;
			for (int32 Spare = 0; Spare < SquadMantle.SpareLanes.Num(); ++Spare)
			{
				Order.Add(Spare);
			}
			const int32 Face = Member.Lane.Face;
			Order.StableSort([this, Face](int32 A, int32 B) { return (SquadMantle.SpareLanes[A].Face == Face) > (SquadMantle.SpareLanes[B].Face == Face); });
			bool bSwapped = false;
			for (const int32 Spare : Order)
			{
				if (IsSquadMantleLaneFor(*Operative, SquadMantle.SpareLanes[Spare], Taken, Landing))
				{
					const FSquadMantleLane Old = Member.Lane;
					Member.Lane = SquadMantle.SpareLanes[Spare];
					Member.Lane.Landing = Landing;
					SquadMantle.SpareLanes[Spare] = Old;
					UE_LOG(LogCodexTactics, Display, TEXT("[SquadMantle] %s: his lane (face %d %+.0f) is no good now (%s) - face %d lane %+.0f instead"),
						*Operative->DisplayName.ToString(), Old.Face, Old.OffsetCm, *Why, Member.Lane.Face, Member.Lane.OffsetCm);
					bSwapped = true;
					break;
				}
			}
			if (!bSwapped)
			{
				// No lane of the obstacle is left for him: he holds at the base, covering (the next one goes).
				UE_LOG(LogCodexTactics, Display, TEXT("[SquadMantle] %s: his lane (face %d %+.0f) is no good (%s) and no spare lane is - he holds at the base"),
					*Operative->DisplayName.ToString(), Member.Lane.Face, Member.Lane.OffsetCm, *Why);
				Member.State = ESquadMantleState::Hold;
				Member.SpotPlace = INDEX_NONE;
				SquadMantle.Queue.Release(Operative);
				return;
			}
		}
	}
	const int32 Before = Operative->GetMantleCount();
	FString Why;
	const FVector LaneBase = Member.Lane.Base;
	if (!Operative->OrderMantleTop(SquadMantleLaneHit(Member.Lane), /*bSprint*/ false, &Why, &LaneBase))
	{
		if (++Member.Retries > 2)
		{
			MarkSquadMantleOut(Member, FString::Printf(TEXT("his climb order is refused (%s)"), *Why));
		}
		return;
	}
	Member.bOrdered = true;
	Member.SinceOrder = 0.f;
	Member.MantlesAtTurn = Before;
	Member.OrderSerial = Operative->GetMoveOrderSerial();
	UE_LOG(LogCodexTactics, Display, TEXT("[SquadMantle] %s's turn: to his lane (face %d %+.0f cm) of %s"), *Operative->DisplayName.ToString(), Member.Lane.Face,
		Member.Lane.OffsetCm, *GetNameSafe(SquadMantle.Obstacle.Get()));
}

void USquadSubsystem::MarkSquadMantleOut(FSquadMantleMember& Member, const FString& Why)
{
	AOperativeCharacter* Operative = Member.Operative.Get();
	Member.State = ESquadMantleState::Out;
	Member.OutWhy = Why;
	Member.LookYaw.Reset();
	SquadMantle.Queue.Leave(Operative);
	SquadMantle.Queue.Release(Operative);
	if (Operative && !Operative->IsMantling())
	{
		Operative->CancelMantleOrder();
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[SquadMantle] %s is out of the climb: %s"), Operative ? *Operative->DisplayName.ToString() : TEXT("(gone)"), *Why);
}

void USquadSubsystem::MarkSquadMantleUp(FSquadMantleMember& Member)
{
	AOperativeCharacter* Operative = Member.Operative.Get();
	Member.State = ESquadMantleState::Up;
	Member.UpIndex = SquadMantle.UpCount++;
	SquadMantle.Queue.Release(Operative);
	SquadMantle.NextDelay = MantleRules::GetConfig().SquadNextDelaySeconds;
	UE_LOG(LogCodexTactics, Display, TEXT("[SquadMantle] %s is %s (%d of %d) after %.1f s - covers from there"), Operative ? *Operative->DisplayName.ToString() : TEXT("(gone)"),
		SquadMantle.bOver ? TEXT("over") : TEXT("up"), Member.UpIndex + 1, SquadMantle.Members.Num(), Member.TurnSeconds);
}

void USquadSubsystem::UpdateSquadMantle(float DeltaTime)
{
	using namespace SquadMantleLocal;
	FSquadMantle& Op = SquadMantle;
	if (!Op.bActive)
	{
		return;
	}
	const FMantleConfig& Config = MantleRules::GetConfig();
	Op.Seconds += DeltaTime;
	Op.NextDelay = FMath::Max(0.f, Op.NextDelay - DeltaTime);
	const UGameFlowSubsystem* Flow = GetWorld() ? GetWorld()->GetSubsystem<UGameFlowSubsystem>() : nullptr;
	if (!IsSquadMantleEnabled())
	{
		EndSquadMantle(TEXT("Codex.Mantle.Squad 0"));
		return;
	}
	if (Flow && Flow->GetCombatMode() == ECodexCombatMode::TurnBased)
	{
		EndSquadMantle(TEXT("the turn-based fight"));
		return;
	}
	if (Op.bSolo != bIsSoloMode)
	{
		EndSquadMantle(TEXT("the squad's mode changed"));
		return;
	}
	if (!Op.Obstacle.IsValid())
	{
		EndSquadMantle(TEXT("the obstacle is gone"));
		return;
	}
	auto IsWaiting = [this](const AOperativeCharacter* Who)
	{
		const FSquadMantleMember* Member = FindSquadMantleMember(Who);
		return Member && Member->State == ESquadMantleState::Waiting && !Who->IsKnockedDown();
	};

	// 1. Down / gone / given an order of his own (the player's next order for the leader ends the whole climb, as for a ladder's group).
	for (int32 Index = 0; Index < Op.Members.Num(); ++Index)
	{
		FSquadMantleMember& Member = Op.Members[Index];
		if (Member.State == ESquadMantleState::Out)
		{
			continue;
		}
		AOperativeCharacter* Operative = Member.Operative.Get();
		if (!Operative || !Operative->HealthComponent || !Operative->HealthComponent->IsAlive())
		{
			MarkSquadMantleOut(Member, TEXT("down / gone"));
			continue;
		}
		if (Operative->GetMoveOrderSerial() != Member.OrderSerial)
		{
			if (Index == 0)
			{
				EndSquadMantle(TEXT("the leader's next order"));
				return;
			}
			MarkSquadMantleOut(Member, TEXT("an order of his own"));
		}
	}

	// 2. The holder's turn: to the face, the climb, up.
	if (AOperativeCharacter* Holder = Op.Queue.GetHolder())
	{
		FSquadMantleMember* Member = FindSquadMantleMember(Holder);
		if (!Member || (Member->State != ESquadMantleState::Turn && Member->State != ESquadMantleState::Climbing))
		{
			Op.Queue.Release(Holder);
		}
		else
		{
			Member->TurnSeconds += DeltaTime;
			Member->SinceOrder += DeltaTime;
			if (Member->State == ESquadMantleState::Turn)
			{
				if (!Member->bOrdered)
				{
					StartSquadMantleTurn(*Member);
				}
				else if (Holder->IsMantling())
				{
					Member->State = ESquadMantleState::Climbing;
					UE_LOG(LogCodexTactics, Display, TEXT("[SquadMantle] %s climbs (%s)"), *Holder->DisplayName.ToString(), *Holder->GetLastMantleClip());
				}
				else if (Holder->GetMantleCount() > Member->MantlesAtTurn)
				{
					MarkSquadMantleUp(*Member); // a short climb ended between two ticks
				}
				else if (!Holder->HasMantleOrder() && !Holder->IsMoving() && Member->SinceOrder > TurnStallSeconds)
				{
					// Refused at the face (or the walk ended short): asked once more (after TurnRetrySeconds from now), then he stays at the base.
					if (Member->Retries < 1)
					{
						++Member->Retries;
						Member->bOrdered = false;
						Member->SinceOrder = 0.f;
					}
					else
					{
						MarkSquadMantleOut(*Member, FString::Printf(TEXT("refused at the face (%s)"), MantleRules::RefusalName(Holder->GetLastMantleRefusal())));
					}
				}
			}
			if (Member->State == ESquadMantleState::Climbing && !Holder->IsMantling())
			{
				const EMantleCancel Cancel = Holder->GetLastMantleCancel();
				if (Cancel != EMantleCancel::None)
				{
					MarkSquadMantleOut(*Member, FString::Printf(TEXT("his climb was cancelled (%d)"), static_cast<int32>(Cancel)));
				}
				else
				{
					MarkSquadMantleUp(*Member);
				}
			}
			if ((Member->State == ESquadMantleState::Turn || Member->State == ESquadMantleState::Climbing) && !Holder->IsMantling()
				&& Member->TurnSeconds > Config.SquadTurnTimeoutSeconds)
			{
				MarkSquadMantleOut(*Member, FString::Printf(TEXT("his turn took over %.0f s"), Config.SquadTurnTimeoutSeconds));
			}
		}
	}

	// 3. The next one in line climbs once the one before him is up (the ladder's rule: the lease free, nobody before him still waiting).
	if (!Op.Queue.GetHolder() && Op.NextDelay <= 0.f)
	{
		for (FSquadMantleMember& Member : Op.Members)
		{
			AOperativeCharacter* Operative = Member.Operative.Get();
			if (Member.State != ESquadMantleState::Waiting || !Operative || !IsWaiting(Operative))
			{
				continue;
			}
			if (Op.Queue.TryLease(Operative, IsWaiting))
			{
				Member.State = ESquadMantleState::Turn;
				Member.TurnSeconds = 0.f;
				Member.SinceOrder = 0.f;
				Member.bOrdered = false;
				Member.Retries = 0;
				Member.LookYaw.Reset();
				StartSquadMantleTurn(Member);
			}
			break;
		}
	}

	// 4. The waiters on their spots (the ladder's spot rule out from their lane's base), covering outwards; the men up covering from there.
	TOptional<float> ThreatYaw;
	{
		float Nearest = TNumericLimits<float>::Max();
		const FVector2D Base2D(Op.Base);
		for (const FFormationThreat& Threat : GatherFormationThreats())
		{
			const float Dist = static_cast<float>(FVector2D::Distance(Threat.Pos, Base2D));
			if (Threat.AgeS >= 0.f && Threat.AgeS <= FormationTuning::Get().BypassThreatMemoryS && Dist < Nearest && Dist > 1.f)
			{
				Nearest = Dist;
				const FVector2D To = Threat.Pos - Base2D;
				ThreatYaw = static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(To.Y, To.X)));
			}
		}
	}
	const float ClimbYaw = static_cast<float>(Op.Dir.Rotation().Yaw);
	for (int32 MemberIndex = 0; MemberIndex < Op.Members.Num(); ++MemberIndex)
	{
		FSquadMantleMember& Member = Op.Members[MemberIndex];
		AOperativeCharacter* Operative = Member.Operative.Get();
		Member.LookYaw.Reset();
		if (!Operative)
		{
			continue;
		}
		if (Member.State == ESquadMantleState::Waiting || Member.State == ESquadMantleState::Hold)
		{
			const int32 Place = Member.State == ESquadMantleState::Waiting ? Op.Queue.GetIndex(Operative, IsWaiting) : MemberIndex;
			if (Place != INDEX_NONE && Place != Member.SpotPlace && !Operative->IsKnockedDown())
			{
				Member.SpotPlace = Place;
				const float Out = TTraversalQueue<AOperativeCharacter>::SpotDistanceCm(Place, Config.SquadQueueFirstCm, Config.SquadQueueSpacingCm);
				// The ladder's spot when he can reach it; else the nearest reachable one towards his face's first lane / nearer the face.
				auto PickSpot = [&]() -> FVector
				{
					for (const float Toward : { 0.f, 0.5f, 1.f })
					{
						for (const float Near : { 1.f, 0.5f })
						{
							const FVector Candidate = Member.Lane.Base - Member.Lane.Tangent * (Member.Lane.OffsetCm * Toward) - Member.Lane.Dir * (Out * Near);
							if (IsReachable(GetWorld(), *Operative, Candidate, SpotArriveCm))
							{
								return Candidate;
							}
						}
					}
					return Member.Lane.Base - Member.Lane.Dir * Out;
				};
				Member.Spot = PickSpot();
				if (FVector::Dist2D(FeetOf(*Operative), Member.Spot) > SpotArriveCm)
				{
					Operative->OrderMoveTo(Member.Spot, false);
					Member.OrderSerial = Operative->GetMoveOrderSerial();
				}
			}
			if (Place != INDEX_NONE && !Operative->IsMoving())
			{
				const float LaneOutYaw = static_cast<float>((-Member.Lane.Dir).Rotation().Yaw); // away from the obstacle at his face
				const float Raw = SquadMantleRules::WaitLookYaw(LaneOutYaw, Place, Config.SquadWaitFanDeg, Config.SquadWaitMaxOffDeg, ThreatYaw);
				// The formation's rule: nobody looks into a wall (FormationSectorRules::ClearLook).
				Member.LookYaw = FormationSectorRules::ClearLook(Op.Walls, FVector2D(Operative->GetActorLocation()), Raw, static_cast<float>(Operative->GetActorRotation().Yaw),
					FormationTuning::Get());
			}
		}
		else if (Member.State == ESquadMantleState::Up)
		{
			Member.LookYaw = SquadMantleRules::UpLookYaw(ClimbYaw, Member.UpIndex, ThreatYaw);
		}
		if (Member.LookYaw.IsSet())
		{
			FrameLooks.Add(Member.Operative, Member.LookYaw.GetValue());
		}
	}

	// 5. Everybody up (or holding / out): a path climb walks on (the formation follows again), a clicked climb holds up there until the next order.
	bool bLeft = false;
	bool bAnyIn = false;
	int32 UpNow = 0;
	for (const FSquadMantleMember& Member : Op.Members)
	{
		bLeft |= Member.State == ESquadMantleState::Waiting || Member.State == ESquadMantleState::Turn || Member.State == ESquadMantleState::Climbing;
		bAnyIn |= Member.State != ESquadMantleState::Out;
		UpNow += Member.State == ESquadMantleState::Up ? 1 : 0;
	}
	if (!bAnyIn)
	{
		EndSquadMantle(TEXT("nobody is left in it"));
		return;
	}
	if (!bLeft && !Op.bHolding)
	{
		if (UpNow == Op.Members.Num())
		{
			++SquadMantleDoneCount;
		}
		UE_LOG(LogCodexTactics, Display, TEXT("[SquadMantle] %d of %d %s %s after %.1f s"), UpNow, Op.Members.Num(), Op.bOver ? TEXT("over") : TEXT("up on"),
			*GetNameSafe(Op.Obstacle.Get()), Op.Seconds);
		if (Op.bFromPath)
		{
			EndSquadMantle(TEXT("all over: walking on"));
			return;
		}
		Op.bHolding = true;
	}
}

void USquadSubsystem::EndSquadMantle(const TCHAR* Why, bool bResumePath)
{
	FSquadMantle& Op = SquadMantle;
	if (!Op.bActive)
	{
		return;
	}
	// A path climb: the men whose own walk it interrupted (the leader, box members) walk on to it whatever ended the climb (all over, out,
	// nobody left, a mode switch, the console) - unless the player gave him another order since, or the turn-based grid owns the squad.
	// Mid-clip the order is taken after the landing (AOperativeCharacter::RequestMove).
	TArray<TPair<TWeakObjectPtr<AOperativeCharacter>, FVector>> Resumes;
	const UGameFlowSubsystem* Flow = GetWorld() ? GetWorld()->GetSubsystem<UGameFlowSubsystem>() : nullptr;
	if (bResumePath && Op.bFromPath && !(Flow && Flow->GetCombatMode() == ECodexCombatMode::TurnBased))
	{
		for (const FSquadMantleMember& Member : Op.Members)
		{
			const AOperativeCharacter* Operative = Member.Operative.Get();
			if (Member.bResume && Operative && Operative->HealthComponent && Operative->HealthComponent->IsAlive() && Operative->GetMoveOrderSerial() == Member.OrderSerial
				&& FVector::Dist2D(SquadMantleLocal::FeetOf(*Operative), Member.ResumeTarget) > 60.f)
			{
				Resumes.Emplace(Member.Operative, Member.ResumeTarget);
			}
		}
	}
	for (FSquadMantleMember& Member : Op.Members)
	{
		// A walk to the face that has not climbed yet stops climbing on arrival (a climb in progress finishes, as on a ladder).
		if (AOperativeCharacter* Operative = Member.Operative.Get(); Operative && Member.State == ESquadMantleState::Turn && !Operative->IsMantling())
		{
			Operative->CancelMantleOrder();
		}
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[SquadMantle] the climb of %s ends after %.1f s: %s%s"), *GetNameSafe(Op.Obstacle.Get()), Op.Seconds, Why,
		Resumes.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" (%d walk on)"), Resumes.Num()));
	Op.Queue.Reset();
	Op.bActive = false;
	Op.bHolding = false;
	for (const TPair<TWeakObjectPtr<AOperativeCharacter>, FVector>& Resume : Resumes)
	{
		if (AOperativeCharacter* Operative = Resume.Key.Get())
		{
			Operative->OrderMoveTo(Resume.Value, false);
		}
	}
}

void USquadSubsystem::CancelSquadMantle(const TCHAR* Why)
{
	EndSquadMantle(Why, /*bResumePath*/ false);
}

bool USquadSubsystem::IsInSquadMantle(const AOperativeCharacter* Operative) const
{
	const FSquadMantleMember* Member = SquadMantle.bActive ? FindSquadMantleMember(Operative) : nullptr;
	return Member && Member->State != ESquadMantleState::Out;
}

USquadSubsystem::FSquadMantleMember* USquadSubsystem::FindSquadMantleMember(const AOperativeCharacter* Operative)
{
	return Operative ? SquadMantle.Members.FindByPredicate([Operative](const FSquadMantleMember& Member) { return Member.Operative.Get() == Operative; }) : nullptr;
}

const USquadSubsystem::FSquadMantleMember* USquadSubsystem::FindSquadMantleMember(const AOperativeCharacter* Operative) const
{
	return Operative ? SquadMantle.Members.FindByPredicate([Operative](const FSquadMantleMember& Member) { return Member.Operative.Get() == Operative; }) : nullptr;
}

FSquadMantleView USquadSubsystem::GetSquadMantleView() const
{
	const FSquadMantle& Op = SquadMantle;
	FSquadMantleView View;
	View.bActive = Op.bActive;
	View.bFromPath = Op.bFromPath;
	View.bHolding = Op.bHolding;
	View.bOver = Op.bOver;
	View.Obstacle = Op.Obstacle;
	View.FacePoint = Op.FacePoint;
	View.Dir = Op.Dir;
	View.OutwardYaw = static_cast<float>((-Op.Dir).Rotation().Yaw);
	auto IsWaiting = [this](const AOperativeCharacter* Who)
	{
		const FSquadMantleMember* Member = FindSquadMantleMember(Who);
		return Member && Member->State == ESquadMantleState::Waiting && !Who->IsKnockedDown();
	};
	for (const FSquadMantleMember& Member : Op.Members)
	{
		FSquadMantleMemberView& Out = View.Members.AddDefaulted_GetRef();
		Out.Operative = Member.Operative;
		Out.State = Member.State;
		Out.LaneOffsetCm = Member.Lane.OffsetCm;
		Out.LaneBase = Member.Lane.Base;
		Out.LaneFacePoint = Member.Lane.FacePoint;
		Out.LaneDir = Member.Lane.Dir;
		Out.Place = Member.State == ESquadMantleState::Waiting && Member.Operative.IsValid() ? Op.Queue.GetIndex(Member.Operative.Get(), IsWaiting) : INDEX_NONE;
		Out.Spot = Member.Spot;
		Out.UpIndex = Member.UpIndex;
		Out.LookYaw = Member.LookYaw;
		Out.OutWhy = Member.OutWhy;
	}
	return View;
}
