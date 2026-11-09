// Dev-only headless check of the game designer's shotgun play-test 2026-11-09 (item 2): "when I select another operative as the group leader
// and the shotgun operative follows in the formation, he constantly twitches / spins with micro-rotations".
//   powershell -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -Commands FormationJitterSmoke -ExtraArgs "-Multiprocess"
// The designer's close-up video 09.11.2026_ Trembling_Bug_01.avi (L_Gen_Depot_1, the Commander leading along walls, "Echelon left -> Column
// (wall)" / "(turn)"): "when all three carried the M16 it was much less; now with one man on the shotgun it seems to affect the whole group".
// Any map (L_MovementTest by default, -Map /Game/Maps/Generated/L_Gen_Depot_1 too), exploration, formation on (Solo off): on a clear stretch two
// 3 m walls are spawned along the route (one beside the first leg, one beside the third) so the formation folds to the column at them and at
// the turns; the Commander (M16) leads the route with turns, the Engineer (D-1, the full-body shotgun weapon layer) and the third member (M16)
// follow. The route is walked three times from the same start:
//   A. the Engineer with the D-1 (the weapon layer on),
//   B. the D-1 with the weapon layer forced off (Codex.Anim.WeaponLayer 0: the ABP's rifle locomotion with the D-1 in hands),
//   C. the Engineer with an M16 (all three on the M16: the baseline - the same slot, the same body).
//   D. C without the formation look filter (Codex.Formation.LookFilter 0: the state of the video).
//   E. the D-1 Engineer LEADS the route (the Commander follows): his own gait's sway without the formation's corrections.
// Every member (the leader too) is measured; the formation: its HUD label's changes per second, each follower's given slot jumps (> 25 cm in
// one frame) and formation-look jumps (> 15 deg in one frame) per second.
// Measured per follower while the leader walks (every frame; the poses are evaluated headless): the yaw jitter of the hip line (the pelvis /
// legs) and of the shoulder line - the frame-to-frame turn minus its 0.3 s running mean (a high-pass: the route's real turns drop out), RMS in
// deg/s - and the turn's sign flips per second (|turn| > 0.15 deg a frame), the capsule's own yaw jitter (his rotation logic), the 8-way
// direction-sector switches per second of the anim's Direction, the weapon layer's dominant directional-clip switches per second, the turn-in-
// place clip's frames / restarts while following, and the idle <-> move flips of the layer's move weight.
// Checks: what following ADDS to the D-1 man's hip / shoulder WOBBLE (per 0.5 s, the turn travelled minus the net turn: clean turns add 0;
// A minus E) is no more than it adds to an M16 man's in the same slot (C follower minus C leader) - at most x1.25 + 5 deg/s -, his dominant-clip switches at most 2 per second, the other follower
// and the leader no worse with him in the squad, the all-M16 squad's capsules wobble less with the look filter than without (D), the
// formation label no more restless; every run reaches the route's end. Per-frame CSVs: Saved/Logs/FormationJitter-<A..D>.csv.
// Godot reference: none (UE-only weapon layer).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Animation/AnimSequenceBase.h"
#include "Characters/OperativeAnimInstance.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Containers/Ticker.h"
#include "Data/WeaponDataAsset.h"
#include "Debug/SmokeUtils.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CoreDelegates.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "TimerManager.h"

namespace FormationJitterSmoke
{
	constexpr float NavWarmupSeconds = 3.f;
	/** The high-pass: the frame-to-frame turn minus its running mean over this, s. */
	constexpr float HighPassSeconds = 0.3f;
	/** The back-and-forth measure's window, s. */
	constexpr float WobbleWindowSeconds = 0.5f;
	/** A frame's turn counts for the sign flips above this, deg. */
	constexpr float FlipMinDeg = 0.15f;
	/** The D-1 follower against the M16 one in the same slot: jitter at most this factor ... */
	constexpr float JitterFactor = 1.25f;
	/** ... plus this, deg/s. */
	constexpr float JitterMarginDegS = 5.f;
	/** The dominant directional clip may change at most this often while following, per second. */
	constexpr float MaxClipSwitchesPerSecond = 2.f;

	/** One yaw signal's jitter accumulator. */
	struct FYawJitter
	{
		bool bHasLast = false;
		float Last = 0.f;
		float LastTurn = 0.f;
		float Mean = 0.f;
		double SumSq = 0.0;
		int32 Samples = 0;
		int32 Flips = 0;
		/** Back-and-forth: per 0.5 s window, the turn travelled minus the net turn (a clean turn adds 0, a wobble its swings). */
		float WindowAbs = 0.f;
		float WindowNet = 0.f;
		float WindowTime = 0.f;
		double Wobble = 0.0;
		double Seconds = 0.0;

		void Reset() { *this = FYawJitter(); }

