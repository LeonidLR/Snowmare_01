// USquadSubsystem: formation rules v2.1 in the runtime (behind Codex.Formation.Sectors + Codex.Formation.V21) - the squad never splits:
// the wall guard (FormationGuardRules::Resolve: a formation a wall cuts takes its mirror echelon, else the TRAIL column along the leader's
// recorded path, FormationShapeRules::TrailSlots; checked at the leader and guard.lookahead further on his path; the request comes back after
// classify.hold_distance of path), the sharp-turn fold (TurnAhead > turn.fold_deg folds a wide formation into the trail column), the corner
// watch (FormationCornerRules::Watch, slicing the pie, also for the leading man of a wall slide) and ClearLook (nobody looks into a wall).
// The rules see the level as FFormationWall: the box collision of the blocking world geometry (FormationGuardRuntime::WallsFromBox).
// User decisions 2026-10-25 (docs/design/FORMATION_SECTORS.md v2.1; contract docs/design/FORMATION_CONTRACT.md 10).
// Godot reference: none (UE-only; reference simulation Tools/TrainingGround/src/movement/sim.ts).

#include "Characters/SquadSubsystem.h"

#include "AIController.h"
#include "Characters/OperativeCharacter.h"
#include "CodexTactics.h"
#include "CollisionQueryParams.h"
#include "Components/BoxComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "Formation/FormationGuardRuntimeRules.h"
#include "Formation/FormationRuntimeRules.h"
#include "Formation/FormationSectorRules.h"
#include "Formation/FormationShapeRules.h"
#include "Formation/FormationTuning.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationData.h"
#include "PhysicsEngine/BodySetup.h"

namespace SquadSubsystemFormationGuardLocal
{
	/** The world walls are gathered this far round the leader (the guard's lookahead + the column; the corner's probe side), cm. */
	constexpr float WorldWallsReachCm = 1600.f;
	/** ... again this often, s, or once the leader walked this far from where they were gathered, cm. */
	constexpr float WorldWallsRefreshSeconds = 0.25f;
	constexpr float WorldWallsRefreshCm = 300.f;
	/** The gather slab's half height round the probe plane, cm. */
	constexpr float WallSlabHalfHeightCm = 5.f;
	/** A box counts as standing upright when its vertical axis is within this cosine of the world's up. */
	constexpr double UprightCos = 0.95;
	/**
	 * A wall must reach this far above the probe plane (FormationRuntime::ProbeHeightCm over the feet): lower obstacles are looked over, as
	 * by the probes (the tutorial's 1.2 m wall: standing, the squad sees the enemy beyond it - nobody turns away from it), cm.
	 */
	constexpr double LookedOverMarginCm = 15.0;

	float GuardFeetZ(const AActor& Actor)
	{
		return static_cast<float>(Actor.GetActorLocation().Z) - Actor.GetSimpleCollisionHalfHeight();
	}

	/** A box (BoxToWorld incl. scale, unscaled half size) crossing PlaneZ and standing upright -> the rules' walls. */
	void AppendGuardBox(const FTransform& BoxToWorld, const FVector& HalfSize, float PlaneZ, TArray<FFormationWall>& Out)
	{
		const FVector Axes[3] = { BoxToWorld.TransformVector(FVector(HalfSize.X, 0.0, 0.0)), BoxToWorld.TransformVector(FVector(0.0, HalfSize.Y, 0.0)),
			BoxToWorld.TransformVector(FVector(0.0, 0.0, HalfSize.Z)) };
		int32 Up = 0;
		double UpCos = -1.0;
		for (int32 Index = 0; Index < 3; ++Index)
		{
			const double Length = Axes[Index].Size();
			const double Cos = Length > KINDA_SMALL_NUMBER ? FMath::Abs(Axes[Index].Z) / Length : 0.0;
			if (Cos > UpCos)
			{
				UpCos = Cos;
				Up = Index;
			}
		}
		if (UpCos < UprightCos)
		{
			return; // tilted (a ramp, a fallen slab): not a wall
		}
		const FVector Center = BoxToWorld.GetLocation();
		const double HalfHeight = FMath::Abs(Axes[Up].Z);
		if (PlaneZ < Center.Z - HalfHeight || PlaneZ + LookedOverMarginCm > Center.Z + HalfHeight)
		{
			return; // this box lies above the probe plane, or is low enough to look over (a 1.2 m wall: the squad sees across it)
		}
		const FVector& A = Axes[(Up + 1) % 3];
		const FVector& B = Axes[(Up + 2) % 3];
		FormationGuardRuntime::WallsFromBox(FVector2D(Center), FVector2D(A), FVector2D(B), Out);
	}
}

