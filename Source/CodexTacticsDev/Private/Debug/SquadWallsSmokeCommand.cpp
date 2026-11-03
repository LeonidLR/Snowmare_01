// Dev-only headless checks of the squad at walls - the game designer's bug report 2026-11-02 (L_Gen_Depot_1; docs/systems/squad-and-orders.md
// "The squad at walls (2026-11-03)"), on L_MovementTest's own arena (SmokeUtils::FindClearArena; the walls are spawned cubes, nothing saved):
//   CodexTactics.WallSectorsSmoke       item 5: the squad (3, group mode, Codex.Formation.Sectors 1) ordered along a SHORT wall with two free ends
//                                        (2.4 m), then along a 2.4 m segment between two 4 m gaps (collinear walls): K looks out, BOTH end men look
//                                        round THEIR corners (mirrored, within 15 deg), no member keeps a duplicated sector while an open one is
//                                        unwatched (FormationWallSectorRules::CountRedundantWhileOpen on the spawned walls), every body turned to his
//                                        look; held 1 s.
//   CodexTactics.GroupCoverNearbySmoke  item 3: the cover-slot group order (Codex.Formation.Sectors 0, as GroupCoverSmoke B) at a 1 m low cube with
//                                        free low cubes 3.2 m left and right: the leader takes his cube, the two followers the side cubes (one each,
//                                        nobody prone); then the side cubes removed: both prone behind the leader's cube (the fallback kept).
//   powershell -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -Commands WallSectorsSmoke,GroupCoverNearbySmoke
//   rendered stills ("Shot" argument; -RenderOffscreen, vision cones on): Saved/Screenshots/SquadWalls/<scenario>.png, a camera above the squad.
// Godot reference: none (UE-only).

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
#include "Formation/FormationRuntimeRules.h"
#include "Formation/FormationTuning.h"
#include "Formation/FormationWallSectorRules.h"
#include "GameFramework/HUD.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "Tactics/CoverTraceRules.h"
#include "UnrealClient.h"

namespace SquadWallsSmoke
{
	constexpr float StepSeconds = 0.05f;
	/** The walls' near face this far ahead of the arena origin; 40 cm thick, 3 m high (item 5); the low cubes 1 m (item 3). */
	constexpr float WallNearF = 400.f;
	constexpr float WallThickness = 40.f;
	constexpr float ShortWallLength = 240.f;
	constexpr float GapCm = 400.f;
	constexpr float SideWallLength = 800.f;
	constexpr float CubeCm = 100.f;
	constexpr float CubeSideR = 320.f;
	/** A body counts as turned to his look within this, deg; the two end men's looks mirror each other within this, deg. */
	constexpr float LookToleranceDeg = 30.f;
	constexpr float MirrorToleranceDeg = 15.f;

	enum class EScenario : uint8
	{
		WallSectors,
		GroupCoverNearby
	};

	struct FState
	{
		EScenario Scenario = EScenario::WallSectors;
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		bool bShot = false;
		FVector P = FVector::ZeroVector;
		FVector F = FVector::ForwardVector;
		FVector R = FVector::RightVector;
		float GroundZ = 0.f;
		TArray<TWeakObjectPtr<AOperativeCharacter>> Members;
		TArray<TWeakObjectPtr<AStaticMeshActor>> Blocks;
		/** The spawned walls as the formation rules see them (item 5's coverage check). */
		TArray<FFormationWall> RuleWalls;
		float HoldTime = 0.f;
		float ShotStart = -1.f;
		bool bShotTaken = false;
		TWeakObjectPtr<ACameraActor> Camera;
		int32 SavedSectors = -1;
	};

	const TCHAR* ScenarioName(EScenario Scenario)
	{
		return Scenario == EScenario::WallSectors ? TEXT("WallSectorsSmoke") : TEXT("GroupCoverNearbySmoke");
	}

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	void DestroyBlocks(FState& State)
	{
		for (const TWeakObjectPtr<AStaticMeshActor>& Block : State.Blocks)
		{
			if (Block.IsValid())
			{
				Block->Destroy();
			}
		}
		State.Blocks.Reset();
		State.RuleWalls.Reset();
	}

