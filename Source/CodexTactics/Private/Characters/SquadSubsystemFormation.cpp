// USquadSubsystem: formation look sectors - the runtime integration of the pure formation rules (stage 6 of
// docs/design/FORMATION_SECTORS.md; FORMATION_CONTRACT.md 7). Behind Codex.Formation.Sectors (default 1; 0 = the old triangle formation).
// Godot reference: none (UE-only); the reference simulation is Tools/TrainingGround/src/movement/sim.ts (simulate / stepTowards / wallAhead).
// Per tick (exploration formation, group mode): the leader's smoothed travel heading, a trace probe round him (FormationRuntime::ProbeRays)
// -> FFormationTerrainTracker (Classify with hysteresis) -> FormationRuntime::ChooseFormation (the wall layout only on arrival) -> the slots
// (FormationShapeRules::Slots / FormationWallRules::Layout) and look yaws (FormationSectorRules::LookYaw) by role; the followers walk to their
// slots with the slot's velocity lead (FormationRuntime::FollowerMove), each member's body turns to his look (AOperativeCharacter facing
// priority), the leader walks at the patrol pace. A group cover order in group mode runs as the wall layout (OrderFormationWall).

#include "Characters/SquadSubsystem.h"

#include "Characters/OperativeCharacter.h"
#include "Characters/OperativeMovementRules.h"
#include "CodexTactics.h"
#include "CollisionQueryParams.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "Formation/FormationRuntimeRules.h"
#include "Formation/FormationSectorRules.h"
#include "Formation/FormationShapeRules.h"
#include "Formation/FormationTerrainRules.h"
#include "Formation/FormationTuning.h"
#include "Formation/FormationWallRules.h"
#include "Environment/EnvironmentSubsystem.h"
#include "HAL/IConsoleManager.h"

namespace SquadSubsystemFormationLocal
{
	TAutoConsoleVariable<int32> CVarFormationDebug(TEXT("Codex.Formation.Debug"), 0,
		TEXT("1: log the formation probe (each raw terrain class change with its probe hits)"));
	/** The last raw class logged (Codex.Formation.Debug). */
	TOptional<EFormationTerrainClass> LastLoggedRawClass;

	/** The probe runs this often (the hysteresis steps every frame on its walls), s. */
	constexpr float ProbeInterval = 0.1f;
	/** The leader stands this long before a wall ahead becomes the wall layout (a stop between two clicks is no arrival), s. */
	constexpr float WallArrivalStillSeconds = 0.25f;
	/** A member of a wall order counts as at his slot within this distance, cm. */
	constexpr float WallSettleCm = 60.f;
	/** ... and turned to his layout yaw within this angle, deg. */
	constexpr float WallSettleYawDeg = 30.f;
	/** The leader of a wall order is at his slot within this distance (his own move stops AcceptanceRadius short), cm. */
	constexpr float WallLeaderSettleCm = 120.f;
	/** A parked follower restarts only once his (still) slot is this much farther than the stop radius (no idle / walk trembling), cm. */
	constexpr float SectorParkedRestartMargin = 40.f;
	/** Speed below which a member counts as standing, cm/s. */
	constexpr float StandingSpeed = 15.f;
	/** The wall probes of a wall order trace this high above the feet (a low cover object counts there), cm. */
	constexpr float WallOrderProbeHeightCm = 50.f;
	/** The room probes sample this far out from the wall face, cm. */
	constexpr float RoomProbeOutCm = 40.f;

	FCollisionObjectQueryParams WallObjects()
	{
		FCollisionObjectQueryParams Objects;
		Objects.AddObjectTypesToQuery(ECC_WorldStatic);
		Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
		return Objects;
	}

	float FormationFeetZ(const AActor& Actor)
	{
		return static_cast<float>(Actor.GetActorLocation().Z) - Actor.GetSimpleCollisionHalfHeight();
	}
}

void USquadSubsystem::ApplyFormationFrame()
{
	for (const TWeakObjectPtr<AOperativeCharacter>& Member : Members)
	{
		AOperativeCharacter* Operative = Member.Get();
		if (!Operative)
		{
			continue;
		}
		const float* Yaw = FrameLooks.Find(Member);
		Operative->SetFormationLookYaw(Yaw ? TOptional<float>(*Yaw) : TOptional<float>());
		Operative->SetFormationPaceMultiplier(FramePaceLeader.Get() == Operative ? FramePace : 1.f);
	}
}

const FFormationSlot* USquadSubsystem::FindFrameSlot(EFormationRole Role) const
{
	return Sectors.FrameSlots.FindByPredicate([Role](const FFormationSlot& Slot) { return Slot.Role == Role; });
}

