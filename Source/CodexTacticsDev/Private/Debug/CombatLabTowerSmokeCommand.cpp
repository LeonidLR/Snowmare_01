// Dev-only check of game designer bug 07 (2026-11-01, L_FormationLabCombat in PIE: "the tower ladders cannot be climbed; a double click on
// a tower shows a ladder text instead of sending the operative up" - "[Ladder] ... climb refused (6)" = NotClimbable, the floating text
// "THIS LADDER CAN'T BE CLIMBED"): the player's click path through ACodexTacticsPlayerController::HandleWorldHit, like the mouse's:
//  1. every tower ladder reads 11 rungs, climbable, its nav link on;
//  2. a click on the LADDER (the cursor ray from a camera-like point onto its click proxy) - the leader (solo) climbs it and stands on the roof;
//  3. a click on the ground below - he climbs down and walks there;
//  4. a DOUBLE click on the ladder (two clicks in one frame = the controller's double click: a sprint) - up again;
//  5. down, then a DOUBLE click on the ROOF - up again; never a refusal, never "CAN'T BE CLIMBED".
// Headless (-game, the map loaded from disk):
//   powershell -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -Commands CombatLabTowerSmoke   (map + -LevelJson from test_map.json)
// In PIE (the designer's case - the editor world copied into a PIE world, editor only):
//   UnrealEditor.exe <uproject> -RenderOffscreen -ResX=1280 -ResY=720 -unattended -nosplash -nosound -NoSound
//     "-ExecCmds=CodexTactics.CombatLabTowerSmokePIE" -abslog=<log>
//   loads /Game/Maps/L_FormationLabCombat in the editor, starts PIE in the level viewport, runs the same check in the PIE world, exits.
// UE-only (no Godot reference).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/CombatPressureSubsystem.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Ladders/LadderActor.h"
#include "Ladders/LadderClimbComponent.h"
#include "NavLinkCustomComponent.h"
#include "NavigationSystem.h"
#include "TimerManager.h"
#include "UI/FloatingTextSubsystem.h"
#if WITH_EDITOR
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "FileHelpers.h"
#include "LevelEditor.h"
#include "Modules/ModuleManager.h"
#include "PlayInEditorDataTypes.h"
#endif

namespace CombatLabTowerSmoke
{
	constexpr float NavWarmupSeconds = 3.f;
	constexpr float Timeout = 400.f;
	constexpr float LegSeconds = 75.f;

	enum class EStage : uint8 { Setup, UpByLadderClick, DownByGroundClick, UpByLadderDoubleClick, DownAgain, UpByRoofDoubleClick, Done };

