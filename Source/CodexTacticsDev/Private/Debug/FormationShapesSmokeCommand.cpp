// Dev-only headless check of the saved formation shapes in the game (game designer 2026-11-10, variant B; docs/design/FORMATION_CONTRACT.md 19):
//   powershell -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -Commands FormationShapesSmoke
//   (map /Game/Maps/L_FormationLab, "-LevelJson=L_FormationLab.json" - Scripts/test_map.json smokeOptions)
// The squad (3 operatives + a 4th spawned here) stands in lane A's open ground with the hold-B menu's manual Diamond. The smoke points the reader
// at a scratch copy (Saved/FormationShapesSmoke/formation_shapes.json - the designer's Content file is never touched) and checks, by the
// slots / looks the squad really gets this frame (USquadSubsystem::GetFormationTarget in the leader's frame):
//   1. no file -> the formula's diamond (FormationShapeRules::LocalSlot), no saved shape;
//   2. a saved diamond-4 written while the game runs (live reload, no restart) -> the slots / looks / arcs of the file; the right flanker walks
//      to his new place;
//   3. only echelon_left saved + the menu's Echelon right -> the mirrored shape;
//   4. the file's custom_1 seeds an empty custom slot (menu Custom 1 filled), the player's own stored shape wins and only it is exported;
//   5. the file removed (Echelon right kept) -> the formula's slots again once the live reload sees it.
// Godot reference: none (UE-only).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsGameMode.h"
#include "Engine/World.h"
#include "Formation/FormationCustomRules.h"
#include "Formation/FormationMenuRules.h"
#include "Formation/FormationRuntimeRules.h"
#include "Formation/FormationShapeRules.h"
#include "Formation/FormationShapesFile.h"
#include "Formation/FormationTuning.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "NavigationSystem.h"
#include "TimerManager.h"

namespace FormationShapesSmoke
{
	constexpr float NavWarmupSeconds = 3.f;
	/** A slot matches within this (the frame's own numbers: only float rounding), cm / deg. */
	constexpr double SlotTolCm = 2.0;
	constexpr double LookTolDeg = 1.0;
	/** The live reload must show a save within this (LiveReloadSeconds + a few frames). */
	constexpr float ReloadLimitSeconds = 3.f;
	/** The squad's spot: this far along the leader's start facing (the lab's spawn faces lane A's open ground), cm. */
	constexpr double OpenGroundAheadCm = 3500.0;

	const TCHAR* const SavedDiamond = TEXT("{ \"schema\": \"codex.formation_shapes/1\", \"formations\": { \"diamond\": { \"4\": { \"members\": [\n")
		TEXT("  { \"role\": \"commander\", \"fwd\": 0, \"right\": 0, \"look\": 0, \"arc\": 90 },\n")
		TEXT("  { \"role\": \"right\", \"fwd\": -310, \"right\": 330, \"look\": 77, \"arc\": 60 },\n")
		TEXT("  { \"role\": \"left\", \"fwd\": -310, \"right\": -330, \"look\": -77 },\n")
		TEXT("  { \"role\": \"rear\", \"fwd\": -620, \"right\": 0, \"look\": 180, \"arc\": 120 } ] } } }, \"custom\": {} }\n");
	const TCHAR* const SavedEchelonLeft = TEXT("{ \"schema\": \"codex.formation_shapes/1\", \"formations\": { \"echelon_left\": { \"4\": { \"members\": [\n")
		TEXT("  { \"role\": \"commander\", \"fwd\": 0, \"right\": 0, \"look\": 0, \"arc\": 90 },\n")
		TEXT("  { \"role\": \"left\", \"fwd\": -200, \"right\": -200, \"look\": -45, \"arc\": 90 },\n")
		TEXT("  { \"role\": \"rear\", \"fwd\": -400, \"right\": -400, \"look\": -90, \"arc\": 90 },\n")
		TEXT("  { \"role\": \"right\", \"fwd\": -200, \"right\": 150, \"look\": 90, \"arc\": 90 } ] } } },\n")
		TEXT(" \"custom\": { \"custom_1\": { \"members\": [ { \"squad\": 0, \"fwd\": 0, \"right\": 0, \"look\": 0 }, { \"squad\": 1, \"fwd\": -300, \"right\": 250, \"look\": 70 } ] } } }\n");

