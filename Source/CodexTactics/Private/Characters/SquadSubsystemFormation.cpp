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
#include "Formation/FormationCoverRules.h"
#include "Formation/FormationGuardRuntimeRules.h"
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
		TEXT("1: log the formation probe (each raw terrain class change with its probe hits); 2: also every follower repath; 3: also each member's formation look whenever it changes, and every look ClearLook turns"));
	TAutoConsoleVariable<int32> CVarFormationWallEnd(TEXT("Codex.Formation.WallEnd"), 1,
		TEXT("Rules v2.4 (user bug report 2026-10-26): 1 = at a wall END the wall layout's looks are spread over the open arc round it (FormationWallRules::EndLooks); 0 = the face looks (L 8 / K 6 / P 4) everywhere (v2.3)"));
	TAutoConsoleVariable<int32> CVarFormationApproach(TEXT("Codex.Formation.Approach"), 1,
		TEXT("Rules v2.5 (user rule 2026-10-26): 1 = the covered approach to the wall - the leader runs in and looks along the wall, the man farthest back covers the rear, the others a flank, until everyone is at his wall slot; 0 = everyone walks in facing his travel (v2.4)"));
	/** v2.4: the world walls for EndLooks are gathered this far beyond the probe ring round the layout, cm. */
	constexpr float WallEndGatherMarginCm = 300.f;
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
	/** A new leader order is decided (slide or not) at the latest this long after it, even while a stance clip still defers his move, s. */
	constexpr float LeaderOrderDecideMaxSeconds = 2.f;
	/** Rules v2.2 look probes: rays round each follower's slot (FormationCoverRules::Allocate's walls). */
	constexpr int32 LookProbeRays = 8;

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
	// Codex.Formation.Debug 3: a member's formation look whenever it changes (set / cleared / > 5 deg), with his facing.
	static TMap<TWeakObjectPtr<AOperativeCharacter>, float> LastLoggedLook;
	const bool bDebugLooks = SquadSubsystemFormationLocal::CVarFormationDebug.GetValueOnGameThread() >= 3;
	for (const TWeakObjectPtr<AOperativeCharacter>& Member : Members)
	{
		AOperativeCharacter* Operative = Member.Get();
		if (!Operative)
		{
			continue;
		}
		const float* Yaw = FrameLooks.Find(Member);
		Operative->SetFormationLookYaw(Yaw ? TOptional<float>(*Yaw) : TOptional<float>());
		const float* Logged = LastLoggedLook.Find(Member);
		const float LookNow = Yaw ? *Yaw : 99999.f;
		if (bDebugLooks && (!Logged || FMath::Abs(FMath::FindDeltaAngleDegrees(*Logged, LookNow)) > 5.f || (*Logged > 9999.f) != (LookNow > 9999.f)))
		{
			LastLoggedLook.Add(Member, LookNow);
			UE_LOG(LogCodexTactics, Display, TEXT("[Formation] frame %s: look %s, facing %.0f, active %d, %s %s, corner looks %d"), *Operative->DisplayName.ToString(),
				Yaw ? *FString::Printf(TEXT("%.0f"), *Yaw) : TEXT("-"), Operative->GetActorRotation().Yaw, Sectors.bActiveThisFrame ? 1 : 0,
				FormationNames::ToString(Sectors.Formation), FormationNames::ToString(Sectors.Mode), Sectors.CornerLooks.Num());
		}
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

bool USquadSubsystem::GetFormationGivenSlot(const AOperativeCharacter* Operative, FVector& OutSlot) const
{
	const FVector2D* Given = Operative ? FrameGivenByMember.Find(TWeakObjectPtr<AOperativeCharacter>(const_cast<AOperativeCharacter*>(Operative))) : nullptr;
	if (!Given)
	{
		return false;
	}
	OutSlot = FVector(Given->X, Given->Y, SquadSubsystemFormationLocal::FormationFeetZ(*Operative));
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
	Sectors.LastStepDistance = StepDistance; // rules v2.1: the guard's hysteresis counts the same path
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
	// Rules v2.2: the coverage allocator needs the walls next to every place (its open arc) - rays round the followers' slots.
	Sectors.LookWalls.Reset();
	if (Sectors.MemberCount >= 3)
	{
		TArray<FVector> SlotFeet;
		for (const FFormationSlot& Slot : Sectors.FrameSlots)
		{
			if (Slot.Role != EFormationRole::Commander)
			{
				SlotFeet.Add(FVector(Slot.Position, FormationFeetZ(LeaderRef)));
			}
		}
		Sectors.LookWalls = TraceLookWalls(SlotFeet, FormationRuntime::ProbeHeightCm, LookProbeRays, FormationTuning::Get().LookWallDist);
	}
}

bool USquadSubsystem::BuildFormationWall(const FVector& Origin, const FVector2D& TowardsWall, float MaxDistance, FVector2D& OutFace, float& OutNormalYaw,
	float& OutRoomLeft, float& OutRoomRight, float RoomMaxCm) const
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
	const float RoomMax = FMath::Max(RoomMaxCm, FormationRuntime::RoomStepCm);
	const int32 Samples = FMath::FloorToInt32(RoomMax / FormationRuntime::RoomStepCm);
	const FVector Out = FVector(OutFace + Normal * RoomProbeOutCm, Hit.ImpactPoint.Z);
	for (int32 SideIndex = 0; SideIndex < 2; ++SideIndex)
	{
		const FVector2D Along = SideIndex == 0 ? -RightAlong : RightAlong;
		float Blocked = RoomMax + 1.f;
		FHitResult AlongHit;
		if (World->LineTraceSingleByObjectType(AlongHit, Out, Out + FVector(Along, 0.f) * RoomMax, Objects, Params))
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
	Sectors.WallNormalYaw = FormationWallRules::OutwardYaw(Wall, ClickedSlot);
	// Bug B (game designer 2026-10-26, "the team split completely"): no place on the far side of a wall from K's place (a T / L junction at the
	// clicked slot, the wall's end) - such a place moves to K's other side (FormationWallEnd::FitLayout).
	const AOperativeCharacter* FitLeader = Leader.Get();
	Sectors.WallSlots = FitWallLayoutToWorld(FormationWallRules::Layout(Wall, ClickedSlot, Count, RoomLeft, RoomRight, Config), Sectors.WallNormalYaw,
		FitLeader ? SquadSubsystemFormationLocal::FormationFeetZ(*FitLeader) : static_cast<float>(Origin.Z) - FormationRuntime::ProbeHeightCm);
	Sectors.bCornerStack = false;
	ApplyFormationWallEnd(Origin, TowardsWall, MaxDistance); // v2.4 wall end: the open arc round an end face (after the fit)
	Sectors.Approach.Reset(); // v2.5: a new layout - the covered approach begins again
	Sectors.ApproachLooks.Reset();
	UE_LOG(LogCodexTactics, Display, TEXT("[Formation] wall layout: %d members, outward yaw %.0f, room left %.0f / right %.0f cm"), Count,
		Sectors.WallNormalYaw, RoomLeft, RoomRight);
	return Sectors.WallSlots.Num() > 0;
}

void USquadSubsystem::ApplyFormationWallEnd(const FVector& Origin, const FVector2D& TowardsWall, float MaxDistance, const TArray<EFormationRole>& FixedRoles)
{
	using namespace SquadSubsystemFormationLocal;
	Sectors.bWallEndSpread = false;
	Sectors.WallEndBasePct = 100.f;
	Sectors.WallEndPct = 100.f;
	if (CVarFormationWallEnd.GetValueOnGameThread() == 0 || Sectors.WallSlots.Num() < 3)
	{
		return; // off, or the pair (its wall looks are a user decision)
	}
	const FFormationConfig& Config = FormationTuning::Get();
	// The face measured out past the probe ring: a long wall reads as a face even when its collision is not a box; at an end it is ~0 long.
	FVector2D Face;
	float NormalYaw = 0.f;
	float RoomLeft = 0.f;
	float RoomRight = 0.f;
	TArray<FFormationWall> Walls;
	if (BuildFormationWall(Origin, TowardsWall, MaxDistance, Face, NormalYaw, RoomLeft, RoomRight, Config.WallEndProbeDist + 2.f * Config.WallSpacing))
	{
		Walls.Add(FormationRuntime::WallAlong(Face, NormalYaw, RoomLeft, RoomRight));
	}
	FVector2D Centre = FVector2D::ZeroVector;
	for (const FFormationSlot& Slot : Sectors.WallSlots)
	{
		Centre += Slot.Position / static_cast<double>(Sectors.WallSlots.Num());
	}
	const float Reach = Config.WallEndProbeDist + WallEndGatherMarginCm;
	Walls.Append(GatherFormationWalls(Centre, FVector2D(Reach, Reach), 0.f, static_cast<float>(Origin.Z)));
	const FFormationWallEndResult End = FormationWallRules::EndLooks(Walls, Sectors.WallSlots, Config, FixedRoles);
	Sectors.WallEndBasePct = static_cast<float>(End.BasePct);
	Sectors.WallEndPct = static_cast<float>(End.Pct);
	if (!End.bSpread)
	{
		return;
	}
	Sectors.bWallEndSpread = true;
	FString Line;
	for (int32 Index = 0; Index < End.Slots.Num(); ++Index)
	{
		Line += FString::Printf(TEXT(" %s %.0f -> %.0f;"), FormationNames::ToString(End.Slots[Index].Role), Sectors.WallSlots[Index].Yaw, End.Slots[Index].Yaw);
	}
	Sectors.WallSlots = End.Slots;
	UE_LOG(LogCodexTactics, Display, TEXT("[Formation] wall END: the looks spread over the open arc (%.0f %% -> %.0f %% held, %d walls):%s"), End.BasePct, End.Pct,
		Walls.Num(), *Line);
}

void USquadSubsystem::ApplyFormationApproach(const AOperativeCharacter& LeaderRef)
{
	using namespace SquadSubsystemFormationLocal;
	Sectors.ApproachLooks.Reset();
	if (CVarFormationApproach.GetValueOnGameThread() == 0 || Sectors.FrameSlots.Num() < 2)
	{
		Sectors.Approach.Reset();
		return;
	}
	TArray<FFormationSlot> Places;
	FFormationSlot Lead;
	Lead.Role = EFormationRole::Commander;
	Lead.Position = FVector2D(LeaderRef.GetActorLocation());
	Lead.Yaw = static_cast<float>(LeaderRef.GetSightFacing().Rotation().Yaw);
	Places.Add(Lead);
	for (const FFollowerState& Follower : Followers)
	{
		const TOptional<EFormationRole> Role = SectorRoleOf(Follower);
		if (Role.IsSet() && Follower.Operative.IsValid())
		{
			FFormationSlot Member;
			Member.Role = Role.GetValue();
			Member.Position = FVector2D(Follower.Operative->GetActorLocation());
			Member.Yaw = static_cast<float>(Follower.Operative->GetSightFacing().Rotation().Yaw); // his look now: the rear guard already looks back
			Places.Add(Member);
		}
	}
	const TArray<FFormationWall> NoWalls;
	const bool bWalls = FormationGuardRuntime::IsEnabled() && Sectors.bWorldWallsValid;
	const bool bBegins = !Sectors.Approach.IsSet();
	const FFormationApproachResult Result = FormationWallRules::Approach(bWalls ? Sectors.WorldWalls : NoWalls, Sectors.FrameSlots, Sectors.WallNormalYaw, Places,
		Sectors.Approach, FormationTuning::Get());
	const bool bWasDone = !bBegins && Sectors.Approach->bDone;
	Sectors.Approach = Result.State;
	if (bBegins && !Result.State.bDone)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Formation] covered approach to the wall: %s runs in, rear %s, %d flank(s)"), FormationNames::ToString(Result.State.Runner),
			Result.State.Rear.IsSet() ? FormationNames::ToString(Result.State.Rear.GetValue()) : TEXT("-"), Result.State.Flanks.Num());
	}
	if (Result.State.bDone)
	{
		if (!bBegins && !bWasDone)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("[Formation] covered approach over: everyone at the wall - the wall layout's looks"));
		}
		return;
	}
	Sectors.ApproachLooks = Result.Looks;
	for (const FFormationApproachLook& Look : Result.Looks)
	{
		FFormationSlot* Slot = Sectors.FrameSlots.FindByPredicate([&Look](const FFormationSlot& Each) { return Each.Role == Look.Role; });
		if (Slot && Look.Yaw.IsSet())
		{
			Slot->Yaw = Look.Yaw.GetValue();
		}
	}
}