	bool Finish(FState& State, bool bComplete)
	{
		if (State.SavedSectors >= 0)
		{
			if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Formation.Sectors")))
			{
				CVar->Set(State.SavedSectors, ECVF_SetByConsole);
			}
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		DestroyBlocks(State);
		FPlatformMisc::RequestExit(false, ScenarioName(State.Scenario));
		return false;
	}

	void Next(FState& State, int32 Stage)
	{
		State.Stage = Stage;
		State.StageTime = 0.f;
		State.HoldTime = 0.f;
		State.ShotStart = -1.f;
		State.bShotTaken = false;
	}

	AOperativeCharacter* Member(const FState& State, int32 Index)
	{
		return State.Members.IsValidIndex(Index) ? State.Members[Index].Get() : nullptr;
	}

	FVector Local(const FState& State, float AlongF, float AlongR, float Up = 0.f)
	{
		return State.P + State.F * AlongF + State.R * AlongR + FVector(0.f, 0.f, State.GroundZ - State.P.Z + Up);
	}

	/** A box: centre (F, R), size along F / along R, HeightCm high; with bRuleWall its centre line along R joins RuleWalls. */
	void SpawnBlock(UWorld* World, FState& State, float CentreF, float CentreR, float SizeF, float SizeR, float HeightCm, bool bRuleWall)
	{
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		const FTransform Transform(State.F.Rotation(), Local(State, CentreF, CentreR, 0.5f * HeightCm), FVector(SizeF / 100.f, SizeR / 100.f, HeightCm / 100.f));
		AStaticMeshActor* Block = Cube ? World->SpawnActorDeferred<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Transform, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn) : nullptr;
		if (!Block)
		{
			return;
		}
		Block->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
		Block->GetStaticMeshComponent()->SetStaticMesh(Cube);
		Block->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
		Block->GetStaticMeshComponent()->SetCanEverAffectNavigation(true);
		Block->FinishSpawning(Transform);
		State.Blocks.Add(Block);
		if (bRuleWall)
		{
			FFormationWall Wall;
			Wall.A = FVector2D(Local(State, CentreF, CentreR - 0.5f * SizeR));
			Wall.B = FVector2D(Local(State, CentreF, CentreR + 0.5f * SizeR));
			Wall.Thickness = SizeF;
			State.RuleWalls.Add(Wall);
		}
	}

	/** The squad back to the start: standing 3-4 m in front of the walls, the first member leading in group mode. */
	void ResetSquad(USquadSubsystem* Squad, FState& State)
	{
		const float Places[3][2] = { { 0.f, 0.f }, { -150.f, -150.f }, { -150.f, 150.f } };
		for (int32 Index = 0; Index < State.Members.Num(); ++Index)
		{
			AOperativeCharacter* Each = Member(State, Index);
			if (!Each)
			{
				continue;
			}
			Each->LeaveCover(TEXT("smoke reset"));
			Each->ClearPendingCover();
			Each->StopOperative();
			Each->SetStance(EOperativeStance::Standing);
			Each->TeleportTo(Local(State, Places[Index][0], Places[Index][1], Each->GetSimpleCollisionHalfHeight() + 5.f), State.F.Rotation(), false, true);
		}
		Squad->ExitSoloMode(false, 0.f);
		Squad->SetLeader(Member(State, 0));
		Squad->ResetFormationState(State.F);
	}