	enum class EStage : uint8
	{
		Setup,
		Formula,
		WaitSaved,
		Walk,
		WaitMirror,
		WaitRemoved,
		Done
	};

	struct FState
	{
		EStage Stage = EStage::Setup;
		float Time = 0.f;
		float StageTime = 0.f;
		/** The stage's start on the wall clock: the live reload checks the file on the wall clock (the smokes run on a fixed game step). */
		double StageWall = 0.0;
		int32 Failures = 0;
		FString Path;
		TArray<FString> CustomBefore;
		TOptional<EFormationMenuItem> MenuBefore;
		TWeakObjectPtr<AOperativeCharacter> Fourth;
		TWeakObjectPtr<AOperativeCharacter> RightMan;
		float RightManStartDist = 0.f;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	void Enter(FState& State, EStage Stage)
	{
		State.Stage = Stage;
		State.StageTime = 0.f;
		State.StageWall = FPlatformTime::Seconds();
	}

	void WriteFile(const FState& State, const TCHAR* Text)
	{
		FFileHelper::SaveStringToFile(FString(Text), *State.Path);
	}

	/** One member's slot this frame in the leader's frame: (fwd, right), the look relative to the heading. */
	struct FLocal
	{
		EFormationRole Role = EFormationRole::Commander;
		FVector2D Offset = FVector2D::ZeroVector;
		float Look = 0.f;
		AOperativeCharacter* Who = nullptr;
	};

	TArray<FLocal> LocalSlots(const USquadSubsystem& Squad)
	{
		TArray<FLocal> Out;
		AOperativeCharacter* Leader = Squad.GetLeader();
		FVector K;
		float KYaw = 0.f;
		if (!Leader || !Squad.GetFormationTarget(Leader, K, KYaw))
		{
			return Out;
		}
		const double Heading = Squad.GetFormationHeadingYaw();
		const FVector2D F = FormationMath::DirOf(Heading);
		const FVector2D R(-F.Y, F.X);
		for (AOperativeCharacter* Member : Squad.GetMembers())
		{
			EFormationRole Role;
			FVector Slot;
			float Yaw = 0.f;
			if (!Member || !Squad.GetFormationRole(Member, Role) || !Squad.GetFormationTarget(Member, Slot, Yaw))
			{
				continue;
			}
			const FVector2D D = FVector2D(Slot) - FVector2D(K);
			FLocal& Each = Out.AddDefaulted_GetRef();
			Each.Role = Role;
			Each.Offset = FVector2D(FVector2D::DotProduct(D, F), FVector2D::DotProduct(D, R));
			Each.Look = static_cast<float>(FormationMath::NormYaw(Yaw - Heading));
			Each.Who = Member;
		}
		return Out;
	}

	FString Show(const TArray<FLocal>& Slots)
	{
		FString Line;
		for (const FLocal& Each : Slots)
		{
			Line += FString::Printf(TEXT(" %s(%.0f, %.0f) look %.0f"), FormationNames::ToString(Each.Role), Each.Offset.X, Each.Offset.Y, Each.Look);
		}
		return Line;
	}

	/** Every member's slot matches Expected (role -> (fwd, right)); bLooks: also the look (relative). */
	bool Matches(const TArray<FLocal>& Slots, const TArray<FFormationSlot>& Expected, bool bLooks)
	{
		if (Slots.Num() != Expected.Num())
		{
			return false;
		}
		for (const FLocal& Each : Slots)
		{
			const FFormationSlot* Want = Expected.FindByPredicate([&Each](const FFormationSlot& S) { return S.Role == Each.Role; });
			if (!Want || FVector2D::Distance(Each.Offset, Want->Position) > SlotTolCm
				|| (bLooks && FMath::Abs(FormationMath::NormYaw(Each.Look - Want->Yaw)) > LookTolDeg))
			{
				return false;
			}
		}
		return true;
	}