		void Add(float Yaw, float Dt)
		{
			if (!bHasLast || Dt <= KINDA_SMALL_NUMBER)
			{
				Last = Yaw;
				bHasLast = true;
				return;
			}
			const float Turn = FMath::FindDeltaAngleDegrees(Last, Yaw);
			Last = Yaw;
			Mean += (Turn - Mean) * (1.f - FMath::Exp(-Dt / HighPassSeconds));
			const float High = (Turn - Mean) / Dt;
			SumSq += static_cast<double>(High) * High;
			++Samples;
			if (FMath::Abs(Turn) > FlipMinDeg && FMath::Abs(LastTurn) > FlipMinDeg && FMath::Sign(Turn) != FMath::Sign(LastTurn))
			{
				++Flips;
			}
			if (FMath::Abs(Turn) > FlipMinDeg)
			{
				LastTurn = Turn;
			}
			WindowAbs += FMath::Abs(Turn);
			WindowNet += Turn;
			WindowTime += Dt;
			Seconds += Dt;
			if (WindowTime >= WobbleWindowSeconds)
			{
				Wobble += WindowAbs - FMath::Abs(WindowNet);
				WindowAbs = WindowNet = WindowTime = 0.f;
			}
		}

		float Rms() const { return Samples > 0 ? static_cast<float>(FMath::Sqrt(SumSq / Samples)) : 0.f; }
		/** The back-and-forth turning, deg/s (0 for clean turns). */
		float WobbleRate() const { return Seconds > 0.0 ? static_cast<float>(Wobble / Seconds) : 0.f; }
	};

	/** One follower's numbers over a run. */
	struct FFollowerStats
	{
		TWeakObjectPtr<AOperativeCharacter> Who;
		FYawJitter Hips;
		FYawJitter Shoulders;
		FYawJitter Capsule;
		float Seconds = 0.f;
		int32 LastSector = INT32_MIN;
		int32 SectorSwitches = 0;
		FString LastClip;
		int32 ClipSwitches = 0;
		int32 TurnFrames = 0;
		int32 TurnRestarts = 0;
		float LastTurnTime = -1.f;
		FString LastTurnClip;
		bool bLastMoving = false;
		int32 MoveFlips = 0;
		double SpeedSum = 0.0;
		double SpeedSq = 0.0;
		int32 Frames = 0;
		bool bHasSlot = false;
		FVector LastSlot = FVector::ZeroVector;
		int32 SlotJumps = 0;
		bool bHasLook = false;
		float LastLook = 0.f;
		int32 LookJumps = 0;
		int32 LookChanges = 0;

		void Reset(AOperativeCharacter* Operative)
		{
			*this = FFollowerStats();
			Who = Operative;
		}

		float PerSecond(int32 Count) const { return Seconds > KINDA_SMALL_NUMBER ? Count / Seconds : 0.f; }

		FString Line(const TCHAR* Label) const
		{
			const float Mean = Frames > 0 ? static_cast<float>(SpeedSum / Frames) : 0.f;
			const float Sd = Frames > 0 ? FMath::Sqrt(FMath::Max(0.f, static_cast<float>(SpeedSq / Frames) - Mean * Mean)) : 0.f;
			const AOperativeCharacter* Operative = Who.Get();
			return FString::Printf(TEXT("%s %s (%s): WOBBLE hips %.1f / shoulders %.1f / capsule %.1f deg/s | hips %.1f deg/s rms, %.2f flips/s | shoulders %.1f deg/s, %.2f flips/s | capsule %.1f deg/s, %.2f flips/s | direction sector %.2f/s | clip %.2f/s | turn clip %d frames, %d restarts | idle<->move %d | speed %.0f +- %.0f cm/s | slot jumps %.2f/s | look jumps %.2f/s, look set/clear %.2f/s | over %.1f s"),
				Label, Operative ? *Operative->DisplayName.ToString() : TEXT("-"), Operative && Operative->CurrentWeapon ? *Operative->CurrentWeapon->WeaponId : TEXT("-"),
				Hips.WobbleRate(), Shoulders.WobbleRate(), Capsule.WobbleRate(), Hips.Rms(), PerSecond(Hips.Flips), Shoulders.Rms(), PerSecond(Shoulders.Flips), Capsule.Rms(), PerSecond(Capsule.Flips), PerSecond(SectorSwitches),
				PerSecond(ClipSwitches), TurnFrames, TurnRestarts, MoveFlips, Mean, Sd, PerSecond(SlotJumps), PerSecond(LookJumps), PerSecond(LookChanges), Seconds);
		}
	};

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		int32 Variant = 0;
		bool bSampling = false;
		TWeakObjectPtr<AOperativeCharacter> Leader;
		TWeakObjectPtr<AOperativeCharacter> Engineer;
		TWeakObjectPtr<AOperativeCharacter> Rifleman;
		TWeakObjectPtr<AOperativeCharacter> Commander;
		TArray<TWeakObjectPtr<AActor>> Walls;
		TArray<FString> Csv;
		float SampleTime = 0.f;
		/** The formation's HUD label and its changes while sampling. */
		FString LastLabel;
		int32 LabelChanges = 0;
		float LabelSeconds = 0.f;
		TMap<FString, float> LabelTime;
		float LabelChangesPerSecond[5] = { 0.f, 0.f, 0.f, 0.f, 0.f };
		FString LabelSummary[5];
		FVector Start = FVector::ZeroVector;
		FVector Forward = FVector::ForwardVector;
		FRotator StartRotation = FRotator::ZeroRotator;
		TArray<FVector> Route;
		int32 Leg = 0;
		float LegTime = 0.f;
		FFollowerStats EngineerStats;
		FFollowerStats RiflemanStats;
		FFollowerStats LeaderStats;
		/** Per variant (A / B / C): the Engineer's, the rifleman's and the leader's numbers. */
		FFollowerStats Results[5][3];
		bool bRouteDone[5] = { false, false, false, false, false };
		FDelegateHandle FrameHandle;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	void SetLookFilterCVar(int32 Value)
	{
		if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Formation.LookFilter")))
		{
			CVar->Set(Value, ECVF_SetByConsole);
		}
	}

