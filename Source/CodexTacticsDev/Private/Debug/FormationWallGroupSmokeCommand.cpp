// Dev-only headless check of the rules v2.2 WALL GROUP (behind Codex.Formation.Sectors; USquadSubsystem::ReallocateWallGroup) - the user's
// playtest bug 2026-10-25: "in group cover at a wall corner the leader dashed (the cover switch) to another wall's edge and held his new corner
// correctly, but the two others stayed 'Hugging the wall, holding the corner!' looking the same way as he - a wide gap opened":
//   powershell -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -Commands FormationWallGroupSmoke [-PreCmds "Codex.Formation.Sectors 0"]
//   rendered stills: UnrealEditor.exe CodexTactics.uproject /Game/Maps/L_MovementTest -game -RenderOffscreen -ResX=1280 -ResY=720 -nosound
//     "-ExecCmds=CodexTactics.FormationWallGroupSmoke Shot" -> Saved/Screenshots/FormationV22/wallgroup_*.png (the squad's vision cones on)
// A runtime arena on L_MovementTest (SmokeUtils::FindClearArena, nothing saved; walls as CoverSwitchSmoke / CoverWrapSmoke): wall A (3 m high,
// 6 m, near face 3 m ahead), wall B (3 m, its near face 4.5 m ahead beyond A's right end), the short wall D (2 m, beyond A's left end) and the
// box H (3 x 3 x 3 m, behind the start on the right). Group mode (Solo off, the exploration formation), every order through the controller:
//  1 the leader in cover at A's right end, the two others in the formation; a double-click on B = the cover switch with the group (the
//    others wait off the wall, then re-enter their slots after him) -> the wall group re-allocates: the leader keeps the corner he arrived
//    at, the others close the open arc - no uncovered open gap over coverage.max_gap_deg, nobody faces a wall, not all the same way;
//  2 from A's left end the switch to the short wall D: one goes prone behind it - same checks with the prone look;
//  3 the wrap round H: the leader goes round its corner along the wall, the others take its side face - same checks;
//  4 (rules v2.4, user bug report 2026-10-26) the wall END: a thin free-standing wall E (4 m x 40 cm) is put up, the squad patrols up in
//    group mode and the player clicks the cover at E's end face (the group cover order = the formation wall layout): the face looks would
//    leave both flanks along E's faces open (the layout reports < 80 % held), the spread looks hold >= 95 % of the open ring round the end
//    (wall_end.probe_dist; the world's box walls), each flank 5 m back along a face is inside the cone of the man on that side, nobody
//    faces a wall, and for 3 s nobody's look or place moves (no flicker / reshuffle). Codex.Formation.WallEnd 0: the face looks (logged);
//  5 (rules v2.5, user rule 2026-10-26: the covered approach to the wall) the squad patrols across the arena towards E's long face and the
//    player clicks the cover in its middle
//    (the group cover order = the formation wall layout): on the way somebody always holds the rear (away from the wall, after ApproachTurnSeconds (1 s of game time) to
//    turn), somebody a flank for >= 90 % of the way, the leader at the wall first looks along it; then everyone in the wall layout's look.
//    Codex.Formation.Approach 0: everyone walks in facing his travel (the rear checks fail - the user's report).
// Codex.Formation.Sectors 0: the old facing untouched (no wall group, nobody taken out of his slot, every follower in cover or prone with
// his own CoverFacingRules); the gap is logged only. Godot reference: none (UE-only).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/VisionConeComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Debug/SmokeUtils.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Formation/FormationRuntimeRules.h"
#include "Formation/FormationTuning.h"
#include "Formation/FormationWallGroupRules.h"
#include "Formation/FormationWallRules.h"
#include "GameFramework/HUD.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "Tactics/CoverGhostActor.h"
#include "Tactics/CoverTraceRules.h"
#include "UnrealClient.h"