	/** The formula's local slots of a 4-man Kind (Yaw unused). */
	TArray<FFormationSlot> Formula(EFormationKind Kind)
	{
		TArray<FFormationSlot> Out;
		for (const EFormationRole Role : FormationShapeRules::Roles(4))
		{
			FFormationSlot& Slot = Out.AddDefaulted_GetRef();
			Slot.Role = Role;
			Slot.Position = FormationShapeRules::LocalSlot(Kind, Role, 4, FormationTuning::Get());
		}
		return Out;
	}

	TArray<FFormationSlot> FormulaDiamond()
	{
		return Formula(EFormationKind::Diamond);
	}

	/** A shape's local slots (Position = (fwd, right), Yaw = the look relative). */
	TArray<FFormationSlot> LocalOf(const FFormationShape& Shape)
	{
		return FormationShapes::Slots(Shape, FVector2D::ZeroVector, 0.f);
	}

	bool Finish(FState& State, UWorld* World, bool bComplete)
	{
		IFileManager::Get().Delete(*State.Path, false, true, true);
		FormationShapes::SetPath(FString());
		if (USquadSubsystem* Squad = World ? World->GetSubsystem<USquadSubsystem>() : nullptr)
		{
			Squad->ImportFormationCustomSlots(State.CustomBefore);
			Squad->SetFormationMenuChoice(State.MenuBefore);
		}
		Enter(State, EStage::Done);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("FormationShapesSmoke"));
		return false;
	}