TOptional<EFormationRole> USquadSubsystem::SectorRoleOf(const FFollowerState& Follower) const
{
	if (Sectors.bWallOrder)
	{
		const EFormationRole* Role = Sectors.WallOrderRoles.Find(Follower.Operative);
		return Role ? TOptional<EFormationRole>(*Role) : TOptional<EFormationRole>();
	}
	return FormationRuntime::RoleForFollowerSlot(Follower.Slot, Sectors.MemberCount);
}

bool USquadSubsystem::GetFormationRole(const AOperativeCharacter* Operative, EFormationRole& OutRole) const
{
	if (!Operative || !Sectors.bActiveThisFrame)
	{
		return false;
	}
	if (Operative == Leader.Get())
	{
		OutRole = EFormationRole::Commander;
		return true;
	}
	for (const FFollowerState& Follower : Followers)
	{
		if (Follower.Operative.Get() == Operative)
		{
			const TOptional<EFormationRole> Role = SectorRoleOf(Follower);
			if (Role.IsSet())
			{
				OutRole = Role.GetValue();
				return true;
			}
			return false;
		}
	}
	return false;
}

bool USquadSubsystem::GetFormationTarget(const AOperativeCharacter* Operative, FVector& OutSlot, float& OutLookYaw) const
{
	EFormationRole Role;
	if (!GetFormationRole(Operative, Role))
	{
		return false;
	}
	const FFormationSlot* Slot = FindFrameSlot(Role);
	if (!Slot)
	{
		return false;
	}
	OutSlot = FVector(Slot->Position.X, Slot->Position.Y, SquadSubsystemFormationLocal::FormationFeetZ(*Operative));
	OutLookYaw = Slot->Yaw;
	return true;
}

bool USquadSubsystem::GetFormationWallNormalYaw(float& OutYaw) const
{
	if (!Sectors.bWallLayoutValid)
	{
		return false;
	}
	OutYaw = Sectors.WallNormalYaw;
	return true;
}

TOptional<FVector2D> USquadSubsystem::LeaderDestination(const AOperativeCharacter& LeaderRef)
{
	// v1.2: at_wall only on arrival - his move's destination (also after he arrived: he stands on it; in the wall order his own wall slot).
	// Before his first move order there is none.
	const FVector Destination = LeaderRef.GetLastMoveDestination();
	return Destination.IsZero() ? TOptional<FVector2D>() : TOptional<FVector2D>(FVector2D(Destination));
}

bool USquadSubsystem::IsFormationBlizzard() const
{
	// v1.2 (user Q4 = A): open ground in a blizzard -> the blizzard tight diamond.
	const UEnvironmentSubsystem* Environment = GetWorld() ? GetWorld()->GetSubsystem<UEnvironmentSubsystem>() : nullptr;
	return Environment && Environment->GetWeather() == EWeatherState::Blizzard;
}

EFormationTerrainClass USquadSubsystem::StepFormationTerrain(const AOperativeCharacter& LeaderRef)
{
	// The one adapter over FFormationTerrainTracker (rules v2: the hysteresis counts the leader's PATH): the accepted probe's walls, read from
	// where they were traced, the leader's destination and the path he walked since the last tick (0 standing; a teleport is not a walk).
	const FVector2D Here(LeaderRef.GetActorLocation());
	float StepDistance = Sectors.bLastLeaderPosValid ? static_cast<float>(FVector2D::Distance(Here, Sectors.LastLeaderPos)) : 0.f;
	if (StepDistance > FormationRuntime::MaxStepDistanceCm)
	{
		StepDistance = 0.f;
	}
	Sectors.LastLeaderPos = Here;
	Sectors.bLastLeaderPosValid = true;
	return Sectors.Terrain.Step(Sectors.ProbeWalls, Sectors.ProbeOrigin, Sectors.ProbeHeadingYaw, LeaderDestination(LeaderRef), StepDistance,
		FormationTuning::Get());
}

