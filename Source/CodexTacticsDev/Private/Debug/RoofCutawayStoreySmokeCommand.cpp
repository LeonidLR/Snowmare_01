// Dev-only check of the roof cutaway's STOREY rule (the game designer's bug 2026-11-22 on the dressed khrushchevka showcase: "I cannot see
// what happens inside when the squad enters - squad on the ground floor -> storeys 2, 3, 4 and the roof invisible; up on storey 2 -> 3 and 4
// invisible, 1 and 2 visible"). docs/systems/camera-hud-minimap.md "Roof cutaway", URoofCutawaySubsystem storey rule.
// On a generated map (grey /Game/Maps/Generated/L_Gen_StoryShowcase or the dressed /Game/Maps/KitPilot/L_Gen_StoryShowcase_Kit) the squad is
// put inside one building (building=<id>, default the first indexed one with 3 storeys) on each storey of storeys=<list> (0+1+2; default 0+1), the
// leader selected, Solo + holding. Per storey, first with Codex.RoofCutaway.Storeys 0 (the head-clearance rule alone: the "before"), then 1:
// the building's visible pieces per storey (grey actors / kit instanced components) and how many of them are cut are logged; with the storey
// rule every visible piece of a storey above the leader's must be cut and none of his storey or below, and the subsystem must put him on that
// storey. shots=1 (needs rendering, Clear 13:00) also writes <dir>/<map>_<building>_s<n>_{before_game,game,top}.png: the game's own tactical
// camera before / after, and a top-down camera over the building. PASS / FAIL, then exits.
//   powershell -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -Commands RoofCutawayStorey -ExtraArgs "-Multiprocess"
//   UnrealEditor.exe <uproject> /Game/Maps/KitPilot/L_Gen_StoryShowcase_Kit -game -RenderOffscreen -ResX=1600 -ResY=900 -nosound
//     -LevelJson=L_Gen_StoryShowcase_Kit.json -ExecCmds="CodexTactics.RoofCutawayStoreySmoke building=S04_rubble_escape shots=1 dir=<abs>"
// Godot reference: none (UE-only).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "SmokeUtils.h"
#include "AIController.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/RoofCutawaySubsystem.h"
#include "Camera/TacticalCameraPawn.h"
#include "CanvasItem.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Components/CapsuleComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Debug/DebugDrawService.h"
#include "Debug/FrontendSmokeUtils.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Environment/EnvironmentRules.h"
#include "Environment/EnvironmentSubsystem.h"
#include "GameFramework/HUD.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Kismet/GameplayStatics.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Paths.h"
#include "NavigationSystem.h"
#include "UnrealClient.h"

namespace RoofCutawayStoreySmoke
{
	constexpr int32 WarmupFrames = 150;
	/** A stage is judged this many frames after it began (> the 0.5 s restore hysteresis + a 0.1 s update; fixed 60 fps or real frames). */
	constexpr int32 SettleFrames = 90;
	/** With shots: the tactical camera's snap and the temporal AA settle this long before a picture. */
	constexpr int32 ShotSettleFrames = 180;
	constexpr int32 MaxFrames = 60 * 400;

	enum class EStep : uint8 { Warmup, Place, Before, After, Switch, ShotGame, ShotTop, Next, Done };

	struct FRunner
	{
		EStep Step = EStep::Warmup;
		int32 Frame = 0;
		int32 StepFrame = 0;
		int32 Failures = 0;
		FString MapName;
		FString BuildingArg;
		FString BuildingId;
		int32 StoreyCount = 0;
		TArray<int32> Storeys = { 0, 1 };
		int32 StoreyIndex = 0;
		bool bShots = false;
		/** switch=1 (designer bug 2026-11-22: "the upper storeys hid only for the engineer"): after the check, the player selects another member
		 * standing on the same storey (the number key, SelectMember) - the cut must stay. */
		bool bSwitch = false;
		FString Dir;
		FString Label;
		FString PendingShot;
		int32 ShotWait = 0;
		TArray<FString> Written;
		FDelegateHandle EndFrameHandle;
		FDelegateHandle DrawHandle;
		TWeakObjectPtr<ACameraActor> TopCamera;
		TWeakObjectPtr<AActor> GameView;
		FBox Bounds = FBox(ForceInit);
	};

