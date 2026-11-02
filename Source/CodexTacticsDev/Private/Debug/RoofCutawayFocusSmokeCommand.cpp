// Dev-only check that the roof cutaway follows the operative the player WORKS WITH (the game designer's bug report 2026-11-02 on
// L_Gen_Depot_1, item 8: "2 operatives on a roof and 1 on the ground - the roof was rendered see-through; it must follow the selected man, or
// the leader in group mode: the selected man on the roof -> the roof stays; switched to the man under the roof -> the roof above him is cut").
// docs/systems/camera-hud-minimap.md "Roof cutaway". URoofCutawaySubsystem + UMinimapSubsystem (its displayed floor), pressure off, Solo
// (nobody walks off). The building: the first walkable-roof hall of the map's accepted layout (L_Gen_Depot_1: B_hall_n, roof +3 = 9 m).
// Two men are put on the roof, one on the ground floor under it:
//  1. the roof man A selected: the roof slab over the ground man is NOT cut away (nothing is cut), the minimap shows the roof floor;
//  2. the ground man selected: that slab IS cut away, the minimap shows floor 0;
//  3. the roof man B selected: the slab comes back (the 0.5 s hysteresis), the minimap the roof floor again;
//  4. all three box-selected, the leader on the ground: cut; 5. the same group, the leader on the roof: not cut.
// PASS / FAIL, then exits.
//   powershell -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -Commands RoofCutawayFocusSmoke -ExtraArgs "-Multiprocess"
//   (map /Game/Maps/Generated/L_Gen_Depot_1, "-LevelJson=L_Gen_Depot_1.json" - Scripts/test_map.json smokeOptions)
// Arguments: building=<layout building id>. Godot reference: none (UE-only).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AIController.h"
#include "Camera/RoofCutawaySubsystem.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "CollisionQueryParams.h"
#include "Components/CapsuleComponent.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "GameFlow/LevelExitSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "NavigationSystem.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UI/MinimapSubsystem.h"

namespace RoofCutawayFocusSmoke
{
	constexpr float StepSeconds = 0.05f;
	constexpr float LevelHeightCm = 300.f;
	/** Each selection is judged this long after it was made (> the 0.5 s restore hysteresis + a 0.1 s update + the minimap's tick), s. */
	constexpr float JudgeAfterSeconds = 1.5f;

	enum class EStage : uint8 { Settle, Place, SelectRoofA, SelectGround, SelectRoofB, GroupGround, GroupRoof, Done };

	struct FState
	{
		TWeakObjectPtr<UWorld> World;
		FString MapName;
		FString BuildingArg;
		EStage Stage = EStage::Settle;
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Failures = 0;
		float GroundZ = 0.f;
		float RoofZ = 0.f;
		int32 RoofFloor = 0;
		FVector2D RoofA = FVector2D::ZeroVector;
		FVector2D RoofB = FVector2D::ZeroVector;
		FVector2D Ground = FVector2D::ZeroVector;
		FString BuildingId;
		TWeakObjectPtr<AOperativeCharacter> ManA;
		TWeakObjectPtr<AOperativeCharacter> ManB;
		TWeakObjectPtr<AOperativeCharacter> ManGround;
		TWeakObjectPtr<UPrimitiveComponent> Slab;
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
		FPlatformMisc::RequestExit(false, TEXT("RoofCutawayFocusSmoke"));
		return false;
	}

	float FeetZ(const AOperativeCharacter& Operative)
	{
		return static_cast<float>(Operative.GetActorLocation().Z) - Operative.GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	}