void USquadSubsystem::ProbeFormationWalls(const AOperativeCharacter& LeaderRef)
{
	using namespace SquadSubsystemFormationLocal;
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(FormationProbe), false);
	for (const TWeakObjectPtr<AOperativeCharacter>& Member : Members)
	{
		Params.AddIgnoredActor(Member.Get());
	}
	FVector Origin = LeaderRef.GetActorLocation();
	Origin.Z = FormationFeetZ(LeaderRef) + FormationRuntime::ProbeHeightCm;
	TArray<FFormationWall> Walls;
	for (const FormationRuntime::FProbeRay& Ray : FormationRuntime::ProbeRays(Sectors.HeadingYaw, FormationTuning::Get()))
	{
		const FVector Direction = FRotator(0.f, Ray.Yaw, 0.f).Vector();
		FHitResult Hit;
		if (World->LineTraceSingleByObjectType(Hit, Origin, Origin + Direction * Ray.Length, WallObjects(), Params)
			&& FMath::Abs(Hit.ImpactNormal.Z) < 0.7f)
		{
			Walls.Add(FormationRuntime::WallFromHit(FVector2D(Hit.ImpactPoint), FVector2D(Hit.ImpactNormal)));
		}
	}
	// One probe alone does not change the class (a ray grazing a wall's end cap reads it as a wall across the heading): a probe whose raw
	// class differs from the class in force is taken only when the probe before said the same (FormationRuntime::AcceptProbe).
	const EFormationTerrainClass Raw = FormationTerrainRules::ClassifyRaw(Walls, FVector2D(Origin), Sectors.HeadingYaw, FormationTuning::Get(),
		LeaderDestination(LeaderRef));
	const bool bAccept = FormationRuntime::AcceptProbe(Raw, Sectors.LastProbeRaw, Sectors.Terrain.State.Current);
	Sectors.LastProbeRaw = Raw;
	if (bAccept)
	{
		// The rules read the hits from where they were traced (the thin hit segments do not survive the leader's walk to the next probe).
		Sectors.ProbeWalls = MoveTemp(Walls);
		Sectors.ProbeOrigin = FVector2D(Origin);
		Sectors.ProbeHeadingYaw = Sectors.HeadingYaw;
	}
}

bool USquadSubsystem::BuildFormationWall(const FVector& Origin, const FVector2D& TowardsWall, float MaxDistance, FVector2D& OutFace, float& OutNormalYaw,
	float& OutRoomLeft, float& OutRoomRight) const
{
	using namespace SquadSubsystemFormationLocal;
	UWorld* World = GetWorld();
	const FVector2D Towards = TowardsWall.GetSafeNormal();
	if (!World || Towards.IsNearlyZero())
	{
		return false;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(FormationWall), false);
	for (const TWeakObjectPtr<AOperativeCharacter>& Member : Members)
	{
		Params.AddIgnoredActor(Member.Get());
	}
	const FCollisionObjectQueryParams Objects = WallObjects();
	FHitResult Hit;
	if (!World->LineTraceSingleByObjectType(Hit, Origin, Origin + FVector(Towards, 0.f) * MaxDistance, Objects, Params)
		|| FMath::Abs(Hit.ImpactNormal.Z) >= 0.7f)
	{
		return false;
	}
	const FVector2D Normal = FVector2D(Hit.ImpactNormal).GetSafeNormal();
	if (Normal.IsNearlyZero())
	{
		return false;
	}
	OutFace = FVector2D(Hit.ImpactPoint);
	OutNormalYaw = FormationMath::YawOf(Normal);
	// The room each side: samples RoomStepCm apart RoomProbeOutCm off the face, each traced back onto the face (the wall goes on there),
	// up to an obstacle along the wall (a corner) - FormationWallRules: right = the right hand of a man FACING the wall.
	const FVector2D RightAlong = FormationMath::Forward(OutNormalYaw + 270.f);
	const int32 Samples = FMath::FloorToInt32(FormationRuntime::RoomMaxCm / FormationRuntime::RoomStepCm);
	const FVector Out = FVector(OutFace + Normal * RoomProbeOutCm, Hit.ImpactPoint.Z);
	for (int32 SideIndex = 0; SideIndex < 2; ++SideIndex)
	{
		const FVector2D Along = SideIndex == 0 ? -RightAlong : RightAlong;
		float Blocked = FormationRuntime::RoomMaxCm + 1.f;
		FHitResult AlongHit;
		if (World->LineTraceSingleByObjectType(AlongHit, Out, Out + FVector(Along, 0.f) * FormationRuntime::RoomMaxCm, Objects, Params))
		{
			Blocked = AlongHit.Distance;
		}
		TArray<bool> Continues;
		for (int32 Step = 1; Step <= Samples; ++Step)
		{
			const float At = static_cast<float>(Step) * FormationRuntime::RoomStepCm;
			if (At >= Blocked)
			{
				Continues.Add(false);
				break;
			}
			const FVector Start = Out + FVector(Along, 0.f) * At;
			FHitResult Back;
			const bool bWall = World->LineTraceSingleByObjectType(Back, Start, Start - FVector(Normal, 0.f) * (RoomProbeOutCm * 2.f), Objects, Params)
				&& FVector2D::DotProduct(FVector2D(Back.ImpactNormal).GetSafeNormal(), Normal) > 0.7f;
			Continues.Add(bWall);
		}
		(SideIndex == 0 ? OutRoomLeft : OutRoomRight) = FormationRuntime::MeasureRoom(Continues);
	}
	return true;
}