void USquadSubsystem::UpdateFormationTrailAndWalls(const AOperativeCharacter& LeaderRef, float DeltaTime)
{
	using namespace SquadSubsystemFormationGuardLocal;
	const FVector2D Here(LeaderRef.GetActorLocation());
	// A teleport (a smoke, a load, a ladder top) starts the trail afresh: he did not walk the gap.
	if (Sectors.Trail.Num() > 0 && FVector2D::Distance(Sectors.Trail.Last(), Here) > FormationRuntime::MaxStepDistanceCm + FormationGuardRuntime::TrailStepCm)
	{
		Sectors.Trail.Reset();
	}
	FormationGuardRuntime::PushTrail(Sectors.Trail, Here);

	Sectors.WorldWallsAge += DeltaTime;
	if (!Sectors.bWorldWallsValid || Sectors.WorldWallsAge >= WorldWallsRefreshSeconds || FVector2D::Distance(Here, Sectors.WorldWallsCenter) > WorldWallsRefreshCm)
	{
		Sectors.WorldWalls = GatherFormationWalls(Here, FVector2D(WorldWallsReachCm, WorldWallsReachCm), 0.f, GuardFeetZ(LeaderRef) + FormationRuntime::ProbeHeightCm);
		Sectors.WorldWallsCenter = Here;
		Sectors.WorldWallsAge = 0.f;
		Sectors.bWorldWallsValid = true;
	}
}

TArray<FFormationWall> USquadSubsystem::GatherFormationWalls(const FVector2D& Center, const FVector2D& HalfExtents, float Yaw, float PlaneZ) const
{
	using namespace SquadSubsystemFormationGuardLocal;
	TArray<FFormationWall> Walls;
	UWorld* World = GetWorld();
	if (!World)
	{
		return Walls;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(FormationWorldWalls), false);
	for (const TWeakObjectPtr<AOperativeCharacter>& Member : Members)
	{
		Params.AddIgnoredActor(Member.Get());
	}
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByObjectType(Overlaps, FVector(Center.X, Center.Y, PlaneZ), FRotator(0.f, Yaw, 0.f).Quaternion(), Objects,
		FCollisionShape::MakeBox(FVector(HalfExtents.X, HalfExtents.Y, WallSlabHalfHeightCm)), Params);
	TSet<TPair<const UPrimitiveComponent*, int32>> Done;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		const UPrimitiveComponent* Component = Overlap.GetComponent();
		if (!Component || Done.Contains({ Component, Overlap.ItemIndex }))
		{
			continue;
		}
		Done.Add({ Component, Overlap.ItemIndex });
		const AActor* Owner = Component->GetOwner();
		if ((Owner && Owner->IsA<APawn>()) || Component->GetCollisionResponseToChannel(ECC_Pawn) != ECR_Block)
		{
			continue; // characters / things the squad walks through are no walls
		}
		if (const UBoxComponent* Box = Cast<UBoxComponent>(Component))
		{
			AppendGuardBox(Box->GetComponentTransform(), Box->GetUnscaledBoxExtent(), PlaneZ, Walls);
			continue;
		}
		const UStaticMeshComponent* Mesh = Cast<UStaticMeshComponent>(Component);
		UBodySetup* BodySetup = Mesh ? const_cast<UStaticMeshComponent*>(Mesh)->GetBodySetup() : nullptr;
		if (!BodySetup || BodySetup->CollisionTraceFlag == CTF_UseComplexAsSimple)
		{
			continue; // only simple box collision is read as walls (a complex mesh's shape is unknown here; the probes still see it)
		}
		FTransform MeshToWorld = Mesh->GetComponentTransform();
		if (const UInstancedStaticMeshComponent* Instances = Cast<UInstancedStaticMeshComponent>(Mesh))
		{
			if (Overlap.ItemIndex == INDEX_NONE || !Instances->GetInstanceTransform(Overlap.ItemIndex, MeshToWorld, /*bWorldSpace*/ true))
			{
				continue;
			}
		}
		for (const FKBoxElem& Elem : BodySetup->AggGeom.BoxElems)
		{
			AppendGuardBox(Elem.GetTransform() * MeshToWorld, FVector(0.5 * Elem.X, 0.5 * Elem.Y, 0.5 * Elem.Z), PlaneZ, Walls);
		}
	}
	return Walls;
}