	TSharedPtr<FRunner> Runner;

	void Check(bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		Runner->Failures += bOk ? 0 : 1;
	}

	void SetStoreyRule(bool bOn)
	{
		if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.RoofCutaway.Storeys")))
		{
			Var->Set(bOn ? 1 : 0, ECVF_SetByConsole);
		}
	}

	void Finish()
	{
		SetStoreyRule(true);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), Runner->Failures == 0 ? TEXT("PASS") : TEXT("FAIL"));
		FCoreDelegates::OnEndFrame.Remove(Runner->EndFrameHandle);
		UDebugDrawService::Unregister(Runner->DrawHandle);
		Runner->Step = EStep::Done;
		FPlatformMisc::RequestExit(false, TEXT("RoofCutawayStoreySmoke"));
	}

	float FeetZ(const AOperativeCharacter& Operative)
	{
		return static_cast<float>(Operative.GetActorLocation().Z) - Operative.GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	}

	/** The median bottom of Building's pieces on Storey (its base), or INDEX_NONE-ish when it has none. */
	bool StoreyBase(const URoofCutawaySubsystem& Roofs, const FString& Building, int32 Storey, float& OutZ)
	{
		TArray<float> Bottoms;
		for (const TPair<UPrimitiveComponent*, int32>& Piece : Roofs.GetStoreyPieces(Building))
		{
			if (Piece.Value == Storey)
			{
				Bottoms.Add(static_cast<float>(Piece.Key->Bounds.GetBox().Min.Z));
			}
		}
		if (Bottoms.IsEmpty())
		{
			return false;
		}
		Bottoms.Sort();
		OutZ = Bottoms[(Bottoms.Num() - 1) / 2];
		return true;
	}

	/** A navmesh spot on Storey inside Building: the middle of its slabs, then points across them, the first that lands on that floor. */
	bool FindSpot(UWorld* World, const URoofCutawaySubsystem& Roofs, const FString& Building, float BaseZ, FVector& OutSpot)
	{
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		TArray<FBox> Slabs = Roofs.GetStoreySlabs(Building);
		Slabs.Sort([](const FBox& A, const FBox& B) { return (A.Max.X - A.Min.X) * (A.Max.Y - A.Min.Y) > (B.Max.X - B.Min.X) * (B.Max.Y - B.Min.Y); });
		const float Fractions[] = { 0.5f, 0.3f, 0.7f, 0.2f, 0.8f };
		for (const FBox& Slab : Slabs)
		{
			for (const float FX : Fractions)
			{
				for (const float FY : Fractions)
				{
					const FVector Try(FMath::Lerp(Slab.Min.X, Slab.Max.X, FX), FMath::Lerp(Slab.Min.Y, Slab.Max.Y, FY), BaseZ + 40.f);
					FNavLocation OnNav;
					if (Nav && Nav->ProjectPointToNavigation(Try, OnNav, FVector(60.f, 60.f, 90.f)) && FMath::Abs(OnNav.Location.Z - BaseZ) < 60.f)
					{
						OutSpot = OnNav.Location;
						return true;
					}
				}
			}
		}
		return false;
	}

	void Put(AOperativeCharacter& Member, const FVector& Spot, float Yaw)
	{
		Member.TeleportTo(Spot + FVector(0.f, 0.f, Member.GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 5.f), FRotator(0.f, Yaw, 0.f), false, true);
		Member.GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		if (AAIController* AI = Cast<AAIController>(Member.GetController()))
		{
			AI->StopMovement();
		}
	}

	/** Per storey of the building: visible pieces (grey / kit) and how many are cut; judged when bJudge (the storey rule on). */
	void Dump(UWorld* World, int32 LeaderStorey, bool bJudge, const TCHAR* Phase)
	{
		const URoofCutawaySubsystem* Roofs = World->GetSubsystem<URoofCutawaySubsystem>();
		struct FCount { int32 Grey = 0; int32 GreyCut = 0; int32 Kit = 0; int32 KitCut = 0; };
		TMap<int32, FCount> Counts;
		int32 KitUntagged = 0;
		FString UntaggedNames;
		int32 CutBlocking = 0;
		int32 CutChecked = 0;
		for (const TPair<UPrimitiveComponent*, int32>& Piece : Roofs->GetStoreyPieces(Runner->BuildingId))
		{
			const UPrimitiveComponent* Comp = Piece.Key;
			const AActor* Owner = Comp->GetOwner();
			if (!Comp->IsVisible() || (Owner && Owner->IsHidden()))
			{
				continue; // hidden proxies (kit dressing): collision / navigation only
			}
			FCount& Count = Counts.FindOrAdd(Piece.Value);
			const bool bCut = Roofs->IsCutAway(Comp);
			// Designer bug 2026-11-23: a cut piece still blocks sight / fire (the Visibility channel) - only the view passes it.
			if (bCut && bJudge)
			{
				++CutChecked;
				CutBlocking += Comp->GetCollisionResponseToChannel(ECC_Visibility) == ECR_Block || Comp->GetCollisionEnabled() == ECollisionEnabled::NoCollision ? 1 : 0;
			}
			if (Comp->IsA<UInstancedStaticMeshComponent>())
			{
				// kit_dress per-storey components carry building=<id> / floor=<n> themselves; one without spans storeys (the old dresser)
				FString TagBuilding;
				int32 TagStorey = INDEX_NONE;
				if (Owner && Owner->ActorHasTag(TEXT("KitDress"))
					&& (!RoofCutawayRules::ParseStoreyTags(Comp->ComponentTags, TagBuilding, TagStorey) || TagStorey == INDEX_NONE))
				{
					++KitUntagged;
					UntaggedNames += FString::Printf(TEXT(" %s.%s"), Owner ? *Owner->GetName() : TEXT("-"), *Comp->GetName());
				}
				++Count.Kit;
				Count.KitCut += bCut ? 1 : 0;
			}
			else
			{
				++Count.Grey;
				Count.GreyCut += bCut ? 1 : 0;
			}
		}
		if (bJudge && CutChecked > 0)
		{
			Check(CutBlocking == CutChecked, FString::Printf(TEXT("%s: every cut piece still blocks sight / fire (Visibility) - %d of %d"), Phase, CutBlocking, CutChecked));
		}
		Counts.KeySort(TLess<int32>());
		for (const TPair<int32, FCount>& Pair : Counts)
		{
			const FCount& C = Pair.Value;
			const bool bAbove = Pair.Key > LeaderStorey;
			const FString Line = FString::Printf(TEXT("%s %s, leader on storey %d: storey %d%s - grey %d/%d cut, kit components %d/%d cut"), Phase,
				*Runner->BuildingId, LeaderStorey, Pair.Key, Pair.Key + 1 == Runner->StoreyCount ? TEXT(" (roof)") : TEXT(""), C.GreyCut, C.Grey, C.KitCut, C.Kit);
			if (bJudge)
			{
				Check(bAbove ? C.GreyCut == C.Grey && C.KitCut == C.Kit : C.GreyCut == 0 && C.KitCut == 0,
					Line + (bAbove ? TEXT(" -> all hidden") : TEXT(" -> all visible")));
			}
			else
			{
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: %s"), *Line);
			}
		}
		if (bJudge)
		{
			Check(KitUntagged == 0, FString::Printf(TEXT("%s: %d kit_dress components (actor tag KitDress) without their own building / floor tags (a component spanning storeys is never hidden whole)%s"),
				*Runner->BuildingId, KitUntagged, *UntaggedNames.Left(600)));
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: %s: %d pieces cut in the level"), Phase, Roofs->GetCutAwayCount());
	}

	void OnDraw(UCanvas* Canvas, APlayerController* /*Controller*/)
	{
		if (!Runner.IsValid() || !Canvas || !GEngine || Runner->Label.IsEmpty() || !Runner->bShots)
		{
			return;
		}
		if (UFont* Font = GEngine->GetLargeFont())
		{
			FCanvasTextItem Shadow(FVector2D(26.f, 22.f), FText::FromString(Runner->Label), Font, FLinearColor::Black);
			Canvas->DrawItem(Shadow);
			FCanvasTextItem Item(FVector2D(24.f, 20.f), FText::FromString(Runner->Label), Font, FLinearColor(1.f, 0.85f, 0.4f));
			Canvas->DrawItem(Item);
		}
	}

	/** The game's own tactical camera (bTop false) or a camera straight down over the building. */
	void View(UWorld* World, bool bTop)
	{
		APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
		if (!PC)
		{
			return;
		}
		if (AHUD* Hud = PC->GetHUD())
		{
			Hud->bShowHUD = false;
		}
		if (!Runner->GameView.IsValid() && PC->GetViewTarget() != Runner->TopCamera.Get())
		{
			Runner->GameView = PC->GetViewTarget();
		}
		if (!bTop)
		{
			if (AActor* Game = Runner->GameView.Get(); Game && PC->GetViewTarget() != Game)
			{
				PC->SetViewTarget(Game);
			}
			return;
		}
		const FVector Centre = Runner->Bounds.GetCenter();
		const float Span = static_cast<float>(FMath::Max(Runner->Bounds.Max.X - Runner->Bounds.Min.X, Runner->Bounds.Max.Y - Runner->Bounds.Min.Y));
		const FVector Eye(Centre.X, Centre.Y, Runner->Bounds.Max.Z + FMath::Max(1500.f, Span * 1.25f));
		const FRotator Down(-89.9f, 0.f, 0.f);
		if (!Runner->TopCamera.IsValid())
		{
			Runner->TopCamera = World->SpawnActor<ACameraActor>(Eye, Down);
		}
		if (ACameraActor* Camera = Runner->TopCamera.Get())
		{
			Camera->SetActorLocationAndRotation(Eye, Down);
			Camera->GetCameraComponent()->SetFieldOfView(55.f);
			Camera->GetCameraComponent()->bConstrainAspectRatio = false;
			if (PC->GetViewTarget() != Camera)
			{
				PC->SetViewTarget(Camera);
			}
		}
	}

	void RequestShot(const FString& Name)
	{
		const FString File = Runner->Dir / FString::Printf(TEXT("%s_%s_s%d_%s.png"), *Runner->MapName, *Runner->BuildingId, Runner->Storeys[Runner->StoreyIndex], *Name);
		FScreenshotRequest::RequestScreenshot(File, /*bInShowUI*/ true, /*bAddFilenameSuffix*/ false);
		Runner->Written.Add(File);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: shot %s"), *File);
	}

	bool PickBuilding(URoofCutawaySubsystem& Roofs)
	{
		Roofs.RebuildStoreyIndex();
		for (const TPair<FString, int32>& Building : Roofs.GetStoreyBuildings())
		{
			// storey count incl. the roof: a 3-storey house = 4
			if (Runner->BuildingArg.IsEmpty() ? Building.Value >= 4 : Building.Key == Runner->BuildingArg)
			{
				Runner->BuildingId = Building.Key;
				Runner->StoreyCount = Building.Value;
				for (const FBox& Slab : Roofs.GetStoreySlabs(Building.Key))
				{
					Runner->Bounds += Slab;
				}
				return true;
			}
		}
		return false;
	}

	void Tick()
	{
		FRunner& R = *Runner;
		if (R.Step == EStep::Done)
		{
			return;
		}
		++R.Frame;
		++R.StepFrame;
		if (R.Frame > MaxFrames)
		{
			Check(false, FString::Printf(TEXT("timeout in step %d"), static_cast<int32>(R.Step)));
			Finish();
			return;
		}
		UWorld* World = FrontendSmokeUtils::FindGameWorld();
		USquadSubsystem* Squad = World ? World->GetSubsystem<USquadSubsystem>() : nullptr;
		URoofCutawaySubsystem* Roofs = World ? World->GetSubsystem<URoofCutawaySubsystem>() : nullptr;
		UEnvironmentSubsystem* Environment = World ? UEnvironmentSubsystem::Get(World) : nullptr;
		if (!World || !Squad || !Roofs || !Squad->GetLeader() || (R.bShots && (!Environment || !Environment->IsInitialised())))
		{
			return;
		}
		if (R.bShots)
		{
			UWidgetLayoutLibrary::RemoveAllWidgets(World);
		}
		auto Go = [&R](EStep Step)
		{
			R.Step = Step;
			R.StepFrame = 0;
		};
		const int32 Storey = R.Storeys.IsValidIndex(R.StoreyIndex) ? R.Storeys[R.StoreyIndex] : 0;
		switch (R.Step)
		{
		case EStep::Warmup:
			if (R.StepFrame < WarmupFrames)
			{
				return;
			}
			if (R.bShots && Environment)
			{
				Environment->SetClockScale(0.f);
				Environment->ForceWeather(EWeatherState::Clear, true);
				Environment->SetHour(13.f);
				GEngine->Exec(World, TEXT("DisableAllScreenMessages"));
				GEngine->Exec(World, TEXT("r.MotionBlurQuality 0"));
			}
			R.MapName = World->GetMapName().Replace(TEXT("UEDPIE_0_"), TEXT(""));
			if (!PickBuilding(*Roofs))
			{
				Check(false, FString::Printf(TEXT("a building of the storey index (%s; %d indexed)"), R.BuildingArg.IsEmpty() ? TEXT("3 storeys") : *R.BuildingArg,
					Roofs->GetStoreyBuildings().Num()));
				Finish();
				return;
			}
			Check(true, FString::Printf(TEXT("%s: %d buildings indexed; %s has %d storeys incl. the roof, %d pieces"), *R.MapName, Roofs->GetStoreyBuildings().Num(),
				*R.BuildingId, R.StoreyCount, Roofs->GetStoreyPieces(R.BuildingId).Num()));
			if (!Squad->IsSoloMode())
			{
				Squad->EnterSoloMode();
			}
			Squad->SetFollowersHolding(true);
			Go(EStep::Place);
			return;
		case EStep::Place:
		{
			float BaseZ = 0.f;
			FVector Spot;
			if (!StoreyBase(*Roofs, R.BuildingId, Storey, BaseZ) || !FindSpot(World, *Roofs, R.BuildingId, BaseZ, Spot))
			{
				Check(false, FString::Printf(TEXT("%s: a navmesh spot on storey %d"), *R.BuildingId, Storey));
				Go(EStep::Next);
				return;
			}
			TArray<AOperativeCharacter*> Members = Squad->GetMembers();
			Members.Remove(nullptr);
			UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
			for (int32 Index = 0; Index < Members.Num(); ++Index)
			{
				FVector At = Spot;
				if (Index > 0)
				{
					FNavLocation Near;
					const FVector Offset = FRotator(0.f, Index * 120.f, 0.f).RotateVector(FVector(110.f, 0.f, 0.f));
					if (Nav && Nav->ProjectPointToNavigation(Spot + Offset, Near, FVector(40.f, 40.f, 60.f)))
					{
						At = Near.Location;
					}
					else
					{
						At = Spot + FVector(0.f, 0.f, 0.f) + Offset * 0.5f;
					}
				}
				Put(*Members[Index], At, 90.f);
			}
			Squad->SetLeader(Members[0]);
			SetStoreyRule(false);
			R.Label = FString::Printf(TEXT("%s %s, squad on storey %d - BEFORE (Codex.RoofCutaway.Storeys 0)"), *R.MapName, *R.BuildingId, Storey);
			Go(EStep::Before);
			return;
		}
		case EStep::Before:
			if (R.bShots)
			{
				View(World, false);
				if (R.StepFrame == 2)
				{
					for (TActorIterator<ATacticalCameraPawn> It(World); It; ++It)
					{
						It->SnapToFollowTarget();
					}
				}
			}
			if (R.StepFrame < (R.bShots ? ShotSettleFrames : SettleFrames))
			{
				return;
			}
			if (R.PendingShot.IsEmpty())
			{
				Dump(World, Storey, false, TEXT("BEFORE (storey rule off)"));
				if (R.bShots)
				{
					RequestShot(TEXT("before_game"));
					R.PendingShot = TEXT("before");
					R.ShotWait = 0;
					return;
				}
			}
			else if (++R.ShotWait < 10)
			{
				return;
			}
			R.PendingShot.Reset();
			SetStoreyRule(true);
			Roofs->RebuildStoreyIndex();
			R.Label = FString::Printf(TEXT("%s %s, squad on storey %d - storey cutaway ON"), *R.MapName, *R.BuildingId, Storey);
			Go(EStep::After);
			return;
		case EStep::After:
		{
			if (R.bShots)
			{
				View(World, false);
				if (R.StepFrame == 2)
				{
					for (TActorIterator<ATacticalCameraPawn> It(World); It; ++It)
					{
						It->SnapToFollowTarget();
						UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: game camera %s at %s, view target %s"), *It->GetName(), *It->GetActorLocation().ToCompactString(),
							*GetNameSafe(UGameplayStatics::GetPlayerController(World, 0) ? UGameplayStatics::GetPlayerController(World, 0)->GetViewTarget() : nullptr));
					}
				}
			}
			if (R.StepFrame < SettleFrames)
			{
				return;
			}
			FString Building;
			int32 InStorey = INDEX_NONE;
			const AOperativeCharacter* Leader = Squad->GetLeader();
			const bool bIn = Roofs->GetOperativeStorey(*Leader, Building, InStorey);
			Check(bIn && Building == R.BuildingId && InStorey == Storey, FString::Printf(TEXT("the leader %s (feet %.0f) is in %s on storey %d (expected %s / %d)"),
				*Leader->DisplayName.ToString(), FeetZ(*Leader), bIn ? *Building : TEXT("-"), InStorey, *R.BuildingId, Storey));
			Dump(World, Storey, true, TEXT("AFTER (storey rule on)"));
			if (R.bSwitch && Squad->GetMembers().Num() > 1)
			{
				if (ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(UGameplayStatics::GetPlayerController(World, 0)))
				{
					PC->SelectMember(1); // the number key 2
				}
				Go(EStep::Switch);
				return;
			}
			Go(R.bShots ? EStep::ShotGame : EStep::Next);
			return;
		}
		case EStep::Switch:
		{
			if (R.StepFrame < SettleFrames)
			{
				return;
			}
			const AOperativeCharacter* Picked = Squad->GetMembers()[1];
			const AOperativeCharacter* Now = Squad->GetLeader();
			FString Building;
			int32 InStorey = INDEX_NONE;
			const bool bIn = Now && Roofs->GetOperativeStorey(*Now, Building, InStorey);
			Check(Now == Picked, FString::Printf(TEXT("switch: the number key selected %s (leader now %s)"), *Picked->DisplayName.ToString(),
				Now ? *Now->DisplayName.ToString() : TEXT("-")));
			Check(bIn && Building == R.BuildingId && InStorey == Storey, FString::Printf(TEXT("switch: %s is in %s on storey %d (expected %s / %d)"),
				Now ? *Now->DisplayName.ToString() : TEXT("-"), bIn ? *Building : TEXT("-"), InStorey, *R.BuildingId, Storey));
			Dump(World, Storey, true, *FString::Printf(TEXT("AFTER SWITCH to %s"), Now ? *Now->DisplayName.ToString() : TEXT("-")));
			Go(R.bShots ? EStep::ShotGame : EStep::Next);
			return;
		}
		case EStep::ShotGame:
			View(World, false);
			if (R.StepFrame == 1)
			{
				RequestShot(TEXT("game"));
			}
			if (R.StepFrame >= 10)
			{
				R.Label = FString::Printf(TEXT("%s %s, squad on storey %d - top-down, storey cutaway ON"), *R.MapName, *R.BuildingId, Storey);
				Go(EStep::ShotTop);
			}
			return;
		case EStep::ShotTop:
			View(World, true);
			if (R.StepFrame == 30)
			{
				RequestShot(TEXT("top"));
			}
			if (R.StepFrame >= 40)
			{
				View(World, false);
				Go(EStep::Next);
			}
			return;
		case EStep::Next:
			++R.StoreyIndex;
			if (R.StoreyIndex < R.Storeys.Num() && R.Storeys[R.StoreyIndex] < R.StoreyCount - 1)
			{
				Go(EStep::Place);
				return;
			}
			if (R.StoreyIndex < R.Storeys.Num())
			{
				Check(false, FString::Printf(TEXT("storey %d: %s has storeys 0..%d below its roof"), R.Storeys[R.StoreyIndex], *R.BuildingId, R.StoreyCount - 2));
			}
			for (const FString& File : R.Written)
			{
				Check(IFileManager::Get().FileSize(*File) > 10000, FString::Printf(TEXT("%s written"), *File));
			}
			Finish();
			return;
		default:
			return;
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		Runner = MakeShared<FRunner>();
		Runner->Dir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("StoryShowcase") / TEXT("cutaway"));
		for (const FString& Arg : Args)
		{
			if (Arg.StartsWith(TEXT("building=")))
			{
				Runner->BuildingArg = Arg.Mid(9);
			}
			else if (Arg.StartsWith(TEXT("shots=")))
			{
				Runner->bShots = Arg.Mid(6) != TEXT("0");
			}
			else if (Arg.StartsWith(TEXT("switch=")))
			{
				Runner->bSwitch = Arg.Mid(7) != TEXT("0");
			}
			else if (Arg.StartsWith(TEXT("dir=")))
			{
				Runner->Dir = Arg.Mid(4);
			}
			else if (Arg.StartsWith(TEXT("storeys=")))
			{
				// "0+1+2" (-ExecCmds splits on commas) or "0/1/2"
				TArray<FString> Parts;
				const TCHAR* Separators[] = { TEXT("+"), TEXT("/"), TEXT(",") };
				Arg.Mid(8).ParseIntoArray(Parts, Separators, UE_ARRAY_COUNT(Separators));
				Runner->Storeys.Reset();
				for (const FString& Part : Parts)
				{
					Runner->Storeys.Add(FCString::Atoi(*Part));
				}
			}
		}
		if (Runner->Storeys.IsEmpty())
		{
			Runner->Storeys = { 0 };
		}
		FPaths::CollapseRelativeDirectories(Runner->Dir);
		if (Runner->bShots)
		{
			IFileManager::Get().MakeDirectory(*Runner->Dir, true);
		}
		if (IConsoleVariable* Pressure = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Pressure.Enabled")))
		{
			Pressure->Set(0, ECVF_SetByConsole);
		}
		SmokeUtils::StopLevelPmc(World); // Showcase Kit PMC squad off (Codex 085)
		Runner->DrawHandle = UDebugDrawService::Register(TEXT("Game"), FDebugDrawDelegate::CreateStatic(&OnDraw));
		Runner->EndFrameHandle = FCoreDelegates::OnEndFrame.AddStatic(&Tick);
		UE_LOG(LogCodexTactics, Display, TEXT("RoofCutawayStoreySmoke: building %s, storeys %d, shots %d -> %s"), Runner->BuildingArg.IsEmpty() ? TEXT("(auto)") : *Runner->BuildingArg,
			Runner->Storeys.Num(), Runner->bShots ? 1 : 0, *Runner->Dir);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.RoofCutawayStoreySmoke"),
		TEXT("Dev check (designer bug 2026-11-22): the squad inside a generated building on each storey of storeys=<0+1> - with Codex.RoofCutaway.Storeys 0 (before) and 1 every ")
		TEXT("visible piece above the leader's storey must be cut, none of his storey or below; [building=<id>] [shots=1 dir=<abs>] (rendered pictures). PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
