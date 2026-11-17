// Dev-only console command: the squad climbs an obstacle in turns (R17-1, designer 2026-11-17 TASK_BOARD: "make climbing onto obstacles work in
// FORMATION mode too ... the same way as on ladders: one climbs, the others cover; he is up, the next climbs while the rest cover"; docs/design/
// MANTLING.md 6.4, docs/design/FORMATION_CONTRACT.md 22) on L_Gen_Depot_1:
//   Scripts/smoke_parallel.ps1 -NoRetry -ExtraArgs -Multiprocess -Commands FormationMantleSmoke
// The clicks take the REAL input path (the camera's ray -> ACodexTacticsPlayerController::HandleWorldHit). Cases, a 3-man squad 4 m off the face:
//   a  formation: click on a 140 cm wreck's top            -> all on top;
//   b  formation: double click on the 230 cm parapet's face -> all on top;
//   c  formation: double click on a 135 cm fence's face     -> all over the fence (the fence vault);
//   d  Solo, all three box-selected: click on a 140 cm wreck's top -> all on top;
//   e  formation: a ground click beyond a 135 cm fence whose way round is long (the leader's path takes its mantle link) -> all over, then the
//      leader walks on to the click.
// Each case: all members end up (on top / over) in the squad's climb order (the leader first, then the formation / selection order), never two
// climbing at once, every waiter settled on his spot stands within squad.base_radius_cm of the face facing outwards (away from the obstacle), and
// nobody's turn takes more than 30 s (nobody stuck). Enemies are parked far away.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/MantleRules.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Components/CapsuleComponent.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Debug/MantleSmokeSites.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/MantleNavigation.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"

namespace FormationMantleSmoke
{
	constexpr float StepSeconds = 0.05f;
	/** The leader stands this far off the face before the click; the followers behind him. */
	constexpr float StandOffCm = 400.f;
	/** The path case: he stands this far off the fence, the goal lies this far past it. */
	constexpr float PathStandOffCm = 450.f;
	constexpr float PathGoalOffCm = 300.f;
	/** "Nobody stuck": one man's turn (the walk to his lane + the climb) is over within this, s. */
	constexpr float TurnLimitSeconds = 30.f;
	/** A waiter's look is judged once he has stood on his spot this long (he turns to it first), s. */
	constexpr float SettledSeconds = 0.8f;
	/** Facing outwards: within this of the outward look (away from the obstacle), deg. */
	constexpr float OutwardToleranceDeg = 90.f;
	/** The path case: the leader arrives at the click within this after the climb ended, s / cm. */
	constexpr float WalkOnLimitSeconds = 25.f;
	constexpr float WalkOnRadiusCm = 150.f;

	enum class EMode : uint8 { Formation, Box };
	enum class EAim : uint8 { Top, Face, PathBeyond };

	struct FCase
	{
		const TCHAR* Label;
		const TCHAR* Kind;
		float HeightCm;
		EAim Aim;
		int32 Clicks;
		EMode Mode;
		/** Over (the fence) - else onto the top. */
		bool bOver;
	};

	static const FCase Cases[] = {
		{ TEXT("a: formation, click on a 140 cm wreck's top"), TEXT("wreck"), 140.f, EAim::Top, 1, EMode::Formation, false },
		{ TEXT("b: formation, double click on the 230 cm parapet's face"), TEXT("cut_wall"), 230.f, EAim::Face, 2, EMode::Formation, false },
		{ TEXT("c: formation, double click on a 135 cm fence's face (the fence vault)"), TEXT("fence"), 135.f, EAim::Face, 2, EMode::Formation, true },
		{ TEXT("d: Solo box selection, click on a 140 cm wreck's top"), TEXT("wreck"), 140.f, EAim::Top, 1, EMode::Box, false },
		{ TEXT("e: formation, a ground click beyond a 135 cm fence (the leader's path over its link)"), TEXT("fence"), 135.f, EAim::PathBeyond, 1, EMode::Formation, true },
	};