TArray<FVector2D> USquadSubsystem::LeaderPathAhead(const AOperativeCharacter& LeaderRef, bool bLeaderMoving) const
{
	const FVector2D LeaderPos(LeaderRef.GetActorLocation());
	if (!bLeaderMoving || !LeaderRef.HasOwnMoveOrder())
	{
		return { LeaderPos };
	}
	TArray<FVector2D> Points;
	const AAIController* Controller = Cast<AAIController>(LeaderRef.GetController());
	const UPathFollowingComponent* Following = Controller ? Controller->GetPathFollowingComponent() : nullptr;
	const FNavPathSharedPtr NavPath = Following ? Following->GetPath() : nullptr;
	if (NavPath.IsValid() && NavPath->IsValid())
	{
		for (const FNavPathPoint& Point : NavPath->GetPathPoints())
		{
			Points.Add(FVector2D(Point.Location));
		}
	}
	TArray<FVector2D> Path = FormationGuardRuntime::PathAheadFrom(Points, LeaderPos);
	// A bypass steers him to its waypoint first: the rest of its waypoints, then his destination (sim: the route's next points).
	if (Sectors.Bypass.bActive)
	{
		for (int32 Index = Sectors.Bypass.Index + 1; Index < Sectors.Bypass.Waypoints.Num(); ++Index)
		{
			Path.Add(Sectors.Bypass.Waypoints[Index]);
		}
	}
	const FVector2D Destination(LeaderRef.GetLastMoveDestination());
	if (FVector2D::Distance(Path.Last(), Destination) > FormationRuntime::StopRadiusCm)
	{
		Path.Add(Destination);
	}
	return Path;
}

void USquadSubsystem::StepFormationGuard(const AOperativeCharacter& LeaderRef, bool bLeaderMoving, int32 Count)
{
	const FFormationConfig& Config = FormationTuning::Get();
	const FVector2D LeaderPos(LeaderRef.GetActorLocation());
	const TArray<FVector2D> Path = LeaderPathAhead(LeaderRef, bLeaderMoving);
	TOptional<FFormationPose> Ahead;
	if (Path.Num() > 1)
	{
		Ahead = FormationGuardRuntime::AheadPose(Path, Sectors.HeadingYaw, Config.GuardLookahead);
	}
	const float Turn = FormationGuardRules::TurnAhead(Path, Sectors.HeadingYaw, Config);
	const FFormationGuardState Before = Sectors.Guard;
	Sectors.Guard = FormationGuardRules::Resolve(Sectors.WorldWalls, Sectors.Requested, Count, LeaderPos, Sectors.HeadingYaw, Ahead, Turn, Sectors.Guard,
		Sectors.LastStepDistance, Config);
	const FFormationGuardEffective Effective = FormationGuardRules::Effective(Sectors.Guard);
	Sectors.Formation = Effective.Formation.Get(Sectors.Requested);
	Sectors.bTrail = Effective.bTrail;
	// The HUD tells the sharp-turn fold "(turn)" from a wall cut "(wall)" (user decision 2026-10-26).
	const bool bWide = Sectors.Requested == EFormationKind::EchelonLeft || Sectors.Requested == EFormationKind::EchelonRight
		|| Sectors.Requested == EFormationKind::Line;
	bool bCut = false;
	if (Sectors.Guard.Level == EFormationGuardLevel::Trail)
	{
		bCut = FormationGuardRules::BlockedRoles(Sectors.WorldWalls, Sectors.Requested, Count, LeaderPos, Sectors.HeadingYaw, Config).Num() > 0
			|| (Ahead.IsSet() && FormationGuardRules::BlockedRoles(Sectors.WorldWalls, Sectors.Requested, Count, Ahead->Pos, Ahead->Yaw, Config).Num() > 0);
	}
	Sectors.bGuardTurnFold = FormationGuardRuntime::IsTurnFold(Sectors.Guard.Level, bWide && Turn > Config.TurnFoldDeg, bCut, Sectors.bGuardTurnFold);
	if (Before.Level != Sectors.Guard.Level || Before.Requested != Sectors.Guard.Requested)
	{
		const bool bTurn = Sectors.Guard.Level == EFormationGuardLevel::Trail && Turn > Config.TurnFoldDeg
			&& (Sectors.Requested == EFormationKind::EchelonLeft || Sectors.Requested == EFormationKind::EchelonRight || Sectors.Requested == EFormationKind::Line);
		// Which places a wall cuts (here / at the pose ahead) - what made the guard fall back.
		const auto Cut = [&](const FVector2D& Pos, float Yaw)
		{
			FString Roles;
			for (const EFormationRole Role : FormationGuardRules::BlockedRoles(Sectors.WorldWalls, Sectors.Requested, Count, Pos, Yaw, Config))
			{
				Roles += FormationNames::ToString(Role);
			}
			return Roles.IsEmpty() ? FString(TEXT("-")) : Roles;
		};
		UE_LOG(LogCodexTactics, Display, TEXT("[Formation] guard: %s (level %d%s, cut here %s / ahead %s, turn ahead %.0f deg, heading %.0f, %d walls) at (%.0f, %.0f)"),
			*FormationGuardRuntime::GuardHudText(Sectors.Requested, Sectors.Formation, Sectors.Guard.Level, Count, Sectors.bGuardTurnFold),
			static_cast<int32>(Sectors.Guard.Level),
			bTurn ? TEXT(", the sharp-turn fold") : TEXT(""), *Cut(LeaderPos, Sectors.HeadingYaw),
			Ahead.IsSet() ? *Cut(Ahead->Pos, Ahead->Yaw) : TEXT("(standing)"), Turn, Sectors.HeadingYaw, Sectors.WorldWalls.Num(), LeaderPos.X, LeaderPos.Y);
	}
}