	struct FState
	{
		EStage Stage = EStage::Setup;
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Failures = 0;
		bool bPie = false;
		TWeakObjectPtr<ALadderActor> Tower;
		bool bClimbedThisLeg = false;
		bool bSprintThisLeg = false;
		int32 Refusals = 0;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s%s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"), State.bPie ? TEXT(" (PIE)") : TEXT(""));
		State.Stage = EStage::Done;
#if WITH_EDITOR
		if (State.bPie && GEditor)
		{
			// The PIE session ends first, the editor exits a few frames later (quitting with PIE still up crashed in the level editor's shutdown).
			GEditor->RequestEndPlayMap();
			TSharedRef<int32> FramesLeft = MakeShared<int32>(30);
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([FramesLeft](float)
			{
				--(*FramesLeft);
				if (*FramesLeft > 0 || (GEditor && GEditor->PlayWorld && *FramesLeft > -600)) // at most ~10 s more for PIE to end
				{
					return true;
				}
				FPlatformMisc::RequestExit(false, TEXT("CombatLabTowerSmokePIE"));
				return false;
			}));
			return false;
		}
#endif
		FPlatformMisc::RequestExit(false, TEXT("CombatLabTowerSmoke"));
		return false;
	}

	void Enter(FState& State, EStage Stage)
	{
		State.Stage = Stage;
		State.StageTime = 0.f;
		State.bClimbedThisLeg = false;
		State.bSprintThisLeg = false;
		UE_LOG(LogCodexTactics, Display, TEXT("[TowerSmoke] stage %d at %.1f s"), static_cast<int32>(Stage), State.Time);
	}

	FVector FeetOf(const ACharacter& Character)
	{
		return Character.GetActorLocation() - FVector(0.f, 0.f, Character.GetSimpleCollisionHalfHeight());
	}

	/** The cursor's ray from a camera-like point (behind and above) onto Point, handed to the controller like the mouse's; Clicks 2 = a double click. */
	bool ClickRay(UWorld* World, const FVector& Eye, const FVector& Point, int32 Clicks, AActor** OutHitActor = nullptr)
	{
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(World->GetFirstPlayerController());
		FHitResult Hit;
		const FVector Direction = (Point - Eye).GetSafeNormal();
		if (!PC || !World->LineTraceSingleByChannel(Hit, Eye, Point + Direction * 200.f, ECC_Visibility))
		{
			return false;
		}
		for (int32 Index = 0; Index < Clicks; ++Index)
		{
			PC->HandleWorldHit(Hit);
		}
		if (OutHitActor)
		{
			*OutHitActor = Hit.GetActor();
		}
		UE_LOG(LogCodexTactics, Display, TEXT("[TowerSmoke] %s on %s at %s"), Clicks > 1 ? TEXT("double click") : TEXT("click"), *GetNameSafe(Hit.GetActor()),
			*Hit.ImpactPoint.ToCompactString());
		return true;
	}

	/** The camera-like eye for a ladder: 6 m out from it and 5 m up. */
	FVector EyeFor(const ALadderActor& Ladder)
	{
		return Ladder.GetBottomLinkWorld() + Ladder.GetOutward() * 600.f + FVector(0.f, 0.f, 500.f);
	}

	FVector GroundSpot(const ALadderActor& Ladder)
	{
		return Ladder.GetBottomLinkWorld() + Ladder.GetOutward() * 450.f;
	}

	FVector RoofSpot(const ALadderActor& Ladder)
	{
		return Ladder.GetTopLinkWorld() - Ladder.GetOutward() * 100.f;
	}

	bool OnRoof(const AOperativeCharacter& Leader, const ALadderActor& Ladder, float& OutFeetOver)
	{
		OutFeetOver = static_cast<float>(FeetOf(Leader).Z) - Ladder.GetTopWorldZ();
		const ULadderClimbComponent* Climb = Leader.LadderClimb.Get();
		return FMath::Abs(OutFeetOver) < 25.f && Climb && !Climb->IsOnLadder() && !Leader.IsMoving();
	}

	bool OnGround(const AOperativeCharacter& Leader, const ALadderActor& Ladder, float& OutFromSpot)
	{
		OutFromSpot = static_cast<float>(FVector::Dist2D(Leader.GetActorLocation(), GroundSpot(Ladder)));
		const ULadderClimbComponent* Climb = Leader.LadderClimb.Get();
		return FMath::Abs(static_cast<float>(FeetOf(Leader).Z) - Ladder.GetFloorZ()) < 25.f && OutFromSpot < 200.f && Climb && !Climb->IsOnLadder() && !Leader.IsMoving();
	}

	bool Step(UWorld* World, FState& State, float Dt)
	{
		State.Time += Dt;
		State.StageTime += Dt;
		if (State.Time > Timeout)
		{
			Check(State, false, FString::Printf(TEXT("timeout in stage %d"), static_cast<int32>(State.Stage)));
			return Finish(State, false);
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		ALadderActor* Tower = State.Tower.Get();
		if (!Squad || !Leader || !Leader->LadderClimb || (State.Stage != EStage::Setup && !Tower))
		{
			Check(State, false, TEXT("squad, a leader with a climb component, the tower"));
			return Finish(State, false);
		}
		const ULadderClimbComponent* Climb = Leader->LadderClimb.Get();
		State.bClimbedThisLeg |= Climb->IsOnLadder();
		State.bSprintThisLeg |= Leader->IsSprinting();
		if (Climb->GetLastRefusal() == ELadderRefusal::NotClimbable)
		{
			++State.Refusals;
		}
		const UFloatingTextSubsystem* Floating = World->GetSubsystem<UFloatingTextSubsystem>();
		const bool bRefusalText = Floating && Floating->HasShown(TEXT("CAN'T BE CLIMBED"));

		switch (State.Stage)
		{
		case EStage::Setup:
		{
			if (UCombatPressureSubsystem* Pressure = World->GetSubsystem<UCombatPressureSubsystem>())
			{
				Pressure->SetAutoSpawn(false);
				Pressure->DespawnAll();
			}
			int32 Ladders = 0;
			float Best = MAX_flt;
			for (TActorIterator<ALadderActor> It(World); It; ++It)
			{
				++Ladders;
				Check(State, It->GetRungCount() == 11 && It->IsClimbable() && It->GetNavLink() && It->GetNavLink()->IsEnabled(),
					FString::Printf(TEXT("%s ladder %s: %d rungs, climbable %d, nav link %d, %s"), State.bPie ? TEXT("PIE") : TEXT("game"), *It->GetName(),
						It->GetRungCount(), It->IsClimbable() ? 1 : 0, It->GetNavLink() && It->GetNavLink()->IsEnabled() ? 1 : 0,
						It->HasWall() ? TEXT("wall") : TEXT("free-standing")));
				const float Distance = static_cast<float>(FVector::Dist2D(It->GetBottomLinkWorld(), Leader->GetActorLocation()));
				if (Distance < Best)
				{
					Best = Distance;
					State.Tower = *It;
				}
			}
			Check(State, Ladders == 4, FString::Printf(TEXT("4 tower ladders (%d)"), Ladders));
			Tower = State.Tower.Get();
			if (!Tower)
			{
				return Finish(State, false);
			}
			Squad->EnterSoloMode();
			Leader->StopOperative();
			const FVector Start = GroundSpot(*Tower);
			// The others wait beside the tower, holding (2026-11-02): the leader is put far from where they stood - past the 25 m solo leash,
			// which ended Solo, and the squad (whose slots now lie on the leader's level, squad-and-orders.md "The formation across height
			// levels") walked up his ladder into the cursor's way. 4 m to each side and 2.5 m further out: off the click ray and the climb.
			Squad->SetFollowersHolding(true);
			{
				const FVector Side = FVector::CrossProduct(FVector::UpVector, Tower->GetOutward()).GetSafeNormal();
				UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
				int32 Slot = 0;
				for (AOperativeCharacter* Member : Squad->GetMembers())
				{
					if (!Member || Member == Leader)
					{
						continue;
					}
					const float Sign = Slot % 2 == 0 ? 1.f : -1.f;
					FVector Spot = Start + Tower->GetOutward() * 250.f + Side * Sign * (400.f + 150.f * static_cast<float>(Slot / 2));
					++Slot;
					FNavLocation OnNav;
					if (Nav && Nav->ProjectPointToNavigation(FVector(Spot.X, Spot.Y, Tower->GetFloorZ()), OnNav, FVector(150.f, 150.f, 150.f)))
					{
						Spot = OnNav.Location;
					}
					Member->StopOperative();
					Member->SetActorLocation(FVector(Spot.X, Spot.Y, Tower->GetFloorZ() + Member->GetSimpleCollisionHalfHeight() + 2.f), false, nullptr,
						ETeleportType::TeleportPhysics);
				}
			}
			Leader->SetActorLocation(FVector(Start.X, Start.Y, Tower->GetFloorZ() + Leader->GetSimpleCollisionHalfHeight() + 2.f), false, nullptr,
				ETeleportType::TeleportPhysics);
			AActor* Clicked = nullptr;
			const bool bClicked = ClickRay(World, EyeFor(*Tower), Tower->GetRungWorldLocation(5), 1, &Clicked); // before the message reads Clicked
			Check(State, bClicked && Clicked == Tower,
				FString::Printf(TEXT("a click on tower ladder %s (the cursor hit %s)"), *Tower->GetName(), *GetNameSafe(Clicked)));
			Enter(State, EStage::UpByLadderClick);
			return true;
		}

		case EStage::UpByLadderClick:
		case EStage::UpByLadderDoubleClick:
		case EStage::UpByRoofDoubleClick:
		{
			float FeetOver = 0.f;
			const bool bUp = OnRoof(*Leader, *Tower, FeetOver);
			if (!bUp && State.StageTime < LegSeconds)
			{
				return true;
			}
			const TCHAR* How = State.Stage == EStage::UpByLadderClick ? TEXT("a click on the ladder")
				: (State.Stage == EStage::UpByLadderDoubleClick ? TEXT("a double click on the ladder") : TEXT("a double click on the roof"));
			Check(State, bUp && State.bClimbedThisLeg, FString::Printf(TEXT("%s: he climbed the ladder and stands on the roof (feet %.0f cm over it, %.1f s)"), How,
				FeetOver, State.StageTime));
			if (State.Stage != EStage::UpByLadderClick)
			{
				Check(State, State.bSprintThisLeg, FString::Printf(TEXT("%s ran (the double click's sprint)"), How));
			}
			Check(State, State.Refusals == 0 && !bRefusalText, FString::Printf(TEXT("never refused (%d frames NotClimbable), no \"CAN'T BE CLIMBED\" text"), State.Refusals));
			if (State.Stage == EStage::UpByRoofDoubleClick)
			{
				return Finish(State, true);
			}
			// Down: a click on the ground below (the ray from the eye onto the spot).
			Check(State, ClickRay(World, EyeFor(*Tower), GroundSpot(*Tower), 1), TEXT("a click on the ground below the tower"));
			Enter(State, State.Stage == EStage::UpByLadderClick ? EStage::DownByGroundClick : EStage::DownAgain);
			return true;
		}

		case EStage::DownByGroundClick:
		case EStage::DownAgain:
		{
			float FromSpot = 0.f;
			const bool bDown = OnGround(*Leader, *Tower, FromSpot);
			if (!bDown && State.StageTime < LegSeconds)
			{
				return true;
			}
			Check(State, bDown && State.bClimbedThisLeg, FString::Printf(TEXT("a click on the ground: he climbed down and stands there (%.0f cm from the spot, %.1f s)"),
				FromSpot, State.StageTime));
			if (State.Stage == EStage::DownByGroundClick)
			{
				AActor* Clicked = nullptr;
				const bool bClicked = ClickRay(World, EyeFor(*Tower), Tower->GetRungWorldLocation(5), 2, &Clicked);
				Check(State, bClicked && Clicked == Tower,
					FString::Printf(TEXT("a double click on the ladder (the cursor hit %s)"), *GetNameSafe(Clicked)));
				Enter(State, EStage::UpByLadderDoubleClick);
			}
			else
			{
				const FVector Roof = RoofSpot(*Tower);
				AActor* Clicked = nullptr;
				const bool bClicked = ClickRay(World, Roof + FVector(0.f, 0.f, 600.f) + Tower->GetOutward() * 300.f, Roof, 2, &Clicked);
				Check(State, bClicked && Clicked && !Clicked->IsA<ALadderActor>(),
					FString::Printf(TEXT("a double click on the tower's roof (the cursor hit %s)"), *GetNameSafe(Clicked)));
				Enter(State, EStage::UpByRoofDoubleClick);
			}
			return true;
		}

		default:
			return false;
		}
	}

	void Start(UWorld* World, bool bPie)
	{
		TSharedRef<FState> State = MakeShared<FState>();
		State->bPie = bPie;
		TWeakObjectPtr<UWorld> WeakWorld(World);
		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([WeakWorld, State]()
		{
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
			{
				UWorld* W = WeakWorld.Get();
				return W && State->Stage != EStage::Done && Step(W, *State, W->IsPaused() ? 0.f : W->GetDeltaSeconds());
			}), 0.f);
		}), NavWarmupSeconds, false);
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		if (World)
		{
			Start(World, World->WorldType == EWorldType::PIE);
		}
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.CombatLabTowerSmoke"),
		TEXT("Dev check (bug 07, 2026-11-01, L_FormationLabCombat): the tower ladders climbable; a click / double click on a ladder or a roof and a click ")
		TEXT("on the ground take the leader up and down by the player controller's click path. PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));

#if WITH_EDITOR
	/** The PIE run: the map loaded in the editor, PIE started in the level viewport (the toolbar's Play), the check in the PIE world. */
	FDelegateHandle PostPieHandle;

	void OnPostPieStarted(const bool /*bSimulating*/)
	{
		FEditorDelegates::PostPIEStarted.Remove(PostPieHandle);
		UWorld* PieWorld = nullptr;
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.WorldType == EWorldType::PIE && Context.World())
			{
				PieWorld = Context.World();
			}
		}
		UE_LOG(LogCodexTactics, Display, TEXT("[TowerSmoke] PIE started: %s"), *GetNameSafe(PieWorld));
		if (PieWorld)
		{
			Start(PieWorld, true);
		}
		else
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke FAIL: no PIE world"));
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: FAIL (PIE)"));
			FPlatformMisc::RequestExit(false, TEXT("CombatLabTowerSmokePIE"));
		}
	}

	void RunPie(const TArray<FString>& Args, UWorld* /*World*/)
	{
		if (!GEditor)
		{
			UE_LOG(LogCodexTactics, Error, TEXT("[TowerSmoke] CombatLabTowerSmokePIE needs the editor (not -game)"));
			return;
		}
		const FString Map = Args.Num() > 0 ? Args[0] : FString(TEXT("/Game/Maps/L_FormationLabCombat"));
		UE_LOG(LogCodexTactics, Display, TEXT("[TowerSmoke] PIE: loading %s in the editor"), *Map);
		FEditorFileUtils::LoadMap(Map, /*LoadAsTemplate*/ false, /*bShowProgress*/ false);
		PostPieHandle = FEditorDelegates::PostPIEStarted.AddStatic(&OnPostPieStarted);
		TSharedRef<int32> FramesLeft = MakeShared<int32>(30);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([FramesLeft](float)
		{
			if (--(*FramesLeft) > 0)
			{
				return true;
			}
			FRequestPlaySessionParams Params;
			FLevelEditorModule& LevelEditor = FModuleManager::Get().LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
			Params.DestinationSlateViewport = LevelEditor.GetFirstActiveViewport();
			GEditor->RequestPlaySession(Params);
			UE_LOG(LogCodexTactics, Display, TEXT("[TowerSmoke] PIE requested (level viewport %s)"), Params.DestinationSlateViewport.IsSet() ? TEXT("yes") : TEXT("no"));
			return false;
		}));
	}

	static FAutoConsoleCommandWithWorldAndArgs PieCommand(
		TEXT("CodexTactics.CombatLabTowerSmokePIE"),
		TEXT("Dev (editor only): CodexTactics.CombatLabTowerSmokePIE [map] loads the map in the editor, starts PIE in the level viewport (the designer's case: ")
		TEXT("the editor world copied into PIE) and runs CombatLabTowerSmoke there. PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunPie));
#endif // WITH_EDITOR
}

#endif // !UE_BUILD_SHIPPING