bool USquadSubsystem::ComputeWallLayout(const FVector& Origin, const FVector2D& TowardsWall, float MaxDistance, const FVector2D& ClickedSlot, int32 MemberCount,
	const AOperativeCharacter* FourthMan)
{
	FVector2D Face;
	float NormalYaw = 0.f;
	float RoomLeft = 0.f;
	float RoomRight = 0.f;
	if (!BuildFormationWall(Origin, TowardsWall, MaxDistance, Face, NormalYaw, RoomLeft, RoomRight))
	{
		return false;
	}
	const FFormationConfig& Config = FormationTuning::Get();
	const FFormationWall Wall = FormationRuntime::WallAlong(Face, NormalYaw, RoomLeft, RoomRight);
	const int32 Count = FMath::Clamp(MemberCount, 1, 4);
	if (Count >= 4 && FourthMan)
	{
		// Design: a tie goes to the side the 4th reaches with less travel.
		const FVector2D Anchor = Face + FormationMath::Forward(NormalYaw) * Config.WallFaceOffset;
		const FVector2D RightAlong = FormationMath::Forward(NormalYaw + 270.f);
		const FVector2D Fourth(FourthMan->GetActorLocation());
		FormationWallRules::BreakRoomTie(RoomLeft, RoomRight, FVector2D::Distance(Fourth, Anchor - RightAlong * (2.f * Config.WallSpacing)),
			FVector2D::Distance(Fourth, Anchor + RightAlong * (2.f * Config.WallSpacing)));
	}
	Sectors.WallSlots = FormationWallRules::Layout(Wall, ClickedSlot, Count, RoomLeft, RoomRight, Config);
	Sectors.WallNormalYaw = FormationWallRules::OutwardYaw(Wall, ClickedSlot);
	UE_LOG(LogCodexTactics, Display, TEXT("[Formation] wall layout: %d members, outward yaw %.0f, room left %.0f / right %.0f cm"), Count,
		Sectors.WallNormalYaw, RoomLeft, RoomRight);
	return Sectors.WallSlots.Num() > 0;
}

