// Dev-only headless check of the camera's minimum zoom on the real map of the game designer's report (bug #52, PIE on main 2026-11-08,
// L_Gen_Depot_1, combat, squad mode, all three PRONE on a roof, the mouse wheel in: "the camera still comes point-blank" - the view 1-2 m above
// the Commander):
//   powershell -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -Commands CameraMinZoomDepotSmoke -ExtraArgs "-Multiprocess"
//   (map /Game/Maps/Generated/L_Gen_Depot_1, "-LevelJson=L_Gen_Depot_1.json" - Scripts/test_map.json smokeOptions)
// The chain it reproduces: in every fight view a manual pan (WASD, the screen edge, a drag) freezes the camera's focus - its height too, the
// ground the pan started on. The squad goes up onto a roof (B_gallery, 3 m); the player pans onto the Commander and wheels in to the 8 m
// minimum: 8 m from a point on the GROUND under the roof is 4.6 m above the ground - 1.3 m above the men lying on it, 2.3 m from the Commander.
// The 7 m floor only ever measured from the focus. Stages (combat started, the squad prone, pressure off, enemies parked):
//  1. ground: 50 wheel notches in - the camera at the wheel's minimum from the view centre and from the Commander, no operative on screen
//     point-blank (each one >= 7 m away or >= the floor's 404 cm below the camera), no capsule; the operative clearance did not move it;
//  2. the designer's chain: a pan freezes the focus on the ground, the squad is put on the roof, the view panned onto the Commander,
//     50 notches in - no operative on screen point-blank, no capsule (before the fix: FAIL, the Commander 2.8 m from the camera, 1.6 m
//     above the men): 2a with the held focus kept on the ground (DetachedHeightRadius 0) - the operative clearance alone moves the camera
//     back; 2b the radius back - the held focus takes the roof's height, the wheel's 8 m measured from the roof, no clearance needed;
//  3. the number key (re-attach) on the roof, 50 notches in: the Commander exactly as far as on the ground (the wheel feels the same);
//  4. the other zoom paths on the roof: a 100 cm glide (focus / death cam / enemy focus), a 50 cm snap, a selection switch, the turn-based
//     entry zoom at 100 cm - each ends >= 7 m from the man it frames, none point-blank.
// Every check measures the view the player sees (APlayerCameraManager's camera location), not only the tactical pawn. A trace line every
// 0.25 s while zooming: view target, camera, the nearest operative (distance / height under the camera), CurrentDistance, MinSafeDistance,
// Pitch, the clearance dolly. A/B: Codex.Camera.OperativeClearance 0 (the old camera) -> stage 2 FAILs.
// Arguments: building=<layout building id> (default: the lowest walkable roof of the layout). Godot reference: none (UE-only).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AIController.h"
#include "Camera/PlayerCameraManager.h"
#include "Camera/TacticalCameraPawn.h"
#include "Camera/TacticalCameraRules.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/OperativeMovementRules.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Components/CapsuleComponent.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Debug/SmokeUtils.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "GameFlow/LevelExitSubsystem.h"
#include "GameFramework/Character.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "NavigationSystem.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace CameraMinZoomDepotSmoke
{
	constexpr float StepSeconds = 0.05f;
	constexpr int32 WheelNotches = 50;
	constexpr float LevelHeightCm = 300.f;
	constexpr float TraceEverySeconds = 0.25f;
	/** Measuring slack, cm. */
	constexpr float Slack = 5.f;

	enum class EStage : uint8
	{
		Settle, PlaceGround, WheelGround, JudgeGround, Detach, PlaceRoof, PanOnto, WheelChain, JudgeChain, HeldHeight, JudgeHeldHeight, Reattach, WheelRoof, JudgeRoof,
		Glide, JudgeGlide, Snap, JudgeSnap, Switch, JudgeSwitch, TurnBasedZoom, JudgeTurnBased, Done
	};

	struct FState
	{
		TWeakObjectPtr<UWorld> World;
		FString MapName;
		FString BuildingArg;
		EStage Stage = EStage::Settle;
		float Time = 0.f;
		float StageTime = 0.f;
		float TraceTime = 0.f;
		int32 Failures = 0;
		int32 Notches = 0;
		FString BuildingId;
		float RoofZ = 0.f;
		FVector2D RoofCentre = FVector2D::ZeroVector;
		FVector2D RoofAxis = FVector2D(1.0, 0.0);
		FVector2D GroundSpot = FVector2D::ZeroVector;
		float GroundLeaderDistance = 0.f;
		float SavedHeightRadius = 0.f;
		TArray<TWeakObjectPtr<AOperativeCharacter>> Roster;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		State.Stage = EStage::Done;
		FPlatformMisc::RequestExit(false, TEXT("CameraMinZoomDepotSmoke"));
		return false;
	}

	/** The layout's lowest walkable roof (or building=<id>): its centre, long axis and roof height; a ground spot 12 m off its west side. */
	bool ReadBuilding(FState& State)
	{
		FString LevelText;
		TSharedPtr<FJsonObject> Level;
		const FString LevelFile = FPaths::ProjectContentDir() / TEXT("Data/LevelJson") / State.MapName + TEXT(".json");
		if (!FFileHelper::LoadFileToString(LevelText, *LevelFile) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(LevelText), Level) || !Level.IsValid()
			|| !Level->HasField(TEXT("levelgen_layout")))
		{
			return false;
		}
		FString Text;
		TSharedPtr<FJsonObject> Layout;
		if (!FFileHelper::LoadFileToString(Text, *(FPaths::ProjectDir() / Level->GetStringField(TEXT("levelgen_layout"))))
			|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Layout) || !Layout.IsValid())
		{
			return false;
		}
		const float GroundZ = Layout->HasField(TEXT("ground_z")) ? static_cast<float>(Layout->GetNumberField(TEXT("ground_z"))) : 0.f;
		int32 BestFloors = TNumericLimits<int32>::Max();
		for (const TSharedPtr<FJsonValue>& BV : Layout->GetArrayField(TEXT("buildings")))
		{
			const TSharedPtr<FJsonObject>& B = BV->AsObject();
			const FString Id = B->GetStringField(TEXT("id"));
			bool bWalkable = false;
			B->TryGetBoolField(TEXT("roof_walkable"), bWalkable);
			const int32 Floors = static_cast<int32>(B->GetNumberField(TEXT("floors")));
			if (State.BuildingArg.IsEmpty() ? !(bWalkable && Floors < BestFloors) : Id != State.BuildingArg)
			{
				continue;
			}
			const TArray<TSharedPtr<FJsonValue>>& Footprint = B->GetArrayField(TEXT("footprint"));
			const FVector2D Min(Footprint[0]->AsArray()[0]->AsNumber(), Footprint[0]->AsArray()[1]->AsNumber());
			const FVector2D Max(Footprint[1]->AsArray()[0]->AsNumber(), Footprint[1]->AsArray()[1]->AsNumber());
			BestFloors = Floors;
			State.BuildingId = Id;
			State.RoofZ = GroundZ + Floors * LevelHeightCm;
			State.RoofCentre = (Min + Max) * 0.5;
			State.RoofAxis = Max.X - Min.X >= Max.Y - Min.Y ? FVector2D(1.0, 0.0) : FVector2D(0.0, 1.0);
			State.GroundSpot = FVector2D(Min.X - 1200.0, State.RoofCentre.Y);
		}
		return !State.BuildingId.IsEmpty();
	}

	void Put(UWorld* World, AOperativeCharacter& Member, const FVector2D& At, float Z)
	{
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		FNavLocation OnNav;
		FVector Spot(At.X, At.Y, Z + 30.f);
		if (Nav && Nav->ProjectPointToNavigation(Spot, OnNav, FVector(150.f, 150.f, 100.f)))
		{
			Spot = OnNav.Location;
		}
		Member.StopOperative();
		Member.TeleportTo(Spot + FVector(0.f, 0.f, Member.GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 5.f), FRotator(0.f, 90.f, 0.f), false, true);
		if (AAIController* AI = Cast<AAIController>(Member.GetController()))
		{
			AI->StopMovement();
		}
	}

	/** Puts the roster in a column along Axis from At (2 m apart), prone. */
	void PlaceColumn(FState& State, UWorld* World, const FVector2D& At, const FVector2D& Axis, float Z)
	{
		for (int32 Index = 0; Index < State.Roster.Num(); ++Index)
		{
			if (AOperativeCharacter* Member = State.Roster[Index].Get())
			{
				Put(World, *Member, At - Axis * (200.0 * Index), Z);
				Member->SetStance(EOperativeStance::Prone);
			}
		}
	}

	float FeetZ(const AOperativeCharacter& Operative)
	{
		return static_cast<float>(Operative.GetActorLocation().Z) - Operative.GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	}

	/** The view the player sees this frame. */
	FMinimalViewInfo PlayerView(APlayerController& PC)
	{
		return PC.PlayerCameraManager ? PC.PlayerCameraManager->GetCameraCacheView() : FMinimalViewInfo();
	}

	/** The operative nearest the view: its distance and its height under the camera. */
	void Nearest(const FState& State, const FVector& View, float& OutDist, float& OutBelow, FString& OutWho)
	{
		OutDist = TNumericLimits<float>::Max();
		OutBelow = 0.f;
		for (const TWeakObjectPtr<AOperativeCharacter>& Weak : State.Roster)
		{
			if (const AOperativeCharacter* Member = Weak.Get())
			{
				const float Dist = static_cast<float>(FVector::Dist(View, Member->GetActorLocation()));
				if (Dist < OutDist)
				{
					OutDist = Dist;
					OutBelow = static_cast<float>(View.Z - Member->GetActorLocation().Z);
					OutWho = Member->DisplayName.ToString();
				}
			}
		}
	}

	void Trace(FState& State, APlayerController& PC, const ATacticalCameraPawn& Camera, const TCHAR* What)
	{
		State.TraceTime += StepSeconds;
		if (State.TraceTime < TraceEverySeconds)
		{
			return;
		}
		State.TraceTime = 0.f;
		const FMinimalViewInfo View = PlayerView(PC);
		float Dist = 0.f;
		float Below = 0.f;
		FString Who;
		Nearest(State, View.Location, Dist, Below, Who);
		const AActor* Target = PC.GetViewTarget();
		UE_LOG(LogCodexTactics, Display, TEXT("CameraMinZoomDepotSmoke trace [%s]: view target %s, camera (%.0f, %.0f, %.0f), nearest operative %s %.0f cm (%.0f cm under it), ")
			TEXT("CurrentDistance %.0f, focus z %.0f, MinSafeDistance %.0f, Pitch %.2f, dolly %.0f, detached %d"),
			What, Target ? *Target->GetName() : TEXT("-"), View.Location.X, View.Location.Y, View.Location.Z, *Who, Dist, Below, Camera.GetCurrentDistance(),
			Camera.GetFocus().Z, Camera.Config.MinSafeDistance, Camera.Config.Pitch, Camera.GetClearanceDolly(), Camera.IsDetachedFromFollow() ? 1 : 0);
	}

	/**
	 * The view the player sees keeps every operative clear: each one on screen (in front, within the clearance cone) is at least the floor away
	 * or at least the floor's height under the camera; no character capsule holds the camera. Framed (optional): that man >= MinFramed away.
	 */
	void JudgeView(FState& State, APlayerController& PC, const ATacticalCameraPawn& Camera, const FString& What, const AOperativeCharacter* Framed, float MinFramed)
	{
		const FMinimalViewInfo View = PlayerView(PC);
		const FVector Forward = View.Rotation.Vector();
		const float Floor = TacticalCameraRules::GetMinSafeDistance(Camera.Config);
		const float Height = TacticalCameraRules::GetMinClearanceHeight(Camera.Config);
		const float ConeCos = FMath::Cos(FMath::DegreesToRadians(Camera.Config.ClearanceConeHalfAngle));
		FString Worst;
		bool bClear = true;
		for (const TWeakObjectPtr<AOperativeCharacter>& Weak : State.Roster)
		{
			const AOperativeCharacter* Member = Weak.Get();
			if (!Member)
			{
				continue;
			}
			const FVector ToMember = Member->GetActorLocation() - View.Location;
			const float Dist = static_cast<float>(ToMember.Size());
			const float Below = static_cast<float>(View.Location.Z - Member->GetActorLocation().Z);
			const bool bOnScreen = Dist < UE_KINDA_SMALL_NUMBER || FVector::DotProduct(ToMember, Forward) / Dist >= ConeCos;
			Worst += FString::Printf(TEXT(" %s %.0f cm / %.0f under%s;"), *Member->DisplayName.ToString(), Dist, Below, bOnScreen ? TEXT(" (on screen)") : TEXT(""));
			if (bOnScreen && Dist < Floor - Slack && Below < Height - Slack)
			{
				bClear = false;
			}
		}
		Check(State, bClear, FString::Printf(TEXT("%s: no operative on screen point-blank (>= %.0f cm away or >= %.0f cm under the camera):%s"), *What, Floor, Height, *Worst));
		FString Inside;
		for (TActorIterator<ACharacter> It(PC.GetWorld()); It; ++It)
		{
			const UCapsuleComponent* Capsule = It->GetCapsuleComponent();
			if (Capsule && !It->IsHidden() && !TacticalCameraRules::PushOutOfCapsule(View.Location, Capsule->GetComponentLocation(), Capsule->GetScaledCapsuleHalfHeight(),
				Capsule->GetScaledCapsuleRadius(), 0.f).Equals(View.Location, 0.5f))
			{
				Inside += It->GetName() + TEXT(" ");
			}
		}
		Check(State, Inside.IsEmpty(), FString::Printf(TEXT("%s: the view is in no character's capsule %s"), *What, *Inside));
		if (Framed)
		{
			const float Dist = static_cast<float>(FVector::Dist(View.Location, Framed->GetActorLocation()));
			Check(State, Dist >= MinFramed - Slack, FString::Printf(TEXT("%s: %s, the man it frames, is %.0f cm from the view (>= %.0f)"), *What, *Framed->DisplayName.ToString(), Dist, MinFramed));
		}
	}

	/** One wheel notch in per step through APlayerController::InputKey (the real mouse wheel path); true once all are in. */
	bool Wheel(FState& State, ACodexTacticsPlayerController& PC)
	{
		if (State.Notches >= WheelNotches)
		{
			return true;
		}
		PC.InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::MouseScrollUp, IE_Pressed, 1.f));
		PC.InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::MouseScrollUp, IE_Released, 0.f));
		++State.Notches;
		return false;
	}

	bool Step(TSharedRef<FState> StateRef)
	{
		FState& State = *StateRef;
		UWorld* World = State.World.Get();
		if (!World || State.Stage == EStage::Done)
		{
			return false;
		}
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(UGameplayStatics::GetPlayerController(World, 0));
		ATacticalCameraPawn* Camera = PC ? Cast<ATacticalCameraPawn>(PC->GetPawn()) : nullptr;
		if (!Squad || !Flow || State.Time > 150.f)
		{
			Check(State, false, FString::Printf(TEXT("squad / flow / timeout (stage %d)"), static_cast<int32>(State.Stage)));
			return Finish(State, false);
		}
		if (State.Stage != EStage::Settle && (!PC || !Camera))
		{
			Check(State, false, TEXT("the player controller and the tactical camera"));
			return Finish(State, false);
		}
		// Nobody fights: the enemies parked and frozen, the squad warm.
		for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
		{
			if (It->CustomTimeDilation != 0.f)
			{
				SmokeUtils::ParkEnemyFarAway(*It);
			}
		}
		for (const TWeakObjectPtr<AOperativeCharacter>& Weak : State.Roster)
		{
			if (AOperativeCharacter* Member = Weak.Get())
			{
				Member->ColdLevel = 0.f;
			}
		}
		AOperativeCharacter* Commander = State.Roster.IsValidIndex(0) ? State.Roster[0].Get() : nullptr;
		auto Next = [&State](EStage Stage)
		{
			State.Stage = Stage;
			State.StageTime = 0.f;
			State.TraceTime = 0.f;
			State.Notches = 0;
		};
		const float WheelMin = Camera ? TacticalCameraRules::GetMinZoomDistance(Camera->Config) : 800.f;
		const float Floor = Camera ? TacticalCameraRules::GetMinSafeDistance(Camera->Config) : 700.f;
		switch (State.Stage)
		{
		case EStage::Settle:
		{
			const ULevelExitSubsystem* Exit = World->GetSubsystem<ULevelExitSubsystem>();
			if (!Squad->GetLeader() || !Camera || State.StageTime < 3.5f
				|| (Exit && Exit->IsStartFadeResolved() && Exit->GetStartFadeAlpha() > 0.f && State.StageTime < 15.f))
			{
				return true;
			}
			TArray<AOperativeCharacter*> Members = Squad->GetMembers();
			Members.Remove(nullptr);
			Members.Sort([](const AOperativeCharacter& A, const AOperativeCharacter& B) { return A.SquadIndex < B.SquadIndex; });
			if (!ReadBuilding(State) || Members.Num() < 3)
			{
				Check(State, false, FString::Printf(TEXT("a walkable roof in %s's layout and 3 operatives (%d)"), *State.MapName, Members.Num()));
				return Finish(State, false);
			}
			for (int32 Index = 0; Index < 3; ++Index)
			{
				State.Roster.Add(Members[Index]);
			}
			UE_LOG(LogCodexTactics, Display, TEXT("CameraMinZoomDepotSmoke: roof %s at %.0f cm, centre (%.0f, %.0f); ground spot (%.0f, %.0f); Codex.Camera.OperativeClearance %d"),
				*State.BuildingId, State.RoofZ, State.RoofCentre.X, State.RoofCentre.Y, State.GroundSpot.X, State.GroundSpot.Y,
				ATacticalCameraPawn::IsOperativeClearanceEnabled() ? 1 : 0);
			if (Squad->IsSoloMode())
			{
				Squad->ExitSoloMode(false, 0.f);
			}
			SmokeUtils::StartWaveFight(Flow); // the fight view (the designer: PHASE COMBAT, real time)
			Check(State, Flow->GetPhase() != ECodexGamePhase::Exploration && Flow->GetCombatMode() != ECodexCombatMode::TurnBased,
				FString::Printf(TEXT("the fight runs in real time (phase %d, mode %d)"), static_cast<int32>(Flow->GetPhase()), static_cast<int32>(Flow->GetCombatMode())));
			const FVector Spot = SmokeUtils::FreeSpot(World, FVector(State.GroundSpot.X, State.GroundSpot.Y, 0.f), nullptr);
			State.GroundSpot = FVector2D(Spot.X, Spot.Y);
			PlaceColumn(State, World, State.GroundSpot, FVector2D(0.0, 1.0), 0.f);
			Squad->SetLeader(State.Roster[0].Get());
			Next(EStage::PlaceGround);
			return true;
		}
		case EStage::PlaceGround:
			if (State.StageTime < 2.f)
			{
				return true;
			}
			Camera->ReattachToFollowTarget();
			Next(EStage::WheelGround);
			return true;
		case EStage::WheelGround:
			Trace(State, *PC, *Camera, TEXT("ground"));
			if (!Wheel(State, *PC) || State.StageTime < 5.f)
			{
				return true;
			}
			Next(EStage::JudgeGround);
			return true;
		case EStage::JudgeGround:
		{
			const FMinimalViewInfo View = PlayerView(*PC);
			const float ToCentre = static_cast<float>(FVector::Dist(View.Location, Camera->GetViewCentre()));
			Check(State, ToCentre >= WheelMin - Slack, FString::Printf(TEXT("1. ground, %d notches in: the view %.0f cm from the view centre (>= the wheel's %.0f)"), WheelNotches, ToCentre, WheelMin));
			Check(State, Camera->GetClearanceDolly() < 1.f, FString::Printf(TEXT("1. ground: the operative clearance does not move the camera on flat ground (%.0f cm)"), Camera->GetClearanceDolly()));
			JudgeView(State, *PC, *Camera, TEXT("1. ground, prone"), Commander, WheelMin);
			State.GroundLeaderDistance = Commander ? static_cast<float>(FVector::Dist(View.Location, Commander->GetActorLocation())) : 0.f;
			Next(EStage::Detach);
			return true;
		}
		case EStage::Detach:
			// A pan (the screen edge / WASD) in the fight view: the focus freezes - its height too.
			Camera->NudgePan(FVector(20.f, 0.f, 0.f));
			Check(State, Camera->IsDetachedFromFollow(), TEXT("2. a manual pan in the fight view holds the camera (detached)"));
			// 2a first proves the operative clearance alone: the held focus keeps the ground height (the old rule) until 2b.
			State.SavedHeightRadius = Camera->Config.DetachedHeightRadius;
			Camera->Config.DetachedHeightRadius = 0.f;
			PlaceColumn(State, World, State.RoofCentre + State.RoofAxis * 200.0, State.RoofAxis, State.RoofZ);
			Next(EStage::PlaceRoof);
			return true;
		case EStage::PlaceRoof:
		{
			if (State.StageTime < 2.f)
			{
				return true;
			}
			bool bOnRoof = true;
			FString Feet;
			for (const TWeakObjectPtr<AOperativeCharacter>& Weak : State.Roster)
			{
				const AOperativeCharacter* Member = Weak.Get();
				bOnRoof &= Member && FMath::Abs(FeetZ(*Member) - State.RoofZ) <= 60.f && Member->GetStance() == EOperativeStance::Prone;
				Feet += Member ? FString::Printf(TEXT(" %s %.0f"), *Member->DisplayName.ToString(), FeetZ(*Member)) : TEXT(" -");
			}
			Check(State, bOnRoof, FString::Printf(TEXT("2. the squad prone on %s's roof (%.0f cm), feet:%s"), *State.BuildingId, State.RoofZ, *Feet));
			if (!bOnRoof || !Commander)
			{
				return Finish(State, false);
			}
			Next(EStage::PanOnto);
			return true;
		}
		case EStage::PanOnto:
		{
			// The view panned so its axis runs through the Commander (from the focus height the camera holds).
			const FVector Centre = Camera->GetViewCentre();
			const FVector Man = Commander->GetActorLocation();
			const FVector Planar = FVector(FRotator(0.f, Camera->GetCurrentYaw(), 0.f).Vector());
			const float Tan = FMath::Tan(FMath::DegreesToRadians(FMath::Abs(Camera->Config.Pitch)));
			const FVector Wanted = Man + Planar * ((Man.Z - Camera->GetFocus().Z) / FMath::Max(Tan, 0.1f));
			Camera->NudgePan(FVector(Wanted.X - Centre.X, Wanted.Y - Centre.Y, 0.f));
			UE_LOG(LogCodexTactics, Display, TEXT("CameraMinZoomDepotSmoke: panned onto %s (%.0f, %.0f, %.0f) from the held focus z %.0f"), *Commander->DisplayName.ToString(),
				Man.X, Man.Y, Man.Z, Camera->GetFocus().Z);
			Next(EStage::WheelChain);
			return true;
		}
		case EStage::WheelChain:
			Trace(State, *PC, *Camera, TEXT("roof, held view"));
			if (State.StageTime < 2.f || !Wheel(State, *PC) || State.StageTime < 7.f)
			{
				return true;
			}
			Next(EStage::JudgeChain);
			return true;
		case EStage::JudgeChain:
			Check(State, Camera->IsDetachedFromFollow(), TEXT("2. the camera is still held by the pan"));
			Check(State, Camera->GetFocus().Z < State.RoofZ - 100.f, FString::Printf(TEXT("2a. the held focus kept the ground height (z %.0f) - the designer's view"), Camera->GetFocus().Z));
			Check(State, Camera->GetClearanceDolly() > 1.f, FString::Printf(TEXT("2a. the operative clearance holds the camera back (this frame's correction %.0f cm)"), Camera->GetClearanceDolly()));
			JudgeView(State, *PC, *Camera, TEXT("2a. the designer's chain (held view on the roof, wheel in), the clearance alone"), nullptr, 0.f);
			Camera->Config.DetachedHeightRadius = State.SavedHeightRadius;
			Next(EStage::HeldHeight);
			return true;
		case EStage::HeldHeight:
			Trace(State, *PC, *Camera, TEXT("roof, held view, height"));
			if (State.StageTime < 3.f)
			{
				return true;
			}
			Next(EStage::JudgeHeldHeight);
			return true;
		case EStage::JudgeHeldHeight:
		{
			const float ToCentre = static_cast<float>(FVector::Dist(PlayerView(*PC).Location, Camera->GetViewCentre()));
			Check(State, FMath::Abs(Camera->GetFocus().Z - (Commander ? Commander->GetActorLocation().Z : 0.f)) <= 10.f,
				FString::Printf(TEXT("2b. the held focus took the roof's height: z %.0f, the Commander %.0f"), Camera->GetFocus().Z, Commander ? Commander->GetActorLocation().Z : 0.f));
			Check(State, Camera->GetClearanceDolly() < 1.f && ToCentre >= WheelMin - Slack,
				FString::Printf(TEXT("2b. ... the wheel's minimum is measured from the roof: %.0f cm from the view centre, no clearance needed (%.0f)"), ToCentre, Camera->GetClearanceDolly()));
			JudgeView(State, *PC, *Camera, TEXT("2b. the designer's chain, the held focus on the roof"), nullptr, 0.f);
			Next(EStage::Reattach);
			return true;
		}
		case EStage::Reattach:
			Camera->ReattachToFollowTarget(); // the number key / a portrait click
			Next(EStage::WheelRoof);
			return true;
		case EStage::WheelRoof:
			Trace(State, *PC, *Camera, TEXT("roof, on the Commander"));
			if (State.StageTime < 2.f || !Wheel(State, *PC) || State.StageTime < 7.f)
			{
				return true;
			}
			Next(EStage::JudgeRoof);
			return true;
		case EStage::JudgeRoof:
		{
			JudgeView(State, *PC, *Camera, TEXT("3. on the Commander on the roof, wheel in"), Commander, WheelMin);
			const float Dist = Commander ? static_cast<float>(FVector::Dist(PlayerView(*PC).Location, Commander->GetActorLocation())) : 0.f;
			Check(State, FMath::Abs(Dist - State.GroundLeaderDistance) <= 25.f, FString::Printf(TEXT("3. the wheel's closest view of the Commander: %.0f cm on the roof, %.0f on the ground"),
				Dist, State.GroundLeaderDistance));
			Camera->SmoothFocusOnTarget(Commander, 0.2f, 100.f); // focus / death cam / enemy focus glides with a tiny distance
			Next(EStage::Glide);
			return true;
		}
		case EStage::Glide:
			Trace(State, *PC, *Camera, TEXT("glide"));
			if (State.StageTime < 2.f)
			{
				return true;
			}
			Next(EStage::JudgeGlide);
			return true;
		case EStage::JudgeGlide:
			JudgeView(State, *PC, *Camera, TEXT("4a. a 100 cm glide onto the Commander"), Commander, Floor);
			if (AOperativeCharacter* Second = State.Roster[1].Get())
			{
				Camera->SnapFocusOnTarget(Second, 50.f);
			}
			Next(EStage::Snap);
			return true;
		case EStage::Snap:
			Trace(State, *PC, *Camera, TEXT("snap"));
			if (State.StageTime < 1.f)
			{
				return true;
			}
			Next(EStage::JudgeSnap);
			return true;
		case EStage::JudgeSnap:
			JudgeView(State, *PC, *Camera, TEXT("4b. a 50 cm snap onto the second man"), State.Roster[1].Get(), Floor);
			Squad->SetLeader(State.Roster[2].Get()); // switching the selected operative
			Next(EStage::Switch);
			return true;
		case EStage::Switch:
			Trace(State, *PC, *Camera, TEXT("switch"));
			if (State.StageTime < 2.5f)
			{
				return true;
			}
			Next(EStage::JudgeSwitch);
			return true;
		case EStage::JudgeSwitch:
			JudgeView(State, *PC, *Camera, TEXT("4c. the third man selected"), State.Roster[2].Get(), Floor);
			Camera->EnterTurnBasedZoom(100.f);
			Next(EStage::TurnBasedZoom);
			return true;
		case EStage::TurnBasedZoom:
			Trace(State, *PC, *Camera, TEXT("turn-based zoom"));
			if (State.StageTime < 2.f)
			{
				return true;
			}
			Next(EStage::JudgeTurnBased);
			return true;
		case EStage::JudgeTurnBased:
			JudgeView(State, *PC, *Camera, TEXT("4d. the turn-based entry zoom at 100 cm"), State.Roster[2].Get(), Floor);
			Camera->ExitTurnBasedZoom();
			return Finish(State, true);
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
		TSharedRef<FState> State = MakeShared<FState>();
		State->World = World;
		State->MapName = World->GetMapName().Replace(TEXT("UEDPIE_0_"), TEXT(""));
		for (const FString& Arg : Args)
		{
			if (Arg.StartsWith(TEXT("building=")))
			{
				State->BuildingArg = Arg.Mid(9);
			}
		}
		if (IConsoleVariable* Pressure = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Pressure.Enabled")))
		{
			Pressure->Set(0, ECVF_SetByConsole);
		}
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float)
		{
			return Step(State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.CameraMinZoomDepotSmoke"),
		TEXT("Dev check (bug #52 on L_Gen_Depot_1, 2026-11-08): combat, the squad prone on the ground then on a roof, 50 wheel notches in (also with a pan-held view), ")
		TEXT("glides / snaps / a selection switch / the turn-based zoom - no operative on screen point-blank, no capsule; [building=<id>]. PASS/FAIL, exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
