// Dev-only console command: the mantle clips' contact / pop / handoff measured frame by frame on L_Gen_Depot_1
// (designer bug report 2026-11-16, docs/port/MANTLING_ALIGN_REPORT.md):
//   Scripts/smoke_parallel.ps1 -Commands MantleAlignSmoke
// One Solo operative stands at the click order's base point in front of each height class (85 cm crate, 140 cm wreck, the cut's 230 cm
// parapet twice - both tall-ledge clips -, a 135 cm fence) and climbs. Every frame his bones are sampled (the mesh ticks its pose here as
// it does on screen). Per climb (tolerances: mantle.json align.*):
//   hands   the nearer hand comes within align.hand_edge_tolerance_cm of the ledge's lip (the designer: "hands do not meet the edge");
//   pop     on a top, neither the lowest foot (beyond the clip's own authored step) nor the capsule rises more than
//           align.pop_tolerance_cm over the final standing height ("thrown upward, then drops down sharply");
//   freeze  from the climb's last motion to moving again he never stands still longer than align.freeze_limit_s ("freezes for about
//           a second"); a bone slower than align.still_speed_cm_s is still.
// The clip's own motion (GetMantleClipTrack) and the runtime trace are logged for the report. Enemies are parked far away.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/MantleRules.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Containers/Ticker.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/MantleNavigation.h"
#include "NavigationSystem.h"

namespace MantleAlignSmoke
{
	constexpr float StepSeconds = 0.05f;
	/** The click order's base point lies this far out from the face (MantleNavigation's MantleClickBaseCm). */
	constexpr float ClickBaseCm = 80.f;
	/** Sampled this long after the climb ended (the handoff to the stance / locomotion). */
	constexpr float AfterSeconds = 1.f;
	/** Bone speeds for the freeze measure are taken over this window, s. */
	constexpr float SpeedWindowSeconds = 0.1f;

	struct FCase
	{
		const TCHAR* Label;
		const TCHAR* Kind;
		float HeightCm;
		const TCHAR* Class;
		bool bOnTop;
	};

	static const FCase Cases[] = {
		{ TEXT("low 85 cm crate"), TEXT("crate"), 85.f, TEXT("low_mantle"), true },
		{ TEXT("medium 140 cm wreck"), TEXT("wreck"), 140.f, TEXT("high_mantle"), true },
		{ TEXT("high 230 cm parapet (1st clip)"), TEXT("cut_wall"), 230.f, TEXT("tall_ledge"), true },
		{ TEXT("high 230 cm parapet (2nd clip)"), TEXT("cut_wall"), 230.f, TEXT("tall_ledge"), true },
		{ TEXT("135 cm fence"), TEXT("fence"), 135.f, TEXT("high_fence_over"), false },
	};

	/** One frame of the climb (world space). */
	struct FSample
	{
		float Time = 0.f;
		FVector Feet = FVector::ZeroVector;
		FVector HandL = FVector::ZeroVector;
		FVector HandR = FVector::ZeroVector;
		FVector FootL = FVector::ZeroVector;
		FVector FootR = FVector::ZeroVector;
		FVector Pelvis = FVector::ZeroVector;
		FVector Head = FVector::ZeroVector;
		bool bMantling = false;
	};

	struct FSite
	{
		TWeakObjectPtr<AActor> Actor;
		FVector Feet = FVector::ZeroVector;
		FVector Dir = FVector::ForwardVector;
		FMantleSite Probe;
		bool bFound = false;
	};