void USquadSubsystem::UpdateFormationSectors(AOperativeCharacter& LeaderRef, bool bLeaderMoving, float DeltaTime)
{
	using namespace SquadSubsystemFormationLocal;
	// Solo: the followers hold (UpdateFollower returns at once) - no formation, no sectors; the leader walks at his own speed.
	if (bIsSoloMode || bFollowersHolding || Followers.IsEmpty())
	{
		if (Sectors.bWallOrder)
		{
			EndFormationWallOrder(bIsSoloMode ? TEXT("Solo") : TEXT("no followers"));
		}
		return;
	}
	const FFormationConfig& Config = FormationTuning::Get();
	Sectors.bActiveThisFrame = true;
	Sectors.MemberCount = 1 + Followers.Num();
	const int32 Count = FMath::Min(Sectors.MemberCount, 4);
	Sectors.HeadingYaw = static_cast<float>(FormationHeading.Rotation().Yaw);
	Sectors.bHeadingValid = true;
	const FVector2D LeaderPos(LeaderRef.GetActorLocation());

	// Terrain: the probe every ProbeInterval, the hysteresis every frame.
	Sectors.ProbeTimeRemaining -= DeltaTime;
	if (Sectors.ProbeTimeRemaining <= 0.f)
	{
		Sectors.ProbeTimeRemaining = ProbeInterval;
		ProbeFormationWalls(LeaderRef);
		if (CVarFormationDebug.GetValueOnGameThread() != 0)
		{
			const EFormationTerrainClass Raw = FormationTerrainRules::ClassifyRaw(Sectors.ProbeWalls, Sectors.ProbeOrigin, Sectors.ProbeHeadingYaw, Config,
				LeaderDestination(LeaderRef));
			if (!LastLoggedRawClass.IsSet() || LastLoggedRawClass.GetValue() != Raw)
			{
				LastLoggedRawClass = Raw;
				FString Hits;
				for (const FFormationWall& Wall : Sectors.ProbeWalls)
				{
					const FVector2D Mid = (Wall.A + Wall.B) * 0.5;
					Hits += FString::Printf(TEXT(" [%.0f deg %.0f cm]"), FMath::FindDeltaAngleDegrees(Sectors.HeadingYaw, FormationMath::YawOf(Mid - LeaderPos)),
						FVector2D::Distance(Mid, LeaderPos));
				}
				UE_LOG(LogCodexTactics, Display, TEXT("[Formation] probe at (%.0f, %.0f) heading %.0f: raw %s,%s"), LeaderPos.X, LeaderPos.Y, Sectors.HeadingYaw,
					FormationNames::ToString(Raw), *Hits);
			}
		}
	}
	const EFormationTerrainClass Class = StepFormationTerrain(LeaderRef);
	Sectors.PassageWidth = Sectors.Terrain.GetPassageWidth(); // v2: held with the corridor class (the column's tail keeps the narrow sectors)

	if (Sectors.bWallOrder)
	{
		UpdateFormationWallOrder(LeaderRef, DeltaTime);
	}

	// The formation: the wall order's wall, else by terrain with the wall layout only on arrival (the leader stood a moment).
	const bool bLeaderStill = !bLeaderMoving && LeaderRef.GetVelocity().Size2D() < StandingSpeed;
	Sectors.LeaderStillSeconds = bLeaderStill ? Sectors.LeaderStillSeconds + DeltaTime : 0.f;
	if (Sectors.bWallOrder)
	{
		Sectors.Formation = EFormationKind::Wall;
	}
	else
	{
		const EFormationKind Previous = Sectors.Formation;
		// v1.2: at_wall fires only on arrival (within classify.arrive_dist of his destination) - that, or a short stand, is the arrival.
		const bool bArrived = FormationTerrainRules::IsArriving(LeaderPos, LeaderDestination(LeaderRef), Config)
			|| Sectors.LeaderStillSeconds >= WallArrivalStillSeconds;
		Sectors.Formation = FormationRuntime::ChooseFormation(Class, Count, bArrived, Sectors.MoveFormation, IsFormationBlizzard());
		if (bLeaderMoving)
		{
			Sectors.MoveFormation = Sectors.Formation;
		}
		if (Sectors.Formation == EFormationKind::Wall)
		{
			if (!Sectors.bWallLayoutValid || FVector2D::Distance(LeaderPos, Sectors.WallAnchor) > FormationRuntime::WallLayoutRecomputeCm)
			{
				const FFollowerState* Fourth = Followers.FindByPredicate([](const FFollowerState& F) { return F.Slot == 2; });
				FVector Origin = LeaderRef.GetActorLocation();
				Origin.Z = FormationFeetZ(LeaderRef) + FormationRuntime::ProbeHeightCm;
				Sectors.bWallLayoutValid = ComputeWallLayout(Origin, FormationMath::Forward(Sectors.HeadingYaw), FormationRuntime::WallAheadCm, LeaderPos, Count,
					Fourth ? Fourth->Operative.Get() : nullptr);
				Sectors.WallAnchor = LeaderPos;
			}
			if (!Sectors.bWallLayoutValid)
			{
				Sectors.Formation = FormationTerrainRules::FormationFor(EFormationTerrainClass::Open, Count, IsFormationBlizzard());
			}
		}
		else
		{
			Sectors.bWallLayoutValid = false;
		}
		if (Sectors.Formation != Previous)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("[Formation] %s -> %s (terrain %s, passage %s)"), FormationNames::ToString(Previous),
				FormationNames::ToString(Sectors.Formation), FormationNames::ToString(Class),
				Sectors.PassageWidth.IsSet() ? *FString::Printf(TEXT("%.0f cm"), Sectors.PassageWidth.GetValue()) : TEXT("-"));
		}
	}
	const bool bWall = Sectors.Formation == EFormationKind::Wall;
	Sectors.Mode = FormationRuntime::ResolveMode(bLeaderMoving, LeaderRef.IsSprinting(), bWall);
	if (Sectors.Mode == EFormationMode::Walk)
	{
		Sectors.PatrolTime += DeltaTime;
	}

	// This frame's slots and looks by role.
	if (bWall)
	{
		Sectors.FrameSlots = Sectors.WallSlots;
	}
	else
	{
		Sectors.FrameSlots = FormationShapeRules::Slots(Sectors.Formation, Count, LeaderPos, Sectors.HeadingYaw, Config);
		for (FFormationSlot& Slot : Sectors.FrameSlots)
		{
			Slot.Yaw = FormationSectorRules::LookYaw(Slot.Role, Sectors.Formation, Sectors.Mode, Count, Sectors.PatrolTime, Sectors.HeadingYaw,
				Sectors.PassageWidth, Config);
		}
	}

	// The leader (not with a box selection: each faces his own path): his sector, and the patrol pace while the squad walks.
	if (!HasMultiSelection())
	{
		const FFormationSlot* Lead = FindFrameSlot(EFormationRole::Commander);
		if (Lead && Sectors.Mode != EFormationMode::Run)
		{
			const bool bWalkingIn = bWall && Sectors.bWallOrder
				&& !FormationRuntime::UseWallLayoutYaw(static_cast<float>(FVector2D::Distance(LeaderPos, Lead->Position)), !bLeaderStill);
			if (!bWalkingIn)
			{
				FrameLooks.Add(&LeaderRef, Lead->Yaw);
			}
		}
		if (Sectors.Mode == EFormationMode::Walk)
		{
			FramePaceLeader = &LeaderRef;
			FramePace = Config.PacePatrol;
		}
	}
}

