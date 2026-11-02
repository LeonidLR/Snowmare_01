// Dev-only headless check of a GENERATED map's ladders and its cut's ways in / out (the game designer's bug report 2026-11-02 on
// L_Gen_Depot_1: "the ladders on the building walls are sunk into the wall and nobody can climb them; the ladder in the cut cannot be
// climbed; the cut cannot be entered at all - ordered there the operatives run round it and stop"; docs/design/levelgen/LEVEL_GENERATOR.md
// section 8, docs/design/LADDERS.md). The layout is the accepted file the map was built from (the level JSON's "levelgen_layout",
// Scripts/Editor/build_levelgen_map.py). With the pressure off (Codex.Pressure.Enabled 0):
//  1. EVERY ladder of the layout: its ALadderActor (tag "ladder=<id>", else the nearest to the layout's pos) is climbable, has a wall
//     behind it, faces AWAY from the wall (+X from pos towards the layout's foot, LADDERS.md 3.1) and tops out on its upper level (+-8 cm);
//     then the leader alone (solo, the others holding beside the foot) climbs it UP by a real move order onto the upper level behind the
//     top, and DOWN again by a move order to the ground in front of the foot - both through the ladder's nav link (the climb must be seen);
//  2. the cut, with the whole squad: per "ramp" crossing the squad walks from the rim beyond the crossing INTO the trench (every member on
//     the cut floor, nobody on a ladder) and OUT again; per "bridge" crossing it walks across at ground level (never below it); then out of
//     the trench by the cut ladder the routes use (V3, else the first) and back in by it (every member climbs).
// Each ladder leg / entry is reported by id; a leg that stops short (still for 8 s off the goal) or runs out of time fails with the leader's
// position, his climb state and a navmesh path check (partial = the goal is not connected). PASS / FAIL, then exits.
//   powershell -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -Commands LevelGenLaddersSmoke -ExtraArgs "-Multiprocess"
//   (map /Game/Maps/Generated/L_Gen_Depot_1, "-LevelJson=L_Gen_Depot_1.json" - Scripts/test_map.json smokeOptions)
// Arguments: map=<L_Gen_...> (must be the loaded map), layout=<accepted name>, only=<ladder or crossing id> (one entry), noladders, nocut.
// Godot reference: none (UE-only).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AIController.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/LevelExitSubsystem.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Ladders/LadderActor.h"
#include "Ladders/LadderClimbComponent.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace LevelGenLaddersSmoke
{
	constexpr float StepSeconds = 0.05f;
	constexpr float LevelHeightCm = 300.f;     // schema.ts LEVEL_HEIGHT
	constexpr float TopToleranceCm = 8.f;      // LADDERS.md 3.1
	constexpr float LevelTolCm = 40.f;         // feet on the leg's level
	constexpr float ArriveCm = 220.f;          // the leader this close to the goal (2D)
	constexpr float StillFailSeconds = 8.f;    // stood still off the goal this long = stopped short
	constexpr float LadderLegSeconds = 75.f;
	constexpr float SquadLegSeconds = 120.f;
	constexpr float SquadLadderLegSeconds = 260.f;
	constexpr float TotalLimitSeconds = 1800.f;

	/** A layout ladder (Content/Data/LevelGen/accepted/<name>.json "ladders"). */
	struct FLayoutLadder
	{
		FString Id;
		FVector2D Pos = FVector2D::ZeroVector;
		FVector2D Foot = FVector2D::ZeroVector;
		FVector2D Top = FVector2D::ZeroVector;
		int32 LevelFrom = 0;
		int32 LevelTo = 0;
		int32 Rungs = 0;
		FString From;
	};

	enum class ELegKind : uint8 { LadderUp, LadderDown, CutIn, CutOut, Bridge, SquadLadderOut, SquadLadderIn };

	struct FLeg
	{
		ELegKind Kind = ELegKind::LadderUp;
		/** The ladder / crossing id the leg is reported by. */
		FString Id;
		int32 Ladder = INDEX_NONE;   // index into FState::Ladders
		/** Where the squad is put first (bTeleport) and the goal of the order; level heights in cm. */
		bool bTeleport = false;
		FVector LeaderSpot = FVector::ZeroVector;
		FVector FollowerCentre = FVector::ZeroVector;
		FVector2D FollowerAxis = FVector2D(1.0, 0.0);
		FVector Goal = FVector::ZeroVector;
		float GoalZ = 0.f;
		float Limit = LadderLegSeconds;
	};

	enum class EPhase : uint8 { Settle, Setup, Order, Run, Done };

	struct FState
	{
		TWeakObjectPtr<UWorld> World;
		FString LayoutName;
		FString MapName;
		float GroundZ = 0.f;
		TArray<FLayoutLadder> Ladders;
		TArray<TWeakObjectPtr<ALadderActor>> Actors;
		TArray<FLeg> Legs;
		int32 Leg = 0;
		EPhase Phase = EPhase::Settle;
		float Time = 0.f;
		float PhaseTime = 0.f;
		float StillTime = 0.f;
		float DoneFor = 0.f;
		float MinFeetZ = 1e9f;
		bool bLeaderClimbed = false;
		TSet<TWeakObjectPtr<AOperativeCharacter>> Climbers;
		int32 Failures = 0;
		int32 LegsOk = 0;
		TArray<FString> Summary;
		bool bNoLadders = false;
		bool bNoCut = false;
		FString Only;
		FString LayoutArg;
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
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke: LevelGenLaddersSmoke %s on %s: %d / %d legs passed, %d failures"), *State.LayoutName, *State.MapName,
			State.LegsOk, State.Legs.Num(), State.Failures);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		State.Phase = EPhase::Done;
		FPlatformMisc::RequestExit(false, TEXT("LevelGenLaddersSmoke"));
		return false;
	}

	const TCHAR* KindName(ELegKind Kind)
	{
		switch (Kind)
		{
		case ELegKind::LadderUp: return TEXT("up");
		case ELegKind::LadderDown: return TEXT("down");
		case ELegKind::CutIn: return TEXT("squad into the cut");
		case ELegKind::CutOut: return TEXT("squad out of the cut");
		case ELegKind::Bridge: return TEXT("squad across the bridge");
		case ELegKind::SquadLadderOut: return TEXT("squad out of the cut by the ladder");
		case ELegKind::SquadLadderIn: return TEXT("squad into the cut by the ladder");
		}
		return TEXT("?");
	}

	FString LoadedMapName(const UWorld* World)
	{
		return World ? World->GetMapName().Replace(TEXT("UEDPIE_0_"), TEXT("")) : FString();
	}

	FVector2D ReadPoint(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
		return Object->TryGetArrayField(Field, Array) && Array->Num() >= 2 ? FVector2D((*Array)[0]->AsNumber(), (*Array)[1]->AsNumber()) : FVector2D::ZeroVector;
	}

	/** The accepted layout: layout=<name>, else the level JSON's "levelgen_layout". */
	TSharedPtr<FJsonObject> LoadLayout(const FString& MapName, const FString& LayoutArg, FString& Error)
	{
		FString File;
		if (!LayoutArg.IsEmpty())
		{
			File = FPaths::ProjectContentDir() / TEXT("Data/LevelGen/accepted") / LayoutArg + TEXT(".json");
		}
		else
		{
			FString LevelText;
			TSharedPtr<FJsonObject> Level;
			const FString LevelFile = FPaths::ProjectContentDir() / TEXT("Data/LevelJson") / MapName + TEXT(".json");
			if (FFileHelper::LoadFileToString(LevelText, *LevelFile) && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(LevelText), Level) && Level.IsValid()
				&& Level->HasField(TEXT("levelgen_layout")))
			{
				File = FPaths::ProjectDir() / Level->GetStringField(TEXT("levelgen_layout"));
			}
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

	/** Point on the navmesh near P (same level), else P. */
	FVector OnNav(UWorld* World, const FVector& P)
	{
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		FNavLocation Out;
		return Nav && Nav->ProjectPointToNavigation(P, Out, FVector(120.f, 120.f, 150.f)) ? Out.Location : P;
	}

	/** "path ok / partial / none" from From to To on the navmesh (the failure's diagnosis). */
	FString PathNote(UWorld* World, const FVector& From, const FVector& To, AActor* Context)
	{
		UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(World, From, To, Context);
		if (!Path || !Path->IsValid())
		{
			return TEXT("no navmesh path");
		}
		const FVector End = Path->PathPoints.Num() ? Path->PathPoints.Last() : From;
		return FString::Printf(TEXT("navmesh path %s (%d points, ends %.0f cm from the goal at z %.0f)"), Path->IsPartial() ? TEXT("PARTIAL") : TEXT("complete"),
			Path->PathPoints.Num(), FVector::Dist2D(End, To), End.Z);
	}

	/** The squad put down: the leader at LeaderSpot, the others along Axis round FollowerCentre (each projected on the navmesh). */
	void TeleportSquad(UWorld* World, const FLeg& Leg)
	{
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		if (!Leader)
		{
			return;
		}
		int32 Slot = 0;
		static const float Offsets[] = { -200.f, 200.f, -400.f, 400.f, -600.f, 600.f };
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			if (!Member)
			{
				continue;
			}
			if (Member->LadderClimb && Member->LadderClimb->IsBusy())
			{
				Member->LadderClimb->AbortClimb(ELadderAbortReason::Teleported);
			}
			FVector Spot = Leg.LeaderSpot;
			if (Member != Leader)
			{
				const float Off = Offsets[FMath::Min(Slot++, 5)];
				Spot = Leg.FollowerCentre + FVector(Leg.FollowerAxis.X * Off, Leg.FollowerAxis.Y * Off, 0.f);
			}
			Spot = OnNav(World, Spot);
			Member->TeleportTo(Spot + FVector(0.f, 0.f, Member->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 5.f), Member->GetActorRotation(), false, true);
			if (AAIController* AI = Cast<AAIController>(Member->GetController()))
			{
				AI->StopMovement();
			}
		}
	}

	ALadderActor* FindLadderActor(UWorld* World, const FLayoutLadder& Ladder, float GroundZ)
	{
		const FName Tag(*FString::Printf(TEXT("ladder=%s"), *Ladder.Id));
		ALadderActor* Best = nullptr;
		double BestDist = 80.0;
		const float FloorZ = GroundZ + Ladder.LevelFrom * LevelHeightCm;
		for (TActorIterator<ALadderActor> It(World); It; ++It)
		{
			if (It->Tags.Contains(Tag))
			{
				return *It;
			}
			const double D = FVector2D::Distance(FVector2D(It->GetActorLocation()), Ladder.Pos);
			if (D < BestDist && FMath::Abs(It->GetFloorZ() - FloorZ) < 60.f)
			{
				BestDist = D;
				Best = *It;
			}
		}
		return Best;
	}

	/** The legs: per ladder up + down (solo), per crossing in / out or across (the squad), the squad out of and into the cut by a ladder. */
	void PlanLegs(UWorld* World, FState& State, const TSharedPtr<FJsonObject>& Layout)
	{
		const float Gz = State.GroundZ;
		if (!State.bNoLadders)
		{
			for (int32 Index = 0; Index < State.Ladders.Num(); ++Index)
			{
				const FLayoutLadder& L = State.Ladders[Index];
				if (!State.Only.IsEmpty() && State.Only != L.Id)
				{
					continue;
				}
				const FVector2D Out = (L.Foot - L.Pos).GetSafeNormal();
				const FVector2D Along(-Out.Y, Out.X);
				const float Low = Gz + L.LevelFrom * LevelHeightCm;
				const float High = Gz + L.LevelTo * LevelHeightCm;
				const FVector2D Below = L.Foot + Out * 150.0;
				const FVector2D Above = L.Top - Out * 50.0;
				FLeg Up;
				Up.Kind = ELegKind::LadderUp;
				Up.Id = L.Id;
				Up.Ladder = Index;
				Up.bTeleport = true;
				Up.LeaderSpot = FVector(Below.X, Below.Y, Low);
				const FVector2D Wait = L.Foot + Out * 350.0;
				Up.FollowerCentre = FVector(Wait.X, Wait.Y, Low);
				Up.FollowerAxis = Along;
				Up.Goal = FVector(Above.X, Above.Y, High + 50.f);
				Up.GoalZ = High;
				State.Legs.Add(Up);
				FLeg Down = Up;
				Down.Kind = ELegKind::LadderDown;
				Down.bTeleport = false;
				Down.Goal = FVector(Below.X, Below.Y, Low + 50.f);
				Down.GoalZ = Low;
				State.Legs.Add(Down);
			}
		}
		const TSharedPtr<FJsonObject>* CutPtr = nullptr;
		if (State.bNoCut || !Layout->TryGetObjectField(TEXT("cut"), CutPtr) || !CutPtr)
		{
			return;
		}
		const TSharedPtr<FJsonObject>& Cut = *CutPtr;
		const float CutZ = Gz + static_cast<float>(Cut->GetNumberField(TEXT("level"))) * LevelHeightCm;
		double Tx0 = 1e9, Ty0 = 1e9, Tx1 = -1e9, Ty1 = -1e9;
		for (const TSharedPtr<FJsonValue>& V : Cut->GetArrayField(TEXT("poly")))
		{
			const TArray<TSharedPtr<FJsonValue>>& P = V->AsArray();
			Tx0 = FMath::Min(Tx0, P[0]->AsNumber()); Tx1 = FMath::Max(Tx1, P[0]->AsNumber());
			Ty0 = FMath::Min(Ty0, P[1]->AsNumber()); Ty1 = FMath::Max(Ty1, P[1]->AsNumber());
		}
		const bool bBandX = (Tx1 - Tx0) < (Ty1 - Ty0); // the band runs along Y: across = X
		// (across, along) -> world XY
		auto World2 = [bBandX](double Across, double AlongV) { return bBandX ? FVector2D(Across, AlongV) : FVector2D(AlongV, Across); };
		const double CentreAcross = bBandX ? (Tx0 + Tx1) * 0.5 : (Ty0 + Ty1) * 0.5;
		const double AlongHi = bBandX ? Ty1 : Tx1;
		const FVector2D AlongAxis = bBandX ? FVector2D(0.0, 1.0) : FVector2D(1.0, 0.0);
		const TArray<TSharedPtr<FJsonValue>>* Crossings = nullptr;
		Cut->TryGetArrayField(TEXT("crossings"), Crossings);
		for (const TSharedPtr<FJsonValue>& V : Crossings ? *Crossings : TArray<TSharedPtr<FJsonValue>>())
		{
			const TSharedPtr<FJsonObject>& X = V->AsObject();
			const FString Id = X->GetStringField(TEXT("id"));
			if (!State.Only.IsEmpty() && State.Only != Id)
			{
				continue;
			}
			double A0 = 1e9, A1 = -1e9, L0 = 1e9, L1 = -1e9; // across / along extents of the crossing
			for (const TSharedPtr<FJsonValue>& P : X->GetArrayField(TEXT("poly")))
			{
				const TArray<TSharedPtr<FJsonValue>>& Q = P->AsArray();
				const double Across = bBandX ? Q[0]->AsNumber() : Q[1]->AsNumber();
				const double AlongV = bBandX ? Q[1]->AsNumber() : Q[0]->AsNumber();
				A0 = FMath::Min(A0, Across); A1 = FMath::Max(A1, Across);
				L0 = FMath::Min(L0, AlongV); L1 = FMath::Max(L1, AlongV);
			}
			const double Mid = (L0 + L1) * 0.5;
			const FVector2D RimHi = World2(A1 + 250.0, Mid);
			const FVector2D RimLo = World2(A0 - 250.0, Mid);
			if (X->GetStringField(TEXT("kind")) == TEXT("ramp"))
			{
				// Inside: the trench floor's middle 12 m past the crossing (the builder's ramp runs that way when there is room).
				const double InAlong = L1 + 1200.0 <= AlongHi - 300.0 ? L1 + 1200.0 : L0 - 1200.0;
				const FVector2D Inside = World2(CentreAcross, InAlong);
				FLeg In;
				In.Kind = ELegKind::CutIn;
				In.Id = Id;
				In.bTeleport = true;
				In.LeaderSpot = FVector(RimHi.X, RimHi.Y, Gz);
				In.FollowerCentre = In.LeaderSpot;
				In.FollowerAxis = AlongAxis;
				In.Goal = FVector(Inside.X, Inside.Y, CutZ + 50.f);
				In.GoalZ = CutZ;
				In.Limit = SquadLegSeconds;
				State.Legs.Add(In);
				FLeg Out = In;
				Out.Kind = ELegKind::CutOut;
				Out.bTeleport = false;
				Out.Goal = FVector(RimHi.X, RimHi.Y, Gz + 50.f);
				Out.GoalZ = Gz;
				State.Legs.Add(Out);
			}
			else
			{
				FLeg Across;
				Across.Kind = ELegKind::Bridge;
				Across.Id = Id;
				Across.bTeleport = true;
				Across.LeaderSpot = FVector(RimHi.X, RimHi.Y, Gz);
				Across.FollowerCentre = Across.LeaderSpot;
				Across.FollowerAxis = AlongAxis;
				Across.Goal = FVector(RimLo.X, RimLo.Y, Gz + 50.f);
				Across.GoalZ = Gz;
				Across.Limit = SquadLegSeconds;
				State.Legs.Add(Across);
			}
		}
		// The squad out of the trench by a cut ladder and back in by it: the one a route goes through (V3), else the first.
		int32 CutLadder = INDEX_NONE;
		const TArray<TSharedPtr<FJsonValue>>* Routes = nullptr;
		if (Layout->TryGetArrayField(TEXT("routes"), Routes))
		{
			for (const TSharedPtr<FJsonValue>& R : *Routes)
			{
				const TArray<TSharedPtr<FJsonValue>>* Links = nullptr;
				if (CutLadder == INDEX_NONE && R->AsObject()->TryGetArrayField(TEXT("links"), Links))
				{
					for (const TSharedPtr<FJsonValue>& Link : *Links)
					{
						const FString Via = Link->AsObject()->GetStringField(TEXT("via"));
						CutLadder = State.Ladders.IndexOfByPredicate([&Via](const FLayoutLadder& L) { return L.Id == Via && L.From == TEXT("cut"); });
						if (CutLadder != INDEX_NONE)
						{
							break;
						}
					}
				}
			}
		}
		if (CutLadder == INDEX_NONE)
		{
			CutLadder = State.Ladders.IndexOfByPredicate([](const FLayoutLadder& L) { return L.From == TEXT("cut"); });
		}
		if (CutLadder != INDEX_NONE && (State.Only.IsEmpty() || State.Only == State.Ladders[CutLadder].Id))
		{
			const FLayoutLadder& L = State.Ladders[CutLadder];
			const FVector2D Out = (L.Foot - L.Pos).GetSafeNormal();
			const FVector2D Below = L.Foot + Out * 150.0;
			// The goals 8 m along the trench / the rim from the ladder: the column trails the leader on the goal's level (a goal right at the
			// foot / the top leaves the men's trail slots on the ladder's other level - they would rightly stay there).
			const double LadderAlong = FVector2D::DotProduct(L.Pos, AlongAxis);
			const FVector2D Along = AlongAxis * (LadderAlong + 1100.0 <= AlongHi - 300.0 ? 800.0 : -800.0);
			const FVector2D InTrench = Below + Along;
			const FVector2D Above = L.Top - Out * 50.0 + Along;
			FLeg Up;
			Up.Kind = ELegKind::SquadLadderOut;
			Up.Id = L.Id;
			Up.Ladder = CutLadder;
			Up.bTeleport = true;
			Up.LeaderSpot = FVector(Below.X, Below.Y, CutZ);
			Up.FollowerCentre = FVector(Below.X, Below.Y, CutZ);
			Up.FollowerAxis = FVector2D(-Out.Y, Out.X);
			Up.Goal = FVector(Above.X, Above.Y, Gz + 50.f);
			Up.GoalZ = Gz;
			Up.Limit = SquadLadderLegSeconds;
			State.Legs.Add(Up);
			FLeg Down = Up;
			Down.Kind = ELegKind::SquadLadderIn;
			Down.bTeleport = false;
			Down.Goal = FVector(InTrench.X, InTrench.Y, CutZ + 50.f);
			Down.GoalZ = CutZ;
			State.Legs.Add(Down);
		}
	}

	bool IsSquadLeg(ELegKind Kind)
	{
		return Kind != ELegKind::LadderUp && Kind != ELegKind::LadderDown;
	}

	bool IsLadderLeg(ELegKind Kind)
	{
		return Kind == ELegKind::LadderUp || Kind == ELegKind::LadderDown || Kind == ELegKind::SquadLadderOut || Kind == ELegKind::SquadLadderIn;
	}

	/** The pre-checks of a ladder actor (LADDERS.md 3.1 / 3.4): climbable, a wall behind it, facing away from it, the top on its level. */
	void CheckLadderActor(FState& State, UWorld* World, int32 Index)
	{
		const FLayoutLadder& L = State.Ladders[Index];
		ALadderActor* Actor = State.Actors[Index].Get();
		if (!Actor)
		{
			Check(State, false, FString::Printf(TEXT("ladder %s: an ALadderActor at (%.0f, %.0f) on level %d"), *L.Id, L.Pos.X, L.Pos.Y, L.LevelFrom));
			return;
		}
		const FVector2D Away = (L.Foot - L.Pos).GetSafeNormal();
		const float Facing = static_cast<float>(FVector2D::DotProduct(FVector2D(Actor->GetOutward()), Away));
		const float TopError = Actor->GetTopWorldZ() - (State.GroundZ + L.LevelTo * LevelHeightCm);
		Check(State, Actor->IsClimbable() && Actor->GetRungCount() == L.Rungs, FString::Printf(TEXT("ladder %s: climbable, %d rungs (layout %d)"), *L.Id,
			Actor->GetRungCount(), L.Rungs));
		Check(State, Facing > 0.9f, FString::Printf(TEXT("ladder %s: faces away from its wall (+X . (foot - pos) = %.2f; yaw %.0f)%s"), *L.Id, Facing,
			Actor->GetActorRotation().Yaw, Facing < -0.9f ? TEXT(" - it FACES THE WALL: the hang line and the bottom link inside the wall, the top link off the edge") : TEXT("")));
		Check(State, Actor->HasWall(), FString::Printf(TEXT("ladder %s: a wall behind the rungs (brackets reach it)"), *L.Id));
		Check(State, FMath::Abs(TopError) <= TopToleranceCm, FString::Printf(TEXT("ladder %s: tops out on level %d (%+.1f cm)"), *L.Id, L.LevelTo, TopError));
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		FNavLocation Bottom, Top;
		const bool bBottom = Nav && Nav->ProjectPointToNavigation(Actor->GetBottomLinkWorld(), Bottom, FVector(40.f, 40.f, 60.f));
		const bool bTop = Nav && Nav->ProjectPointToNavigation(Actor->GetTopLinkWorld(), Top, FVector(40.f, 40.f, 60.f));
		Check(State, bBottom && bTop, FString::Printf(TEXT("ladder %s: both link points on the navmesh (bottom %s, top %s)"), *L.Id,
			bBottom ? TEXT("yes") : TEXT("NO"), bTop ? TEXT("yes") : TEXT("NO")));
	}

	/** The layout, the ladder actors and the legs, once the level has settled (its actors loaded). False = finished (failed). */
	bool Prepare(FState& State, UWorld* World)
	{
		FString Error;
		const TSharedPtr<FJsonObject> Layout = LoadLayout(State.MapName, State.LayoutArg, Error);
		if (!Layout.IsValid())
		{
			Check(State, false, Error);
			return Finish(State, false);
		}
		State.LayoutName = Layout->GetStringField(TEXT("name"));
		State.GroundZ = Layout->HasField(TEXT("ground_z")) ? static_cast<float>(Layout->GetNumberField(TEXT("ground_z"))) : 0.f;
		const TArray<TSharedPtr<FJsonValue>>* Ladders = nullptr;
		if (Layout->TryGetArrayField(TEXT("ladders"), Ladders))
		{
			for (const TSharedPtr<FJsonValue>& V : *Ladders)
			{
				const TSharedPtr<FJsonObject>& O = V->AsObject();
				FLayoutLadder L;
				L.Id = O->GetStringField(TEXT("id"));
				L.Pos = ReadPoint(O, TEXT("pos"));
				L.Foot = ReadPoint(O, TEXT("foot"));
				L.Top = ReadPoint(O, TEXT("top"));
				L.LevelFrom = static_cast<int32>(O->GetNumberField(TEXT("level_from")));
				L.LevelTo = static_cast<int32>(O->GetNumberField(TEXT("level_to")));
				L.Rungs = static_cast<int32>(O->GetNumberField(TEXT("rungs")));
				L.From = O->GetStringField(TEXT("from"));
				State.Ladders.Add(L);
				State.Actors.Add(FindLadderActor(World, L, State.GroundZ));
			}
		}
		PlanLegs(World, State, Layout);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke: LevelGenLaddersSmoke layout %s on %s: %d ladders, %d legs"), *State.LayoutName, *State.MapName,
			State.Ladders.Num(), State.Legs.Num());
		return true;
	}

	void StartLeg(FState& State, UWorld* World)
	{
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		const FLeg& Leg = State.Legs[State.Leg];
		if (IsSquadLeg(Leg.Kind))
		{
			if (Squad->IsSoloMode())
			{
				Squad->ExitSoloMode();
			}
			Squad->SetFollowersHolding(false);
		}
		else
		{
			if (!Squad->IsSoloMode())
			{
				Squad->EnterSoloMode();
			}
			Squad->SetFollowersHolding(true);
		}
		if (Leg.bTeleport)
		{
			TeleportSquad(World, Leg);
		}
		State.PhaseTime = 0.f;
		State.StillTime = 0.f;
		State.DoneFor = 0.f;
		State.MinFeetZ = 1e9f;
		State.bLeaderClimbed = false;
		State.Climbers.Reset();
		State.Phase = EPhase::Order;
	}

	void EndLeg(FState& State, bool bOk, const FString& What)
	{
		const FLeg& Leg = State.Legs[State.Leg];
		const FString Line = FString::Printf(TEXT("%s %s: %s"), *Leg.Id, KindName(Leg.Kind), *What);
		Check(State, bOk, Line);
		State.Summary.Add(FString::Printf(TEXT("%-4s %s"), bOk ? TEXT("ok") : TEXT("FAIL"), *Line));
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
			for (int32 Index = 0; Index < State.Ladders.Num(); ++Index)
			{
				if (State.Only.IsEmpty() || State.Only == State.Ladders[Index].Id)
				{
					CheckLadderActor(State, World, Index);
				}
			}
			Check(State, State.Legs.Num() > 0, FString::Printf(TEXT("%d legs planned (%d ladders in the layout, %d operatives)"), State.Legs.Num(), State.Ladders.Num(),
				Squad->GetMembers().Num()));
			if (State.Legs.Num() == 0)
			{
				return Finish(State, false);
			}
			State.Phase = EPhase::Setup;
			return true;
		}
		case EPhase::Setup:
		{
			if (State.Leg >= State.Legs.Num())
			{
				return Finish(State, true);
			}
			if (!Leader)
			{
				Check(State, false, TEXT("a squad leader"));
				return Finish(State, false);
			}
			StartLeg(State, World);
			return true;
		}
		case EPhase::Order:
		{
			if (State.PhaseTime < 1.f)
			{
				return true; // the teleport settles
			}
			const FLeg& Leg = State.Legs[State.Leg];
			const EOperativeOrderResult Result = Leader->OrderMoveTo(Leg.Goal, false);
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: leg %d / %d - %s %s: the leader at %s ordered to %s (result %d)"), State.Leg + 1, State.Legs.Num(), *Leg.Id,
				KindName(Leg.Kind), *Leader->GetActorLocation().ToCompactString(), *Leg.Goal.ToCompactString(), static_cast<int32>(Result));
			State.PhaseTime = 0.f;
			State.Phase = EPhase::Run;
			return true;
		}
		case EPhase::Run:
		{
			const FLeg& Leg = State.Legs[State.Leg];
			ALadderActor* Ladder = State.Actors.IsValidIndex(Leg.Ladder) ? State.Actors[Leg.Ladder].Get() : nullptr;
			const ULadderClimbComponent* Climb = Leader->LadderClimb.Get();
			const bool bBusy = Climb && Climb->IsBusy();
			TArray<AOperativeCharacter*> Members = IsSquadLeg(Leg.Kind) ? Squad->GetMembers() : TArray<AOperativeCharacter*>{ Leader };
			for (AOperativeCharacter* Member : Members)
			{
				const ULadderClimbComponent* MemberClimb = Member ? Member->LadderClimb.Get() : nullptr;
				if (MemberClimb && MemberClimb->IsClimbing())
				{
					State.Climbers.Add(Member);
					State.bLeaderClimbed |= Member == Leader && (!Ladder || MemberClimb->GetLadder() == Ladder);
				}
			}
			const float Feet = FeetZ(*Leader);
			State.MinFeetZ = FMath::Min(State.MinFeetZ, Feet);
			const float Dist = static_cast<float>(FVector::Dist2D(Leader->GetActorLocation(), Leg.Goal));
			const bool bStill = !bBusy && Leader->GetVelocity().Size2D() < 5.f;
			// Every member on the leg's level (squad legs: the cut floor = well below the ground; else within LevelTolCm).
			bool bAllThere = true;
			FString Where;
			for (int32 Index = 0; Index < Members.Num(); ++Index)
			{
				const AOperativeCharacter* Member = Members[Index];
				if (!Member)
				{
					continue;
				}
				const float MemberFeet = FeetZ(*Member);
				const bool bInCut = Leg.Kind == ELegKind::CutIn || Leg.Kind == ELegKind::SquadLadderIn;
				const bool bThere = bInCut ? MemberFeet < State.GroundZ - 200.f : FMath::Abs(MemberFeet - Leg.GoalZ) <= LevelTolCm;
				const bool bSettled = !(Member->LadderClimb && Member->LadderClimb->IsBusy());
				bAllThere &= bThere && bSettled;
				Where += FString::Printf(TEXT(" #%d%s feet %.0f%s"), Index, Member == Leader ? TEXT(" (leader)") : TEXT(""), MemberFeet, bThere ? TEXT("") : TEXT(" (off)"));
			}
			const bool bLadderOk = !IsLadderLeg(Leg.Kind) || State.bLeaderClimbed;
			const bool bDone = Dist <= ArriveCm && bAllThere && bLadderOk;
			State.DoneFor = bDone ? State.DoneFor + Dt : 0.f;
			State.StillTime = bStill && Dist > ArriveCm ? State.StillTime + Dt : 0.f;
			if (State.DoneFor >= 0.5f)
			{
				FString What = FString::Printf(TEXT("done in %.1f s, the leader %.0f cm from the goal at z %.0f"), State.PhaseTime, Dist, Feet);
				if (IsLadderLeg(Leg.Kind))
				{
					What += FString::Printf(TEXT(", climbed by %d of %d"), State.Climbers.Num(), Members.Num());
				}
				bool bOk = true;
				if (Leg.Kind == ELegKind::CutIn || Leg.Kind == ELegKind::CutOut)
				{
					// The ramp, not a ladder: nobody climbed.
					bOk = State.Climbers.Num() == 0;
					What += bOk ? TEXT(", by the ramp (nobody on a ladder)") : FString::Printf(TEXT(", but %d took a ladder instead of the ramp"), State.Climbers.Num());
				}
				else if (Leg.Kind == ELegKind::Bridge)
				{
					bOk = State.MinFeetZ > State.GroundZ - 60.f && State.Climbers.Num() == 0;
					What += FString::Printf(TEXT(", lowest feet %.0f (stays on the ground level)"), State.MinFeetZ);
				}
				else if (Leg.Kind == ELegKind::SquadLadderOut || Leg.Kind == ELegKind::SquadLadderIn)
				{
					What += FString::Printf(TEXT(";%s"), *Where);
				}
				EndLeg(State, bOk, What);
				return true;
			}
			if (State.StillTime >= StillFailSeconds || State.PhaseTime > Leg.Limit)
			{
				const FVector Here = Leader->GetActorLocation();
				EndLeg(State, false, FString::Printf(TEXT("%s after %.0f s: the leader at %s (feet %.0f, goal level %.0f) is %.0f cm from the goal %s; climbed %d, climb state %d;%s; %s"),
					State.StillTime >= StillFailSeconds ? TEXT("STOPPED SHORT") : TEXT("TIMED OUT"), State.PhaseTime, *Here.ToCompactString(), Feet, Leg.GoalZ, Dist,
					*Leg.Goal.ToCompactString(), State.bLeaderClimbed ? 1 : 0, Climb ? static_cast<int32>(Climb->GetState()) : -1, *Where,
					*PathNote(World, Leg.bTeleport ? OnNav(World, Leg.LeaderSpot) : Here, Leg.Goal, Leader)));
				if (Leader->LadderClimb && Leader->LadderClimb->IsBusy())
				{
					Leader->LadderClimb->AbortClimb(ELadderAbortReason::Teleported);
				}
				// The next leg of a pair starts from where this one should have ended.
				if (State.Leg < State.Legs.Num() && !State.Legs[State.Leg].bTeleport)
				{
					FLeg& Next = State.Legs[State.Leg];
					Next.bTeleport = true;
					Next.LeaderSpot = FVector(Leg.Goal.X, Leg.Goal.Y, Leg.GoalZ);
					Next.FollowerCentre = Next.LeaderSpot;
				}
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
		State->MapName = LoadedMapName(World);
		FString MapArg, LayoutArg;
		for (const FString& Arg : Args)
		{
			if (Arg.StartsWith(TEXT("map=")))
			{
				MapArg = FPaths::GetBaseFilename(Arg.Mid(4));
			}
			else if (Arg.StartsWith(TEXT("layout=")))
			{
				LayoutArg = Arg.Mid(7);
			}
			else if (Arg.StartsWith(TEXT("only=")))
			{
				State->Only = Arg.Mid(5);
			}
			State->bNoLadders |= Arg == TEXT("noladders");
			State->bNoCut |= Arg == TEXT("nocut");
		}
		if (!MapArg.IsEmpty() && !MapArg.Equals(State->MapName, ESearchCase::IgnoreCase))
		{
			Check(*State, false, FString::Printf(TEXT("map=%s is the loaded map (loaded: %s)"), *MapArg, *State->MapName));
			Finish(*State, false);
			return;
		}
		State->LayoutArg = LayoutArg;
		// A walk check, not a fight: no pressure enemies.
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
		TEXT("CodexTactics.LevelGenLaddersSmoke"),
		TEXT("Dev check (2026-11-02, docs/design/levelgen/LEVEL_GENERATOR.md 8): on a generated map every ladder of its accepted layout faces away from its wall, ")
		TEXT("is climbed up and down by move orders through its nav link, and the squad walks into the cut and out by each ramp crossing, across each bridge, ")
		TEXT("out of and into the cut by a cut ladder; [map=<L_Gen_...>] [layout=<accepted name>] [only=<id>] [noladders] [nocut]. PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