	struct FMemberTrack
	{
		TWeakObjectPtr<AOperativeCharacter> Operative;
		int32 MantlesBefore = 0;
		/** The step his first climb of the case started (the climb order). */
		int32 ClimbedAtStep = INDEX_NONE;
		/** When his turn began (the squad's view: his turn / climbing), case time. */
		float TurnStart = -1.f;
		float LongestTurn = 0.f;
		float SettledFor = 0.f;
		int32 WaitSamples = 0;
		int32 InwardSamples = 0;
		float MaxBaseCm = 0.f;
		float WorstLookOff = 0.f;
	};

	struct FState
	{
		TWeakObjectPtr<UWorld> World;
		SmokeUtils::FPreloadGate PreloadGate;
		TArray<TWeakObjectPtr<AOperativeCharacter>> Squad;
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Case = 0;
		int32 Failures = 0;
		int32 Step = 0;
		MantleSmokeSites::FSite Site;
		FVector Goal = FVector::ZeroVector;
		FHitResult Hit;
		TSet<const AActor*> Used;
		TSet<const AActor*> Hidden;
		int32 Retries = 0;
		/** Obstacles skipped for having too little room for the squad. */
		int32 RoomRetries = 0;
		TArray<FMemberTrack> Tracks;
		int32 MaxClimbing = 0;
		int32 ClimbsBefore = 0;
		bool bSawOp = false;
		FSquadMantleView LastView;
		float DoneAt = -1.f;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("FormationMantleSmoke"));
		return false;
	}

	void Next(FState& State, int32 Stage)
	{
		State.Stage = Stage;
		State.StageTime = 0.f;
	}

	FString Who(const AOperativeCharacter* Operative)
	{
		return Operative ? Operative->DisplayName.ToString() : TEXT("-");
	}

	FVector FeetOf(const AOperativeCharacter& Operative)
	{
		return Operative.GetActorLocation() - FVector(0.f, 0.f, Operative.GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	}

	/** The camera's view of the face (the side the isometric camera shows) at half height, or the top's middle over the face he stands at. */
	FVector AimPoint(UWorld* World, const MantleSmokeSites::FSite& Site, EAim Aim)
	{
		const FMantleSite& Probe = Site.Probe;
		if (Aim == EAim::Top)
		{
			const FVector Top = Probe.EdgeTop + Probe.Dir * FMath::Max(20.f, Probe.Probe.DepthCm * 0.5f);
			return FVector(Top.X, Top.Y, Probe.TopZ);
		}
		// The face he stands at (its middle height), a hair in front of it.
		return FVector(Probe.FacePoint.X, Probe.FacePoint.Y, Site.Feet.Z + Probe.Probe.HeightCm * 0.5f) - Probe.Dir * 1.f;
	}

	/** Puts the squad StandOff in front of the face: the leader on the site's spot, the others behind him to his sides. */
	bool PlaceSquad(UWorld* World, FState& State, const FVector& LeaderFeet, const FVector& Dir)
	{
		const FVector Side = FVector::CrossProduct(FVector::UpVector, Dir).GetSafeNormal2D();
		for (int32 Index = 0; Index < State.Squad.Num(); ++Index)
		{
			AOperativeCharacter* Operative = State.Squad[Index].Get();
			if (!Operative)
			{
				return false;
			}
			Operative->StopOperative();
			Operative->SetStance(EOperativeStance::Standing);
			const float HalfHeight = Operative->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			FVector Centre = LeaderFeet + FVector(0.f, 0.f, HalfHeight + 4.f);
			if (Index > 0)
			{
				// Behind him on HIS level (a free navmesh spot within 40 cm of his feet' height - never on a roof next to him).
				const float Lateral = (Index % 2 == 1 ? 1.f : -1.f) * 140.f * ((Index + 1) / 2);
				UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
				FCollisionQueryParams Params(SCENE_QUERY_STAT(FormationMantleSmokePlace), false, Operative);
				bool bPlaced = false;
				for (const float Back : { 170.f, 260.f, 100.f, 350.f })
				{
					for (const float Side2 : { Lateral, Lateral * 0.5f, -Lateral, 0.f })
					{
						FNavLocation OnNav;
						const FVector Want = LeaderFeet - Dir * Back + Side * Side2;
						if (bPlaced || !Nav || !Nav->ProjectPointToNavigation(Want, OnNav, FVector(40.f, 40.f, 40.f))
							|| FMath::Abs(OnNav.Location.Z - LeaderFeet.Z) > 40.f
							|| World->OverlapAnyTestByChannel(OnNav.Location + FVector(0.f, 0.f, HalfHeight + 6.f), FQuat::Identity, ECC_Pawn,
								FCollisionShape::MakeCapsule(42.f, HalfHeight), Params))
						{
							continue;
						}
						Centre = OnNav.Location + FVector(0.f, 0.f, HalfHeight + 4.f);
						bPlaced = true;
					}
				}
			}
			Operative->TeleportTo(Centre, Dir.Rotation(), false, true);
		}
		return true;
	}

	void ArmCase(FState& State)
	{
		State.Tracks.Reset();
		for (const TWeakObjectPtr<AOperativeCharacter>& Member : State.Squad)
		{
			FMemberTrack& Track = State.Tracks.AddDefaulted_GetRef();
			Track.Operative = Member;
			Track.MantlesBefore = Member.IsValid() ? Member->GetMantleCount() : 0;
		}
		State.MaxClimbing = 0;
		State.bSawOp = false;
		State.DoneAt = -1.f;
		State.Step = 0;
	}

	/** Every step of a case: who climbs, the waiters' places and looks, the turns' lengths. */
	void Sample(FState& State, USquadSubsystem& Squad)
	{
		++State.Step;
		const FMantleConfig& Config = MantleRules::GetConfig();
		const FSquadMantleView View = Squad.GetSquadMantleView();
		if (View.bActive)
		{
			State.bSawOp = true;
			State.LastView = View;
		}
		int32 Climbing = 0;
		for (FMemberTrack& Track : State.Tracks)
		{
			AOperativeCharacter* Operative = Track.Operative.Get();
			if (!Operative)
			{
				continue;
			}
			Climbing += Operative->IsMantling() ? 1 : 0;
			if (Track.ClimbedAtStep == INDEX_NONE && Operative->GetMantleCount() > Track.MantlesBefore)
			{
				Track.ClimbedAtStep = State.Step;
			}
			const FSquadMantleMemberView* Member = View.Members.FindByPredicate([Operative](const FSquadMantleMemberView& M) { return M.Operative.Get() == Operative; });
			if (!View.bActive || !Member)
			{
				continue;
			}
			const bool bTurn = Member->State == ESquadMantleState::Turn || Member->State == ESquadMantleState::Climbing;
			if (bTurn && Track.TurnStart < 0.f)
			{
				Track.TurnStart = State.StageTime;
			}
			if (!bTurn && Track.TurnStart >= 0.f)
			{
				Track.LongestTurn = FMath::Max(Track.LongestTurn, State.StageTime - Track.TurnStart);
				Track.TurnStart = -1.f;
			}
			if (bTurn)
			{
				Track.LongestTurn = FMath::Max(Track.LongestTurn, State.StageTime - Track.TurnStart);
			}
			const bool bWaiting = Member->State == ESquadMantleState::Waiting && Member->Place != INDEX_NONE;
			const bool bOnSpot = bWaiting && !Operative->IsMoving() && Member->Spot != FVector::ZeroVector;
			Track.SettledFor = bOnSpot ? Track.SettledFor + StepSeconds : 0.f;
			if (bOnSpot && Track.SettledFor >= SettledSeconds)
			{
				++Track.WaitSamples;
				// Off his lane's face plane, out along its normal (the base side); facing away from the obstacle there.
				const float Out = static_cast<float>(FVector::DotProduct(FeetOf(*Operative) - Member->LaneFacePoint, -Member->LaneDir));
				Track.MaxBaseCm = FMath::Max(Track.MaxBaseCm, Out);
				const float OutYaw = static_cast<float>((-Member->LaneDir).Rotation().Yaw);
				const float Off = FMath::Abs(FMath::FindDeltaAngleDegrees(OutYaw, static_cast<float>(Operative->GetActorRotation().Yaw)));
				Track.WorstLookOff = FMath::Max(Track.WorstLookOff, Off);
				Track.InwardSamples += Off > OutwardToleranceDeg || Out > Config.SquadBaseRadiusCm ? 1 : 0;
			}
		}
		State.MaxClimbing = FMath::Max(State.MaxClimbing, Climbing);
	}

	/** The case's verdict once every member is up / over (or the time ran out). */
	void Judge(FState& State, const FCase& Case, bool bTimedOut)
	{
		const FMantleConfig& Config = MantleRules::GetConfig();
		const FSquadMantleView& View = State.LastView;
		Check(State, State.bSawOp, FString::Printf(TEXT("%s: the squad's climb in turns started (%d members)"), Case.Label, View.Members.Num()));
		Check(State, !bTimedOut, FString::Printf(TEXT("%s: done within the time limit (%.1f s)"), Case.Label, State.StageTime));
		Check(State, State.MaxClimbing <= 1, FString::Printf(TEXT("%s: at most one climbs at a time (max %d)"), Case.Label, State.MaxClimbing));
		// All up / over, each on his own side of the face.
		int32 Up = 0;
		FString Where;
		for (const FMemberTrack& Track : State.Tracks)
		{
			const AOperativeCharacter* Operative = Track.Operative.Get();
			if (!Operative)
			{
				continue;
			}
			const FVector Feet = FeetOf(*Operative);
			const FSquadMantleMemberView* Member = View.Members.FindByPredicate([Operative](const FSquadMantleMemberView& M) { return M.Operative.Get() == Operative; });
			const FVector FacePoint = Member ? Member->LaneFacePoint : View.FacePoint;
			const FVector Dir = Member ? Member->LaneDir : View.Dir;
			const float Past = static_cast<float>(FVector::DotProduct(Feet - FacePoint, Dir));
			const float Rise = static_cast<float>(Feet.Z - FacePoint.Z);
			const bool bThere = Case.bOver ? Past > 20.f : (Past > 0.f && Operative->DidLastMantleEndOnTop() && Operative->GetMantleCount() > Track.MantlesBefore);
			Up += bThere ? 1 : 0;
			Where += FString::Printf(TEXT(" %s %s (%.0f cm past the face, %.0f up)"), *Who(Operative), bThere ? TEXT("there") : TEXT("NOT there"), Past, Rise);
		}
		Check(State, Up == State.Tracks.Num(), FString::Printf(TEXT("%s: all %d %s:%s"), Case.Label, State.Tracks.Num(), Case.bOver ? TEXT("over") : TEXT("on top"), *Where));
		// The climb order: the squad's (the leader first, then the formation / selection order).
		TArray<TPair<int32, FString>> Started;
		for (const FMemberTrack& Track : State.Tracks)
		{
			Started.Emplace(Track.ClimbedAtStep == INDEX_NONE ? MAX_int32 : Track.ClimbedAtStep, Who(Track.Operative.Get()));
		}
		Started.StableSort([](const TPair<int32, FString>& A, const TPair<int32, FString>& B) { return A.Key < B.Key; });
		FString Climbed;
		FString Expected;
		for (const TPair<int32, FString>& Entry : Started)
		{
			Climbed += Entry.Value + TEXT(" ");
		}
		for (const FSquadMantleMemberView& Member : View.Members)
		{
			Expected += Who(Member.Operative.Get()) + TEXT(" ");
		}
		const AOperativeCharacter* First = View.Members.IsEmpty() ? nullptr : View.Members[0].Operative.Get();
		Check(State, Climbed == Expected && !Started.IsEmpty() && Started[0].Value == Who(First), FString::Printf(
			TEXT("%s: they climbed in the squad's order, the leader first (climbed: %s| order: %s)"), Case.Label, *Climbed, *Expected));
		int32 Waited = 0;
		for (const FMemberTrack& Track : State.Tracks)
		{
			const AOperativeCharacter* Operative = Track.Operative.Get();
			const bool bLeader = Operative && Operative == First;
			if (!bLeader)
			{
				// The next man in line may be called before he has settled on his spot (a short climb): his samples are none, never bad.
				Waited += Track.WaitSamples > 0 ? 1 : 0;
				Check(State, Track.InwardSamples == 0, FString::Printf(
					TEXT("%s: %s waited his turn on his spot within %.0f cm of the face facing outwards (%d settled samples, %.0f cm out at most, look at most %.0f deg off outwards, %d bad)"),
					Case.Label, *Who(Operative), Config.SquadBaseRadiusCm, Track.WaitSamples, Track.MaxBaseCm, Track.WorstLookOff, Track.InwardSamples));
			}
			Check(State, Track.LongestTurn <= TurnLimitSeconds, FString::Printf(TEXT("%s: %s's turn took %.1f s (nobody stuck: at most %.0f s)"), Case.Label, *Who(Operative),
				Track.LongestTurn, TurnLimitSeconds));
		}
		Check(State, Waited > 0 || State.Tracks.Num() < 3, FString::Printf(TEXT("%s: the waiters were seen settled at the base (%d of %d)"), Case.Label, Waited,
			State.Tracks.Num() - 1));
	}

	bool StepSmoke(TSharedRef<FState> State)
	{
		UWorld* World = State->World.Get();
		if (!World)
		{
			return false;
		}
		switch (State->PreloadGate.Poll(World))
		{
		case SmokeUtils::EPreloadGate::Waiting:
			return true;
		case SmokeUtils::EPreloadGate::Failed:
			Check(*State, false, State->PreloadGate.FailReason);
			return Finish(*State, false);
		default:
			break;
		}
		State->Time += StepSeconds;
		State->StageTime += StepSeconds;
		if (State->Time > 840.f)
		{
			Check(*State, false, FString::Printf(TEXT("time out in stage %d, case %d"), State->Stage, State->Case));
			return Finish(*State, false);
		}
		for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
		{
			if (It->CustomTimeDilation > 0.f)
			{
				SmokeUtils::ParkEnemyFarAway(*It);
			}
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UMantleSubsystem* Mantle = World->GetSubsystem<UMantleSubsystem>();
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(World->GetFirstPlayerController());
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		if (!Squad || !Mantle || !PC || !Nav)
		{
			return State->Time < 60.f || Finish(*State, false);
		}
		for (const TWeakObjectPtr<AOperativeCharacter>& Member : State->Squad)
		{
			if (AOperativeCharacter* Operative = Member.Get())
			{
				Operative->ColdLevel = 0.f;
			}
		}
		AOperativeCharacter* Leader = State->Squad.IsEmpty() ? nullptr : State->Squad[0].Get();
		switch (State->Stage)
		{
		case 0:
		{
			if (!Squad->GetLeader() || State->StageTime < 3.f || (!Mantle->HasGenerated() && State->StageTime < 90.f))
			{
				return true;
			}
			const TArray<AOperativeCharacter*> Members = Squad->GetMembers();
			Check(*State, Members.Num() >= 3, FString::Printf(TEXT("a squad of at least 3 on the map (%d)"), Members.Num()));
			if (Members.Num() < 2)
			{
				return Finish(*State, false);
			}
			for (int32 Index = 0; Index < FMath::Min(3, Members.Num()); ++Index)
			{
				State->Squad.Add(Members[Index]);
				Members[Index]->bTacticalCeaseFire = true;
			}
			Check(*State, USquadSubsystem::IsSquadMantleEnabled(), TEXT("Codex.Mantle.Squad is on"));
			Next(*State, 1);
			return true;
		}
		case 1: // the case's site, the squad in front of it, the mode
		{
			if (State->Case >= UE_ARRAY_COUNT(Cases))
			{
				return Finish(*State, true);
			}
			const FCase& Case = Cases[State->Case];
			Squad->CancelSquadMantle(TEXT("the smoke's next case"));
			TSet<const AActor*> Skip = State->Used;
			Skip.Append(State->Hidden);
			const bool bPath = Case.Aim == EAim::PathBeyond;
			State->Site = MantleSmokeSites::FindSite(World, Leader, Case.Kind, Case.HeightCm, bPath ? PathStandOffCm : StandOffCm, Leader->GetActorLocation(), Skip,
				Case.bOver, bPath ? PathGoalOffCm : 150.f);
			if (!State->Site.bFound)
			{
				Check(*State, false, FString::Printf(TEXT("%s: a test site on the map"), Case.Label));
				++State->Case;
				return true;
			}
			const FMantleSite& Probe = State->Site.Probe;
			if (bPath)
			{
				// A fence whose way round costs more than the link's entering cost: the leader's path takes the link (else another fence).
				State->Goal = Probe.FacePoint + Probe.Dir * (Probe.Probe.DepthCm + PathGoalOffCm);
				State->Goal.Z = State->Site.Feet.Z;
				FNavLocation OnNav;
				const UNavigationPath* Round = Nav->ProjectPointToNavigation(State->Goal, OnNav, FVector(60.f, 60.f, 100.f))
					? Nav->FindPathToLocationSynchronously(World, State->Site.Feet, OnNav.Location, Leader, UNavFilter_OperativeNoMantle::StaticClass()) : nullptr;
				const float Direct = static_cast<float>(FVector::Dist2D(State->Site.Feet, State->Goal));
				const float RoundCm = Round && Round->IsValid() && !Round->IsPartial() ? static_cast<float>(Round->GetPathLength()) : TNumericLimits<float>::Max();
				if (!Round || RoundCm < Direct + MantleRules::GetConfig().LinkEnteringCostCm + 150.f)
				{
					State->Used.Add(State->Site.Actor.Get());
					if (++State->Retries > 12)
					{
						Check(*State, false, FString::Printf(TEXT("%s: a fence whose way round makes the path take its link"), Case.Label));
						State->Retries = 0;
						++State->Case;
					}
					return true;
				}
				State->Goal = OnNav.Location;
			}
			State->Used.Add(State->Site.Actor.Get());
			if (Case.Mode == EMode::Formation)
			{
				Squad->ClearGroupSelection();
				if (Squad->IsSoloMode())
				{
					Squad->ExitSoloMode(false, 0.f);
				}
				Squad->SetLeader(Leader);
			}
			else
			{
				Squad->EnterSoloMode();
				TArray<AOperativeCharacter*> Box;
				for (const TWeakObjectPtr<AOperativeCharacter>& Member : State->Squad)
				{
					Box.Add(Member.Get());
				}
				Squad->SetSelectedGroup(Box, false);
				Squad->SetLeader(Leader, false);
			}
			PlaceSquad(World, *State, State->Site.Feet, State->Site.Dir);
			Next(*State, 2);
			return true;
		}
		case 2: // the camera has followed: the click(s)
		{
			if (State->StageTime < 2.f)
			{
				return true;
			}
			const FCase& Case = Cases[State->Case];
			const bool bModeOk = Case.Mode == EMode::Formation ? !Squad->IsSoloMode() && Squad->GetFormationLeader() == Leader
				: Squad->IsSoloMode() && Squad->HasMultiSelection() && Squad->GetSelectedGroup().Num() == State->Squad.Num();
			Check(*State, bModeOk, FString::Printf(TEXT("%s: the squad is in %s"), Case.Label, Case.Mode == EMode::Formation ? TEXT("formation, the leader leading") : TEXT("Solo, all box-selected")));
			bool bHit = false;
			if (Case.Aim == EAim::PathBeyond)
			{
				bHit = World->LineTraceSingleByChannel(State->Hit, State->Goal + FVector(0.f, 0.f, 150.f), State->Goal - FVector(0.f, 0.f, 150.f), ECC_Visibility);
			}
			else
			{
				bHit = MantleSmokeSites::CameraRayHit(World, AimPoint(World, State->Site, Case.Aim), State->Hit) && State->Hit.GetActor() == State->Site.Actor.Get();
			}
			if (!bHit)
			{
				// The camera cannot see that face / top (another obstacle in front of it): the same case on another one of the kind.
				UE_LOG(LogCodexTactics, Display, TEXT("[FormationMantleSmoke] %s: the cursor's ray hits %s, not %s - another one"), Case.Label, *GetNameSafe(State->Hit.GetActor()),
					*GetNameSafe(State->Site.Actor.Get()));
				State->Hidden.Add(State->Site.Actor.Get());
				if (++State->Retries > 8)
				{
					Check(*State, false, FString::Printf(TEXT("%s: the camera sees a test site"), Case.Label));
					State->Retries = 0;
					++State->Case;
				}
				Next(*State, 1);
				return true;
			}
			State->Retries = 0;
			ArmCase(*State);
			State->ClimbsBefore = Squad->GetSquadMantleDoneCount();
			UE_LOG(LogCodexTactics, Display, TEXT("[FormationMantleSmoke] %s: %d click(s) on %s at %s"), Case.Label, Case.Clicks, *GetNameSafe(State->Hit.GetActor()),
				*State->Hit.ImpactPoint.ToCompactString());
			for (int32 Click = 0; Click < Case.Clicks; ++Click)
			{
				PC->HandleWorldHit(State->Hit);
			}
			Next(*State, 3);
			return true;
		}
		case 3: // the climb in turns
		{
			const FCase& Case = Cases[State->Case];
			Sample(*State, *Squad);
			const FSquadMantleView View = Squad->GetSquadMantleView();
			// An obstacle with room for fewer lanes than men (one side against a wall: the third man rightly holds at the base) cannot show
			// "all up": the same case on another obstacle of the kind.
			if (View.bActive && Case.Aim != EAim::PathBeyond
				&& View.Members.ContainsByPredicate([](const FSquadMantleMemberView& M) { return M.State == ESquadMantleState::Hold; }) && State->RoomRetries < 6)
			{
				UE_LOG(LogCodexTactics, Display, TEXT("[FormationMantleSmoke] %s: %s has no lane for every man (one holds at the base) - another one"), Case.Label,
					*GetNameSafe(View.Obstacle.Get()));
				++State->RoomRetries;
				Squad->CancelSquadMantle(TEXT("the smoke takes another obstacle"));
				Next(*State, 1);
				return true;
			}
			bool bAllDone = State->bSawOp && !View.Members.IsEmpty();
			for (const FSquadMantleMemberView& Member : View.Members)
			{
				bAllDone &= Member.State == ESquadMantleState::Up || Member.State == ESquadMantleState::Out || Member.State == ESquadMantleState::Hold;
			}
			const bool bEnded = State->bSawOp && (!View.bActive || View.bHolding || bAllDone);
			const float Limit = TurnLimitSeconds * State->Squad.Num() + 15.f;
			if (!bEnded && State->StageTime < Limit)
			{
				return true;
			}
			if (Case.Aim == EAim::PathBeyond && !(!bEnded && State->StageTime >= Limit))
			{
				Next(*State, 4); // the walk on
				return true;
			}
			Judge(*State, Case, !bEnded);
			++State->Case;
			Next(*State, 1);
			return true;
		}
		case 4: // the path case: the leader walks on to the click, the squad after him
		{
			const FCase& Case = Cases[State->Case];
			const bool bArrived = FVector::Dist2D(FeetOf(*Leader), State->Goal) <= WalkOnRadiusCm && !Leader->IsMantling();
			if (!bArrived && State->StageTime < WalkOnLimitSeconds)
			{
				return true;
			}
			Judge(*State, Case, false);
			Check(*State, bArrived, FString::Printf(TEXT("%s: then the leader walks on to the click (%.0f cm from it after %.1f s)"), Case.Label,
				FVector::Dist2D(FeetOf(*Leader), State->Goal), State->StageTime));
			Check(*State, !Squad->IsSquadMantleActive(), TEXT("e: the squad's climb is over (the formation follows again)"));
			++State->Case;
			Next(*State, 1);
			return true;
		}
		default:
			return Finish(*State, false);
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
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float)
		{
			return StepSmoke(State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.FormationMantleSmoke"),
		TEXT("Dev check (R17-1, L_Gen_Depot_1): the formation / a Solo box selection climbs a 140 wreck, the 230 parapet and over a 135 fence in turns - all ")
		TEXT("up / over in the squad's order, one climbing at a time, the waiters at the base facing out, nobody stuck; a formation path over a fence ")
		TEXT("link too; PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