bool USquadSubsystem::UpdateSectorFollower(FFollowerState& Follower, AOperativeCharacter& Operative, AOperativeCharacter& LeaderRef, bool bLeaderMoving,
	float DeltaTime, float TimeSeconds)
{
	using namespace SquadSubsystemFormationLocal;
	const TOptional<EFormationRole> Role = SectorRoleOf(Follower);
	const FFormationSlot* Slot = Role.IsSet() ? FindFrameSlot(Role.GetValue()) : nullptr;
	if (!Slot)
	{
		return false; // a 5th member and beyond: the old slot math
	}
	const FVector2D Raw = Slot->Position;
	FVector SlotLocation(Raw.X, Raw.Y, Operative.GetActorLocation().Z);
	SlotLocation = ResolveReachableSlot(LeaderRef, SlotLocation);
	const FVector2D Here(Operative.GetActorLocation());
	const FVector2D Target(SlotLocation);
	const float Distance = static_cast<float>(FVector2D::Distance(Here, Target));
	const bool bMoving = Operative.GetVelocity().Size2D() > StandingSpeed;
	const bool bWall = Sectors.Formation == EFormationKind::Wall;

	// His look: run = his path (everybody faces 12); the wall layout: his path until 1 m from his slot (contract 7); else the sector.
	if (Sectors.Mode != EFormationMode::Run && (!bWall || FormationRuntime::UseWallLayoutYaw(Distance, bMoving)))
	{
		FrameLooks.Add(Follower.Operative, Slot->Yaw);
	}
	if (Sectors.bWallOrder && Distance <= WallSettleCm && !bMoving
		&& FormationMath::AngleBetween(static_cast<float>(Operative.GetActorRotation().Yaw), Slot->Yaw) <= WallSettleYawDeg)
	{
		SettleFormationWallPose(Operative);
	}
	else if (!Sectors.bWallOrder && Operative.IsInFormationWallPose())
	{
		// The wall order is over (the leader walks on): up from the squad's crouch to walk with a standing leader.
		Operative.SetFormationWallPose(false);
		if (Operative.GetStance() == EOperativeStance::Crouching && Operative.IsStanceFromSquad() && LeaderRef.GetStance() == EOperativeStance::Standing)
		{
			Operative.SetStance(EOperativeStance::Standing);
			Operative.MarkStanceFromSquad();
			Follower.RepathTimeRemaining = FMath::Max(Follower.RepathTimeRemaining, Operative.GetMoveHoldSeconds());
		}
	}

	Follower.RepathTimeRemaining -= DeltaTime;
	if (Follower.RepathTimeRemaining > 0.f)
	{
		return true;
	}
	Follower.RepathTimeRemaining = FormationRuntime::FollowRepathSeconds; // the speed follows the moving slot closely (contract 7)
	if (TryWarmPull(Follower, Operative, LeaderRef))
	{
		Follower.RepathTimeRemaining = FormationConfig.RepathInterval; // the warm pull keeps its own (old) cadence
		return true;
	}
	if (HoldsCoverOrLeavesIt(Follower, Operative, LeaderRef, bLeaderMoving, Distance))
	{
		return true;
	}

	// The slot's own velocity since the last repath (contract 7 feed-forward).
	FVector2D SlotVelocity = FVector2D::ZeroVector;
	const double Elapsed = static_cast<double>(TimeSeconds) - Follower.PrevSectorSlotTime;
	if (Follower.PrevSectorSlotTime >= 0.0 && Elapsed > 1e-3 && Elapsed < 1.0)
	{
		SlotVelocity = (Raw - Follower.PrevSectorSlot) / Elapsed;
	}
	Follower.PrevSectorSlot = Raw;
	Follower.PrevSectorSlotTime = TimeSeconds;

	const float Jitter = bLeaderMoving ? 1.f + FMath::Sin(TimeSeconds * 0.9f + static_cast<float>(Follower.Slot + 1) * 1.7f) * FormationConfig.SpeedJitter : 1.f;
	// Running - or crawling (prone: no sector look, he crawls facing his way) - there is no walking backwards to cap.
	const bool bRun = LeaderRef.IsSprinting() || Operative.GetStance() == EOperativeStance::Prone;
	const float Top = Operative.GetMaxSpeed(); // his walk / crawl, or the sprint he copies from the leader
	const FormationRuntime::FFollowerMove Move = FormationRuntime::FollowerMove(Here, static_cast<float>(Operative.GetActorRotation().Yaw), Target,
		SlotVelocity, Jitter, Top, Top, bRun, FormationTuning::Get());
	const bool bSlotStill = SlotVelocity.Size() < FormationRuntime::SlotStillSpeed;
	const bool bStayParked = Follower.bParked && bSlotStill && Distance < FormationRuntime::StopRadiusCm + SectorParkedRestartMargin;
	if (Move.bStop || bStayParked || Move.Speed <= 1.f)
	{
		if (!Follower.bParked)
		{
			Operative.StopOperative();
			Follower.bParked = true;
		}
		return true;
	}
	Follower.bParked = false;
	const FVector2D Destination = FormationRuntime::LeadDestination(Target, SlotVelocity);
	const EOperativeOrderResult Result = Operative.FollowTo(FVector(Destination.X, Destination.Y, SlotLocation.Z), Move.Speed);
	if (CVarFormationDebug.GetValueOnGameThread() >= 2)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Formation] %s %s: %.0f cm off his slot, slot %.0f cm/s, speed %.0f%s -> %d"), *Operative.DisplayName.ToString(),
			FormationNames::ToString(Role.GetValue()), Distance, SlotVelocity.Size(), Move.Speed, Move.bBackward ? TEXT(" (backwards)") : TEXT(""),
			static_cast<int32>(Result));
	}
	return true;
}