	bool SetUp(UWorld* World, FState& State, USquadSubsystem* Squad, ACodexTacticsPlayerController* PC)
	{
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
			FBox2D(FVector2D(-450.f, -1300.f), FVector2D(1100.f, 1300.f)), Ignore);
		if (!Arena.bFound)
		{
			Check(State, false, FString::Printf(TEXT("an empty, flat, navigable 15.5 x 26 m spot for the smoke's own set-up (%d tried)"), Arena.Tried));
			return false;
		}
		State.P = Arena.Origin;
		State.F = Arena.Forward;
		State.R = Arena.Right;
		State.GroundZ = Arena.Origin.Z;
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke: arena at %s"), *Arena.Origin.ToCompactString());
		if (State.bShot)
		{
			UVisionConeComponent::SetPlayerSquadCones(true);
		}
		return true;
	}

	/**
	 * Shot runs: a still from straight above the squad (FormationWallHugSmoke's camera: no time dilation - it smeared the snow - and no motion
	 * blur), the walls' forward up. The camera first, the picture 1.5 s later. True while it is being taken.
	 */
	bool TakeShot(UWorld* World, FState& State, const TCHAR* Name)
	{
		if (!State.bShot || State.bShotTaken)
		{
			return false;
		}
		if (State.ShotStart < 0.f)
		{
			FVector Centre = FVector::ZeroVector;
			for (const TWeakObjectPtr<AOperativeCharacter>& Weak : State.Members)
			{
				Centre += Weak.IsValid() ? Weak->GetActorLocation() / 3.0 : FVector::ZeroVector;
			}
			const FVector Eye = Centre + FVector(0.f, 0.f, 1300.f);
			const FRotator Down(-90.f, static_cast<float>(State.F.Rotation().Yaw), 0.f);
			if (!State.Camera.IsValid())
			{
				State.Camera = World->SpawnActor<ACameraActor>(Eye, Down);
				if (ACameraActor* Created = State.Camera.Get())
				{
					Created->GetCameraComponent()->SetFieldOfView(50.f);
					Created->GetCameraComponent()->bConstrainAspectRatio = false;
					Created->GetCameraComponent()->PostProcessSettings.bOverride_MotionBlurAmount = true;
					Created->GetCameraComponent()->PostProcessSettings.MotionBlurAmount = 0.f;
				}
			}
			if (ACameraActor* Cam = State.Camera.Get())
			{
				Cam->SetActorLocationAndRotation(Eye, Down);
				if (APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0))
				{
					PC->SetViewTarget(Cam);
					if (AHUD* Hud = PC->GetHUD())
					{
						Hud->bShowHUD = false;
					}
				}
			}
			State.ShotStart = State.StageTime;
			return true;
		}
		if (State.StageTime - State.ShotStart < 1.5f)
		{
			return true;
		}
		const FString Path = FPaths::ProjectSavedDir() / TEXT("Screenshots") / TEXT("SquadWalls") / FString::Printf(TEXT("%s.png"), Name);
		FScreenshotRequest::RequestScreenshot(Path, /*bShowUI*/ false, /*bAddFilenameSuffix*/ false);
		UE_LOG(LogCodexTactics, Display, TEXT("%s: screenshot %s"), ScenarioName(State.Scenario), *Path);
		State.bShotTaken = true;
		return true;
	}

	/** After the still (a second later): the view back to the player's pawn. */
	void ReleaseShot(UWorld* World)
	{
		if (APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0))
		{
			PC->SetViewTarget(PC->GetPawn());
		}
	}

	// ---------------------------------------------------------------------------------------------------------------------------------
	// Item 5: both ends of a short wall watched.

	/** Everyone crouched in the wall layout's pose on his slot, still. */
	bool InWallLayout(const USquadSubsystem* Squad, const FState& State)
	{
		bool bAll = Squad->HasFormationWallOrder() && Squad->GetFormationKind() == EFormationKind::Wall;
		for (const TWeakObjectPtr<AOperativeCharacter>& Weak : State.Members)
		{
			const AOperativeCharacter* Each = Weak.Get();
			FVector Slot;
			float Look = 0.f;
			bAll &= Each && Squad->GetFormationTarget(Each, Slot, Look) && Each->IsInFormationWallPose() && FVector::Dist2D(Slot, Each->GetActorLocation()) <= 60.f
				&& Each->GetVelocity().Size2D() < 5.f;
		}
		return bAll;
	}

	void CheckSectors(FState& State, const USquadSubsystem* Squad, const TCHAR* Label)
	{
		const TArray<FFormationSlot>& Slots = Squad->GetFormationWallSlots();
		float Normal = 0.f;
		const bool bNormal = Squad->GetFormationWallNormalYaw(Normal);
		Check(State, Slots.Num() == 3 && bNormal, FString::Printf(TEXT("%s: the wall layout of three in force (%d slots)"), Label, Slots.Num()));
		if (Slots.Num() != 3 || !bNormal)
		{
			return;
		}
		// The end men along the wall line (FormationWallSectorRules' frame: a look turned to Normal + 90 has a positive offset).
		const FVector2D Along = FormationMath::Forward(Normal + 90.f);
		int32 Low = 0;
		int32 High = 0;
		for (int32 Index = 1; Index < Slots.Num(); ++Index)
		{
			Low = FVector2D::DotProduct(Slots[Index].Position, Along) < FVector2D::DotProduct(Slots[Low].Position, Along) ? Index : Low;
			High = FVector2D::DotProduct(Slots[Index].Position, Along) > FVector2D::DotProduct(Slots[High].Position, Along) ? Index : High;
		}
		const auto Rel = [Normal](const FFormationSlot& Each) { return static_cast<float>(FormationMath::NormYaw(static_cast<double>(Each.Yaw) - Normal)); };
		FString Line;
		for (const FFormationSlot& Each : Slots)
		{
			Line += FString::Printf(TEXT(" %s %.0f (%+.0f off the normal);"), FormationNames::ToString(Each.Role), Each.Yaw, Rel(Each));
		}
		Check(State, FMath::Abs(Rel(Slots[0])) <= 1.f, FString::Printf(TEXT("%s: K looks out (away from the wall):%s"), Label, *Line));
		Check(State, Rel(Slots[Low]) < -90.f, FString::Printf(TEXT("%s: the %s end man (%s) looks round HIS corner (%+.0f, beyond -90)"), Label, TEXT("one"),
			FormationNames::ToString(Slots[Low].Role), Rel(Slots[Low])));
		Check(State, Rel(Slots[High]) > 90.f, FString::Printf(TEXT("%s: the %s end man (%s) looks round HIS corner (%+.0f, beyond +90)"), Label, TEXT("other"),
			FormationNames::ToString(Slots[High].Role), Rel(Slots[High])));
		Check(State, FMath::Abs(FMath::Abs(Rel(Slots[Low])) - FMath::Abs(Rel(Slots[High]))) <= MirrorToleranceDeg,
			FString::Printf(TEXT("%s: both ends alike - mirrored (%.0f vs %.0f off the normal)"), Label, FMath::Abs(Rel(Slots[Low])), FMath::Abs(Rel(Slots[High]))));
		const int32 Redundant = FormationWallSectorRules::CountRedundantWhileOpen(State.RuleWalls, Slots, FormationTuning::Get());
		const FFormationArcCoverage Coverage = FormationWallSectorRules::RingCoverage(State.RuleWalls, Slots, FormationTuning::Get());
		Check(State, Redundant == 0, FString::Printf(TEXT("%s: no duplicated sector while an open one is unwatched (%d; the near + far rings %.0f %% held, %d open points unwatched)"),
			Label, Redundant, Coverage.Pct, Coverage.Open - Coverage.Covered));
		FString Bodies;
		bool bTurned = true;
		for (const TWeakObjectPtr<AOperativeCharacter>& Weak : State.Members)
		{
			const AOperativeCharacter* Each = Weak.Get();
			FVector Slot;
			float Look = 0.f;
			if (Each && Squad->GetFormationTarget(Each, Slot, Look))
			{
				const float Off = FormationMath::AngleBetween(static_cast<float>(Each->GetSightFacing().Rotation().Yaw), Look);
				bTurned &= Off <= LookToleranceDeg;
				Bodies += FString::Printf(TEXT(" %s faces %.0f, his look %.0f;"), *Each->DisplayName.ToString(), Each->GetSightFacing().Rotation().Yaw, Look);
			}
		}
		Check(State, bTurned, FString::Printf(TEXT("%s: every body turned to his look (within %.0f deg):%s"), Label, LookToleranceDeg, *Bodies));
	}

	bool OrderAlongShortWall(UWorld* World, FState& State, USquadSubsystem* Squad, ACodexTacticsPlayerController* PC, const TCHAR* Label)
	{
		FCoverSlot Slot;
		const bool bSlot = CoverTraceRules::FindCoverSlotAt(World, Local(State, WallNearF, 0.f, 90.f), State.F, Slot);
		AOperativeCharacter* Leader = Squad->GetLeader();
		const bool bOrdered = bSlot && Leader && PC->OrderGroupCover(Leader, Slot, false, 1200.f, false);
		Check(State, bOrdered && Squad->HasFormationWallOrder(), FString::Printf(TEXT("%s: the group cover order at the short wall's middle is the formation wall layout"), Label));
		return bOrdered;
	}

	bool StepWallSectors(UWorld* World, FState& State, USquadSubsystem* Squad, ACodexTacticsPlayerController* PC)
	{
		const bool bGaps = State.Stage >= 10;
		const TCHAR* Label = bGaps ? TEXT("segment between two gaps") : TEXT("short wall");
		switch (State.Stage)
		{
		case 1:
		case 11:
			// The walls: the short one (R -120..120) - and with the gaps the two collinear 8 m walls 4 m off its ends.
			ResetSquad(Squad, State);
			SpawnBlock(World, State, WallNearF + 0.5f * WallThickness, 0.f, WallThickness, ShortWallLength, 300.f, true);
			if (bGaps)
			{
				const float SideCentre = 0.5f * ShortWallLength + GapCm + 0.5f * SideWallLength;
				SpawnBlock(World, State, WallNearF + 0.5f * WallThickness, -SideCentre, WallThickness, SideWallLength, 300.f, true);
				SpawnBlock(World, State, WallNearF + 0.5f * WallThickness, SideCentre, WallThickness, SideWallLength, 300.f, true);
			}
			Next(State, State.Stage + 1);
			return true;
		case 2:
		case 12:
			if (State.StageTime < 2.f)
			{
				return true; // the nav mesh round the new walls
			}
			if (!OrderAlongShortWall(World, State, Squad, PC, Label))
			{
				return Finish(State, false);
			}
			Next(State, State.Stage + 1);
			return true;
		case 3:
		case 13:
		{
			State.HoldTime = InWallLayout(Squad, State) ? State.HoldTime + StepSeconds : 0.f;
			if (State.HoldTime < 1.f && State.StageTime < 25.f)
			{
				return true;
			}
			float BasePct = 0.f;
			float Pct = 0.f;
			const bool bBalanced = Squad->IsFormationWallSectorsBalanced(BasePct, Pct);
			Check(State, State.HoldTime >= 1.f, FString::Printf(TEXT("%s: the squad crouched along it in the wall layout (%.1f s)"), Label, State.StageTime));
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: %s: the wall sectors balanced %d (%.0f %% -> %.0f %%)"), Label, bBalanced ? 1 : 0, BasePct, Pct);
			CheckSectors(State, Squad, Label);
			Next(State, State.Stage + 1);
			return true;
		}
		case 4:
		case 14:
			if (TakeShot(World, State, bGaps ? TEXT("wall_sectors_gap_segment") : TEXT("wall_sectors_short_wall")) && !State.bShotTaken)
			{
				return true;
			}
			if (State.bShot && State.StageTime - State.ShotStart < 2.5f)
			{
				return true;
			}
			if (State.bShot)
			{
				ReleaseShot(World);
			}
			DestroyBlocks(State);
			if (bGaps)
			{
				return Finish(State, true);
			}
			Next(State, 11);
			return true;
		default:
			return Finish(State, false);
		}
	}

	// ---------------------------------------------------------------------------------------------------------------------------------
	// Item 3: followers into nearby covers, prone only without one.

	bool StepGroupCoverNearby(UWorld* World, FState& State, USquadSubsystem* Squad, ACodexTacticsPlayerController* PC)
	{
		const bool bAlone = State.Stage >= 10;
		const TCHAR* Label = bAlone ? TEXT("one cube") : TEXT("three cubes");
		AOperativeCharacter* A = Member(State, 0);
		switch (State.Stage)
		{
		case 1:
		case 11:
		{
			ResetSquad(Squad, State);
			// The leader's low cube in front (its near face at WallNearF), and - not alone - two free ones 3.2 m to each side.
			SpawnBlock(World, State, WallNearF + 0.5f * CubeCm, 0.f, CubeCm, CubeCm, CubeCm, false);
			if (!bAlone)
			{
				SpawnBlock(World, State, WallNearF + 0.5f * CubeCm, -CubeSideR, CubeCm, CubeCm, CubeCm, false);
				SpawnBlock(World, State, WallNearF + 0.5f * CubeCm, CubeSideR, CubeCm, CubeCm, CubeCm, false);
			}
			Next(State, State.Stage + 1);
			return true;
		}
		case 2:
		case 12:
		{
			if (State.StageTime < 2.f)
			{
				return true;
			}
			FCoverSlot Slot;
			if (!CoverTraceRules::FindCoverSlotAt(World, Local(State, WallNearF, 0.f, 50.f), State.F, Slot) || Slot.Height != ECoverHeight::LowCover)
			{
				Check(State, false, FString::Printf(TEXT("%s: a low cover slot at the leader's cube"), Label));
				return Finish(State, false);
			}
			Check(State, Squad->GetGroupCoverMembers(A).Num() == 2, FString::Printf(TEXT("%s: group mode - both followers go along"), Label));
			PC->ShowCoverPreview(A, Slot);
			PC->ConfirmCoverPreview();
			const int32 Slots = PC->GetLastGroupCoverSlotCount();
			const int32 Nearby = PC->GetLastGroupCoverNearbyCount();
			const int32 Prone = PC->GetLastGroupCoverProneCount();
			if (bAlone)
			{
				Check(State, Nearby == 0 && Prone == 2, FString::Printf(TEXT("%s: no cover nearby - the two prone behind it, the fallback (%d slots, %d nearby, %d prone)"), Label,
					Slots, Nearby, Prone));
			}
			else
			{
				Check(State, Nearby == 2 && Prone == 0 && Slots == 3, FString::Printf(TEXT("%s: the two followers take the free cubes nearby, nobody prone (%d slots, %d nearby, %d prone)"),
					Label, Slots, Nearby, Prone));
			}
			Next(State, State.Stage + 1);
			return true;
		}
		case 3:
		case 13:
		{
			// Wait until everyone is in cover / prone at his spot.
			bool bDone = A && A->bInCover;
			for (int32 Index = 1; Index < 3; ++Index)
			{
				const AOperativeCharacter* Each = Member(State, Index);
				bDone &= Each && (bAlone ? Squad->HasGroupCoverProne(Each, true) : Each->bInCover);
			}
			State.HoldTime = bDone ? State.HoldTime + StepSeconds : 0.f;
			if (State.HoldTime < 1.f && State.StageTime < 25.f)
			{
				return true;
			}
			FString Line;
			TArray<const AActor*> Covers;
			for (int32 Index = 0; Index < 3; ++Index)
			{
				const AOperativeCharacter* Each = Member(State, Index);
				if (!Each)
				{
					continue;
				}
				const AActor* Cover = Each->bInCover ? Each->GetCoverSlot().WallActor.Get() : nullptr;
				Covers.Add(Cover);
				Line += FString::Printf(TEXT(" %s: cover %d (%s), prone %d, stance %d at F %.0f R %.0f;"), *Each->DisplayName.ToString(), Each->bInCover ? 1 : 0, *GetNameSafe(Cover),
					Squad->HasGroupCoverProne(Each) ? 1 : 0, static_cast<int32>(Each->GetStance()), FVector::DotProduct(Each->GetActorLocation() - State.P, State.F),
					FVector::DotProduct(Each->GetActorLocation() - State.P, State.R));
			}
			Check(State, State.HoldTime >= 1.f, FString::Printf(TEXT("%s: everyone at his place after %.1f s:%s"), Label, State.StageTime, *Line));
			if (!bAlone)
			{
				const bool bOwnCubes = Covers.Num() == 3 && Covers[0] && Covers[1] && Covers[2] && Covers[0] != Covers[1] && Covers[0] != Covers[2] && Covers[1] != Covers[2];
				Check(State, bOwnCubes, FString::Printf(TEXT("%s: each in his own cube - nobody prone in the open, no two on one spot:%s"), Label, *Line));
			}
			Next(State, State.Stage + 1);
			return true;
		}
		case 4:
		case 14:
			if (TakeShot(World, State, bAlone ? TEXT("group_cover_one_cube_prone") : TEXT("group_cover_nearby_cubes")) && !State.bShotTaken)
			{
				return true;
			}
			if (State.bShot && State.StageTime - State.ShotStart < 2.5f)
			{
				return true;
			}
			if (State.bShot)
			{
				ReleaseShot(World);
			}
			DestroyBlocks(State);
			if (bAlone)
			{
				return Finish(State, true);
			}
			Next(State, 11);
			return true;
		default:
			return Finish(State, false);
		}
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time > 200.f)
		{
			Check(State, false, FString::Printf(TEXT("timeout (stage %d)"), State.Stage));
			return World ? Finish(State, false) : false;
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(UGameplayStatics::GetPlayerController(World, 0));
		if (State.Stage == 0)
		{
			if (State.StageTime < 3.f || !Squad || !PC || Squad->GetMembers().Num() < 3)
			{
				return true;
			}
			if (!SetUp(World, State, Squad, PC))
			{
				return Finish(State, false);
			}
			// Item 5 is the formation's wall layout (Sectors 1); item 3 the cover-slot group order (Sectors 0, as GroupCoverSmoke B).
			if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Formation.Sectors")))
			{
				State.SavedSectors = CVar->GetInt();
				CVar->Set(State.Scenario == EScenario::WallSectors ? 1 : 0, ECVF_SetByConsole);
			}
			Next(State, 1);
			return true;
		}
		if (!Squad || !PC || !Squad->GetLeader())
		{
			return Finish(State, false);
		}
		return State.Scenario == EScenario::WallSectors ? StepWallSectors(World, State, Squad, PC) : StepGroupCoverNearby(World, State, Squad, PC);
	}

	void Run(EScenario Scenario, const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		TSharedRef<FState> State = MakeShared<FState>();
		State->Scenario = Scenario;
		State->bShot = Args.ContainsByPredicate([](const FString& Arg) { return Arg.Equals(TEXT("Shot"), ESearchCase::IgnoreCase); });
		TWeakObjectPtr<UWorld> WeakWorld(World);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs WallSectorsCommand(
		TEXT("CodexTactics.WallSectorsSmoke"),
		TEXT("Dev check (game designer bug report 2026-11-02 item 5): along a short wall / a segment between two gaps both end men look round their own corners, no duplicated sector while one is open; PASS/FAIL, exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) { Run(EScenario::WallSectors, Args, World); }));

	static FAutoConsoleCommandWithWorldAndArgs GroupCoverNearbyCommand(
		TEXT("CodexTactics.GroupCoverNearbySmoke"),
		TEXT("Dev check (game designer bug report 2026-11-02 item 3): a group cover order at a low cube - the followers take the free cubes nearby, prone behind it only without one; PASS/FAIL, exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) { Run(EScenario::GroupCoverNearby, Args, World); }));
}

#endif // !UE_BUILD_SHIPPING