	/** The accepted layout's first enterable hall with a walkable roof (or building=<id>): its footprint, roof level. */
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
		State.GroundZ = Layout->HasField(TEXT("ground_z")) ? static_cast<float>(Layout->GetNumberField(TEXT("ground_z"))) : 0.f;
		for (const TSharedPtr<FJsonValue>& BV : Layout->GetArrayField(TEXT("buildings")))
		{
			const TSharedPtr<FJsonObject>& B = BV->AsObject();
			const FString Id = B->GetStringField(TEXT("id"));
			bool bWalkable = false;
			bool bEnterable = false;
			bool bHollow = false;
			B->TryGetBoolField(TEXT("roof_walkable"), bWalkable);
			B->TryGetBoolField(TEXT("enterable"), bEnterable);
			B->TryGetBoolField(TEXT("hollow"), bHollow);
			if (State.BuildingArg.IsEmpty() ? !(bWalkable && bEnterable && bHollow) : Id != State.BuildingArg)
			{
				continue;
			}
			const TArray<TSharedPtr<FJsonValue>>& Footprint = B->GetArrayField(TEXT("footprint"));
			const FVector2D Min(Footprint[0]->AsArray()[0]->AsNumber(), Footprint[0]->AsArray()[1]->AsNumber());
			const FVector2D Max(Footprint[1]->AsArray()[0]->AsNumber(), Footprint[1]->AsArray()[1]->AsNumber());
			const FVector2D Centre = (Min + Max) * 0.5;
			// Along the footprint's long axis: the two roof men 3 m either side of the middle, the ground man under the middle.
			const FVector2D Axis = Max.X - Min.X >= Max.Y - Min.Y ? FVector2D(1.0, 0.0) : FVector2D(0.0, 1.0);
			State.BuildingId = Id;
			State.RoofFloor = static_cast<int32>(B->GetNumberField(TEXT("floors")));
			State.RoofZ = State.GroundZ + State.RoofFloor * LevelHeightCm;
			State.RoofA = Centre - Axis * 300.0;
			State.RoofB = Centre + Axis * 300.0;
			State.Ground = Centre;
			return true;
		}
		return false;
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
		Member.TeleportTo(Spot + FVector(0.f, 0.f, Member.GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 5.f), Member.GetActorRotation(), false, true);
		if (AAIController* AI = Cast<AAIController>(Member.GetController()))
		{
			AI->StopMovement();
		}
	}

	/** The roof piece over the ground man: the first static piece above his head + 40 cm (object query: a cut piece is still found). */
	UPrimitiveComponent* FindSlabAbove(UWorld* World, const AOperativeCharacter& Man)
	{
		const FVector Head = Man.GetActorLocation() + FVector(0.f, 0.f, Man.GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
		FCollisionObjectQueryParams Objects;
		Objects.AddObjectTypesToQuery(ECC_WorldStatic);
		Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
		FCollisionQueryParams Params(SCENE_QUERY_STAT(RoofFocusSmoke), false, &Man);
		for (int32 Step = 0; Step < 8; ++Step)
		{
			FHitResult Hit;
			if (!World->LineTraceSingleByObjectType(Hit, Head + FVector(0.f, 0.f, 10.f), Head + FVector(0.f, 0.f, 1500.f), Objects, Params) || !Hit.GetComponent())
			{
				return nullptr;
			}
			if (Hit.GetComponent()->Bounds.GetBox().Min.Z >= Head.Z + 40.f && !Cast<APawn>(Hit.GetActor()))
			{
				return Hit.GetComponent();
			}
			Params.AddIgnoredComponent(Hit.GetComponent());
		}
		return nullptr;
	}

	FString Name(const TWeakObjectPtr<AOperativeCharacter>& Man)
	{
		return Man.IsValid() ? Man->DisplayName.ToString() : TEXT("-");
	}

	/** Judges the current selection: the slab cut (bExpectCut) and the minimap's floor. */
	void Judge(FState& State, UWorld* World, bool bExpectCut, int32 ExpectFloor, const FString& What)
	{
		const URoofCutawaySubsystem* Roofs = World->GetSubsystem<URoofCutawaySubsystem>();
		const UMinimapSubsystem* Minimap = World->GetSubsystem<UMinimapSubsystem>();
		const USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		const UPrimitiveComponent* Slab = State.Slab.Get();
		const bool bCut = Roofs && Slab && Roofs->IsCutAway(Slab);
		const bool bDrawn = Slab && Slab->bRenderInMainPass;
		Check(State, Slab && bCut == bExpectCut && bDrawn != bExpectCut, FString::Printf(TEXT("%s (leader %s, %d selected): the roof over the ground man %s (cut %d, drawn %d, %d pieces cut)"),
			*What, Squad && Squad->GetLeader() ? *Squad->GetLeader()->DisplayName.ToString() : TEXT("-"), Squad ? Squad->GetSelectedGroup().Num() : 0,
			bExpectCut ? TEXT("is CUT AWAY") : TEXT("stays VISIBLE"), bCut ? 1 : 0, bDrawn ? 1 : 0, Roofs ? Roofs->GetCutAwayCount() : -1));
		if (!bExpectCut)
		{
			Check(State, Roofs && Roofs->GetCutAwayCount() == 0, FString::Printf(TEXT("%s: nothing cut at all (%d)"), *What, Roofs ? Roofs->GetCutAwayCount() : -1));
		}
		if (Minimap && Minimap->GetFloorCount() > 1)
		{
			const int32 Expected = FMath::Min(ExpectFloor, Minimap->GetFloorCount() - 1);
			Check(State, Minimap->GetDisplayedFloor() == Expected, FString::Printf(TEXT("%s: the minimap shows floor %d (expected %d)"), *What, Minimap->GetDisplayedFloor(),
				Expected));
		}
	}

	void Select(USquadSubsystem& Squad, AOperativeCharacter* Man, bool bGroup)
	{
		if (bGroup)
		{
			Squad.SetSelectedGroup(Squad.GetMembers(), false);
			Squad.SetLeader(Man, /*bResetGroup*/ false);
		}
		else
		{
			Squad.SetLeader(Man);
		}
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
		if (!Squad || State.Time > 180.f)
		{
			Check(State, false, FString::Printf(TEXT("squad / timeout (stage %d)"), static_cast<int32>(State.Stage)));
			return Finish(State, false);
		}
		auto Next = [&State](EStage Stage)
		{
			State.Stage = Stage;
			State.StageTime = 0.f;
		};
		switch (State.Stage)
		{
		case EStage::Settle:
		{
			const ULevelExitSubsystem* Exit = World->GetSubsystem<ULevelExitSubsystem>();
			if (!Squad->GetLeader() || State.StageTime < 3.5f || (Exit && Exit->IsStartFadeResolved() && Exit->GetStartFadeAlpha() > 0.f && State.StageTime < 15.f))
			{
				return true;
			}
			TArray<AOperativeCharacter*> Members = Squad->GetMembers();
			Members.Remove(nullptr);
			if (!ReadBuilding(State) || Members.Num() < 3)
			{
				Check(State, false, FString::Printf(TEXT("a walkable-roof hall in %s's layout and 3 operatives (%d)"), *State.MapName, Members.Num()));
				return Finish(State, false);
			}
			if (!Squad->IsSoloMode())
			{
				Squad->EnterSoloMode();
			}
			Squad->SetFollowersHolding(true);
			State.ManA = Members[0];
			State.ManB = Members[1];
			State.ManGround = Members[2];
			Put(World, *Members[0], State.RoofA, State.RoofZ);
			Put(World, *Members[1], State.RoofB, State.RoofZ);
			Put(World, *Members[2], State.Ground, State.GroundZ);
			Next(EStage::Place);
			return true;
		}
		case EStage::Place:
		{
			if (State.StageTime < 1.5f)
			{
				return true;
			}
			AOperativeCharacter* Ground = State.ManGround.Get();
			State.Slab = Ground ? FindSlabAbove(World, *Ground) : nullptr;
			const bool bPlaced = State.ManA.IsValid() && State.ManB.IsValid() && Ground && FMath::Abs(FeetZ(*State.ManA) - State.RoofZ) <= 40.f
				&& FMath::Abs(FeetZ(*State.ManB) - State.RoofZ) <= 40.f && FMath::Abs(FeetZ(*Ground) - State.GroundZ) <= 40.f;
			Check(State, bPlaced, FString::Printf(TEXT("%s: %s and %s on the roof (feet %.0f / %.0f, roof %.0f), %s on the ground floor under it (feet %.0f)"),
				*State.BuildingId, *Name(State.ManA), *Name(State.ManB), State.ManA.IsValid() ? FeetZ(*State.ManA) : -1.f, State.ManB.IsValid() ? FeetZ(*State.ManB) : -1.f,
				State.RoofZ, *Name(State.ManGround), Ground ? FeetZ(*Ground) : -1.f));
			Check(State, State.Slab.IsValid(), FString::Printf(TEXT("the roof piece over the ground man: %s.%s (bottom z %.0f)"),
				State.Slab.IsValid() && State.Slab->GetOwner() ? *State.Slab->GetOwner()->GetName() : TEXT("-"), State.Slab.IsValid() ? *State.Slab->GetName() : TEXT("-"),
				State.Slab.IsValid() ? State.Slab->Bounds.GetBox().Min.Z : 0.f));
			if (!bPlaced || !State.Slab.IsValid())
			{
				return Finish(State, false);
			}
			Select(*Squad, State.ManA.Get(), false);
			Next(EStage::SelectRoofA);
			return true;
		}
		case EStage::SelectRoofA:
			if (State.StageTime < JudgeAfterSeconds)
			{
				return true;
			}
			Judge(State, World, false, State.RoofFloor, FString::Printf(TEXT("1. roof man %s selected"), *Name(State.ManA)));
			Select(*Squad, State.ManGround.Get(), false);
			Next(EStage::SelectGround);
			return true;
		case EStage::SelectGround:
			if (State.StageTime < JudgeAfterSeconds)
			{
				return true;
			}
			Judge(State, World, true, 0, FString::Printf(TEXT("2. ground man %s selected"), *Name(State.ManGround)));
			Select(*Squad, State.ManB.Get(), false);
			Next(EStage::SelectRoofB);
			return true;
		case EStage::SelectRoofB:
			if (State.StageTime < JudgeAfterSeconds)
			{
				return true;
			}
			Judge(State, World, false, State.RoofFloor, FString::Printf(TEXT("3. roof man %s selected (switched back)"), *Name(State.ManB)));
			Select(*Squad, State.ManGround.Get(), true);
			Next(EStage::GroupGround);
			return true;
		case EStage::GroupGround:
			if (State.StageTime < JudgeAfterSeconds)
			{
				return true;
			}
			Judge(State, World, true, 0, FString::Printf(TEXT("4. all three selected, the leader %s on the ground"), *Name(State.ManGround)));
			Select(*Squad, State.ManA.Get(), true);
			Next(EStage::GroupRoof);
			return true;
		case EStage::GroupRoof:
			if (State.StageTime < JudgeAfterSeconds)
			{
				return true;
			}
			Judge(State, World, false, State.RoofFloor, FString::Printf(TEXT("5. all three selected, the leader %s on the roof"), *Name(State.ManA)));
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
		TEXT("CodexTactics.RoofCutawayFocusSmoke"),
		TEXT("Dev check (bug 2026-11-02, item 8): on a generated map with 2 men on a hall roof and 1 under it, the roof cutaway and the minimap floor follow the ")
		TEXT("SELECTED man (the leader in group mode): roof man -> roof visible, ground man -> cut, back -> visible; [building=<id>]. PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