void USquadSubsystem::BuildTrailFrameSlots(const FVector2D& LeaderPos, int32 Count)
{
	// Each man's sectors by his own room (the corridor's held width, else a wall beside his slot -> narrow), relative to the trail's
	// direction at his slot; the rear man keeps the squad's 6 o'clock (sim). Rules v2.3 (contract 13.2): the world's walls go in - the
	// stagger aside shrinks in a narrow passage so no slot lands in a wall's clearance (an unwalkable slot -> a stand-in round the leader).
	Sectors.FrameSlots = FormationGuardRuntime::TrailFrameSlots(FormationGuardRuntime::TrailWithLeader(Sectors.Trail, LeaderPos), Sectors.HeadingYaw, Count,
		Sectors.WorldWalls, Sectors.Mode, Sectors.PatrolTime, Sectors.PassageWidth, FormationTuning::Get());
}

void USquadSubsystem::ApplyFormationCornerWatch(const FVector2D& LeaderPos)
{
	Sectors.CornerLooks.Reset();
	// The watch runs on the move and on a slide; the standing wall layout forgets it.
	if (Sectors.Formation == EFormationKind::Wall && !Sectors.bSlide)
	{
		Sectors.Corner = FFormationCornerState();
		return;
	}
	TArray<FFormationSlot> Places;
	FFormationSlot Lead;
	Lead.Role = EFormationRole::Commander;
	Lead.Position = LeaderPos;
	Places.Add(Lead);
	for (const FFollowerState& Follower : Followers)
	{
		const AOperativeCharacter* Operative = Follower.Operative.Get();
		const TOptional<EFormationRole> Role = SectorRoleOf(Follower);
		if (Operative && Role.IsSet() && FindFrameSlot(Role.GetValue()))
		{
			FFormationSlot Place;
			Place.Role = Role.GetValue();
			Place.Position = FVector2D(Operative->GetActorLocation());
			Places.Add(Place);
		}
	}
	const FFormationCornerState Before = Sectors.Corner;
	const FFormationCornerResult Watch = FormationCornerRules::Watch(Sectors.WorldWalls, LeaderPos, Sectors.HeadingYaw, Places, Sectors.Corner,
		FormationTuning::Get());
	Sectors.Corner = Watch.State;
	const auto Describe = [](const TOptional<FFormationCornerWatch>& Side) { return Side.IsSet() ? FormationNames::ToString(Side->Role) : TEXT("-"); };
	if (Before.Left.IsSet() != Watch.State.Left.IsSet() || Before.Right.IsSet() != Watch.State.Right.IsSet())
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Formation] corner watch: left %s, right %s at (%.0f, %.0f)"), Describe(Watch.State.Left), Describe(Watch.State.Right),
			LeaderPos.X, LeaderPos.Y);
	}
	if (Sectors.Mode != EFormationMode::Walk)
	{
		return; // patrol only (not run, "all forward", halt)
	}
	for (const FFormationCornerLook& Look : Watch.Looks)
	{
		if (FFormationSlot* Slot = Sectors.FrameSlots.FindByPredicate([&Look](const FFormationSlot& Each) { return Each.Role == Look.Role; }))
		{
			Slot->Yaw = Look.Yaw;
			Sectors.CornerLooks.Add(Look);
		}
	}
}