namespace FormationWallGroupSmoke
{
	constexpr float StepSeconds = 0.05f;
	/** Everyone settled this long before the scene is judged (the looks turn, the allocation re-runs), s. */
	constexpr float SettleHoldSeconds = 2.5f;
	constexpr float SceneCapSeconds = 30.f;

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		bool bShot = false;
		bool bSectors = true;
		FVector P = FVector::ZeroVector;
		FVector F = FVector::ForwardVector;
		FVector R = FVector::RightVector;
		float GroundZ = 0.f;
		TArray<TWeakObjectPtr<AOperativeCharacter>> Members;
		TArray<TWeakObjectPtr<AStaticMeshActor>> Boxes;
		FCoverSlot SlotAEnd;
		FCoverSlot SlotAOther;
		FCoverSlot SlotB;
		FCoverSlot SlotD;
		FCoverSlot SlotH;
		/** The scene being judged: its wall actor, when everyone was first seen settled, the leader's look then. */
		TWeakObjectPtr<AActor> SceneWall;
		float StableSince = -1.f;
		float LeaderYawAtArrival = 0.f;
		bool bLeaderArrived = false;
		ECoverFacing WrapGoalFacing = ECoverFacing::Right;
		int32 ShotsTaken = 0;
		TWeakObjectPtr<ACameraActor> Camera;
		/** Scene 4 (v2.4): the end face's cover slot, the settled looks / places, the worst drift while they hold. */
		FCoverSlot SlotEEnd;
		TArray<float> HeldLooks;
		TArray<FVector2D> HeldPlaces;
		float WorstLookDrift = 0.f;
		float WorstPlaceDrift = 0.f;
		/** Scene 5 (v2.5): the cover slot at A's middle, the approach's frames / misses, the leader's best along-the-wall look. */
		FCoverSlot SlotAMid;
		bool bApproachSeen = false;
		float ApproachTime = 0.f;
		float RearMissTime = 0.f;
		float FlankTime = 0.f;
		/** Scene 5: game time judged (after ApproachTurnSeconds), the last sample's game time (the ticker runs on real time, the game on a fixed step). */
		float JudgedTime = 0.f;
		double LastGameTime = -1.0;
		float BestAlongDeg = 180.f;
		FString FirstRearMiss;
	};

	/** Scene 4's thin wall E: along F from F -450 to F -50 at R -600 (40 cm thick); its free end face is at F -50, facing +F. */
	constexpr float WallEFrom = -450.f;
	constexpr float WallEEnd = -50.f;
	constexpr float WallER = -600.f;
	constexpr float WallEThickness = 40.f;
	/** Scene 4: everyone in the wall pose this long before the coverage is judged; then the hold watched for flicker, s. */
	constexpr float EndSettleSeconds = 2.f;
	constexpr float EndHoldSeconds = 3.f;
	/** Scene 5: the rear cover may need to turn round when the order comes (in this cluttered arena ClearLook keeps the patrol rear guard off
	 * his 6 o'clock): a 180 deg turn takes the game ~1 s (the simulator turns faster: its check 14 allows 0.5 s) - game time, s. */
	constexpr float ApproachTurnSeconds = 1.f;

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		for (const TWeakObjectPtr<AStaticMeshActor>& Box : State.Boxes)
		{
			if (Box.IsValid())
			{
				Box->Destroy();
			}
		}
		FPlatformMisc::RequestExit(false, TEXT("FormationWallGroupSmoke"));
		return false;
	}

	void Next(FState& State, int32 Stage)
	{
		State.Stage = Stage;
		State.StageTime = 0.f;
	}

	AOperativeCharacter* Member(const FState& State, int32 Index)
	{
		return State.Members.IsValidIndex(Index) ? State.Members[Index].Get() : nullptr;
	}

	FVector Local(const FState& State, float AlongF, float AlongR, float Up = 0.f)
	{
		return State.P + State.F * AlongF + State.R * AlongR + FVector(0.f, 0.f, State.GroundZ - State.P.Z + Up);
	}

	/** A box centred (CentreF, CentreR) in the arena frame, SizeF x SizeR, HeightCm high. */
	void SpawnBox(UWorld* World, FState& State, float CentreF, float CentreR, float SizeF, float SizeR, float HeightCm)
	{
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		const FTransform Transform(State.F.Rotation(), Local(State, CentreF, CentreR, HeightCm * 0.5f), FVector(SizeF / 100.f, SizeR / 100.f, HeightCm / 100.f));
		AStaticMeshActor* Block = Cube ? World->SpawnActorDeferred<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Transform, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn) : nullptr;
		if (Block)
		{
			Block->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
			Block->GetStaticMeshComponent()->SetStaticMesh(Cube);
			Block->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
			Block->GetStaticMeshComponent()->SetCanEverAffectNavigation(true);
			Block->FinishSpawning(Transform);
		}
		State.Boxes.Add(Block);
	}

	/** The cover slot a click on a face at (AtF, AtR), 90 cm up, looking along Into gives. */
	bool FindSlot(UWorld* World, const FState& State, float AtF, float AtR, const FVector& Into, FCoverSlot& OutSlot)
	{
		return CoverTraceRules::FindCoverSlotAt(World, Local(State, AtF, AtR, 90.f), Into, OutSlot);
	}

	FHitResult GroundHit(UWorld* World, const FState& State, float AtF, float AtR)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(FormationWallGroupSmokeGround), false);
		for (const TWeakObjectPtr<AOperativeCharacter>& Each : State.Members)
		{
			Params.AddIgnoredActor(Each.Get());
		}
		for (TActorIterator<ACoverGhostActor> It(World); It; ++It)
		{
			Params.AddIgnoredActor(*It);
		}
		FHitResult Hit;
		const FVector Top = Local(State, AtF, AtR, 300.f);
		World->LineTraceSingleByChannel(Hit, Top, Top - FVector(0.f, 0.f, 600.f), ECC_Visibility, Params);
		return Hit;
	}

	/** Everyone out of cover, standing; the leader at Start (arena frame), the others parked behind him. */
	void ResetSquad(USquadSubsystem* Squad, FState& State, const FVector2D& Start)
	{
		Squad->EnterSoloMode();
		for (int32 Index = 0; Index < State.Members.Num(); ++Index)
		{
			AOperativeCharacter* Each = Member(State, Index);
			if (!Each)
			{
				continue;
			}
			Each->LeaveCover(TEXT("smoke reset"));
			Each->SetFormationWallPose(false);
			Each->ClearPendingCover();
			Each->StopOperative();
			Each->SetStance(EOperativeStance::Standing);
			const FVector2D At = Index == 0 ? Start : FVector2D(Start.X - 300.f, Start.Y + (Index == 1 ? -150.f : 150.f));
			Each->TeleportTo(Local(State, At.X, At.Y, Each->GetSimpleCollisionHalfHeight() + 5.f), State.F.Rotation(), false, true);
		}
		Squad->SetLeader(Member(State, 0));
	}

	/** At his place by the wall and still: in a cover slot, the back-to-wall pose or lying behind the cover. */
	bool AtWall(const USquadSubsystem* Squad, const AOperativeCharacter* Op)
	{
		const bool bStill = Op && !Op->IsMoving() && Op->GetVelocity().Size2D() < 5.f && Op->GetMoveHoldSeconds() <= 0.f;
		return bStill && !Squad->IsWaitingForGroupCoverLeader(Op)
			&& ((Op->bInCover && !Op->bShimmying && Op->GetCoverEntryMeshOffset().Size() < 1.f) || Op->IsInFormationWallPose() || Squad->HasGroupCoverProne(Op, true));
	}

	/**
	 * Check 1 (contract 4: "facing within 45 deg of the wall normal, towards it", a surface closer than 200 cm) from 16 rays round him: the
	 * hits' own normals (thin probe segments would read a face he looks ALONG as a wall ahead).
	 */
	bool StaresAtWall(UWorld* World, const FState& State, const FVector& Feet, float LookYaw)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(FormationWallGroupSmokeStare), false);
		for (const TWeakObjectPtr<AOperativeCharacter>& Each : State.Members)
		{
			Params.AddIgnoredActor(Each.Get());
		}
		const FVector Origin = Feet + FVector(0.f, 0.f, 50.f);
		for (int32 Ray = 0; Ray < 16; ++Ray)
		{
			FHitResult Hit;
			const FVector Direction = FRotator(0.f, Ray * 22.5f, 0.f).Vector();
			if (World->LineTraceSingleByChannel(Hit, Origin, Origin + Direction * FormationWallGroup::StareDistCm * 1.5f, ECC_Visibility, Params)
				&& FMath::Abs(Hit.ImpactNormal.Z) < 0.7f)
			{
				const FVector2D Normal = FVector2D(Hit.ImpactNormal).GetSafeNormal();
				const double Off = FVector2D::DotProduct(FVector2D(Origin) - FVector2D(Hit.ImpactPoint), Normal);
				if (Off <= FormationWallGroup::StareDistCm && FormationMath::AngleBetween(LookYaw, FormationMath::YawOf(-Normal)) < FormationWallGroup::StareAngleDeg)
				{
					return true;
				}
			}
		}
		return false;
	}

	/** Thin walls from 16 rays round each member (50 cm up, look.wall_dist long) - what the checks call "a wall near him". */
	TArray<FFormationWall> TraceWalls(UWorld* World, const FState& State, const TArray<FVector>& Feet)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(FormationWallGroupSmokeWalls), false);
		for (const TWeakObjectPtr<AOperativeCharacter>& Each : State.Members)
		{
			Params.AddIgnoredActor(Each.Get());
		}
		TArray<FFormationWall> Walls;
		const float Reach = FormationTuning::Get().LookWallDist;
		for (const FVector& At : Feet)
		{
			const FVector Origin = At + FVector(0.f, 0.f, 50.f);
			for (int32 Ray = 0; Ray < 16; ++Ray)
			{
				FHitResult Hit;
				const FVector Direction = FRotator(0.f, Ray * 22.5f, 0.f).Vector();
				if (World->LineTraceSingleByChannel(Hit, Origin, Origin + Direction * Reach, ECC_Visibility, Params) && FMath::Abs(Hit.ImpactNormal.Z) < 0.7f)
				{
					Walls.Add(FormationRuntime::WallFromHit(FVector2D(Hit.ImpactPoint), FVector2D(Hit.ImpactNormal), 30.f));
				}
			}
		}
		return Walls;
	}

	float Fov(const AOperativeCharacter* Op)
	{
		return Op->GetStance() == EOperativeStance::Prone ? 110.f : FormationTuning::Get().SightFov;
	}

	void PlaceCamera(UWorld* World, FState& State, const FVector& Eye, const FVector& LookAt)
	{
		if (!State.Camera.IsValid())
		{
			State.Camera = World->SpawnActor<ACameraActor>(Eye, (LookAt - Eye).Rotation());
			if (ACameraActor* Created = State.Camera.Get())
			{
				Created->GetCameraComponent()->SetFieldOfView(60.f);
				Created->GetCameraComponent()->bConstrainAspectRatio = false;
			}
		}
		if (ACameraActor* Cam = State.Camera.Get())
		{
			Cam->SetActorLocationAndRotation(Eye, (LookAt - Eye).Rotation());
			if (APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0))
			{
				PC->SetViewTarget(Cam);
				if (AHUD* Hud = PC->GetHUD())
				{
					Hud->bShowHUD = false;
				}
			}
		}
	}

	void RequestShot(FState& State, const TCHAR* Name)
	{
		const FString Path = FPaths::ProjectSavedDir() / TEXT("Screenshots") / TEXT("FormationV22") / FString::Printf(TEXT("%s.png"), Name);
		FScreenshotRequest::RequestScreenshot(Path, /*bShowUI*/ false, /*bAddFilenameSuffix*/ false);
		++State.ShotsTaken;
		UE_LOG(LogCodexTactics, Display, TEXT("FormationWallGroup: screenshot %s"), *Path);
	}

	/** Waits for the scene to settle (true when it is judged): the leader in cover at SceneWall, the others at the wall, SettleHoldSeconds. */
	bool SceneSettled(FState& State, USquadSubsystem* Squad)
	{
		AOperativeCharacter* Op = Member(State, 0);
		const bool bLeader = Op && Op->bInCover && !Op->bShimmying && !Op->IsCoverWrapping() && Op->GetCoverSlot().WallActor.Get() == State.SceneWall.Get()
			&& Op->GetVelocity().Size2D() < 5.f;
		if (bLeader && !State.bLeaderArrived)
		{
			State.bLeaderArrived = true;
			State.LeaderYawAtArrival = static_cast<float>(Op->GetSightFacing().Rotation().Yaw);
		}
		const bool bAll = bLeader && AtWall(Squad, Member(State, 1)) && AtWall(Squad, Member(State, 2));
		if (!bAll)
		{
			State.StableSince = -1.f;
			return State.StageTime >= SceneCapSeconds;
		}
		if (State.StableSince < 0.f)
		{
			State.StableSince = State.StageTime;
		}
		return State.StageTime - State.StableSince >= SettleHoldSeconds;
	}

	/** The wall group's checks over the settled scene (Label); bSceneOk = everyone got there. */
	void CheckScene(UWorld* World, FState& State, USquadSubsystem* Squad, const TCHAR* Label)
	{
		AOperativeCharacter* Op = Member(State, 0);
		const bool bLeader = Op && Op->bInCover && Op->GetCoverSlot().WallActor.Get() == State.SceneWall.Get();
		Check(State, bLeader && AtWall(Squad, Member(State, 1)) && AtWall(Squad, Member(State, 2)) && State.StableSince >= 0.f,
			FString::Printf(TEXT("%s: the leader in cover at the new wall, the others at it (in a slot, back to the wall, or prone) after %.1f s"), Label, State.StageTime));
		if (!bLeader)
		{
			return;
		}
		const float LeaderYaw = static_cast<float>(Op->GetSightFacing().Rotation().Yaw);
		Check(State, FormationMath::AngleBetween(LeaderYaw, State.LeaderYawAtArrival) <= 10.f && !Op->GetCoverLookSide().IsSet(),
			FString::Printf(TEXT("%s: the leader keeps his own look at his corner (%.0f, arrived %.0f)"), Label, LeaderYaw, State.LeaderYawAtArrival));
		TArray<FVector> Feet;
		TArray<FVector2D> Positions;
		TArray<float> Looks;
		TArray<float> Fovs;
		FString Line;
		float MostApart = 0.f;
		for (int32 Index = 0; Index < 3; ++Index)
		{
			const AOperativeCharacter* Each = Member(State, Index);
			if (!Each)
			{
				continue;
			}
			Feet.Add(Each->GetActorLocation() - FVector(0.f, 0.f, Each->GetSimpleCollisionHalfHeight()));
			Positions.Add(FVector2D(Each->GetActorLocation()));
			Looks.Add(static_cast<float>(Each->GetSightFacing().Rotation().Yaw));
			Fovs.Add(Fov(Each));
			MostApart = FMath::Max(MostApart, FormationMath::AngleBetween(Looks.Last(), LeaderYaw));
			float Allocated = 0.f;
			EWallGroupLook Kind = EWallGroupLook::Free;
			const bool bAllocated = Squad->GetWallGroupLook(Each, Allocated, Kind);
			Line += FString::Printf(TEXT(" [%s: %s, look %.0f%s]"), *Each->DisplayName.ToString(), Each->bInCover ? TEXT("in cover") : Each->IsInFormationWallPose()
				? TEXT("back to the wall") : Squad->HasGroupCoverProne(Each) ? TEXT("prone") : TEXT("?"), Looks.Last(),
				bAllocated ? *FString::Printf(TEXT(", allocated %.0f"), Allocated) : TEXT(""));
		}
		const TArray<FFormationWall> Walls = TraceWalls(World, State, Feet);
		const float Gap = FormationWallGroup::LargestOpenGap(Walls, Positions, Looks, Fovs, FormationTuning::Get().LookWallDist);
		int32 Stares = 0;
		for (int32 Index = 0; Index < Positions.Num(); ++Index)
		{
			if (StaresAtWall(World, State, Feet[Index], Looks[Index]))
			{
				++Stares;
				Line += FString::Printf(TEXT(" STARE: %s (look %.0f)"), *Member(State, Index)->DisplayName.ToString(), Looks[Index]);
			}
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: %s:%s; largest open gap %.0f deg, %d stare(s), wall group %d (%d allocations, %d re-entries)"), Label,
			*Line, Gap, Stares, Squad->HasWallGroup() ? 1 : 0, Squad->GetWallGroupAllocationCount(), Squad->GetWallGroupReentryCount());
		if (!State.bSectors)
		{
			// Codex.Formation.Sectors 0: the old path untouched.
			bool bOld = !Squad->HasWallGroup();
			for (int32 Index = 1; Index < 3; ++Index)
			{
				const AOperativeCharacter* Each = Member(State, Index);
				bOld &= Each && !Each->IsInFormationWallPose() && !Each->GetCoverLookSide().IsSet() && (Each->bInCover || Squad->HasGroupCoverProne(Each, true));
			}
			Check(State, bOld, FString::Printf(TEXT("%s [Sectors 0]: the old facing - no wall group, every follower in his slot / prone with his own cover facing"), Label));
			return;
		}
		Check(State, Squad->HasWallGroup() && Squad->GetWallGroupAllocationCount() >= 1,
			FString::Printf(TEXT("%s: the wall group re-allocated the looks (%d allocations)"), Label, Squad->GetWallGroupAllocationCount()));
		Check(State, Gap <= FormationTuning::Get().CoverageMaxGapDeg,
			FString::Printf(TEXT("%s: no uncovered open gap over coverage.max_gap_deg (%.0f <= %.0f deg)"), Label, Gap, FormationTuning::Get().CoverageMaxGapDeg));
		Check(State, Stares == 0, FString::Printf(TEXT("%s: nobody faces a wall within 200 cm (%d)"), Label, Stares));
		Check(State, MostApart > 60.f, FString::Printf(TEXT("%s: not all looking the leader's way (the widest %.0f deg from his look)"), Label, MostApart));
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time > 240.f)
		{
			Check(State, false, FString::Printf(TEXT("timeout (stage %d)"), State.Stage));
			return World ? Finish(State, false) : false;
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(UGameplayStatics::GetPlayerController(World, 0));
		AOperativeCharacter* Op = Member(State, 0);
		switch (State.Stage)
		{
		case 0:
		{
			if (State.StageTime < 3.f || !Squad || !PC || Squad->GetMembers().Num() < 3)
			{
				return true;
			}
			State.bSectors = FormationRuntime::IsEnabled();
			TArray<AOperativeCharacter*> Roster = Squad->GetMembers();
			Roster.Sort([](const AOperativeCharacter& X, const AOperativeCharacter& Y) { return X.SquadIndex < Y.SquadIndex; });
			TArray<const AActor*> Ignore;
			for (int32 Index = 0; Index < 3; ++Index)
			{
				State.Members.Add(Roster[Index]);
				Ignore.Add(Roster[Index]);
			}
			Ignore.Add(PC->GetPawn());
			const FVector Feet = Roster[0]->GetActorLocation() - FVector(0.f, 0.f, Roster[0]->GetSimpleCollisionHalfHeight());
			const SmokeUtils::FClearArena Arena = SmokeUtils::FindClearArena(World, Feet, Roster[0]->GetActorForwardVector(),
				FBox2D(FVector2D(-500.f, -950.f), FVector2D(800.f, 950.f)), Ignore);
			if (!Arena.bFound)
			{
				Check(State, false, TEXT("an empty, flat, navigable 13 x 19 m spot for the smoke's own set-up"));
				return Finish(State, false);
			}
			State.P = Arena.Origin;
			State.F = Arena.Forward;
			State.R = Arena.Right;
			State.GroundZ = Arena.Origin.Z;
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: arena at %s, Codex.Formation.Sectors %d"), *Arena.Origin.ToCompactString(), State.bSectors ? 1 : 0);
			ResetSquad(Squad, State, FVector2D(0.f, 100.f));
			SpawnBox(World, State, 320.f, 0.f, 40.f, 600.f, 300.f);     // A: near face F 300, R -300..300
			SpawnBox(World, State, 470.f, 700.f, 40.f, 300.f, 300.f);   // B: near face F 450, R 550..850
			SpawnBox(World, State, 470.f, -750.f, 40.f, 200.f, 300.f);  // D: near face F 450, R -850..-650 (short: one prone)
			SpawnBox(World, State, -300.f, 700.f, 300.f, 300.f, 300.f); // H: faces F -450 / -150, R 550 / 850 (the wrap)
			if (State.bShot)
			{
				UVisionConeComponent::SetPlayerSquadCones(true);
			}
			Next(State, 1);
			return true;
		}
		case 1:
		{
			if (State.StageTime < 2.5f)
			{
				return true; // the navmesh rebuilds round the walls
			}
			const bool bFound = FindSlot(World, State, 300.f, 250.f, State.F, State.SlotAEnd) && FindSlot(World, State, 300.f, -250.f, State.F, State.SlotAOther)
				&& FindSlot(World, State, 450.f, 650.f, State.F, State.SlotB) && FindSlot(World, State, 450.f, -700.f, State.F, State.SlotD)
				&& FindSlot(World, State, -150.f, 780.f, -State.F, State.SlotH);
			Check(State, bFound, TEXT("set-up: cover slots at A (both ends), B, D and H's front face"));
			if (!bFound)
			{
				return Finish(State, false);
			}
			Op->OrderTakeCover(State.SlotAEnd, false);
			Next(State, 2);
			return true;
		}
		case 2:
		case 6:
		case 10:
		{
			// The leader settled in his first cover (Solo): group mode, the others come up in the formation.
			const bool bSettled = Op->bInCover && !Op->IsMoving() && Op->GetVelocity().Size2D() < 5.f && Op->GetCoverEntryMeshOffset().Size() < 1.f;
			if ((!bSettled || State.StageTime < 1.5f) && State.StageTime < 15.f)
			{
				return true;
			}
			Check(State, bSettled, FString::Printf(TEXT("stage %d: the leader in cover at his first wall"), State.Stage));
			Squad->ExitSoloMode(false, 0.f);
			Next(State, State.Stage + 1);
			return true;
		}
		case 3:
		case 7:
		{
			if (State.StageTime < 3.f)
			{
				return true;
			}
			const bool bToB = State.Stage == 3;
			const TArray<AOperativeCharacter*> Joining = Squad->GetGroupCoverMembers(Op);
			Check(State, Joining.Num() == 2 && !Squad->IsSoloMode(), FString::Printf(TEXT("%s: group mode, %d go along"), bToB ? TEXT("1 switch to B") : TEXT("2 switch to D"),
				Joining.Num()));
			PC->ShowCoverPreview(Op, bToB ? State.SlotB : State.SlotD);
			PC->ConfirmCoverPreview(true);
			Check(State, PC->WasLastCoverConfirmSwitch(), FString::Printf(TEXT("%s: the double-click is the cover switch with the group (%d at the wall, %d prone)"),
				bToB ? TEXT("1") : TEXT("2"), PC->GetLastGroupCoverSlotCount(), PC->GetLastGroupCoverProneCount()));
			State.SceneWall = bToB ? State.Boxes[1].Get() : State.Boxes[2].Get();
			State.StableSince = -1.f;
			State.bLeaderArrived = false;
			Next(State, State.Stage + 1);
			return true;
		}
		case 4:
		case 8:
		case 12:
		{
			if (!SceneSettled(State, Squad))
			{
				return true;
			}
			const TCHAR* Label = State.Stage == 4 ? TEXT("1 the corner switch to B") : State.Stage == 8 ? TEXT("2 the switch to the short wall D") : TEXT("3 the wrap round H");
			CheckScene(World, State, Squad, Label);
			if (State.Stage == 4 && State.bSectors)
			{
				Check(State, Squad->GetWallGroupReentryCount() >= 1, FString::Printf(TEXT("1: the followers' re-entry after the leader re-allocated (%d)"),
					Squad->GetWallGroupReentryCount()));
			}
			if (State.Stage == 8)
			{
				const bool bProne = Squad->HasGroupCoverProne(Member(State, 1), true) || Squad->HasGroupCoverProne(Member(State, 2), true);
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: 2: %s"), bProne ? TEXT("one lies prone behind the short wall") : TEXT("nobody prone (both fitted at the wall)"));
			}
			if (State.Stage == 12)
			{
				// The wrap's own end facing is CoverWrapSmoke's check; here only logged (the leader's look is checked above: unchanged since he arrived).
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: 3: the leader ends the wrap facing %s (his plan: %s)"),
					Op->CoverFacing == ECoverFacing::Left ? TEXT("left") : TEXT("right"), State.WrapGoalFacing == ECoverFacing::Left ? TEXT("left") : TEXT("right"));
			}
			if (State.bShot)
			{
				const FVector Centre = (Member(State, 0)->GetActorLocation() + Member(State, 1)->GetActorLocation() + Member(State, 2)->GetActorLocation()) / 3.f;
				const FVector Out = State.Stage == 12 ? State.R : -State.F;
				PlaceCamera(World, State, Centre + Out * 650.f + FVector(0.f, 0.f, 900.f), Centre + Out * 150.f);
				Next(State, State.Stage + 1);
				return true;
			}
			Next(State, State.Stage == 12 ? 20 : State.Stage + 1);
			return true;
		}
		case 5:
		case 9:
		case 13:
		{
			// The still (shot mode only reaches here after a judged scene) - or straight on to the next scene.
			if (State.bShot)
			{
				if (State.StageTime < 2.f)
				{
					return true;
				}
				const int32 Wanted = State.Stage == 5 ? 1 : State.Stage == 9 ? 2 : 3;
				if (State.ShotsTaken < Wanted)
				{
					RequestShot(State, State.Stage == 5 ? TEXT("wallgroup_1_corner_switch") : State.Stage == 9 ? TEXT("wallgroup_2_short_wall") : TEXT("wallgroup_3_wrap"));
					return true;
				}
				if (State.StageTime < 3.f)
				{
					return true;
				}
				if (APlayerController* Player = UGameplayStatics::GetPlayerController(World, 0))
				{
					Player->SetViewTarget(Player->GetPawn());
				}
			}
			if (State.Stage == 13)
			{
				Next(State, 20); // rules v2.4: the wall END
				return true;
			}
			if (State.Stage == 5)
			{
				ResetSquad(Squad, State, FVector2D(0.f, -100.f));
				Op->OrderTakeCover(State.SlotAOther, false);
				Next(State, 6);
				return true;
			}
			ResetSquad(Squad, State, FVector2D(150.f, 650.f));
			Op->OrderTakeCover(State.SlotH, false);
			Next(State, 10);
			return true;
		}
		case 11:
		{
			if (State.StageTime < 3.f)
			{
				return true;
			}
			Check(State, Squad->GetGroupCoverMembers(Op).Num() == 2, TEXT("3: group mode, the two others go along"));
			PC->HandleWorldHit(GroundHit(World, State, -300.f, 910.f));
			Check(State, Op->IsCoverWrapping(), FString::Printf(TEXT("3: the ground click by H's side face is the wrap with the group (%d at the wall, %d prone)"),
				PC->GetLastGroupCoverSlotCount(), PC->GetLastGroupCoverProneCount()));
			State.WrapGoalFacing = Op->GetCoverWrapPlan().GoalFacing;
			State.SceneWall = State.Boxes[3].Get();
			State.StableSince = -1.f;
			State.bLeaderArrived = false;
			Next(State, 12);
			return true;
		}
		case 20:
		{
			// Scene 4 (v2.4): the wall END. Out of cover, Solo; the thin wall E goes up; the navmesh rebuilds round it.
			if (!State.bSectors)
			{
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: 4 the wall end: skipped with Codex.Formation.Sectors 0 (no wall layout)"));
				return Finish(State, true);
			}
			ResetSquad(Squad, State, FVector2D(200.f, WallER));
			SpawnBox(World, State, 0.5f * (WallEFrom + WallEEnd), WallER, WallEEnd - WallEFrom, WallEThickness, 300.f);
			Next(State, 21);
			return true;
		}
		case 21:
		{
			if (State.StageTime < 2.5f)
			{
				return true;
			}
			const bool bFound = FindSlot(World, State, WallEEnd, WallER, -State.F, State.SlotEEnd);
			Check(State, bFound, TEXT("4: a cover slot at the thin wall E's end face"));
			if (!bFound)
			{
				return Finish(State, false);
			}
			Squad->ExitSoloMode(false, 0.f); // group mode: the two others come up in the formation
			Next(State, 22);
			return true;
		}
		case 22:
		{
			if (State.StageTime < 3.f)
			{
				return true;
			}
			PC->ShowCoverPreview(Op, State.SlotEEnd);
			PC->ConfirmCoverPreview(false);
			Check(State, Squad->GetFormationKind() == EFormationKind::Wall && !PC->WasLastCoverConfirmSwitch(),
				TEXT("4: the click on the end face's cover is the group cover order = the formation wall layout"));
			State.StableSince = -1.f;
			Next(State, 23);
			return true;
		}
		case 23:
		{
			bool bAll = true;
			for (int32 Index = 0; Index < 3; ++Index)
			{
				const AOperativeCharacter* Each = Member(State, Index);
				bAll &= Each && Each->IsInFormationWallPose() && !Each->IsMoving() && Each->GetVelocity().Size2D() < 5.f;
			}
			if (!bAll)
			{
				State.StableSince = -1.f;
				if (State.StageTime < SceneCapSeconds)
				{
					return true;
				}
			}
			else if (State.StableSince < 0.f)
			{
				State.StableSince = State.StageTime;
			}
			if (bAll && State.StageTime - State.StableSince < EndSettleSeconds)
			{
				return true;
			}
			Check(State, bAll, FString::Printf(TEXT("4: all three crouched in the wall layout at E's end after %.1f s"), State.StageTime));
			float BasePct = 0.f;
			float SpreadPct = 0.f;
			const bool bSpread = Squad->IsFormationWallEndSpread(BasePct, SpreadPct);
			Check(State, bSpread && BasePct < 80.f && SpreadPct >= 95.f, FString::Printf(TEXT("4: the layout knows it stands at a wall END - the face looks would hold %.0f %% "
				"of the open arc, the spread looks %.0f %%"), BasePct, SpreadPct));
			// The judged geometry: the world's box walls round the squad (E, A, D, ...) plus E itself (exact).
			TArray<FFormationWall> Walls = Squad->GetFormationWorldWalls();
			FFormationWall WallE;
			WallE.A = FVector2D(Local(State, WallEFrom, WallER));
			WallE.B = FVector2D(Local(State, WallEEnd, WallER));
			WallE.Thickness = WallEThickness;
			Walls.Add(WallE);
			TArray<FVector2D> Positions;
			TArray<float> Looks;
			TArray<FVector> Feet;
			FString Line;
			State.HeldLooks.Reset();
			State.HeldPlaces.Reset();
			for (int32 Index = 0; Index < 3; ++Index)
			{
				const AOperativeCharacter* Each = Member(State, Index);
				Positions.Add(FVector2D(Each->GetActorLocation()));
				Looks.Add(static_cast<float>(Each->GetSightFacing().Rotation().Yaw));
				Feet.Add(Each->GetActorLocation() - FVector(0.f, 0.f, Each->GetSimpleCollisionHalfHeight()));
				Line += FString::Printf(TEXT(" [%s look %.0f at (%.0f, %.0f)]"), *Each->DisplayName.ToString(), Looks.Last(), Positions.Last().X, Positions.Last().Y);
			}
			State.HeldLooks = Looks;
			State.HeldPlaces = Positions;
			const FFormationConfig& Config = FormationTuning::Get();
			const FFormationArcCoverage Arc = FormationWallRules::OpenArcCoverage(Walls, Positions, Looks, Config.SightFov, Config.WallEndProbeDist);
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: 4 the wall end:%s; open arc held %.1f %% (%d / %d probe points), %d walls"), *Line, Arc.Pct, Arc.Covered,
				Arc.Open, Walls.Num());
			Check(State, Arc.Pct >= 95.0, FString::Printf(TEXT("4: the open arc round the end is held (%.1f %% >= 95 %%)"), Arc.Pct));
			for (const float Side : { 1.f, -1.f })
			{
				// 5 m back along each face, 1.5 m off it: inside the cone of somebody on that side with a clear line to it.
				const FVector2D Point(Local(State, WallEEnd - 500.f, WallER + Side * 150.f));
				bool bHeld = false;
				for (int32 Index = 0; Index < 3; ++Index)
				{
					bHeld |= !FormationMath::SegmentBlocked(Positions[Index], Point, Walls, 1.0)
						&& FormationMath::AngleBetween(Looks[Index], FormationMath::YawOf(Point - Positions[Index])) <= 0.5f * Fov(Member(State, Index));
				}
				Check(State, bHeld, FString::Printf(TEXT("4: the flank along E's %s face (5 m back) is inside a cone"), Side > 0.f ? TEXT("right") : TEXT("left")));
			}
			int32 Stares = 0;
			for (int32 Index = 0; Index < 3; ++Index)
			{
				Stares += StaresAtWall(World, State, Feet[Index], Looks[Index]) ? 1 : 0;
			}
			Check(State, Stares == 0, FString::Printf(TEXT("4: nobody faces a wall within 200 cm (%d)"), Stares));
			State.WorstLookDrift = 0.f;
			State.WorstPlaceDrift = 0.f;
			Next(State, 24);
			return true;
		}
		case 24:
		{
			// Stable: nobody's look or place moves while they hold (no flicker, no reshuffle).
			for (int32 Index = 0; Index < 3; ++Index)
			{
				const AOperativeCharacter* Each = Member(State, Index);
				State.WorstLookDrift = FMath::Max(State.WorstLookDrift, FormationMath::AngleBetween(static_cast<float>(Each->GetSightFacing().Rotation().Yaw), State.HeldLooks[Index]));
				State.WorstPlaceDrift = FMath::Max(State.WorstPlaceDrift, static_cast<float>(FVector2D::Distance(FVector2D(Each->GetActorLocation()), State.HeldPlaces[Index])));
			}
			if (State.StageTime < EndHoldSeconds)
			{
				return true;
			}
			float BasePct = 0.f;
			float SpreadPct = 0.f;
			Check(State, State.WorstLookDrift <= 10.f && State.WorstPlaceDrift <= 30.f && Squad->IsFormationWallEndSpread(BasePct, SpreadPct)
				&& Squad->GetFormationKind() == EFormationKind::Wall, FString::Printf(TEXT("4: steady for %.0f s - looks moved at most %.1f deg, places %.0f cm, the layout kept"),
				EndHoldSeconds, State.WorstLookDrift, State.WorstPlaceDrift));
			if (State.bShot)
			{
				const FVector Centre = (Member(State, 0)->GetActorLocation() + Member(State, 1)->GetActorLocation() + Member(State, 2)->GetActorLocation()) / 3.f;
				PlaceCamera(World, State, Centre - State.F * 650.f + FVector(0.f, 0.f, 1000.f), Centre - State.F * 100.f);
			}
			Next(State, 25);
			return true;
		}
		case 25:
		{
			if (State.bShot)
			{
				if (State.StageTime < 2.f)
				{
					return true;
				}
				if (State.ShotsTaken < 4)
				{
					RequestShot(State, TEXT("wallgroup_4_wall_end"));
					return true;
				}
				if (State.StageTime < 3.f)
				{
					return true;
				}
			}
			Next(State, 30); // rules v2.5: the covered approach
			return true;
		}
		case 30:
		{
			// Scene 5 (v2.5): patrol across the arena towards the long face of wall E (scene 4's), then the order to hug it (~9 m in).
			ResetSquad(Squad, State, FVector2D(-200.f, 450.f));
			Squad->ExitSoloMode(false, 0.f);
			Next(State, 31);
			return true;
		}
		case 31:
		{
			if (State.StageTime < 2.5f)
			{
				return true;
			}
			const bool bFound = FindSlot(World, State, 0.5f * (WallEFrom + WallEEnd), WallER + 0.5f * WallEThickness, -State.R, State.SlotAMid);
			Check(State, bFound, TEXT("5: a cover slot in the middle of wall E's long face"));
			if (!bFound)
			{
				return Finish(State, false);
			}
			Op->OrderMoveTo(Local(State, -250.f, -300.f, Op->GetSimpleCollisionHalfHeight()), false); // the patrol walk towards E
			Next(State, 32);
			return true;
		}
		case 32:
		{
			if (FVector::DotProduct(Op->GetActorLocation() - State.P, State.R) > 200.f && State.StageTime < 15.f) // on patrol (2.5 m walked): the order comes while he walks
			{
				return true;
			}
			PC->ShowCoverPreview(Op, State.SlotAMid);
			PC->ConfirmCoverPreview(false);
			Check(State, Squad->GetFormationKind() == EFormationKind::Wall, TEXT("5: on patrol, the click on E's cover is the formation wall layout"));
			State.bApproachSeen = false;
			State.ApproachTime = 0.f;
			State.RearMissTime = 0.f;
			State.FlankTime = 0.f;
			State.JudgedTime = 0.f;
			State.LastGameTime = -1.0;
			State.BestAlongDeg = 180.f;
			State.FirstRearMiss.Reset();
			Next(State, 33);
			return true;
		}
		case 33:
		{
			const TArray<FFormationApproachLook>& Looks = Squad->GetFormationApproachLooks();
			const float NormalYaw = static_cast<float>(State.R.Rotation().Yaw); // E's long face's outward normal: back the way they came
			// The approach = from the order until all three are within approach.arrive_dist of a wall slot (judged the same with the rule off).
			const TArray<FFormationSlot>& WallSlotsNow = Squad->GetFormationWallSlots();
			bool bAllIn = WallSlotsNow.Num() > 0;
			for (int32 Index = 0; Index < 3 && bAllIn; ++Index)
			{
				const FVector2D Here(Member(State, Index)->GetActorLocation());
				bool bNear = false;
				for (const FFormationSlot& Slot : WallSlotsNow)
				{
					bNear |= FVector2D::Distance(Slot.Position, Here) <= FormationTuning::Get().ApproachArriveDist;
				}
				bAllIn = bNear;
			}
			if (!bAllIn && State.StageTime < SceneCapSeconds)
			{
				State.bApproachSeen = true;
				const double Now = World->GetTimeSeconds();
				const float Dt = State.LastGameTime < 0.0 ? 0.f : static_cast<float>(Now - State.LastGameTime);
				State.LastGameTime = Now;
				State.ApproachTime += Dt;
				const float Lim = 0.5f * FormationTuning::Get().SightFov - 30.f; // the rear sector +-30 inside a cone (check 2's margin)
				bool bRear = false;
				bool bFlank = false;
				FString Line;
				for (int32 Index = 0; Index < 3; ++Index)
				{
					const float Look = static_cast<float>(Member(State, Index)->GetSightFacing().Rotation().Yaw);
					bRear |= FormationMath::AngleBetween(Look, NormalYaw) <= Lim;
					bFlank |= FormationMath::AngleBetween(Look, NormalYaw + 90.f) <= Lim || FormationMath::AngleBetween(Look, NormalYaw - 90.f) <= Lim;
					Line += FString::Printf(TEXT(" %s %.0f"), *Member(State, Index)->DisplayName.ToString(), Look);
				}
				if (State.ApproachTime >= ApproachTurnSeconds)
				{
					State.JudgedTime += Dt;
					State.FlankTime += bFlank ? Dt : 0.f;
					if (!bRear)
					{
						State.RearMissTime += Dt;
						if (State.FirstRearMiss.IsEmpty())
						{
							State.FirstRearMiss = FString::Printf(TEXT("t=%.2f s:%s"), State.ApproachTime, *Line);
						}
					}
				}
				for (const FFormationApproachLook& Look : Looks)
				{
					if (Look.Role == EFormationRole::Commander && Look.Duty == EFormationApproachDuty::Along && Look.Yaw.IsSet())
					{
						State.BestAlongDeg = FMath::Min(State.BestAlongDeg, FormationMath::AngleBetween(static_cast<float>(Op->GetSightFacing().Rotation().Yaw), Look.Yaw.GetValue()));
					}
				}
				return true;
			}
			if (!State.bApproachSeen && State.StageTime < 3.f)
			{
				return true; // the approach begins with the next formation frame
			}
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: 5 the covered approach: %.1f s, rear missed %.2f s%s, a flank held %.0f %%, the leader's best look along the wall %.0f deg off"),
				State.ApproachTime, State.RearMissTime, State.FirstRearMiss.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" (first %s)"), *State.FirstRearMiss),
				State.JudgedTime > 0.f ? 100.f * State.FlankTime / State.JudgedTime : 0.f, State.BestAlongDeg);
			Check(State, State.bApproachSeen && State.ApproachTime > 1.f, FString::Printf(TEXT("5: the squad walked a covered approach (%.1f s game time, %.1f s judged)"), State.ApproachTime, State.JudgedTime));
			Check(State, State.RearMissTime <= 0.f, FString::Printf(TEXT("5: somebody held the rear all the way in (missed %.2f s)"), State.RearMissTime));
			Check(State, State.JudgedTime > 0.5f && State.FlankTime >= 0.9f * State.JudgedTime,
				FString::Printf(TEXT("5: somebody held a flank >= 90 %% of the way (%.0f %%)"), State.JudgedTime > 0.f ? 100.f * State.FlankTime / State.JudgedTime : 0.f));
			Check(State, State.BestAlongDeg <= 30.f, FString::Printf(TEXT("5: the leader, first at the wall, looked along it (within %.0f deg of his along look)"), State.BestAlongDeg));
			State.StableSince = -1.f;
			Next(State, 34);
			return true;
		}
		case 34:
		{
			// Then the plain wall layout: everyone crouched at the wall facing his layout look.
			bool bAll = true;
			for (int32 Index = 0; Index < 3; ++Index)
			{
				const AOperativeCharacter* Each = Member(State, Index);
				bAll &= Each && Each->IsInFormationWallPose() && !Each->IsMoving();
			}
			if (!bAll)
			{
				State.StableSince = -1.f;
				if (State.StageTime < SceneCapSeconds)
				{
					return true;
				}
			}
			else if (State.StableSince < 0.f)
			{
				State.StableSince = State.StageTime;
			}
			if (bAll && State.StageTime - State.StableSince < 1.5f)
			{
				return true;
			}
			const TArray<FFormationSlot>& Slots = Squad->GetFormationWallSlots();
			float Worst = 0.f;
			FString Line;
			for (int32 Index = 0; Index < 3; ++Index)
			{
				const AOperativeCharacter* Each = Member(State, Index);
				const FVector2D Here(Each->GetActorLocation());
				const FFormationSlot* Nearest = nullptr;
				for (const FFormationSlot& Slot : Slots)
				{
					if (!Nearest || FVector2D::Distance(Slot.Position, Here) < FVector2D::Distance(Nearest->Position, Here))
					{
						Nearest = &Slot;
					}
				}
				const float Look = static_cast<float>(Each->GetSightFacing().Rotation().Yaw);
				const float Off = Nearest ? FormationMath::AngleBetween(Look, Nearest->Yaw) : 180.f;
				Worst = FMath::Max(Worst, Off);
				Line += FString::Printf(TEXT(" [%s look %.0f, layout %.0f]"), *Each->DisplayName.ToString(), Look, Nearest ? Nearest->Yaw : 0.f);
			}
			Check(State, bAll && Squad->GetFormationApproachLooks().Num() == 0 && Worst <= 20.f,
				FString::Printf(TEXT("5: then the normal wall layout - everyone crouched facing his layout look (worst %.0f deg):%s"), Worst, *Line));
			return Finish(State, true);
		}
		case 99:
			return Finish(State, true);
		default:
			return Finish(State, false);
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		TSharedRef<FState> State = MakeShared<FState>();
		State->bShot = Args.ContainsByPredicate([](const FString& Arg) { return Arg.Equals(TEXT("Shot"), ESearchCase::IgnoreCase); });
		TWeakObjectPtr<UWorld> WeakWorld(World);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.FormationWallGroupSmoke"),
		TEXT("Dev check (rules v2.2 wall group): after a cover switch / a wrap with the group the leader keeps his corner, the others close the open arc; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