	bool Step(UWorld* World, FState& State, float DeltaTime)
	{
		State.Time += DeltaTime;
		State.StageTime += DeltaTime;
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		if (!Squad || !Leader)
		{
			if (State.Time > 20.f)
			{
				Check(State, false, TEXT("a squad with a leader"));
				return Finish(State, World, false);
			}
			return true;
		}
		// The live reload waits run on the wall clock (FormationShapes::LiveReloadSeconds is wall time; the game step is fixed).
		const auto TimedOutWall = [&State](double Limit, const FString& What)
		{
			if (FPlatformTime::Seconds() - State.StageWall > Limit)
			{
				Check(State, false, FString::Printf(TEXT("timeout: %s"), *What));
				return true;
			}
			return false;
		};
		const auto TimedOut = [&State](float Limit, const FString& What)
		{
			if (State.StageTime > Limit)
			{
				Check(State, false, FString::Printf(TEXT("timeout: %s"), *What));
				return true;
			}
			return false;
		};

		switch (State.Stage)
		{
		case EStage::Setup:
		{
			if (!FormationRuntime::IsEnabled() || !FormationShapes::IsEnabled())
			{
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: Codex.Formation.Sectors / Codex.Formation.SavedShapes is 0 - nothing to check: skipped"));
				return Finish(State, World, true);
			}
			State.CustomBefore = Squad->ExportFormationCustomSlots();
			State.MenuBefore = Squad->GetFormationMenuChoice();
			if (Squad->IsSoloMode())
			{
				Squad->ExitSoloMode();
			}
			if (ACodexTacticsGameMode* GameMode = World->GetAuthGameMode<ACodexTacticsGameMode>(); GameMode && Squad->GetMembers().Num() < 4)
			{
				State.Fourth = GameMode->SpawnOperative(GameMode->RecruitSusanin, 3, Leader->GetActorLocation() - Leader->GetActorForwardVector() * 300.f,
					Leader->GetActorRotation(), true);
			}
			// Lane A's open ground ahead of the spawn: the diamond, its mirror echelon and the custom shape fit without a wall cutting them.
			const FRotator Facing(0.f, Leader->GetActorRotation().Yaw, 0.f);
			const FVector Start = Leader->GetActorLocation() + Facing.Vector() * OpenGroundAheadCm;
			UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
			int32 Index = 0;
			for (AOperativeCharacter* Each : Squad->GetMembers())
			{
				Each->StopOperative();
				const FVector Spot = Each == Leader ? Start : Start - Facing.Vector() * (250.0 + 150.0 * Index++);
				FNavLocation OnNav;
				const bool bOnNav = Nav && Nav->ProjectPointToNavigation(Spot, OnNav, FVector(50.f, 50.f, 300.f));
				Each->TeleportTo(bOnNav ? OnNav.Location + FVector(0.f, 0.f, Each->GetSimpleCollisionHalfHeight()) : Spot, Facing, false, true);
			}
			Squad->ResetFormationState(Facing.Vector());
			Squad->SetFormationMenuChoice(TOptional<EFormationMenuItem>(EFormationMenuItem::Diamond));
			// The scratch file: absent first.
			IFileManager::Get().Delete(*State.Path, false, true, true);
			FormationShapes::SetPath(State.Path);
			Check(State, Squad->GetMembers().Num() == 4, FString::Printf(TEXT("4 operatives in the squad (%d), reader on %s"), Squad->GetMembers().Num(), *State.Path));
			Enter(State, EStage::Formula);
			return true;
		}

		case EStage::Formula:
		{
			// 1. No file: the formula's diamond.
			if (State.StageTime < 2.f)
			{
				return true;
			}
			const TArray<FLocal> Slots = LocalSlots(*Squad);
			const bool bReady = Squad->IsFormationSectorsActive() && Squad->GetFormationKind() == EFormationKind::Diamond && Slots.Num() == 4;
			if (!bReady)
			{
				if (TimedOut(10.f, FString::Printf(TEXT("the diamond of 4 in force (%s, %d slots)"), FormationNames::ToString(Squad->GetFormationKind()), Slots.Num())))
				{
					return Finish(State, World, false);
				}
				return true;
			}
			Check(State, !Squad->IsFormationSavedShape() && Matches(Slots, FormulaDiamond(), false),
				FString::Printf(TEXT("no file: the formula's diamond (saved %d):%s"), Squad->IsFormationSavedShape() ? 1 : 0, *Show(Slots)));
			// 2. The designer saves a diamond-4 while the game runs.
			WriteFile(State, SavedDiamond);
			Enter(State, EStage::WaitSaved);
			return true;
		}

		case EStage::WaitSaved:
		{
			if (!Squad->IsFormationSavedShape())
			{
				if (TimedOutWall(ReloadLimitSeconds, TEXT("the saved diamond-4 shows up (live reload)")))
				{
					return Finish(State, World, false);
				}
				return true;
			}
			const double ReloadWall = FPlatformTime::Seconds() - State.StageWall;
			Check(State, ReloadWall <= ReloadLimitSeconds, FString::Printf(TEXT("live reload: the save in force after %.2f s wall clock (no restart)"), ReloadWall));
			FFormationShapesFile File;
			TArray<FString> Errors;
			FString Text;
			FFileHelper::LoadFileToString(Text, *State.Path);
			FormationShapes::Parse(Text, File, Errors);
			const TOptional<FFormationShape> Shape = FormationShapes::Find(File, EFormationKind::Diamond, 4);
			const TArray<FLocal> Slots = LocalSlots(*Squad);
			const bool bLooks = Squad->GetFormationMode() == EFormationMode::Halt || Squad->GetFormationMode() == EFormationMode::Walk;
			Check(State, Shape.IsSet() && Matches(Slots, LocalOf(Shape.GetValue()), bLooks) && Squad->GetFormationSavedShapeName() == TEXT("diamond/4"),
				FString::Printf(TEXT("saved diamond-4: the file's slots%s (%s, mode %s):%s"), bLooks ? TEXT(" and looks") : TEXT(""), *Squad->GetFormationSavedShapeName(),
					FormationNames::ToString(Squad->GetFormationMode()), *Show(Slots)));
			Check(State, !Matches(Slots, FormulaDiamond(), false), TEXT("... which differ from the formula's"));
			float ArcP = 0.f;
			float ArcL = 0.f;
			float ArcZ = 0.f;
			const bool bArcs = Squad->GetFormationLookArc(EFormationRole::Right, ArcP) && Squad->GetFormationLookArc(EFormationRole::Left, ArcL)
				&& Squad->GetFormationLookArc(EFormationRole::Rear, ArcZ);
			Check(State, bArcs && ArcP == 60.f && ArcL == 90.f && ArcZ == 120.f,
				FString::Printf(TEXT("arcs from the file: P %.0f (60), L %.0f (default 90), Z %.0f (120)"), ArcP, ArcL, ArcZ));
			const FLocal* Right = Slots.FindByPredicate([](const FLocal& Each) { return Each.Role == EFormationRole::Right; });
			State.RightMan = Right ? Right->Who : nullptr;
			FVector Slot;
			float Yaw = 0.f;
			State.RightManStartDist = State.RightMan.IsValid() && Squad->GetFormationTarget(State.RightMan.Get(), Slot, Yaw)
				? static_cast<float>(FVector::Dist2D(State.RightMan->GetActorLocation(), Slot)) : 0.f;
			Enter(State, EStage::Walk);
			return true;
		}

		case EStage::Walk:
		{
			// ... and the right flanker walks to his new place.
			AOperativeCharacter* Right = State.RightMan.Get();
			FVector Slot;
			float Yaw = 0.f;
			const float Dist = Right && Squad->GetFormationTarget(Right, Slot, Yaw) ? static_cast<float>(FVector::Dist2D(Right->GetActorLocation(), Slot)) : 99999.f;
			// He starts ~1 m off (the formula's place); the follow step brings him to its stop radius (25 cm).
			if (Dist > 50.f || State.StageTime < 0.5f)
			{
				if (TimedOut(15.f, FString::Printf(TEXT("the right flanker at his saved place (%.0f cm off, was %.0f)"), Dist, State.RightManStartDist)))
				{
					return Finish(State, World, false);
				}
				return true;
			}
			Check(State, State.RightManStartDist > 50.f, FString::Printf(TEXT("the right flanker walked to his saved place: %.0f cm off (was %.0f) after %.1f s"), Dist, State.RightManStartDist,
				State.StageTime));
			// 3. Only echelon_left saved (+ a custom_1 default) and the menu's Echelon right -> its mirror.
			WriteFile(State, SavedEchelonLeft);
			Squad->SetFormationMenuChoice(TOptional<EFormationMenuItem>(EFormationMenuItem::EchelonRight));
			Enter(State, EStage::WaitMirror);
			return true;
		}

		case EStage::WaitMirror:
		{
			if (Squad->GetFormationSavedShapeName() != TEXT("echelon_right/4 mirrored"))
			{
				if (TimedOutWall(ReloadLimitSeconds + 2.0, FString::Printf(TEXT("echelon_right from the saved echelon_left (in force %s, shape \"%s\")"),
					FormationNames::ToString(Squad->GetFormationKind()), *Squad->GetFormationSavedShapeName())))
				{
					return Finish(State, World, false);
				}
				return true;
			}
			FFormationShapesFile File;
			TArray<FString> Errors;
			FString Text;
			FFileHelper::LoadFileToString(Text, *State.Path);
			FormationShapes::Parse(Text, File, Errors);
			const TArray<FLocal> Slots = LocalSlots(*Squad);
			const TOptional<FFormationShape> Left = FormationShapes::Find(File, EFormationKind::EchelonLeft, 4);
			Check(State, Left.IsSet() && Matches(Slots, LocalOf(FormationShapes::Mirror(Left.GetValue())), Squad->GetFormationMode() == EFormationMode::Halt),
				FString::Printf(TEXT("echelon right = the saved echelon left mirrored:%s"), *Show(Slots)));

			// 4. The custom defaults: an empty slot shows the file's custom_1; the player's own shape wins; only his own is exported.
			Squad->ImportFormationCustomSlots(TArray<FString>());
			const FString FileDefault = FormationCustomRules::ToJsonString(File.Custom[0]);
			Check(State, Squad->IsFormationCustomSlotFilled(1) && Squad->IsFormationCustomSlotDefault(1)
				&& FormationCustomRules::ToJsonString(Squad->GetFormationCustomShape(1)) == FileDefault && !Squad->IsFormationCustomSlotFilled(2),
				FString::Printf(TEXT("custom_1 seeded from the file (%s), custom_2 empty"), *FormationCustomRules::ToJsonString(Squad->GetFormationCustomShape(1))));
			const TArray<FString> Exported = Squad->ExportFormationCustomSlots();
			Check(State, Exported.Num() == 3 && Exported[0].IsEmpty(), TEXT("the file default is not exported (save game / carry keep only the player's own)"));
			FFormationCustomShape Own;
			Own.Members.Add({ 0, 0.f, 0.f, 0.f });
			Own.Members.Add({ 1, -150.f, -200.f, -30.f });
			Squad->SetFormationCustomShape(1, Own);
			const FString OwnJson = FormationCustomRules::ToJsonString(FormationCustomRules::RoundShape(Own));
			Check(State, FormationCustomRules::ToJsonString(Squad->GetFormationCustomShape(1)) == OwnJson && !Squad->IsFormationCustomSlotDefault(1)
				&& Squad->ExportFormationCustomSlots()[0] == OwnJson, TEXT("the player's own Custom 1 wins over the file and is the one exported"));
			Squad->ImportFormationCustomSlots(State.CustomBefore);

			// 5. The file removed (the menu's Echelon right kept): the formula again once the live reload sees it.
			IFileManager::Get().Delete(*State.Path, false, true, true);
			Enter(State, EStage::WaitRemoved);
			return true;
		}

		case EStage::WaitRemoved:
		{
			const TArray<FLocal> Slots = LocalSlots(*Squad);
			if (Squad->IsFormationSavedShape() || Squad->GetFormationKind() != EFormationKind::EchelonRight || Slots.Num() != 4)
			{
				if (TimedOutWall(ReloadLimitSeconds + 2.0, FString::Printf(TEXT("the formula after the file was removed (%s, shape \"%s\")"),
					FormationNames::ToString(Squad->GetFormationKind()), *Squad->GetFormationSavedShapeName())))
				{
					return Finish(State, World, false);
				}
				return true;
			}
			Check(State, Matches(Slots, Formula(EFormationKind::EchelonRight), false), FString::Printf(TEXT("file removed: the formula's echelon right again after %.2f s wall clock:%s"), FPlatformTime::Seconds() - State.StageWall,
				*Show(Slots)));
			return Finish(State, World, true);
		}

		default:
			return false;
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		TWeakObjectPtr<UWorld> WeakWorld(World);
		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([WeakWorld]()
		{
			TSharedRef<FState> State = MakeShared<FState>();
			State->Path = FPaths::ProjectSavedDir() / TEXT("FormationShapesSmoke/formation_shapes.json");
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
			{
				UWorld* W = WeakWorld.Get();
				return W && State->Stage != EStage::Done && Step(W, *State, W->IsPaused() ? 0.f : W->GetDeltaSeconds());
			}), 0.f);
		}), NavWarmupSeconds, false);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.FormationShapesSmoke"),
		TEXT("Dev check (2026-11-11, contract 19): the formation editor's saved shapes in the game - no file = the formula, a diamond-4 saved while the game runs ")
		TEXT("(live reload) = its slots / looks / arcs, echelon right mirrored from a saved echelon left, custom_1 seeds an empty custom slot; PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