	void SetLayerCVar(int32 Value)
	{
		if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Anim.WeaponLayer")))
		{
			CVar->Set(Value, ECVF_SetByConsole);
		}
	}

	bool Finish(FState& State, bool bComplete)
	{
		SetLayerCVar(1);
		SetLookFilterCVar(1);
		FCoreDelegates::OnEndFrame.Remove(State.FrameHandle);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("FormationJitterSmoke"));
		return false;
	}

	void NextStage(FState& State, int32 Stage)
	{
		State.Stage = Stage;
		State.StageTime = 0.f;
		UE_LOG(LogCodexTactics, Display, TEXT("[FormationJitter] stage %d at %.1f s"), Stage, State.Time);
	}

	UOperativeAnimInstance* AnimOf(const AOperativeCharacter* Operative)
	{
		return Operative && Operative->GetMesh() ? Cast<UOperativeAnimInstance>(Operative->GetMesh()->GetAnimInstance()) : nullptr;
	}

	/** The yaw of a body line (left bone -> right bone; only its frame-to-frame change is used). */
	float LineYaw(const USkeletalMeshComponent* Mesh, const TCHAR* Left, const TCHAR* Right)
	{
		const FVector Line = Mesh->GetBoneLocation(Right) - Mesh->GetBoneLocation(Left);
		return FMath::RadiansToDegrees(FMath::Atan2(static_cast<float>(Line.Y), static_cast<float>(Line.X)));
	}

	void SampleFollower(FFollowerStats& Stats, float Dt, const USquadSubsystem* Squad, TArray<FString>* Csv, float Time)
	{
		const AOperativeCharacter* Who = Stats.Who.Get();
		const USkeletalMeshComponent* Mesh = Who ? Who->GetMesh() : nullptr;
		const UOperativeAnimInstance* Anim = AnimOf(Who);
		if (!Mesh || !Anim || Dt <= KINDA_SMALL_NUMBER)
		{
			return;
		}
		Stats.Seconds += Dt;
		++Stats.Frames;
		Stats.Hips.Add(LineYaw(Mesh, TEXT("thigh_l"), TEXT("thigh_r")), Dt);
		Stats.Shoulders.Add(LineYaw(Mesh, TEXT("upperarm_l"), TEXT("upperarm_r")), Dt);
		Stats.Capsule.Add(static_cast<float>(Who->GetActorRotation().Yaw), Dt);
		// The formation's orders to him: the slot it gives (a jump = the slot moved > 25 cm in one frame; it walks ~4 cm a frame) and his look.
		FVector Slot;
		if (Squad && Squad->GetFormationGivenSlot(Who, Slot))
		{
			Stats.SlotJumps += Stats.bHasSlot && FVector::Dist2D(Slot, Stats.LastSlot) > 25.f ? 1 : 0;
			Stats.LastSlot = Slot;
			Stats.bHasSlot = true;
		}
		const TOptional<float> Look = Who->GetFormationLookYaw();
		if (Look.IsSet() != Stats.bHasLook && Stats.Frames > 1)
		{
			++Stats.LookChanges;
		}
		if (Look.IsSet())
		{
			const bool bJump = Stats.bHasLook && FMath::Abs(FMath::FindDeltaAngleDegrees(Stats.LastLook, Look.GetValue())) > 15.f;
			Stats.LookJumps += bJump ? 1 : 0;
			if (bJump)
			{
				UE_LOG(LogCodexTactics, Display, TEXT("[FormationJitter] look jump %s: %.0f -> %.0f (facing %.0f, speed %.0f, t %.2f)"), *Who->DisplayName.ToString(), Stats.LastLook,
					Look.GetValue(), Who->GetActorRotation().Yaw, Who->GetVelocity().Size2D(), Time);
			}
			Stats.LastLook = Look.GetValue();
		}
		Stats.bHasLook = Look.IsSet();
		const float Speed = static_cast<float>(Who->GetVelocity().Size2D());
		if (Csv)
		{
			// One row per member and frame (Saved/Logs/FormationJitter-<variant>.csv; offline analysis).
			Csv->Add(FString::Printf(TEXT("%.4f,%s,%.2f,%.2f,%.2f,%.1f,%.1f,%.1f,%.1f,%.1f,%.3f,%.3f"), Time, *Who->DisplayName.ToString(),
				Who->GetActorRotation().Yaw, LineYaw(Mesh, TEXT("thigh_l"), TEXT("thigh_r")), LineYaw(Mesh, TEXT("upperarm_l"), TEXT("upperarm_r")), Speed,
				Anim->Direction, Look.IsSet() ? Look.GetValue() : 9999.f, Stats.bHasSlot ? Stats.LastSlot.X : 0.f, Stats.bHasSlot ? Stats.LastSlot.Y : 0.f,
				Anim->GetShotgunUpperWeight(), Anim->GetShotgunLowerWeight()));
		}
		Stats.SpeedSum += Speed;
		Stats.SpeedSq += static_cast<double>(Speed) * Speed;
		// The layer's move weight is half in at 37.5 cm/s (move_start 15 .. move_full 60).
		const bool bMoving = Speed > 37.5f;
		Stats.MoveFlips += Stats.Frames > 1 && bMoving != Stats.bLastMoving ? 1 : 0;
		Stats.bLastMoving = bMoving;
		if (Speed > 30.f)
		{
			const int32 Sector = FMath::RoundToInt(Anim->Direction / 45.f) & 7;
			Stats.SectorSwitches += Stats.LastSector != INT32_MIN && Sector != Stats.LastSector ? 1 : 0;
			Stats.LastSector = Sector;
		}
		// The weapon layer's clips: the dominant directional clip (not the idle / turn), the turn-in-place clip's restarts.
		const FOperativeClipState* Best = nullptr;
		const FOperativeClipState* Turn = nullptr;
		for (const FOperativeClipState& Clip : Anim->GetWeaponLayerClips())
		{
			if (!Clip.Sequence)
			{
				continue;
			}
			const FString Name = Clip.Sequence->GetName();
			if (Name.Contains(TEXT("turn_90")))
			{
				Turn = &Clip;
				continue;
			}
			if (!Name.Contains(TEXT("idle")) && (!Best || Clip.Weight > Best->Weight))
			{
				Best = &Clip;
			}
		}
		const bool bLayer = Anim->GetShotgunUpperWeight() > 0.05f || Anim->GetShotgunLowerWeight() > 0.05f;
		const FString ClipName = bLayer && Best ? Best->Sequence->GetName() : FString();
		Stats.ClipSwitches += !ClipName.IsEmpty() && !Stats.LastClip.IsEmpty() && ClipName != Stats.LastClip ? 1 : 0;
		if (!ClipName.IsEmpty())
		{
			Stats.LastClip = ClipName;
		}
		if (bLayer && Turn && Turn->Weight > 0.05f)
		{
			++Stats.TurnFrames;
			const FString TurnName = Turn->Sequence->GetName();
			Stats.TurnRestarts += (TurnName != Stats.LastTurnClip || (Stats.LastTurnTime >= 0.f && Turn->Time + 0.05f < Stats.LastTurnTime)) ? 1 : 0;
			Stats.LastTurnClip = TurnName;
			Stats.LastTurnTime = Turn->Time;
		}
		else
		{
			Stats.LastTurnClip.Reset();
			Stats.LastTurnTime = -1.f;
		}
	}

	void SampleFrame(FState& State)
	{
		if (!State.bSampling)
		{
			return;
		}
		const AOperativeCharacter* Leader = State.Leader.Get();
		const UWorld* World = Leader ? Leader->GetWorld() : nullptr;
		if (!World)
		{
			return;
		}
		const float Dt = World->GetDeltaSeconds();
		const USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		State.SampleTime += Dt;
		SampleFollower(State.EngineerStats, Dt, Squad, &State.Csv, State.SampleTime);
		SampleFollower(State.RiflemanStats, Dt, Squad, &State.Csv, State.SampleTime);
		SampleFollower(State.LeaderStats, Dt, Squad, &State.Csv, State.SampleTime);
		const FString Label = Squad ? Squad->GetFormationHudText() : FString();
		State.LabelChanges += !State.LastLabel.IsEmpty() && Label != State.LastLabel ? 1 : 0;
		State.LastLabel = Label;
		State.LabelSeconds += Dt;
		State.LabelTime.FindOrAdd(Label.IsEmpty() ? FString(TEXT("(none)")) : Label) += Dt;
	}

	AStaticMeshActor* SpawnWall(UWorld* World, const FVector& Centre, const FRotator& Rotation, const FVector& Scale)
	{
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		AStaticMeshActor* Block = Cube ? World->SpawnActorDeferred<AStaticMeshActor>(AStaticMeshActor::StaticClass(), FTransform(Rotation, Centre, Scale),
			nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn) : nullptr;
		if (!Block)
		{
			return nullptr;
		}
		Block->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
		Block->GetStaticMeshComponent()->SetStaticMesh(Cube);
		Block->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
		Block->GetStaticMeshComponent()->SetCanEverAffectNavigation(true);
		Block->FinishSpawning(FTransform(Rotation, Centre, Scale));
		return Block;
	}

	/** Every member back at the start (the leader on it, the followers behind him), standing, stopped. */
	void ResetSquad(UWorld* World, FState& State)
	{
		AOperativeCharacter* Leader = State.Leader.Get();
		const FVector Right = FVector::CrossProduct(FVector::UpVector, State.Forward);
		auto Place = [World, &State](AOperativeCharacter* Who, const FVector& Spot)
		{
			if (Who)
			{
				Who->StopOperative();
				Who->SetStance(EOperativeStance::Standing);
				Who->TeleportTo(SmokeUtils::FreeSpot(World, Spot, Who), State.StartRotation, false, true);
				Who->SetIdleFacingYaw(State.StartRotation.Yaw);
			}
		};
		Place(Leader, State.Start);
		float Side = -150.f;
		for (const TWeakObjectPtr<AOperativeCharacter>& Member : { State.Commander, State.Engineer, State.Rifleman })
		{
			if (Member.Get() && Member.Get() != Leader)
			{
				Place(Member.Get(), State.Start - State.Forward * 250.f + Right * Side);
				Side = -Side;
			}
		}
	}

	bool Step(UWorld* World, FState& State, float Dt)
	{
		State.Time += Dt;
		State.StageTime += Dt;
		if (State.Time > 330.f)
		{
			Check(State, false, FString::Printf(TEXT("finished in time (stage %d, variant %d)"), State.Stage, State.Variant));
			return Finish(State, false);
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		const TArray<AOperativeCharacter*> Members = Squad ? Squad->GetMembers() : TArray<AOperativeCharacter*>();
		if (!Squad || Members.Num() < 3)
		{
			if (State.Time < 10.f)
			{
				return true;
			}
			Check(State, false, FString::Printf(TEXT("a squad of three (%d)"), Members.Num()));
			return Finish(State, false);
		}
		for (AOperativeCharacter* Member : Members)
		{
			Member->ColdLevel = 0.f;
		}
		AOperativeCharacter* Leader = State.Leader.Get();
		AOperativeCharacter* Engineer = State.Engineer.Get();
		switch (State.Stage)
		{
		case 0: // the squad: the Commander leads (M16), the Engineer (D-1) and the third member follow
		{
			if (State.StageTime < NavWarmupSeconds)
			{
				return true;
			}
			for (AOperativeCharacter* Member : Members)
			{
				Member->HealthComponent->SetMaxHealth(100000.f, true);
				Member->bTacticalCeaseFire = true;
				if (Member->GetMesh())
				{
					// Headless meshes are never rendered: evaluate their poses every frame (the bones are measured).
					Member->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
				}
				if (Member->SquadRole == EOperativeRole::Commander)
				{
					State.Leader = Member;
					State.Commander = Member;
				}
				else if (Member->SquadRole == EOperativeRole::Engineer)
				{
					State.Engineer = Member;
				}
				else
				{
					State.Rifleman = Member;
				}
			}
			Leader = State.Leader.Get();
			Engineer = State.Engineer.Get();
			if (!Leader || !Engineer || !State.Rifleman.IsValid())
			{
				Check(State, false, TEXT("the Commander, the Engineer and a third member"));
				return Finish(State, false);
			}
			if (Squad->IsSoloMode())
			{
				Squad->ExitSoloMode(false, 0.f);
			}
			Squad->SetLeader(Leader);
			Check(State, Squad->IsFormationActive() && Squad->GetFormationLeader() == Leader, TEXT("formation mode, the Commander leads"));
			Check(State, Engineer->CurrentWeapon && Engineer->CurrentWeapon->WeaponId == TEXT("shotgun"), TEXT("the Engineer follows with the D-1"));
			// A clear stretch of the map for the route (any map: L_MovementTest, the Depot).
			TArray<const AActor*> Ignore;
			for (AOperativeCharacter* Member : Members)
			{
				Ignore.Add(Member);
			}
			// The route's size: full on an open map (L_MovementTest), smaller where the map has no such open stretch (the Depot's yard).
			float Scale = 1.f;
			SmokeUtils::FClearArena Arena;
			for (const float Try : { 1.f, 0.6f, 0.4f })
			{
				Arena = SmokeUtils::FindClearArena(World, Leader->GetActorLocation(), Leader->GetActorForwardVector().GetSafeNormal2D(),
					FBox2D(FVector2D(-600.f, -1500.f) * Try, FVector2D(2800.f, 1500.f) * Try), Ignore);
				Scale = Try;
				if (Arena.bFound)
				{
					break;
				}
			}
			State.Start = Arena.bFound ? Arena.Origin + FVector(0.f, 0.f, Leader->GetSimpleCollisionHalfHeight() + 5.f) : Leader->GetActorLocation();
			State.Forward = Arena.bFound ? Arena.Forward.GetSafeNormal2D() : Leader->GetActorForwardVector().GetSafeNormal2D();
			State.StartRotation = FRotator(0.f, State.Forward.Rotation().Yaw, 0.f);
			UE_LOG(LogCodexTactics, Display, TEXT("[FormationJitter] arena %s at %s (route scale %.1f)"), Arena.bFound ? TEXT("found") : TEXT("NOT found (the leader's spot)"),
				*State.Start.ToCompactString(), Scale);
			// A route with turns: ahead, a right turn, ahead, a left turn, a diagonal back - the formation slots swing round each turn.
			const FVector Right = FVector::CrossProduct(FVector::UpVector, State.Forward);
			const FVector Offsets[5] = { State.Forward * 1400.f * Scale, (State.Forward * 1400.f + Right * 1100.f) * Scale, (State.Forward * 2400.f + Right * 1100.f) * Scale,
				(State.Forward * 2400.f - Right * 300.f) * Scale, (State.Forward * 1200.f - Right * 1000.f) * Scale };
			// Walls along the route (the video: the Commander leading along walls, the formation folding to "Column (wall)"): 3 m high, 40 cm thick
			// - one 2.2 m right of the first leg (forward 100 .. 1300), one 2.2 m left of the third leg (right 1100: forward 1500 .. 2200); scaled
			// along the route, the 2.2 m beside it kept.
			const float GroundZ = static_cast<float>(State.Start.Z) - Leader->GetSimpleCollisionHalfHeight();
			const FVector WallA = State.Start + State.Forward * 700.f * Scale + Right * 220.f;
			const FVector WallB = State.Start + State.Forward * 1850.f * Scale + Right * (1100.f * Scale - 220.f);
			State.Walls.Add(SpawnWall(World, FVector(WallA.X, WallA.Y, GroundZ + 150.f), State.Forward.Rotation(), FVector(12.f * Scale, 0.4f, 3.f)));
			State.Walls.Add(SpawnWall(World, FVector(WallB.X, WallB.Y, GroundZ + 150.f), State.Forward.Rotation(), FVector(7.f * Scale, 0.4f, 3.f)));
			FVector From = State.Start;
			for (const FVector& Offset : Offsets)
			{
				From = SmokeUtils::ClearPoint(World, From, State.Start + Offset);
				State.Route.Add(From);
			}
			ResetSquad(World, State);
			NextStage(State, 1);
			return true;
		}
		case 1: // a variant: set the Engineer's weapon / the layer, settle
		{
			if (State.StageTime < 0.1f)
			{
				return true;
			}
			if (State.StageTime < 0.2f)
			{
				SetLayerCVar(State.Variant == 1 ? 0 : 1);
				SetLookFilterCVar(State.Variant == 3 ? 0 : 1); // D: the all-M16 squad without the look filter (the video's state)
				if (State.Variant == 2)
				{
					if (UWeaponDataAsset* M16 = LoadObject<UWeaponDataAsset>(nullptr, TEXT("/Game/Data/Weapons/DA_Weapon_m16.DA_Weapon_m16")))
					{
						Engineer->AddArsenalWeapon(M16);
					}
					Engineer->SwitchToWeaponById(TEXT("m16"));
				}
				if (State.Variant == 4)
				{
					// E: the D-1 Engineer LEADS the route (no formation corrections): his own gait's sway, the baseline of his follower numbers.
					Engineer->SwitchToWeaponById(TEXT("shotgun"));
					State.Leader = Engineer;
					Squad->SetLeader(Engineer);
					Leader = Engineer;
				}
				ResetSquad(World, State);
				return true;
			}
			if (State.StageTime < 3.f) // the navmesh rebuilt round the spawned walls, the squad settled
			{
				return true;
			}
			if (State.Variant == 2)
			{
				Check(State, Engineer->CurrentWeapon && Engineer->CurrentWeapon->WeaponId == TEXT("m16"), TEXT("C: the Engineer holds an M16 (the baseline)"));
			}
			if (State.Variant == 4)
			{
				Check(State, Squad->GetFormationLeader() == Engineer && Engineer->CurrentWeapon && Engineer->CurrentWeapon->WeaponId == TEXT("shotgun"),
					TEXT("E: the Engineer leads with the D-1"));
			}
			State.EngineerStats.Reset(Engineer);
			State.RiflemanStats.Reset(State.Rifleman.Get());
			// (E: the Commander follows - his numbers stand in the leader's column.)
			State.LeaderStats.Reset(State.Variant == 4 ? State.Commander.Get() : Leader);
			State.LastLabel.Reset();
			State.LabelChanges = 0;
			State.LabelSeconds = 0.f;
			State.LabelTime.Reset();
			State.Csv.Reset();
			State.Csv.Add(TEXT("t,who,capsule,hips,shoulders,speed,direction,look,slot_x,slot_y,upper_w,lower_w"));
			State.SampleTime = 0.f;
			State.Leg = 0;
			State.LegTime = 0.f;
			Leader->OrderMoveTo(State.Route[0], false);
			NextStage(State, 2);
			return true;
		}
		case 2: // the leader walks the route; the followers are measured from 1 s in
		{
			State.LegTime += Dt;
			State.bSampling = State.StageTime > 1.f;
			const float ToWaypoint = static_cast<float>(FVector::Dist2D(Leader->GetActorLocation(), State.Route[State.Leg]));
			if (ToWaypoint < 120.f || (State.LegTime > 1.5f && Leader->GetVelocity().Size2D() < 5.f))
			{
				++State.Leg;
				State.LegTime = 0.f;
				if (State.Leg >= State.Route.Num())
				{
					State.bSampling = false;
					State.bRouteDone[State.Variant] = ToWaypoint < 300.f;
					State.Results[State.Variant][0] = State.EngineerStats;
					State.Results[State.Variant][1] = State.RiflemanStats;
					State.Results[State.Variant][2] = State.LeaderStats;
					FFileHelper::SaveStringArrayToFile(State.Csv, *(FPaths::ProjectSavedDir() / FString::Printf(TEXT("Logs/FormationJitter-%c.csv"), TEXT('A') + State.Variant)));
					static const TCHAR* const Labels[5] = { TEXT("A (D-1, layer on)"), TEXT("B (D-1, layer off)"), TEXT("C (all M16)"), TEXT("D (all M16, look filter off)"),
						TEXT("E (the D-1 Engineer leads)") };
					UE_LOG(LogCodexTactics, Display, TEXT("[FormationJitter] %s"), *State.EngineerStats.Line(Labels[State.Variant]));
					UE_LOG(LogCodexTactics, Display, TEXT("[FormationJitter] %s"), *State.RiflemanStats.Line(Labels[State.Variant]));
					UE_LOG(LogCodexTactics, Display, TEXT("[FormationJitter] %s"), *State.LeaderStats.Line(Labels[State.Variant]));
					FString Shares;
					for (const TPair<FString, float>& Each : State.LabelTime)
					{
						Shares += FString::Printf(TEXT(" '%s' %.1f s;"), *Each.Key, Each.Value);
					}
					State.LabelChangesPerSecond[State.Variant] = State.LabelSeconds > 0.f ? State.LabelChanges / State.LabelSeconds : 0.f;
					State.LabelSummary[State.Variant] = Shares;
					UE_LOG(LogCodexTactics, Display, TEXT("[FormationJitter] %s formation label: %d changes over %.1f s (%.2f/s):%s"), Labels[State.Variant], State.LabelChanges,
						State.LabelSeconds, State.LabelChangesPerSecond[State.Variant], *Shares);
					Check(State, State.bRouteDone[State.Variant], FString::Printf(TEXT("%s: the leader walks the whole route (%.0f cm from its end)"), Labels[State.Variant], ToWaypoint));
					NextStage(State, 3);
					return true;
				}
				Leader->OrderMoveTo(State.Route[State.Leg], false);
			}
			if (State.StageTime > 70.f)
			{
				Check(State, false, FString::Printf(TEXT("variant %d: the route in time (leg %d)"), State.Variant, State.Leg));
				return Finish(State, false);
			}
			return true;
		}
		case 3: // let the followers arrive, then the next variant
		{
			if (State.StageTime < 2.f)
			{
				return true;
			}
			if (++State.Variant < 5)
			{
				NextStage(State, 1);
				return true;
			}
			const FFollowerStats& D1 = State.Results[0][0];
			const FFollowerStats& Base = State.Results[2][0];
			const FFollowerStats& Off = State.Results[1][0];
			auto Limit = [](float Baseline) { return Baseline * JitterFactor + JitterMarginDegS; };
			// What the formation ADDS to each gait (the follower's wobble minus the same gait leading the route): the D-1's (A - E) no more than
			// the M16's (C follower - C leader). The pack's forward walk sways its hips / chest more than the ABP's rifle walk by itself.
			const FFollowerStats& D1Lead = State.Results[4][0];
			const FFollowerStats& M16Lead = State.Results[2][2];
			const float D1HipsAdded = D1.Hips.WobbleRate() - D1Lead.Hips.WobbleRate();
			const float M16HipsAdded = Base.Hips.WobbleRate() - M16Lead.Hips.WobbleRate();
			const float D1ShouldersAdded = D1.Shoulders.WobbleRate() - D1Lead.Shoulders.WobbleRate();
			const float M16ShouldersAdded = Base.Shoulders.WobbleRate() - M16Lead.Shoulders.WobbleRate();
			Check(State, D1HipsAdded <= Limit(M16HipsAdded), FString::Printf(TEXT("following adds no more back-and-forth turning (wobble) to the D-1 man's hips than to an M16 man's in his slot (+%.1f deg/s: follower %.1f - leading %.1f; M16 +%.1f: %.1f - %.1f; layer off %.1f; limit +%.1f)"),
				D1HipsAdded, D1.Hips.WobbleRate(), D1Lead.Hips.WobbleRate(), M16HipsAdded, Base.Hips.WobbleRate(), M16Lead.Hips.WobbleRate(), Off.Hips.WobbleRate(), Limit(M16HipsAdded)));
			Check(State, D1ShouldersAdded <= Limit(M16ShouldersAdded), FString::Printf(TEXT("... nor to his shoulders (+%.1f deg/s: %.1f - %.1f; M16 +%.1f: %.1f - %.1f; layer off %.1f; limit +%.1f)"),
				D1ShouldersAdded, D1.Shoulders.WobbleRate(), D1Lead.Shoulders.WobbleRate(), M16ShouldersAdded, Base.Shoulders.WobbleRate(), M16Lead.Shoulders.WobbleRate(),
				Off.Shoulders.WobbleRate(), Limit(M16ShouldersAdded)));
			Check(State, D1.PerSecond(D1.ClipSwitches) <= MaxClipSwitchesPerSecond, FString::Printf(TEXT("... and his directional clip changes at most %.0f times a second (%.2f/s, %d over %.1f s)"),
				MaxClipSwitchesPerSecond, D1.PerSecond(D1.ClipSwitches), D1.ClipSwitches, D1.Seconds));
			// The whole group (the designer: "it seems to affect the whole group's movement"): the other follower and the leader with the D-1 man in
			// the squad are no worse than in the all-M16 squad, and the formation's label does not flicker more.
			const FFollowerStats& OtherD1 = State.Results[0][1];
			const FFollowerStats& OtherBase = State.Results[2][1];
			const FFollowerStats& LeadD1 = State.Results[0][2];
			const FFollowerStats& LeadBase = State.Results[2][2];
			Check(State, OtherD1.Hips.WobbleRate() <= Limit(OtherBase.Hips.WobbleRate()) && OtherD1.Shoulders.WobbleRate() <= Limit(OtherBase.Shoulders.WobbleRate()),
				FString::Printf(TEXT("the other follower is no worse with the D-1 man in the squad (hips %.1f / shoulders %.1f deg/s; all-M16 %.1f / %.1f)"),
					OtherD1.Hips.WobbleRate(), OtherD1.Shoulders.WobbleRate(), OtherBase.Hips.WobbleRate(), OtherBase.Shoulders.WobbleRate()));
			Check(State, LeadD1.Hips.WobbleRate() <= Limit(LeadBase.Hips.WobbleRate()) && LeadD1.Shoulders.WobbleRate() <= Limit(LeadBase.Shoulders.WobbleRate()),
				FString::Printf(TEXT("... nor the leader (hips %.1f / shoulders %.1f deg/s; all-M16 %.1f / %.1f)"), LeadD1.Hips.WobbleRate(), LeadD1.Shoulders.WobbleRate(),
					LeadBase.Hips.WobbleRate(), LeadBase.Shoulders.WobbleRate()));
			// The group's own trembling (every member, the M16 men too): the formation look's one-frame flips filtered (C) against unfiltered (D).
			const float FilteredSpin = State.Results[2][0].Capsule.WobbleRate() + State.Results[2][1].Capsule.WobbleRate() + State.Results[2][2].Capsule.WobbleRate();
			const float RawSpin = State.Results[3][0].Capsule.WobbleRate() + State.Results[3][1].Capsule.WobbleRate() + State.Results[3][2].Capsule.WobbleRate();
			Check(State, FilteredSpin <= RawSpin * 0.85f, FString::Printf(TEXT("the all-M16 squad's bodies turn back and forth less with the look filter (capsules %.1f deg/s summed; without it %.1f)"),
				FilteredSpin, RawSpin));
			Check(State, State.LabelChangesPerSecond[0] <= State.LabelChangesPerSecond[2] * 1.5f + 0.1f,
				FString::Printf(TEXT("the formation's label changes no more often with the D-1 man (%.2f/s; all-M16 %.2f/s)"), State.LabelChangesPerSecond[0],
					State.LabelChangesPerSecond[2]));
			for (const TWeakObjectPtr<AActor>& Wall : State.Walls)
			{
				if (AActor* Actor = Wall.Get())
				{
					Actor->Destroy();
				}
			}
			return Finish(State, true);
		}
		default:
			return Finish(State, false);
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		FTimerHandle PlaceHandle;
		TWeakObjectPtr<UWorld> PlaceWorld(World);
		World->GetTimerManager().SetTimer(PlaceHandle, FTimerDelegate::CreateLambda([PlaceWorld]()
		{
			SmokeUtils::PlaceSquadAtTestStart(PlaceWorld.Get());
		}), 0.5f, false);
		TSharedRef<FState> State = MakeShared<FState>();
		State->FrameHandle = FCoreDelegates::OnEndFrame.AddLambda([State]() { SampleFrame(*State); });
		TWeakObjectPtr<UWorld> WeakWorld(World);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float Dt)
		{
			UWorld* Current = WeakWorld.Get();
			return Current ? Step(Current, *State, Dt) : false;
		}));
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.FormationJitterSmoke"),
		TEXT("Dev check: the D-1 Engineer following the Commander in the formation does not twitch / spin (yaw jitter vs an M16 follower, layer on / off); logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