	struct FState
	{
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<AOperativeCharacter> Leader;
		SmokeUtils::FPreloadGate PreloadGate;
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Case = 0;
		int32 Failures = 0;
		int32 Count = 0;
		FSite Site;
		bool bSampling = false;
		float SampleClock = 0.f;
		float EndedAt = -1.f;
		TArray<FSample> Samples;
		TSet<int32> DumpedClips;
		FTSTicker::FDelegateHandle SampleTicker;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State, bool bComplete)
	{
		FTSTicker::GetCoreTicker().RemoveTicker(State.SampleTicker);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("MantleAlignSmoke"));
		return false;
	}

	void Next(FState& State, int32 Stage)
	{
		State.Stage = Stage;
		State.StageTime = 0.f;
	}

	float TopOf(const AActor& Actor)
	{
		FVector Origin;
		FVector Extent;
		Actor.GetActorBounds(false, Origin, Extent);
		return static_cast<float>(Origin.Z + Extent.Z);
	}

	/** A face of an obstacle of Kind (+-6 cm of HeightCm) with free navmesh ground StandOffCm out where the probe says go; nearest to Near. */
	FSite FindSite(UWorld* World, AOperativeCharacter* Leader, const TCHAR* Kind, float HeightCm, float StandOffCm, const FVector& Near, const TSet<const AActor*>& Used)
	{
		FSite Best;
		float BestDist = TNumericLimits<float>::Max();
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		const float Radius = Leader->GetCapsuleComponent()->GetScaledCapsuleRadius();
		const float HalfHeight = Leader->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		const FMantleConfig& Config = MantleRules::GetConfig();
		FCollisionQueryParams Params(SCENE_QUERY_STAT(MantleAlignSmoke), false, Leader);
		const FName KindName(Kind);
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (!It->Tags.Contains(KindName) || Used.Contains(*It))
			{
				continue;
			}
			const FBox Local = It->CalculateComponentsBoundingBoxInLocalSpace(false);
			if (!Local.IsValid)
			{
				continue;
			}
			const FTransform Transform = It->GetActorTransform();
			const float Bottom = static_cast<float>(Transform.TransformPosition(FVector(Local.GetCenter().X, Local.GetCenter().Y, Local.Min.Z)).Z);
			if (FMath::Abs(TopOf(**It) - Bottom - HeightCm) > 6.f)
			{
				continue;
			}
			for (int32 Face = 0; Face < 4; ++Face)
			{
				const int32 Axis = Face % 2;
				const float Sign = Face < 2 ? 1.f : -1.f;
				FVector LocalFace = Local.GetCenter();
				LocalFace[Axis] = Sign > 0.f ? Local.Max[Axis] : Local.Min[Axis];
				FVector LocalNormal = FVector::ZeroVector;
				LocalNormal[Axis] = Sign;
				const int32 TangentAxis = 1 - Axis;
				if ((Local.Max[TangentAxis] - Local.Min[TangentAxis]) * Transform.GetScale3D().GetAbs()[TangentAxis] < 2.f * Radius + 20.f)
				{
					continue; // an end face: no square climb there
				}
				const FVector Normal = Transform.TransformVectorNoScale(LocalNormal).GetSafeNormal2D();
				FVector FaceWorld = Transform.TransformPosition(LocalFace);
				FaceWorld.Z = Bottom;
				const FVector Spot = FaceWorld + Normal * StandOffCm;
				const float Dist = static_cast<float>(FVector::Dist2D(Spot, Near));
				if (Dist >= BestDist)
				{
					continue;
				}
				FNavLocation OnNav;
				if (!Nav || !Nav->ProjectPointToNavigation(Spot, OnNav, FVector(50.f, 50.f, 100.f)) || FMath::Abs(OnNav.Location.Z - Bottom) > 30.f
					|| World->OverlapAnyTestByChannel(Spot + FVector(0.f, 0.f, HalfHeight + 6.f), FQuat::Identity, ECC_Pawn,
						FCollisionShape::MakeCapsule(Radius, HalfHeight), Params))
				{
					continue;
				}
				FMantleSite Probe;
				if (!MantleNavigation::ProbeSite(*World, Spot, -Normal, StandOffCm + 100.f, Radius, HalfHeight, Params, Config, Probe)
					|| Probe.Obstacle.Get() != *It || !MantleRules::Decide(Config, Probe.Probe).IsGo())
				{
					continue;
				}
				BestDist = Dist;
				Best.Actor = *It;
				Best.Feet = Spot;
				Best.Dir = -Normal;
				Best.Probe = Probe;
				Best.bFound = true;
			}
		}
		return Best;
	}

	/** Every frame while sampling: his feet and bones (world). */
	bool Sample(TSharedRef<FState> State, float DeltaTime)
	{
		AOperativeCharacter* Leader = State->Leader.Get();
		if (!State->bSampling || !Leader)
		{
			return true;
		}
		const USkeletalMeshComponent* Body = Leader->GetMesh();
		FSample Each;
		Each.Time = State->SampleClock;
		State->SampleClock += DeltaTime;
		Each.Feet = Leader->GetActorLocation() - FVector(0.f, 0.f, Leader->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
		Each.HandL = Body->GetSocketLocation(TEXT("hand_l"));
		Each.HandR = Body->GetSocketLocation(TEXT("hand_r"));
		Each.FootL = Body->GetSocketLocation(TEXT("foot_l"));
		Each.FootR = Body->GetSocketLocation(TEXT("foot_r"));
		Each.Pelvis = Body->GetSocketLocation(TEXT("pelvis"));
		Each.Head = Body->GetSocketLocation(TEXT("head"));
		Each.bMantling = Leader->IsMantling();
		State->Samples.Add(Each);
		return true;
	}

	/** The clip's authored motion (clip space: X forward, Z up, cm) every 0.1 s, once per clip. */
	void DumpClip(FState& State, AOperativeCharacter& Leader)
	{
		const int32 ClipIndex = Leader.GetLastMantleClipIndex();
		if (State.DumpedClips.Contains(ClipIndex))
		{
			return;
		}
		State.DumpedClips.Add(ClipIndex);
		const FMantleClipTrack* Track = Leader.GetMantleClipTrack(ClipIndex);
		if (!Track || Track->Num() == 0)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("[MantleAlignSmoke] clip %s: no track"), *Leader.GetLastMantleClip());
			return;
		}
		const int32 Every = FMath::Max(1, FMath::RoundToInt(0.1f / Track->SampleSeconds));
		UE_LOG(LogCodexTactics, Display, TEXT("[MantleAlignSmoke] clip %s track (t root.x root.z | pelvis.x pelvis.z | handL.x handL.z | handR.x handR.z | footL.z footR.z):"),
			*Leader.GetLastMantleClip());
		for (int32 Index = 0; Index < Track->Num(); Index += Every)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("[MantleAlignSmoke]   %.2f %6.1f %6.1f | %6.1f %6.1f | %6.1f %6.1f | %6.1f %6.1f | %6.1f %6.1f"), Track->TimeAt(Index),
				Track->Root[Index].X, Track->Root[Index].Z, Track->Pelvis[Index].X, Track->Pelvis[Index].Z, Track->HandL[Index].X, Track->HandL[Index].Z,
				Track->HandR[Index].X, Track->HandR[Index].Z, Track->FootL[Index].Z, Track->FootR[Index].Z);
		}
		const FMantleConfig& Config = MantleRules::GetConfig();
		if (Config.Clips.IsValidIndex(ClipIndex))
		{
			const FMantleClipContact Contact = MantleRules::AnalyseClipTrack(Config, *Track, Config.Clips[ClipIndex].RiseCm);
			UE_LOG(LogCodexTactics, Display, TEXT("[MantleAlignSmoke] clip %s: plant %s at %.2f s, hand %s, root then %s, root at frame 0 %s, exit %.2f s of %.2f"),
				*Leader.GetLastMantleClip(), Contact.bValid ? TEXT("found") : TEXT("NOT found"), Contact.ContactSeconds, *Contact.HandPoint.ToCompactString(),
				*Contact.RootAtContact.ToCompactString(), *Contact.RootStart.ToCompactString(), Contact.ExitSeconds, Track->TimeAt(Track->Num() - 1));
		}
	}

	/** The three measures of one climb from its samples. */
	void Measure(FState& State, const FCase& Case, AOperativeCharacter& Leader)
	{
		const FMantleConfig& Config = MantleRules::GetConfig();
		const FMantleSite& Site = State.Site.Probe;
		const TArray<FSample>& Samples = State.Samples;
		if (Samples.Num() < 3)
		{
			Check(State, false, FString::Printf(TEXT("%s: sampled the climb (%d frames)"), Case.Label, Samples.Num()));
			return;
		}
		// The runtime trace (every 6th frame): t, feet (forward of the face, z over the top), hands / lowest foot / pelvis the same way.
		auto Forward = [&Site](const FVector& Point) { return static_cast<float>(FVector::DotProduct(Point - Site.FacePoint, Site.Dir)); };
		UE_LOG(LogCodexTactics, Display, TEXT("[MantleAlignSmoke] %s trace (t m | feet fwd z | handL fwd z | handR fwd z | lowfoot z | pelvis z), z over the top %.0f:"),
			Case.Label, Site.TopZ);
		for (int32 Index = 0; Index < Samples.Num(); Index += 6)
		{
			const FSample& S = Samples[Index];
			UE_LOG(LogCodexTactics, Display, TEXT("[MantleAlignSmoke]   %.2f %d | %6.1f %6.1f | %6.1f %6.1f | %6.1f %6.1f | %6.1f | %6.1f"), S.Time, S.bMantling ? 1 : 0,
				Forward(S.Feet), S.Feet.Z - Site.TopZ, Forward(S.HandL), S.HandL.Z - Site.TopZ, Forward(S.HandR), S.HandR.Z - Site.TopZ,
				FMath::Min(S.FootL.Z, S.FootR.Z) - Site.TopZ, S.Pelvis.Z - Site.TopZ);
		}
		// 1. Hands to the lip: in front of the face the distance to the lip line, over the top the height over it.
		float BestGap = TNumericLimits<float>::Max();
		float BestFwd = 0.f;
		float BestUp = 0.f;
		float BestTime = 0.f;
		for (const FSample& S : Samples)
		{
			if (!S.bMantling)
			{
				continue;
			}
			for (const FVector& Hand : { S.HandL, S.HandR })
			{
				const float Fwd = Forward(Hand);
				const float Up = static_cast<float>(Hand.Z) - Site.TopZ;
				const float Gap = FMath::Sqrt(FMath::Square(FMath::Min(Fwd, 0.f)) + FMath::Square(Up));
				if (Gap < BestGap)
				{
					BestGap = Gap;
					BestFwd = Fwd;
					BestUp = Up;
					BestTime = S.Time;
				}
			}
		}
		Check(State, BestGap <= Config.AlignHandEdgeToleranceCm, FString::Printf(
			TEXT("%s (%s): the hands meet the edge - nearest %.1f cm (%.1f cm %s the face, %.1f cm over the top at %.2f s; tolerance %.0f)"), Case.Label,
			*Leader.GetLastMantleClip(), BestGap, FMath::Abs(BestFwd), BestFwd < 0.f ? TEXT("in front of") : TEXT("past"), BestUp, BestTime,
			Config.AlignHandEdgeToleranceCm));
		// 2. The pop: the final standing height = the feet after the climb.
		int32 EndIndex = Samples.IndexOfByPredicate([](const FSample& S) { return !S.bMantling && S.Time > 0.f; });
		EndIndex = EndIndex == INDEX_NONE ? Samples.Num() - 1 : EndIndex;
		const float FinalZ = static_cast<float>(Samples.Last().Feet.Z);
		if (Case.bOnTop)
		{
			float FootPop = -TNumericLimits<float>::Max();
			float CapsulePop = -TNumericLimits<float>::Max();
			float FootPopTime = 0.f;
			for (const FSample& S : Samples)
			{
				const float Low = static_cast<float>(FMath::Min(S.FootL.Z, S.FootR.Z)) - FinalZ;
				if (Low > FootPop)
				{
					FootPop = Low;
					FootPopTime = S.Time;
				}
				CapsulePop = FMath::Max(CapsulePop, static_cast<float>(S.Feet.Z) - FinalZ);
			}
			// The lowest foot of a standing / crouching body sits a few cm over the floor (ankle): measured against the same foot at rest.
			const FSample& Rest = Samples.Last();
			const float RestFoot = static_cast<float>(FMath::Min(Rest.FootL.Z, Rest.FootR.Z)) - FinalZ;
			// The clip's own step: how far its lowest foot rises over its end height anywhere in the clip (an authored hop onto the top is no pop).
			float AuthoredLift = 0.f;
			if (const FMantleClipTrack* Track = Leader.GetMantleClipTrack(Leader.GetLastMantleClipIndex()); Track && Track->Num() > 0)
			{
				const float EndLow = static_cast<float>(FMath::Min(Track->FootL.Last().Z, Track->FootR.Last().Z));
				for (int32 Index = 0; Index < Track->Num(); ++Index)
				{
					AuthoredLift = FMath::Max(AuthoredLift, static_cast<float>(FMath::Min(Track->FootL[Index].Z, Track->FootR[Index].Z)) - EndLow);
				}
			}
			const float FootExcess = FootPop - RestFoot - AuthoredLift;
			const float Pop = FMath::Max(FootExcess, CapsulePop);
			Check(State, Pop <= Config.AlignPopToleranceCm, FString::Printf(
				TEXT("%s: no pop over the top - the lowest foot rises %.1f cm over its rest (at %.2f s; the clip's own step %.1f -> excess %.1f), the capsule %.1f cm over the landing (tolerance %.0f)"),
				Case.Label, FootPop - RestFoot, FootPopTime, AuthoredLift, FootExcess, CapsulePop, Config.AlignPopToleranceCm));
		}
		// 3. The freeze: the longest still stretch from the climb's second half to the end of the sampling (a bone moving = he is not frozen).
		const float ClipEnd = Samples[EndIndex].Time;
		const float From = ClipEnd * 0.5f;
		float Longest = 0.f;
		float LongestAt = 0.f;
		float Run = 0.f;
		// Speeds over SpeedWindowSeconds (a frame-to-frame difference turns the pose's sub-cm jitter into a speed).
		int32 Lag = 1;
		while (Lag < Samples.Num() - 1 && Samples[Lag].Time - Samples[0].Time < SpeedWindowSeconds)
		{
			++Lag;
		}
		for (int32 Index = Lag; Index < Samples.Num(); ++Index)
		{
			const FSample& A = Samples[Index - Lag];
			const FSample& B = Samples[Index];
			const float Dt = FMath::Max(B.Time - A.Time, KINDA_SMALL_NUMBER);
			const float Step = FMath::Max(B.Time - Samples[Index - 1].Time, 0.f);
			if (B.Time < From)
			{
				continue;
			}
			float Speed = 0.f;
			for (const TPair<FVector, FVector>& Bone : { TPair<FVector, FVector>(A.HandL, B.HandL), TPair<FVector, FVector>(A.HandR, B.HandR),
				TPair<FVector, FVector>(A.FootL, B.FootL), TPair<FVector, FVector>(A.FootR, B.FootR), TPair<FVector, FVector>(A.Pelvis, B.Pelvis),
				TPair<FVector, FVector>(A.Head, B.Head) })
			{
				Speed = FMath::Max(Speed, static_cast<float>(FVector::Dist(Bone.Key, Bone.Value)) / Dt);
			}
			if (Speed < Config.AlignStillSpeedCmPerSecond)
			{
				Run += Step;
				if (Run > Longest)
				{
					Longest = Run;
					LongestAt = B.Time;
				}
			}
			else
			{
				Run = 0.f;
			}
		}
		// A body that has finished everything stands still at the very end of the window: a still stretch running into the window's end
		// counts only up to the end of the climb's handoff (the clip end + blend out).
		const float WindowEnd = Samples.Last().Time;
		const float HandoffEnd = ClipEnd + Config.BlendOutSeconds;
		if (LongestAt >= WindowEnd - KINDA_SMALL_NUMBER)
		{
			Longest = FMath::Max(0.f, FMath::Min(LongestAt, HandoffEnd) - (LongestAt - Longest));
		}
		Check(State, Longest <= Config.AlignFreezeLimitSeconds, FString::Printf(
			TEXT("%s: no freeze at the handoff - longest still stretch %.2f s (ending %.2f s; the climb ended at %.2f s; limit %.2f)"), Case.Label, Longest,
			LongestAt, ClipEnd, Config.AlignFreezeLimitSeconds));
		UE_LOG(LogCodexTactics, Display, TEXT("[MantleAlignSmoke] %s: %s, face %.0f cm off at the start, climb %.2f s, landing error %.1f cm"), Case.Label,
			*Leader.GetLastMantleClip(), State.Site.Probe.Probe.FaceDistanceCm, ClipEnd, Leader.GetLastMantleLandingError());
	}

	bool Step(TSharedRef<FState> State)
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
		if (State->Time > 300.f)
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
		AOperativeCharacter* Leader = State->Leader.Get();
		switch (State->Stage)
		{
		case 0:
		{
			if (!Squad || !Squad->GetLeader() || !Mantle || State->StageTime < 3.f || (!Mantle->HasGenerated() && State->StageTime < 90.f))
			{
				return true;
			}
			State->Leader = Squad->GetLeader();
			Leader = State->Leader.Get();
			Squad->EnterSoloMode();
			Squad->SetFollowersHolding(true);
			Leader->bTacticalCeaseFire = true;
			// The pose ticks and the bones refresh as on screen (a headless game skips an unseen mesh's pose).
			Leader->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			State->SampleTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float DeltaTime) { return Sample(State, DeltaTime); }));
			Next(*State, 1);
			return true;
		}
		case 1: // place him at the click order's base point in front of the case's obstacle
		{
			if (State->Case >= UE_ARRAY_COUNT(Cases))
			{
				return Finish(*State, true);
			}
			const FCase& Case = Cases[State->Case];
			TSet<const AActor*> Used;
			State->Site = FindSite(World, Leader, Case.Kind, Case.HeightCm, ClickBaseCm, Leader->GetActorLocation(), Used);
			if (!State->Site.bFound)
			{
				Check(*State, false, FString::Printf(TEXT("%s: a test site on the map"), Case.Label));
				++State->Case;
				return true;
			}
			Leader->StopOperative();
			Leader->SetStance(EOperativeStance::Standing);
			Leader->TeleportTo(State->Site.Feet + FVector(0.f, 0.f, Leader->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 4.f), State->Site.Dir.Rotation(),
				false, true);
			State->Count = Leader->GetMantleCount();
			Next(*State, 2);
			return true;
		}
		case 2: // settled: climb
		{
			if (State->StageTime < 1.f)
			{
				return true;
			}
			const FCase& Case = Cases[State->Case];
			State->Samples.Reset();
			State->SampleClock = 0.f;
			State->EndedAt = -1.f;
			State->bSampling = true;
			EMantleRefusal Refusal = EMantleRefusal::None;
			const bool bStarted = Leader->TryStartMantle(State->Site.Dir, &Refusal, true);
			Check(*State, bStarted && Leader->IsMantling() && Leader->GetLastMantleClass() == FName(Case.Class), FString::Printf(TEXT("%s: the climb starts (%s, %s, class %s)"),
				Case.Label, *GetNameSafe(State->Site.Actor.Get()), bStarted ? *Leader->GetLastMantleClip() : MantleRules::RefusalName(Refusal),
				*Leader->GetLastMantleClass().ToString()));
			if (!bStarted)
			{
				State->bSampling = false;
				++State->Case;
				Next(*State, 1);
				return true;
			}
			DumpClip(*State, *Leader);
			Next(*State, 3);
			return true;
		}
		case 3: // the climb, then AfterSeconds of the handoff
		{
			if (Leader->IsMantling() && State->StageTime < 12.f)
			{
				return true;
			}
			if (State->EndedAt < 0.f)
			{
				State->EndedAt = State->StageTime;
			}
			if (State->StageTime - State->EndedAt < AfterSeconds)
			{
				return true;
			}
			State->bSampling = false;
			const FCase& Case = Cases[State->Case];
			Check(*State, !Leader->IsMantling() && Leader->GetMantleCount() == State->Count + 1, FString::Printf(TEXT("%s: the climb ended (%.1f s)"), Case.Label, State->EndedAt));
			Measure(*State, Case, *Leader);
			// Down again for the next case (a top: back to the base by teleport - the hop down is MantleSmoke's).
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
			return Step(State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.MantleAlignSmoke"),
		TEXT("Dev check (mantle alignment, designer bug report 2026-11-16, L_Gen_Depot_1): per height class the hands meet the ledge's lip, no pop ")
		TEXT("over the top, no freeze at the handoff (tolerances mantle.json align.*); logs the clip tracks and the runtime trace, PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