void USquadSubsystem::SettleFormationWallPose(AOperativeCharacter& Member)
{
	if (Member.IsInFormationWallPose())
	{
		return;
	}
	if (Member.GetStance() == EOperativeStance::Standing)
	{
		Member.SetStance(EOperativeStance::Crouching);
		Member.MarkStanceFromSquad(); // the squad's crouch: his next order / the walk-on stands him up
	}
	Member.SetFormationWallPose(true);
	UE_LOG(LogCodexTactics, Display, TEXT("[Formation] %s: at the wall, crouched facing out (yaw %.0f)"), *Member.DisplayName.ToString(),
		Member.GetActorRotation().Yaw);
}

void USquadSubsystem::UpdateFormationWallOrder(AOperativeCharacter& LeaderRef, float DeltaTime)
{
	using namespace SquadSubsystemFormationLocal;
	AOperativeCharacter* OrderLeader = Sectors.WallOrderLeader.Get();
	if (!OrderLeader || OrderLeader != &LeaderRef)
	{
		EndFormationWallOrder(TEXT("the leader changed"));
		return;
	}
	if (OrderLeader->GetMoveOrderSerial() != Sectors.WallOrderSerial)
	{
		EndFormationWallOrder(TEXT("the leader's next order"));
		return;
	}
	Sectors.WallOrderSeconds += DeltaTime;
	const FFormationSlot* Lead = Sectors.WallSlots.FindByPredicate([](const FFormationSlot& Slot) { return Slot.Role == EFormationRole::Commander; });
	if (Lead && !OrderLeader->IsMoving() && FVector2D::Distance(FVector2D(OrderLeader->GetActorLocation()), Lead->Position) <= WallLeaderSettleCm
		&& FormationMath::AngleBetween(static_cast<float>(OrderLeader->GetActorRotation().Yaw), Lead->Yaw) <= WallSettleYawDeg)
	{
		SettleFormationWallPose(*OrderLeader);
	}
}