float USquadSubsystem::ClearFormationLook(const AOperativeCharacter& Member, EFormationRole Role, bool bLeader, float Look)
{
	const TWeakObjectPtr<AOperativeCharacter> Who(const_cast<AOperativeCharacter*>(&Member));
	if (!FormationGuardRuntime::IsEnabled())
	{
		FormationGuardRuntime::RememberClearLook(ClearHeld, Who, TOptional<float>());
		return Look;
	}
	const bool bWatcher = Sectors.CornerLooks.ContainsByPredicate([Role](const FFormationCornerLook& Each) { return Each.Role == Role; });
	TOptional<float> Travel;
	if (bLeader && Member.GetVelocity().Size2D() > 1.0)
	{
		Travel = FormationMath::YawOf(FVector2D(Member.GetVelocity()));
	}
	if (!FormationGuardRuntime::ShouldClearLook(Sectors.Formation, Sectors.Mode, bWatcher, bLeader, Travel, Look))
	{
		// Rules v2.3 (contract 13.1): ClearLook does not apply - his held look is reset (the wall layout, a run, the corner watcher, the
		// leader looking where he walks).
		FormationGuardRuntime::RememberClearLook(ClearHeld, Who, TOptional<float>());
		return Look;
	}
	// The world walls only (the v2.1 rules' one view of the level): a wall the squad looks over (below FormationRuntime::ProbeHeightCm +
	// LookedOverMarginCm) never turns a look away - standing, they see across it. Rules v2.3: his last frame's result is the held look.
	const TOptional<float> Held = FormationGuardRuntime::HeldClearLook(ClearHeld, Who);
	const float Cleared = FormationSectorRules::ClearLook(Sectors.WorldWalls, FVector2D(Member.GetActorLocation()), Look,
		static_cast<float>(Member.GetActorRotation().Yaw), FormationTuning::Get(), Held);
	FormationGuardRuntime::RememberClearLook(ClearHeld, Who, TOptional<float>(Cleared));
	static const IConsoleVariable* DebugVar = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Formation.Debug"));
	if (DebugVar && DebugVar->GetInt() >= 3 && FMath::Abs(FMath::FindDeltaAngleDegrees(Look, Cleared)) > 1.f)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Formation] look %s %s: %.0f -> ClearLook %.0f (facing %.0f, held %s, %s %s, %d walls)"), *Member.DisplayName.ToString(),
			FormationNames::ToString(Role), Look, Cleared, Member.GetActorRotation().Yaw, Held.IsSet() ? *FString::Printf(TEXT("%.0f"), Held.GetValue()) : TEXT("-"),
			FormationNames::ToString(Sectors.Formation), FormationNames::ToString(Sectors.Mode), Sectors.WorldWalls.Num());
	}
	return Cleared;
}

FString USquadSubsystem::GetFormationHudText() const
{
	if (!Sectors.bActiveThisFrame)
	{
		return FString();
	}
	const int32 Count = FMath::Min(Sectors.MemberCount, 4);
	if (Sectors.Formation == EFormationKind::Wall || !FormationGuardRuntime::IsEnabled())
	{
		return FormationGuardRuntime::FormationLabel(Sectors.Formation, Count);
	}
	FString Text = FormationGuardRuntime::GuardHudText(Sectors.Requested, Sectors.Formation, Sectors.Guard.Level, Count, Sectors.bGuardTurnFold);
	if (Sectors.bAllForward)
	{
		Text += TEXT(", all forward");
	}
	return Text;
}

void USquadSubsystem::SetFormationMenuChoice(const TOptional<EFormationMenuItem>& Choice)
{
	Sectors.MenuChoice = Choice;
	UE_LOG(LogCodexTactics, Display, TEXT("[Formation] menu choice: %s"), Choice.IsSet() ? FormationMenuRules::ToString(Choice.GetValue()) : TEXT("auto"));
}