TOptional<EFormationApproachDuty> USquadSubsystem::FormationApproachDuty(EFormationRole Role) const
{
	const FFormationApproachLook* Look = Sectors.ApproachLooks.FindByPredicate([Role](const FFormationApproachLook& Each) { return Each.Role == Role; });
	return Look ? TOptional<EFormationApproachDuty>(Look->Duty) : TOptional<EFormationApproachDuty>();
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
		Sectors.bSlide = false;
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
	// Rules v2.1: the leader's recorded path (the trail column) and the world's walls round him (the guard, the corner watch, ClearLook).
	const bool bV21 = FormationGuardRuntime::IsEnabled();
	if (bV21)
	{
		UpdateFormationTrailAndWalls(LeaderRef, DeltaTime);
	}

	// A new order of the leader's while the wall layout stands: rules v2.2 wall slide when its destination lies along the same wall.
	const uint32 LeaderSerial = LeaderRef.GetMoveOrderSerial();
	if (Sectors.bLastLeaderSerialValid && LeaderSerial != Sectors.LastLeaderSerial)
	{
		Sectors.bLeaderOrderPending = true;
		Sectors.LeaderOrderPendingSeconds = 0.f;
	}
	Sectors.LastLeaderSerial = LeaderSerial;
	Sectors.bLastLeaderSerialValid = true;
	// His order's move may wait for a stand-up clip (the wall pose's crouch): its destination is known once it runs.
	Sectors.LeaderOrderPendingSeconds += DeltaTime;
	const bool bLeaderMoveDeferred = LeaderRef.GetMoveHoldSeconds() > 0.f && Sectors.LeaderOrderPendingSeconds < LeaderOrderDecideMaxSeconds;
	const bool bNewLeaderOrder = Sectors.bLeaderOrderPending && !bLeaderMoveDeferred;
	if (bNewLeaderOrder)
	{
		Sectors.bLeaderOrderPending = false;
	}
	if (Sectors.bWallOrder)
	{
		UpdateFormationWallOrder(LeaderRef, DeltaTime); // its own serial: a slide, or the order ends
	}
	else if (Sectors.bSlide)
	{
		if (bNewLeaderOrder && LeaderSerial != Sectors.SlideSerial && !TryStartFormationSlide(LeaderRef, false))
		{
			EndFormationSlide(LeaderRef, false, TEXT("the leader's next order"));
		}
	}
	else if (bNewLeaderOrder && Sectors.Formation == EFormationKind::Wall && Sectors.bWallLayoutValid)
	{
		TryStartFormationSlide(LeaderRef, false);
	}
	// Rules v2.1 bypass "B": a new own order of the leader (not a slide) - the side round a free-standing wall on his way; then the steering.
	if (bV21 && bNewLeaderOrder && !Sectors.bSlide && !Sectors.bWallOrder)
	{
		PlanFormationBypass(LeaderRef);
	}
	if (bV21)
	{
		UpdateFormationBypass(LeaderRef);
	}
	else
	{
		EndFormationBypass(TEXT("Codex.Formation.V21 0"));
	}

	// The formation: the wall order's wall, else by terrain with the wall layout only on arrival (the leader stood a moment).
	const bool bLeaderStill = !bLeaderMoving && LeaderRef.GetVelocity().Size2D() < StandingSpeed;
	Sectors.LeaderStillSeconds = bLeaderStill ? Sectors.LeaderStillSeconds + DeltaTime : 0.f;
	if (Sectors.bSlide || Sectors.bWallOrder)
	{
		// The wall layout (an order's, a held one, its slide) is what is asked for: no guard, no trail column.
		Sectors.Requested = EFormationKind::Wall;
		Sectors.Guard = FFormationGuardState();
		Sectors.bTrail = false;
	}
	if (Sectors.bSlide)
	{
		UpdateFormationSlide(LeaderRef, DeltaTime); // the wall layout side-steps along its wall (FrameSlots by the leader's progress)
	}
	else if (Sectors.bWallOrder)
	{
		Sectors.Formation = EFormationKind::Wall;
	}
	else
	{
		const EFormationKind Previous = Sectors.Formation;
		// v1.2: at_wall fires only on arrival (within classify.arrive_dist of his destination) - that, or a short stand, is the arrival.
		const bool bArrived = FormationTerrainRules::IsArriving(LeaderPos, LeaderDestination(LeaderRef), Config)
			|| Sectors.LeaderStillSeconds >= WallArrivalStillSeconds;
		const EFormationKind Terrain = FormationRuntime::ChooseFormation(Class, Count, bArrived, Sectors.MoveFormation, IsFormationBlizzard());
		if (bLeaderMoving)
		{
			Sectors.MoveFormation = Terrain;
		}
		// The hold-B menu's manual choice holds until Auto (FormationMenuRules::Resolve: too few members -> Auto); else the terrain's.
		const FFormationMenuResolved Menu = FormationMenuRules::Resolve(Sectors.MenuChoice, Count);
		Sectors.bAllForward = Menu.bAllForward;
		Sectors.Requested = Menu.ManualFormation.Get(Terrain);
		Sectors.Formation = Sectors.Requested;
		Sectors.bTrail = false;
		if (Sectors.Formation == EFormationKind::Wall)
		{
			Sectors.Guard = FFormationGuardState(); // the wall layout is never guarded (its slots are on the leader's side of his wall)
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
			if (bV21)
			{
				// Rules v2.1: the wall guard - the request, its mirror echelon, or the trail column (the sharp-turn fold too).
				StepFormationGuard(LeaderRef, bLeaderMoving, Count);
			}
			else
			{
				Sectors.Guard = FFormationGuardState();
			}
		}
		if (Sectors.Formation != Previous)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("[Formation] %s -> %s (terrain %s, passage %s%s)"), FormationNames::ToString(Previous),
				FormationNames::ToString(Sectors.Formation), FormationNames::ToString(Class),
				Sectors.PassageWidth.IsSet() ? *FString::Printf(TEXT("%.0f cm"), Sectors.PassageWidth.GetValue()) : TEXT("-"),
				Sectors.Requested != Sectors.Formation ? *FString::Printf(TEXT(", asked %s"), FormationNames::ToString(Sectors.Requested)) : TEXT(""));
		}
	}
	const bool bWall = Sectors.Formation == EFormationKind::Wall;
	// The slide walks at the patrol pace (its looks are the slide's own: the lead end ahead, K out, the trail end back).
	Sectors.Mode = Sectors.bSlide ? EFormationMode::Walk : FormationRuntime::ResolveMode(bLeaderMoving, LeaderRef.IsSprinting(), bWall);
	if (Sectors.Mode == EFormationMode::Walk)
	{
		Sectors.PatrolTime += DeltaTime;
		if (Sectors.bAllForward && !Sectors.bSlide && !bWall)
		{
			Sectors.Mode = EFormationMode::Forward; // the menu's "All forward": walking, everybody looks at 12
		}
	}

	// This frame's slots and looks by role.
	if (!bWall || Sectors.bSlide)
	{
		Sectors.ApproachLooks.Reset(); // rules v2.5: the covered approach is only the way INTO a standing wall layout
		Sectors.Approach.Reset();
	}
	if (Sectors.bSlide)
	{
		// UpdateFormationSlide set them; rules v2.1: the leading end looks round the wall's end (the corner watch).
		if (bV21)
		{
			ApplyFormationCornerWatch(LeaderPos);
		}
	}
	else if (bWall)
	{
		Sectors.FrameSlots = Sectors.WallSlots;
		Sectors.CornerLooks.Reset();
		Sectors.Corner = FFormationCornerState();
		ApplyFormationApproach(LeaderRef); // rules v2.5: the covered approach until everyone is at his wall slot
	}
	else
	{
		if (Sectors.bTrail)
		{
			BuildTrailFrameSlots(LeaderPos, Count); // rules v2.1: the column along the leader's own path
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
		ReassignFormationPlaces();
		// Rules v2.1: slicing the pie - the watcher's look is fixed for the allocator (the others cover what he leaves).
		if (bV21)
		{
			ApplyFormationCornerWatch(LeaderPos);
		}
		else
		{
			Sectors.CornerLooks.Reset();
		}
		// Rules v2.2: walking / halted with 3+ the coverage allocator replaces the per-formation tables (the pair and a run keep LookYaw).
		AllocateFormationLooks(LeaderRef, LeaderPos);
	}

	// The leader (not with a box selection: each faces his own path): his sector, and the patrol pace while the squad walks.
	if (!HasMultiSelection())
	{
		const FFormationSlot* Lead = FindFrameSlot(EFormationRole::Commander);
		// Halted: he turns his search back to 12 for a moment, then his facing is his own (the player's, a facing order) and the heading
		// follows it; the wall layout keeps him facing out.
		const bool bHaltLookOver = Sectors.Mode == EFormationMode::Halt && Sectors.LeaderStillSeconds >= FormationRuntime::LeaderHaltLookSeconds;
		if (Lead && Sectors.Mode != EFormationMode::Run && !bHaltLookOver)
		{
			const bool bWalkingIn = bWall && Sectors.bWallOrder && !Sectors.bSlide
				&& !FormationRuntime::UseWallLayoutYaw(static_cast<float>(FVector2D::Distance(LeaderPos, Lead->Position)), !bLeaderStill);
			if (!bWalkingIn)
			{
				FrameLooks.Add(&LeaderRef, ClearFormationLook(LeaderRef, EFormationRole::Commander, /*bLeader*/ true, Lead->Yaw));
			}
		}
		if (Sectors.Mode == EFormationMode::Walk || Sectors.Mode == EFormationMode::Forward)
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
	SlotLocation = ResolveReachableSlot(LeaderRef, SlotLocation, /*bLeaderSide*/ FormationGuardRuntime::IsEnabled(), Operative.GetActorLocation());
	FrameGivenByMember.Add(Follower.Operative, FVector2D(SlotLocation));
	const FVector2D Here(Operative.GetActorLocation());
	const FVector2D Target(SlotLocation);
	const float Distance = static_cast<float>(FVector2D::Distance(Here, Target));
	const bool bMoving = Operative.GetVelocity().Size2D() > StandingSpeed;
	const bool bWall = Sectors.Formation == EFormationKind::Wall;

	// His look: run = his path (everybody faces 12); the wall layout: his path until 1 m from his slot (contract 7); else the sector.
	// Rules v2.5: on the covered approach a member with an approach look (rear, flank, at the wall) holds it all the way in.
	const TOptional<EFormationApproachDuty> Duty = bWall && !Sectors.bSlide ? FormationApproachDuty(Role.GetValue()) : TOptional<EFormationApproachDuty>();
	const bool bApproachLook = Duty.IsSet() && Duty.GetValue() != EFormationApproachDuty::Run;
	if (Sectors.Mode != EFormationMode::Run && (!bWall || Sectors.bSlide || bApproachLook || FormationRuntime::UseWallLayoutYaw(Distance, bMoving)))
	{
		float Look = Slot->Yaw;
		// Rules v2.2: the rear guard catching up sideways eases his look within his rear band (5-7 o'clock stays in his cone).
		if (!bWall && Sectors.Mode == EFormationMode::Walk && Follower.bCatchUp && Distance > FormationRuntime::RearCatchUpCm
			&& Sectors.RearGuard.IsSet() && Role.GetValue() == Sectors.RearGuard.GetValue())
		{
			const FFormationConfig& LookConfig = FormationTuning::Get();
			Look = FormationRuntime::RearGuardCatchUpLook(Look, Sectors.HeadingYaw,
				0.5f * LookConfig.SightFov - static_cast<float>(FormationCoverRules::RearSector), Follower.CatchUpMoveYaw);
		}
		// Rules v2.1.1 (user "6A"): nobody looks into a wall - in any formation (not the wall layout, a run, the corner watcher).
		FrameLooks.Add(Follower.Operative, ClearFormationLook(Operative, Role.GetValue(), /*bLeader*/ false, Look));
	}
	if (Sectors.bWallOrder && Sectors.bWallOrderCrouch && !Sectors.bSlide && Distance <= WallSettleCm && !bMoving
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
	// Rules v2.5: the rear cover on the covered approach walks backwards at the backward cap of the WALK (the approved rear guard), even
	// while the leader runs in; the others at the approach pace.
	const bool bRearCover = Duty.IsSet() && Duty.GetValue() == EFormationApproachDuty::Rear && Operative.GetStance() != EOperativeStance::Prone;
	const bool bRun = !bRearCover && (LeaderRef.IsSprinting() || Operative.GetStance() == EOperativeStance::Prone);
	const float Top = Operative.GetMaxSpeed(); // his walk / crawl, or the sprint he copies from the leader
	// Bug D (2026-10-26): walking into the wall layout along the wall, a team-mate standing in the line between him and his place is stepped
	// round on the open side (the nav mesh ignores bodies: a man re-forming past the crouched leader pushed against him for good).
	FVector2D MoveTarget = Target;
	if (bWall && !Sectors.bSlide && Distance > WallSettleCm)
	{
		TArray<FVector2D> Others;
		Others.Add(FVector2D(LeaderRef.GetActorLocation()));
		for (const FFollowerState& Other : Followers)
		{
			if (Other.Operative.IsValid() && Other.Operative.Get() != &Operative)
			{
				Others.Add(FVector2D(Other.Operative->GetActorLocation()));
			}
		}
		if (const TOptional<FVector2D> Step = FormationWallEnd::StepRound(Here, Target, Others, Sectors.WallNormalYaw); Step.IsSet())
		{
			MoveTarget = Step.GetValue();
			SlotVelocity = FVector2D::ZeroVector;
		}
	}
	const FormationRuntime::FFollowerMove Move = FormationRuntime::FollowerMove(Here, static_cast<float>(Operative.GetActorRotation().Yaw), MoveTarget,
		SlotVelocity, Jitter, Top, bRearCover ? FMath::Min(Top, FormationTuning::Get().PaceWalkSpeed) : Top, bRun, FormationTuning::Get());
	Follower.bCatchUp = !Move.bStop && Move.Speed > 1.f;
	Follower.CatchUpMoveYaw = FormationMath::YawOf(Move.Velocity);
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
	const FVector2D Destination = FormationRuntime::LeadDestination(MoveTarget, SlotVelocity);
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
		if (Sectors.bLeaderOrderPending && OrderLeader->GetMoveHoldSeconds() > 0.f && Sectors.LeaderOrderPendingSeconds < LeaderOrderDecideMaxSeconds)
		{
			return; // his order's move waits for the stand-up: its destination (a slide?) is not known yet
		}
		Sectors.bLeaderOrderPending = false;
		// Rules v2.2: a destination along the same wall is the wall slide (the order and its roles stay), anything else ends the order.
		if (!TryStartFormationSlide(*OrderLeader, Sectors.bSlide ? Sectors.bSlideFromOrder : Sectors.bWallOrderCrouch))
		{
			if (Sectors.bSlide)
			{
				EndFormationSlide(*OrderLeader, false, TEXT("the leader's next order"));
			}
			else
			{
				EndFormationWallOrder(TEXT("the leader's next order"));
			}
		}
		return;
	}
	Sectors.WallOrderSeconds += DeltaTime;
	const FFormationSlot* Lead = Sectors.WallSlots.FindByPredicate([](const FFormationSlot& Slot) { return Slot.Role == EFormationRole::Commander; });
	if (Lead && Sectors.bWallOrderCrouch && !Sectors.bSlide && !OrderLeader->IsMoving()
		&& FVector2D::Distance(FVector2D(OrderLeader->GetActorLocation()), Lead->Position) <= WallLeaderSettleCm
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
	Sectors.bWallOrderCrouch = true;
	Sectors.bSlide = false;
	Sectors.bSlideStack = false;
	Sectors.bCornerStack = false;
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
	EndWallGroup(TEXT("reset"));
	// Rules v2.1: the guard, the trail, the corner watch and the bypass start afresh (the menu's choice and reported threats stay).
	EndFormationBypass(TEXT("reset"));
	Sectors.Guard = FFormationGuardState();
	Sectors.bTrail = false;
	Sectors.Trail.Reset();
	Sectors.bPlacesKeyValid = false;
	Sectors.bWorldWallsValid = false;
	Sectors.Corner = FFormationCornerState();
	Sectors.CornerLooks.Reset();
	Sectors.bSlide = false;
	Sectors.bSlideStack = false;
	Sectors.bCornerStack = false;
	Sectors.bLastLeaderSerialValid = false;
	Sectors.RearGuard.Reset();
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
