// Dev-only headless check of the squad going UP a generated map's ramps (the game designer's bug report 2026-11-02 on L_Gen_Depot_1, item 7:
// "the squad stands together at the foot of a ramp; one operative ordered up onto the roof - 'Run to position!', 'Group (3 operatives) moving
// into position!' - they all just get STUCK at the bottom; moved aside and ordered again, 2 of 3 went up and 1 stayed on the ground").
// docs/systems/squad-and-orders.md "The formation across height levels", docs/design/levelgen/LEVEL_GENERATOR.md 7 (external stairs /
// stairwells). The stairs are the accepted layout's (the level JSON's "levelgen_layout"); with the pressure and the PMC encounters off (Codex.Pressure.Enabled 0,
// Codex.Pmc.Encounters 0),
// per stair from the ground (every external stair and stairwell of the layout; a stair whose ramp navmesh does not join its foot to its top is a
// layout defect - reported as a WARN line and skipped, strictnav = a FAIL):
//  1. squad  - squad mode, the squad bunched in the stair's mouth; the man at the BACK is selected (he becomes the leader) and the player's
//              click (HandleWorldHit) on the roof sends him up: every member ends on the stair's top level, nobody stuck at the bottom;
//  2. group  - squad mode, all three box-selected (the HUD's "Group (3 operatives) moving into position!"), a double click (sprint) on the
//              roof right past the stair's top: every member ends on the top level (no group target falls off the roof edge);
//  3. solo   - Solo, the man at the back selected and sent up: he ends on the top level, the others stay on the ground.
// A member standing still for 10 s off the top level (squad legs) fails the leg with every member's position, order and path state.
// PASS / FAIL, then exits.
//   powershell -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -Commands LevelGenRampSmoke -ExtraArgs "-Multiprocess"
//   (map /Game/Maps/Generated/L_Gen_Depot_1, "-LevelJson=L_Gen_Depot_1.json" - Scripts/test_map.json smokeOptions)
// Arguments: only=<stair id>, legs=<squad,group,solo> (a subset), strictnav, trace (every member every 0.5 s). Godot reference: none (UE-only).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AIController.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "GameFlow/LevelExitSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace LevelGenRampSmoke
{
	constexpr float StepSeconds = 0.05f;
	constexpr float LevelHeightCm = 300.f;   // schema.ts LEVEL_HEIGHT
	constexpr float LevelTolCm = 40.f;       // feet on the level
	constexpr float StillFailSeconds = 10.f; // a member still this long off the top level = stuck
	constexpr float SoloSettleSeconds = 4.f; // Solo: the others watched this long after the ordered man arrived
	constexpr float TotalLimitSeconds = 1500.f;

	enum class ELegKind : uint8 { Squad, Group, Solo };

	struct FStair
	{
		FString Id;
		FString Kind;
		FVector2D Foot = FVector2D::ZeroVector; // the ramp's low end ("pos")
		FVector2D Dir = FVector2D(1.0, 0.0);    // up the ramp (pos -> mid / top)
		FVector2D Top = FVector2D::ZeroVector;  // the layout's "top" (on the upper level)
		FVector2D Inward = FVector2D(1.0, 0.0); // from the stair's top onto the roof
		int32 LevelFrom = 0;
		int32 LevelTo = 0;
		FBox2D Roof = FBox2D(ForceInit);         // the building's footprint
		TArray<FVector2D> Holes;                  // roof holes on the top level (half 120 / stairwell openings)
	};

	struct FLeg
	{
		ELegKind Kind = ELegKind::Squad;
		int32 Stair = 0;
		FVector Goal = FVector::ZeroVector;
		float Limit = 120.f;
	};

	enum class EPhase : uint8 { Settle, Setup, Order, Run, Done };

	struct FState
	{
		TWeakObjectPtr<UWorld> World;
		FString MapName;
		float GroundZ = 0.f;
		TArray<FStair> Stairs;
		TArray<FLeg> Legs;
		int32 Leg = 0;
		EPhase Phase = EPhase::Settle;
		float Time = 0.f;
		float PhaseTime = 0.f;
		float DoneFor = 0.f;
		float OrderedUpFor = 0.f;
		TMap<TWeakObjectPtr<AOperativeCharacter>, float> StillFor;
		TMap<TWeakObjectPtr<AOperativeCharacter>, FVector> LastPos;
		TWeakObjectPtr<AOperativeCharacter> Ordered;
		int32 Failures = 0;
		int32 LegsOk = 0;
		TArray<FString> Summary;
		FString Only;
		bool bStrictNav = false;
		bool bTrace = false;
		float TraceClock = 0.f;
		TSet<FString> LegFilter;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State, bool bComplete)
	{
		for (const FString& Line : State.Summary)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke summary: %s"), *Line);
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke: LevelGenRampSmoke on %s: %d / %d legs passed, %d failures"), *State.MapName, State.LegsOk, State.Legs.Num(),
			State.Failures);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		State.Phase = EPhase::Done;
		FPlatformMisc::RequestExit(false, TEXT("LevelGenRampSmoke"));
		return false;
	}

	const TCHAR* KindName(ELegKind Kind)
	{
		switch (Kind)
		{
		case ELegKind::Squad: return TEXT("squad (the man at the back selected)");
		case ELegKind::Group: return TEXT("group (all three box-selected, sprint)");
		case ELegKind::Solo: return TEXT("solo (only he goes up)");
		}
		return TEXT("?");
	}

	FVector2D ReadPoint(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
		return Object->TryGetArrayField(Field, Array) && Array->Num() >= 2 ? FVector2D((*Array)[0]->AsNumber(), (*Array)[1]->AsNumber()) : FVector2D::ZeroVector;
	}

	TSharedPtr<FJsonObject> LoadLayout(const FString& MapName, FString& Error)
	{
		FString LevelText;
		TSharedPtr<FJsonObject> Level;
		FString File;
		const FString LevelFile = FPaths::ProjectContentDir() / TEXT("Data/LevelJson") / MapName + TEXT(".json");
		if (FFileHelper::LoadFileToString(LevelText, *LevelFile) && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(LevelText), Level) && Level.IsValid()
			&& Level->HasField(TEXT("levelgen_layout")))
		{
			File = FPaths::ProjectDir() / Level->GetStringField(TEXT("levelgen_layout"));
		}
		FString Text;
		TSharedPtr<FJsonObject> Layout;
		if (File.IsEmpty() || !FFileHelper::LoadFileToString(Text, *File) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Layout) || !Layout.IsValid())
		{
			Error = FString::Printf(TEXT("no accepted layout for %s (%s)"), *MapName, *File);
			return nullptr;
		}
		return Layout;
	}

	float FeetZ(const AOperativeCharacter& Operative)
	{
		return static_cast<float>(Operative.GetActorLocation().Z) - Operative.GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	}

	FVector OnNav(UWorld* World, const FVector& P)
	{
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		FNavLocation Out;
		return Nav && Nav->ProjectPointToNavigation(P, Out, FVector(120.f, 120.f, 150.f)) ? Out.Location : P;
	}

	FString PathNote(UWorld* World, const FVector& From, const FVector& To, AActor* Context)
	{
		UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(World, From, To, Context);
		if (!Path || !Path->IsValid())
		{
			return TEXT("no navmesh path");
		}
		const FVector End = Path->PathPoints.Num() ? Path->PathPoints.Last() : From;
		return FString::Printf(TEXT("path %s, %d points, ends %.0f cm from the goal at z %.0f"), Path->IsPartial() ? TEXT("PARTIAL") : TEXT("complete"),
			Path->PathPoints.Num(), FVector::Dist2D(End, To), End.Z);
	}

	/** The goal on the roof: Inward of the stair's top, kept clear of the roof's holes and 2.5 m inside its edges. */
	FVector RoofGoal(const FState& State, const FStair& Stair, float InwardCm)
	{
		const float TopZ = State.GroundZ + Stair.LevelTo * LevelHeightCm;
		FVector2D Goal = Stair.Top + Stair.Inward * InwardCm;
		const FBox2D Inner(Stair.Roof.Min + FVector2D(250.0), Stair.Roof.Max - FVector2D(250.0));
		for (int32 Try = 0; Try < 8; ++Try)
		{
			bool bClear = Inner.IsInside(Goal);
			for (const FVector2D& Hole : Stair.Holes)
			{
				bClear &= FVector2D::Distance(Goal, Hole) > 450.0;
			}
			if (bClear)
			{
				break;
			}
			Goal += FVector2D(-Stair.Dir.Y, Stair.Dir.X) * 200.0; // sideways along the roof
			Goal = FVector2D(FMath::Clamp(Goal.X, Inner.Min.X, Inner.Max.X), FMath::Clamp(Goal.Y, Inner.Min.Y, Inner.Max.Y));
		}
		return FVector(Goal.X, Goal.Y, TopZ + 20.f);
	}

	/** Diagnosis: the navmesh along the ramp (every metre at its height) and a path from its foot to its top level. */
	bool LogRampNav(UWorld* World, const FState& State, const FStair& Stair, const FVector2D& RampEnd)
	{
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		if (!Nav)
		{
			return false;
		}
		bool bConnected = true;
		const float Z0 = State.GroundZ + Stair.LevelFrom * LevelHeightCm;
		const float Z1 = State.GroundZ + Stair.LevelTo * LevelHeightCm;
		const double Length = FVector2D::Distance(Stair.Foot, RampEnd);
		FString Samples;
		for (double D = 0.0; D <= Length + 1.0; D += 100.0)
		{
			const double T = FMath::Clamp(D / FMath::Max(Length, 1.0), 0.0, 1.0);
			const FVector2D P = Stair.Foot + Stair.Dir * D;
			FNavLocation Out;
			const bool bOn = Nav->ProjectPointToNavigation(FVector(P.X, P.Y, FMath::Lerp(Z0, Z1, static_cast<float>(T)) + 20.f), Out, FVector(30.f, 30.f, 60.f));
			Samples += bOn ? FString::Printf(TEXT(" %.0f"), Out.Location.Z) : FString(TEXT(" ---"));
			bConnected &= bOn;
		}
		const FVector From(Stair.Foot.X - Stair.Dir.X * 40.0, Stair.Foot.Y - Stair.Dir.Y * 40.0, Z0 + 20.f);
		const FVector2D TopIn = Stair.Top + Stair.Inward * 300.0;
		const FString Note = PathNote(World, OnNav(World, From), FVector(TopIn.X, TopIn.Y, Z1 + 20.f), nullptr);
		bConnected &= !Note.Contains(TEXT("PARTIAL")) && !Note.StartsWith(TEXT("no "));
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke: ramp nav %s (%s, level %d -> %d, %.0f cm run): navmesh z along it every 1 m:%s; foot -> top: %s"), *Stair.Id, *Stair.Kind,
			Stair.LevelFrom, Stair.LevelTo, Length, *Samples, *Note);
		return bConnected;
	}

	bool Prepare(FState& State, UWorld* World)
	{
		FString Error;
		const TSharedPtr<FJsonObject> Layout = LoadLayout(State.MapName, Error);
		if (!Layout.IsValid())
		{
			Check(State, false, Error);
			return Finish(State, false);
		}
		State.GroundZ = Layout->HasField(TEXT("ground_z")) ? static_cast<float>(Layout->GetNumberField(TEXT("ground_z"))) : 0.f;
		for (const TSharedPtr<FJsonValue>& BV : Layout->GetArrayField(TEXT("buildings")))
		{
			const TSharedPtr<FJsonObject>& B = BV->AsObject();
			const TArray<TSharedPtr<FJsonValue>>* Stairs = nullptr;
			if (!B->TryGetArrayField(TEXT("stairs"), Stairs))
			{
				continue;
			}
			const TArray<TSharedPtr<FJsonValue>>& Footprint = B->GetArrayField(TEXT("footprint"));
			const FBox2D Roof(FVector2D(Footprint[0]->AsArray()[0]->AsNumber(), Footprint[0]->AsArray()[1]->AsNumber()),
				FVector2D(Footprint[1]->AsArray()[0]->AsNumber(), Footprint[1]->AsArray()[1]->AsNumber()));
			const int32 Floors = static_cast<int32>(B->GetNumberField(TEXT("floors")));
			TArray<FVector2D> RoofHoles;
			const TArray<TSharedPtr<FJsonValue>>* Holes = nullptr;
			if (B->TryGetArrayField(TEXT("roof_holes"), Holes))
			{
				for (const TSharedPtr<FJsonValue>& H : *Holes)
				{
					RoofHoles.Add(FVector2D(H->AsArray()[0]->AsNumber(), H->AsArray()[1]->AsNumber()));
				}
			}
			for (const TSharedPtr<FJsonValue>& SV : *Stairs)
			{
				const TSharedPtr<FJsonObject>& S = SV->AsObject();
				FStair Stair;
				Stair.Id = S->GetStringField(TEXT("id"));
				Stair.Kind = S->GetStringField(TEXT("kind"));
				Stair.Foot = ReadPoint(S, TEXT("pos"));
				Stair.Top = ReadPoint(S, TEXT("top"));
				const FVector2D Mid = S->HasField(TEXT("mid")) ? ReadPoint(S, TEXT("mid")) : Stair.Top;
				Stair.Dir = (Mid - Stair.Foot).GetSafeNormal();
				Stair.Inward = Stair.Kind == TEXT("external") ? (Stair.Top - Mid).GetSafeNormal() : Stair.Dir;
				Stair.LevelFrom = static_cast<int32>(S->GetNumberField(TEXT("from")));
				Stair.LevelTo = static_cast<int32>(S->GetNumberField(TEXT("to")));
				Stair.Roof = Roof;
				if (Stair.LevelTo == Floors)
				{
					Stair.Holes = RoofHoles;
				}
				// The stairwell's own opening on its top level (the builder's 3.5 m hole ending at the ramp's top).
				for (const TSharedPtr<FJsonValue>& OV : *Stairs)
				{
					const TSharedPtr<FJsonObject>& O = OV->AsObject();
					if (O->GetStringField(TEXT("kind")) == TEXT("stairwell") && static_cast<int32>(O->GetNumberField(TEXT("to"))) == Stair.LevelTo)
					{
						const FVector2D OPos = ReadPoint(O, TEXT("pos"));
						const FVector2D OTop = ReadPoint(O, TEXT("top"));
						Stair.Holes.Add(OTop - (OTop - OPos).GetSafeNormal() * 175.0);
					}
				}
				const bool bConnected = LogRampNav(World, State, Stair, Mid);
				const bool bWanted = State.Only.IsEmpty() || State.Only == Stair.Id;
				if (!bWanted || Stair.LevelFrom != 0)
				{
					continue;
				}
				if (!bConnected)
				{
					// A layout defect, not the squad's: the ramp's navmesh is cut (L_Gen_Depot_1's B_hall_n stairwell: the overlapping B_office_w's
					// 9 m wall E_0 at y -2850 crosses it 2.8 m up). Reported, not walked (strictnav = a FAIL).
					const FString Line = FString::Printf(TEXT("%s: the ramp's navmesh does not join its foot to its top level (layout defect, see 'ramp nav') - not walked"), *Stair.Id);
					UE_LOG(LogCodexTactics, Warning, TEXT("Smoke WARN: %s"), *Line);
					State.Summary.Add(FString::Printf(TEXT("WARN %s"), *Line));
					if (State.bStrictNav)
					{
						Check(State, false, Line);
					}
					continue;
				}
				const int32 Index = State.Stairs.Add(Stair);
				const float Rise = (Stair.LevelTo - Stair.LevelFrom) * LevelHeightCm;
				for (const ELegKind Kind : { ELegKind::Squad, ELegKind::Group, ELegKind::Solo })
				{
					const FString KindKey = Kind == ELegKind::Squad ? TEXT("squad") : Kind == ELegKind::Group ? TEXT("group") : TEXT("solo");
					if (!State.LegFilter.IsEmpty() && !State.LegFilter.Contains(KindKey))
					{
						continue;
					}
					FLeg Leg;
					Leg.Kind = Kind;
					Leg.Stair = Index;
					// The group's click lands right past the stair's top (the player clicks where the stair arrives); the others further in.
					Leg.Goal = RoofGoal(State, State.Stairs[Index], Kind == ELegKind::Group ? 250.f : 600.f);
					Leg.Limit = 60.f + Rise / 10.f;
					State.Legs.Add(Leg);
				}
			}
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke: LevelGenRampSmoke on %s: %d stairs, %d legs"), *State.MapName, State.Stairs.Num(), State.Legs.Num());
		return true;
	}

	/** The squad bunched in the stair's mouth (Ordered = the man at the BACK, 2.6 m off the foot; the others 1.2 m off it side by side). */
	AOperativeCharacter* PlaceSquad(UWorld* World, USquadSubsystem& Squad, const FStair& Stair, float GroundZ)
	{
		TArray<AOperativeCharacter*> Members = Squad.GetMembers();
		Members.Remove(nullptr);
		if (Members.Num() < 2)
		{
			return nullptr;
		}
		// A stairwell starts close to its building's wall: the squad bunches in front of / beside the foot on the hall's open side.
		FVector2D Side(-Stair.Dir.Y, Stair.Dir.X);
		const bool bStairwell = Stair.Kind == TEXT("stairwell");
		if (bStairwell && !Stair.Roof.IsInside(Stair.Foot + Side * 300.0))
		{
			Side = -Side;
		}
		const TArray<FVector2D> Spots = bStairwell
			? TArray<FVector2D>{ Stair.Foot - Stair.Dir * 50.0 + Side * 40.0, Stair.Foot + Side * 120.0, Stair.Foot - Stair.Dir * 40.0 + Side * 260.0, Stair.Foot + Side * 260.0 + Stair.Dir * 100.0 }
			: TArray<FVector2D>{ Stair.Foot - Stair.Dir * 120.0 + Side * 60.0, Stair.Foot - Stair.Dir * 120.0 - Side * 60.0, Stair.Foot - Stair.Dir * 260.0,
				Stair.Foot - Stair.Dir * 260.0 + Side * 130.0 };
		// The current leader in the mouth, the last member at the back (the one the player selects).
		AOperativeCharacter* Back = Members.Last() != Squad.GetLeader() ? Members.Last() : Members[0];
		Members.Remove(Back);
		Members.Insert(Back, 0);
		for (int32 Index = 0; Index < Members.Num(); ++Index)
		{
			AOperativeCharacter* Member = Members[Index];
			const FVector2D Spot = Index == 0 ? Spots[2] : Index == 1 ? Spots[0] : Index == 2 ? Spots[1] : Spots[3];
			const FVector At = OnNav(World, FVector(Spot.X, Spot.Y, GroundZ + 50.f));
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: %s placed at %s (asked %s)%s"), *Member->DisplayName.ToString(), *At.ToCompactString(), *Spot.ToString(),
				Index == 0 ? TEXT(" - at the back, the one sent up") : TEXT(""));
			Member->TeleportTo(At + FVector(0.f, 0.f, Member->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 5.f), FVector(Stair.Dir, 0.0).Rotation(), false, true);
			if (AAIController* AI = Cast<AAIController>(Member->GetController()))
			{
				AI->StopMovement();
			}
		}
		return Back;
	}

	/** The player's click on Goal (HandleWorldHit; bDouble = a double click: sprint). */
	void Click(UWorld* World, const FVector& Goal, bool bDouble)
	{
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(World->GetFirstPlayerController());
		if (!PC)
		{
			return;
		}
		FHitResult Hit;
		Hit.ImpactPoint = Goal;
		Hit.Location = Goal;
		Hit.ImpactNormal = FVector::UpVector;
		Hit.Normal = FVector::UpVector;
		Hit.bBlockingHit = true;
		PC->HandleWorldHit(Hit);
		if (bDouble)
		{
			PC->HandleWorldHit(Hit); // the second click within the double-click time (same frame, same mouse position): sprint
		}
	}

	FString Describe(UWorld* World, USquadSubsystem& Squad, const FVector& Goal, float TopZ, const FState& State)
	{
		FString Out;
		for (AOperativeCharacter* Member : Squad.GetMembers())
		{
			if (!Member)
			{
				continue;
			}
			const float* Still = State.StillFor.Find(Member);
			Out += FString::Printf(TEXT(" | %s%s at %s feet %.0f%s, speed %.0f, still %.1f s, order %s -> %s, %s"), *Member->DisplayName.ToString(),
				Member == Squad.GetLeader() ? TEXT(" (leader)") : TEXT(""), *FVector2D(Member->GetActorLocation()).ToString(), FeetZ(*Member),
				FMath::Abs(FeetZ(*Member) - TopZ) <= LevelTolCm ? TEXT(" (top)") : TEXT(""), Member->GetVelocity().Size2D(), Still ? *Still : 0.f,
				Member->HasOwnMoveOrder() ? TEXT("own") : TEXT("none/formation"), *FVector2D(Member->GetLastMoveDestination()).ToString(),
				*PathNote(World, Member->GetActorLocation(), Goal, Member));
		}
		return Out;
	}

	void StartLeg(FState& State, UWorld* World)
	{
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		const FLeg& Leg = State.Legs[State.Leg];
		const FStair& Stair = State.Stairs[Leg.Stair];
		if (Leg.Kind == ELegKind::Solo)
		{
			if (!Squad->IsSoloMode())
			{
				Squad->EnterSoloMode();
			}
			Squad->SetFollowersHolding(true);
		}
		else
		{
			if (Squad->IsSoloMode())
			{
				Squad->ExitSoloMode();
			}
			Squad->SetFollowersHolding(false);
		}
		Squad->SetSelectedGroup(TArray<AOperativeCharacter*>{ Squad->GetLeader() }, false);
		State.Ordered = PlaceSquad(World, *Squad, Stair, State.GroundZ);
		State.PhaseTime = 0.f;
		State.DoneFor = 0.f;
		State.OrderedUpFor = 0.f;
		State.StillFor.Reset();
		State.LastPos.Reset();
		State.Phase = EPhase::Order;
	}

	void EndLeg(FState& State, bool bOk, const FString& What)
	{
		const FLeg& Leg = State.Legs[State.Leg];
		const FString Line = FString::Printf(TEXT("%s %s: %s"), *State.Stairs[Leg.Stair].Id, KindName(Leg.Kind), *What);
		Check(State, bOk, Line);
		State.Summary.Add(FString::Printf(TEXT("%-4s %s"), bOk ? TEXT("ok") : TEXT("FAIL"), *Line.Left(900)));
		State.LegsOk += bOk ? 1 : 0;
		++State.Leg;
		State.Phase = EPhase::Setup;
	}

	bool Step(TSharedRef<FState> StateRef)
	{
		FState& State = *StateRef;
		UWorld* World = State.World.Get();
		if (!World || State.Phase == EPhase::Done)
		{
			return false;
		}
		const float Dt = World->IsPaused() ? 0.f : StepSeconds;
		State.Time += Dt;
		State.PhaseTime += Dt;
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		if (State.Time > TotalLimitSeconds)
		{
			Check(State, false, TEXT("the smoke ran out of time"));
			return Finish(State, false);
		}
		switch (State.Phase)
		{
		case EPhase::Settle:
		{
			const ULevelExitSubsystem* Exit = World->GetSubsystem<ULevelExitSubsystem>();
			if (!Leader || State.PhaseTime < 3.5f || (Exit && Exit->IsStartFadeResolved() && Exit->GetStartFadeAlpha() > 0.f && State.PhaseTime < 15.f))
			{
				return true;
			}
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				if (Member)
				{
					Member->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
				}
			}
			if (!Prepare(State, World))
			{
				return false;
			}
			Check(State, State.Legs.Num() > 0 && Squad->GetMembers().Num() >= 3, FString::Printf(TEXT("%d legs planned, %d operatives"), State.Legs.Num(),
				Squad->GetMembers().Num()));
			if (State.Legs.Num() == 0)
			{
				return Finish(State, false);
			}
			State.Phase = EPhase::Setup;
			return true;
		}
		case EPhase::Setup:
			if (State.Leg >= State.Legs.Num())
			{
				return Finish(State, true);
			}
			StartLeg(State, World);
			return true;
		case EPhase::Order:
		{
			if (State.PhaseTime < 1.5f)
			{
				return true; // the teleport settles
			}
			const FLeg& Leg = State.Legs[State.Leg];
			AOperativeCharacter* Ordered = State.Ordered.Get();
			if (!Ordered)
			{
				EndLeg(State, false, TEXT("no operative to order"));
				return true;
			}
			if (Leg.Kind == ELegKind::Group)
			{
				Squad->SetSelectedGroup(Squad->GetMembers(), false);
				Squad->SetLeader(Ordered, /*bResetGroup*/ false);
			}
			else
			{
				Squad->SetLeader(Ordered);
			}
			Click(World, Leg.Goal, Leg.Kind == ELegKind::Group);
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: leg %d / %d - %s %s: %s (selected, %d in the selection) clicked to %s"), State.Leg + 1, State.Legs.Num(),
				*State.Stairs[Leg.Stair].Id, KindName(Leg.Kind), *Ordered->DisplayName.ToString(), Squad->GetSelectedGroup().Num(), *Leg.Goal.ToCompactString());
			State.PhaseTime = 0.f;
			State.Phase = EPhase::Run;
			return true;
		}
		case EPhase::Run:
		{
			const FLeg& Leg = State.Legs[State.Leg];
			const FStair& Stair = State.Stairs[Leg.Stair];
			const float TopZ = State.GroundZ + Stair.LevelTo * LevelHeightCm;
			AOperativeCharacter* Ordered = State.Ordered.Get();
			bool bAllUp = true;
			bool bStuck = false;
			bool bOthersDown = true;
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				if (!Member)
				{
					continue;
				}
				const bool bUp = FMath::Abs(FeetZ(*Member) - TopZ) <= LevelTolCm;
				bAllUp &= bUp;
				if (Member != Ordered)
				{
					bOthersDown &= FMath::Abs(FeetZ(*Member) - State.GroundZ) <= LevelTolCm;
				}
				const FVector Pos = Member->GetActorLocation();
				const FVector* Last = State.LastPos.Find(Member);
				float& Still = State.StillFor.FindOrAdd(Member);
				Still = Last && FVector::Dist(*Last, Pos) < 2.f ? Still + Dt : 0.f;
				State.LastPos.Add(Member, Pos);
				const bool bCounts = Leg.Kind != ELegKind::Solo || Member == Ordered;
				bStuck |= bCounts && !bUp && Still >= StillFailSeconds;
			}
			State.TraceClock += Dt;
			if (State.bTrace && State.TraceClock >= 0.5f)
			{
				State.TraceClock = 0.f;
				FString Line;
				for (AOperativeCharacter* Member : Squad->GetMembers())
				{
					if (Member)
					{
						Line += FString::Printf(TEXT(" | %s%s (%.0f, %.0f) feet %.0f v %.0f%s %s"), *Member->DisplayName.ToString(), Member == Squad->GetLeader() ? TEXT("*") : TEXT(""),
							Member->GetActorLocation().X, Member->GetActorLocation().Y, FeetZ(*Member), Member->GetVelocity().Size2D(),
							Member->HasOwnMoveOrder() ? TEXT(" own") : TEXT(""), Member->GetStance() == EOperativeStance::Standing ? TEXT("std") : TEXT("low"));
					}
				}
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke trace %.1f s:%s"), State.PhaseTime, *Line);
			}
			const bool bOrderedUp = Ordered && FMath::Abs(FeetZ(*Ordered) - TopZ) <= LevelTolCm && FVector::Dist2D(Ordered->GetActorLocation(), Leg.Goal) <= 400.f;
			if (Leg.Kind == ELegKind::Solo)
			{
				State.OrderedUpFor = bOrderedUp ? State.OrderedUpFor + Dt : 0.f;
				if (State.OrderedUpFor >= SoloSettleSeconds || !bOthersDown)
				{
					EndLeg(State, bOrderedUp && bOthersDown, FString::Printf(TEXT("%s in %.1f s:%s"), bOthersDown ? TEXT("he is up, the others stayed on the ground")
						: TEXT("ANOTHER MAN LEFT THE GROUND in Solo"), State.PhaseTime, *Describe(World, *Squad, Leg.Goal, TopZ, State)));
					return true;
				}
			}
			else
			{
				State.DoneFor = bAllUp && bOrderedUp ? State.DoneFor + Dt : 0.f;
				if (State.DoneFor >= 1.f)
				{
					EndLeg(State, true, FString::Printf(TEXT("all on the top level in %.1f s:%s"), State.PhaseTime, *Describe(World, *Squad, Leg.Goal, TopZ, State)));
					return true;
				}
			}
			if (bStuck || State.PhaseTime > Leg.Limit)
			{
				EndLeg(State, false, FString::Printf(TEXT("%s after %.0f s (top level z %.0f):%s"), bStuck ? TEXT("STUCK") : TEXT("TIMED OUT"), State.PhaseTime, TopZ,
					*Describe(World, *Squad, Leg.Goal, TopZ, State)));
			}
			return true;
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
		TSharedRef<FState> State = MakeShared<FState>();
		State->World = World;
		State->MapName = World->GetMapName().Replace(TEXT("UEDPIE_0_"), TEXT(""));
		for (const FString& Arg : Args)
		{
			if (Arg.StartsWith(TEXT("only=")))
			{
				State->Only = Arg.Mid(5);
			}
			else if (Arg.StartsWith(TEXT("legs=")))
			{
				TArray<FString> Parts;
				Arg.Mid(5).ParseIntoArray(Parts, TEXT(","));
				State->LegFilter.Append(Parts);
			}
			State->bStrictNav |= Arg == TEXT("strictnav");
			State->bTrace |= Arg == TEXT("trace");
		}
		if (IConsoleVariable* Pressure = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Pressure.Enabled")))
		{
			Pressure->Set(0, ECVF_SetByConsole);
		}
		// 2026-11-10: nor the Depot's scheduled PMC squads (pmc_squads at 30 s / 120 s) - their firefight (covers, grenades, the RPG, the
		// horde after it) kept the ordered man in cover / took his order mid-leg; this smoke checks the stairs, not a fight.
		if (IConsoleVariable* Encounters = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Pmc.Encounters")))
		{
			Encounters->Set(0, ECVF_SetByConsole);
		}
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float)
		{
			return Step(State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.LevelGenRampSmoke"),
		TEXT("Dev check (bug 2026-11-02, item 7): on a generated map the squad bunched at a ramp's / stair's foot goes up when one man is sent onto the roof ")
		TEXT("(squad mode: all follow, single file; all three selected: all up; Solo: only he); [only=<stair id>] [legs=squad,group,solo] [strictnav] [trace]. PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