void USquadSubsystem::EndFormationWallOrder(const TCHAR* Why)
{
	if (!Sectors.bWallOrder)
	{
		return;
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[Formation] wall order over after %.1f s: %s"), Sectors.WallOrderSeconds, Why);
	Sectors.bWallOrder = false;
	Sectors.bWallLayoutValid = false;
	Sectors.WallOrderRoles.Reset();
	Sectors.WallOrderLeader.Reset();
	Sectors.WallOrderSeconds = 0.f;
	Sectors.Formation = Sectors.MoveFormation;
}

bool USquadSubsystem::OrderFormationWall(AOperativeCharacter* Direct, const TArray<AOperativeCharacter*>& Others, const FCoverSlot& Slot)
{
	using namespace SquadSubsystemFormationLocal;
	if (!FormationRuntime::IsEnabled() || !Direct || Direct != Leader.Get() || Others.IsEmpty() || Others.Num() > 3 || !Slot.IsValid())
	{
		return false;
	}
	TArray<AOperativeCharacter*> Placed;
	for (AOperativeCharacter* Other : Others)
	{
		if (!Other || !Followers.ContainsByPredicate([Other](const FFollowerState& F) { return F.Operative.Get() == Other; }))
		{
			return false; // only formation followers walk into the wall layout (the formation drive takes them there)
		}
		Placed.Add(Other);
	}
	const int32 Count = 1 + Placed.Num();
	// The wall behind the clicked slot (low cover objects count: the probe runs low here) and its room each side.
	FVector Origin = Slot.WorldLocation;
	Origin.Z = FormationFeetZ(*Direct) + WallOrderProbeHeightCm;
	const FVector2D Clicked(Slot.WorldLocation);
	const FFollowerState* Fourth = Count >= 4 ? Followers.FindByPredicate([](const FFollowerState& F) { return F.Slot == 2; }) : nullptr;
	if (!ComputeWallLayout(Origin, -FVector2D(Slot.WallNormal), 250.f, Clicked, Count, Fourth ? Fourth->Operative.Get() : nullptr))
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Formation] wall order refused: no wall found behind the cover slot"));
		return false;
	}
	// K = the direct order on the clicked slot; the others take the remaining roles by minimal travel.
	const TArray<EFormationRole> Roles = FormationShapeRules::Roles(Count);
	TArray<FVector2D> From;
	TArray<FVector2D> To;
	TArray<EFormationRole> PlaceRoles;
	for (AOperativeCharacter* Other : Placed)
	{
		From.Add(FVector2D(Other->GetActorLocation()));
	}
	for (const FFormationSlot& Each : Sectors.WallSlots)
	{
		if (Each.Role != EFormationRole::Commander)
		{
			To.Add(Each.Position);
			PlaceRoles.Add(Each.Role);
		}
	}
	const TArray<int32> Assignment = FormationRuntime::AssignByTravel(From, To);
	const FFormationSlot* Lead = Sectors.WallSlots.FindByPredicate([](const FFormationSlot& Each) { return Each.Role == EFormationRole::Commander; });
	if (Assignment.Num() != Placed.Num() || !Lead)
	{
		return false;
	}
	// The leader runs to his slot (his own order); the formation drive takes the others to theirs.
	const EOperativeOrderResult Result = Direct->OrderMoveTo(FVector(Lead->Position.X, Lead->Position.Y, Slot.WorldLocation.Z), true);
	if (Result != EOperativeOrderResult::Accepted)
	{
		Sectors.bWallLayoutValid = false;
		return false;
	}
	Sectors.bWallOrder = true;
	Sectors.bWallLayoutValid = true;
	Sectors.WallAnchor = Clicked;
	Sectors.WallOrderLeader = Direct;
	Sectors.WallOrderSerial = Direct->GetMoveOrderSerial();
	Sectors.WallOrderSeconds = 0.f;
	Sectors.WallOrderRoles.Reset();
	Sectors.Formation = EFormationKind::Wall;
	Direct->SetFormationWallPose(false);
	for (int32 Index = 0; Index < Placed.Num(); ++Index)
	{
		AOperativeCharacter* Other = Placed[Index];
		Sectors.WallOrderRoles.Add(Other, PlaceRoles[Assignment[Index]]);
		Other->SetFormationWallPose(false);
		for (FFollowerState& Follower : Followers)
		{
			if (Follower.Operative.Get() == Other)
			{
				Follower.RepathTimeRemaining = 0.f;
				Follower.bParked = false;
			}
		}
		UE_LOG(LogCodexTactics, Display, TEXT("[Formation] wall order: %s -> %s"), *Other->DisplayName.ToString(),
			FormationNames::ToString(PlaceRoles[Assignment[Index]]));
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[Formation] wall order: %s + %d along the wall (outward yaw %.0f)"), *Direct->DisplayName.ToString(),
		Placed.Num(), Sectors.WallNormalYaw);
	return true;
}

void USquadSubsystem::ResetFormationState(const FVector& Forward)
{
	EndFormationWallOrder(TEXT("reset"));
	FormationHeading = FVector(Forward.X, Forward.Y, 0.f).GetSafeNormal();
	Sectors.Terrain.Reset();
	Sectors.bLastLeaderPosValid = false;
	Sectors.bHeadingValid = !FormationHeading.IsNearlyZero();
	Sectors.HeadingYaw = static_cast<float>(FormationHeading.Rotation().Yaw);
	Sectors.ProbeTimeRemaining = 0.f;
	Sectors.ProbeWalls.Reset();
	Sectors.LastProbeRaw.Reset();
	Sectors.bWallLayoutValid = false;
	Sectors.LeaderStillSeconds = 0.f;
	Sectors.PatrolTime = 0.f;
	Sectors.MoveFormation = FormationTerrainRules::FormationFor(EFormationTerrainClass::Open, FMath::Min(1 + Followers.Num(), 4));
	Sectors.Formation = Sectors.MoveFormation;
	for (FFollowerState& Follower : Followers)
	{
		Follower.PrevSectorSlotTime = -1.0;
		Follower.RepathTimeRemaining = 0.f;
		Follower.bParked = false;
	}
}
