// Dev-only headless check: arriving at a wall edge, the corner hold must go enter -> [idle_to_fire] -> fire_idle with no
// tuck back (fire_to_idle / the plain cover idle / the look-around pose) in between (user PIE 2026-10-07 after ba59bb4;
// UE-only, no Godot reference):
//   Scripts/smoke.ps1 -Command CodexTactics.CornerEntrySmoke -Log Smoke-CornerEntry.log
// A 6 m x 3 m wall spawned at runtime (nothing saved); the leader enters its right-hand corner slot by every path - walking,
// running, crouched, shimmying along the wall to the edge, with and without a known threat behind the wall, and the
// stand <-> crouch switch at the edge. Every frame the playing FullBody montages (clip + weight) and the cover flags are
// sampled; every change is logged ("trace"), and a tuck-back clip after the first fire-stance clip fails the path.

#include "CoreMinimal.h"
#include "Animation/AnimMontage.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeAnimInstance.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "Tactics/CoverTraceRules.h"

#if !UE_BUILD_SHIPPING

namespace CornerEntrySmoke
{
	enum class EEntry : uint8 { Walk, Run, Shimmy, StanceSwitch };

	struct FPath
	{
		const TCHAR* Name;
		EEntry Entry;
		bool bCrouched;
		bool bThreat;
		/** The cover slot is picked this far in from the wall's right-hand end, cm (the corner stand-off is 57). */
		float SlotFromEdgeCm = 50.f;
	};

	const FPath Paths[] = {
		{ TEXT("walk in, standing, no threat"), EEntry::Walk, false, false },
		{ TEXT("run in, standing, no threat"), EEntry::Run, false, false },
		{ TEXT("walk in, standing, threat behind the wall"), EEntry::Walk, false, true },
		{ TEXT("run in, standing, threat behind the wall"), EEntry::Run, false, true },
		{ TEXT("walk in, crouched, no threat"), EEntry::Walk, true, false },
		{ TEXT("walk in, crouched, threat behind the wall"), EEntry::Walk, true, true },
		{ TEXT("shimmy to the edge, standing"), EEntry::Shimmy, false, false },
		{ TEXT("shimmy to the edge, crouched"), EEntry::Shimmy, true, false },
		{ TEXT("stand <-> crouch at the edge"), EEntry::StanceSwitch, false, false },
		{ TEXT("stand <-> crouch at the edge, firing"), EEntry::StanceSwitch, false, true },
		// The click is not exactly at the edge: the slot 90 cm / 170 cm in (he walks the rest to the corner stand-off).
		{ TEXT("walk in, slot 90 cm from the edge"), EEntry::Walk, false, false, 90.f },
		{ TEXT("run in, slot 90 cm from the edge, firing"), EEntry::Run, false, true, 90.f },
		{ TEXT("run in, slot 170 cm from the edge"), EEntry::Run, false, false, 170.f },
		{ TEXT("walk in crouched, slot 90 cm from the edge, firing"), EEntry::Walk, true, true, 90.f },
	};

	struct FState
	{
		int32 Stage = 0;
		int32 PathIndex = 0;
		int32 Step = 0;
		float StepTime = 0.f;
		float Time = 0.f;
		int32 Failures = 0;
		FVector P = FVector::ZeroVector;
		FVector F = FVector::ForwardVector;
		FVector R = FVector::RightVector;
		float GroundZ = 0.f;
		TWeakObjectPtr<AOperativeCharacter> Op;
		TWeakObjectPtr<AEnemyCharacter> Threat;
		// Trace of the current path.
		bool bTracing = false;
		float TraceTime = 0.f;
		float SettledTime = -1.f;
		FString LastSignature;
		bool bSawFireClip = false;
		TArray<FString> TuckBacks;
		TSet<FString> TuckNames;
		float MinSlotAfterFire = 1.f;
		int32 BreaksAtStart = 0;
		int32 Shots = 0;
		float ProbeTimer = 0.f;
		TArray<FString> Sequence;
		FString PathTrace;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("CornerEntrySmoke"));
		return false;
	}

	AStaticMeshActor* SpawnBlock(UWorld* World, const FVector& Centre, const FRotator& Rotation, const FVector& Scale)
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

	/** A clip the body tucks back into (out of the fire stance): the exit, the plain cover idle, the look-around pose. */
	bool IsTuckBack(const FString& Clip)
	{
		if (Clip.Contains(TEXT("fire_to_")))
		{
			return true;
		}
		if (Clip.Contains(TEXT("idle_L_to_cvr_")) || Clip.Contains(TEXT("idle_R_to_cvr_")))
		{
			return true; // the stance switch at the wall goes idle -> idle (the rifle down, back to the wall)
		}
		if (Clip.Contains(TEXT("look_at")))
		{
			return true;
		}
		// anim_M4_cvr_std_idle_L / _R, anim_M4_cvr_crch_idle_L / _R (the loops, not idle_L_to_fire / stance switches).
		return (Clip.EndsWith(TEXT("cvr_std_idle_L")) || Clip.EndsWith(TEXT("cvr_std_idle_R")) || Clip.EndsWith(TEXT("cvr_crch_idle_L"))
			|| Clip.EndsWith(TEXT("cvr_crch_idle_R")));
	}

	bool IsFireClip(const FString& Clip)
	{
		return Clip.Contains(TEXT("_to_fire")) || Clip.Contains(TEXT("fire_idle")) || Clip.Contains(TEXT("_fire_L")) || Clip.Contains(TEXT("_fire_R"));
	}

	/** Every FullBody montage with weight > 0.02, heaviest first: "clip w0.83 + clip w0.17". */
	FString PlayingClips(const UOperativeAnimInstance& Anim, FString& OutDominant)
	{
		TArray<TPair<float, FString>> Playing;
		for (const FAnimMontageInstance* Instance : Anim.MontageInstances)
		{
			if (!Instance || !Instance->Montage || Instance->GetWeight() <= 0.02f)
			{
				continue;
			}
			FString Name = TEXT("?");
			for (const FSlotAnimationTrack& Track : Instance->Montage->SlotAnimTracks)
			{
				if (Track.AnimTrack.AnimSegments.Num() > 0 && Track.AnimTrack.AnimSegments[0].GetAnimReference())
				{
					Name = Track.AnimTrack.AnimSegments[0].GetAnimReference()->GetName();
					break;
				}
			}
			Playing.Emplace(Instance->GetWeight(), Name);
		}
		Playing.Sort([](const TPair<float, FString>& A, const TPair<float, FString>& B) { return A.Key > B.Key; });
		OutDominant = Playing.Num() > 0 ? Playing[0].Value : FString(TEXT("graph"));
		TArray<FString> Parts;
		for (const TPair<float, FString>& Entry : Playing)
		{
			Parts.Add(FString::Printf(TEXT("%s w%.2f"), *Entry.Value.Replace(TEXT("anim_M4_"), TEXT("")), Entry.Key));
		}
		return Parts.Num() > 0 ? FString::Join(Parts, TEXT(" + ")) : FString(TEXT("graph (no montage)"));
	}

	/** Per-frame sample of the current path: logs every change, records the dominant clip sequence and tuck-backs. */
	void Sample(FState& State, AOperativeCharacter& Op, float DeltaSeconds)
	{
		const UOperativeAnimInstance* Anim = Op.GetMesh() ? Cast<UOperativeAnimInstance>(Op.GetMesh()->GetAnimInstance()) : nullptr;
		if (!Anim)
		{
			return;
		}
		State.TraceTime += DeltaSeconds;
		FString Dominant;
		const FString Clips = PlayingClips(*Anim, Dominant);
		const FString Flags = FString::Printf(TEXT("cover %d corner %d shimmy %d aim %d fireReady %d firePose %d %s facing %s"),
			Op.bInCover ? 1 : 0, Op.bAtCoverCorner ? 1 : 0, Op.bShimmying ? 1 : 0, Op.IsCornerAimActive() ? 1 : 0,
			Anim->bCoverFireReady ? 1 : 0, Anim->IsCoverInFirePose() ? 1 : 0, Op.GetStance() == EOperativeStance::Crouching ? TEXT("crch") : TEXT("std"),
			Op.CoverFacing == ECoverFacing::Right ? TEXT("R") : TEXT("L"));
		// The signature: the clip names (weights rounded to 0.25) + the flags - a log line on every change.
		FString Names;
		for (const FAnimMontageInstance* Instance : Anim->MontageInstances)
		{
			if (Instance && Instance->Montage && Instance->GetWeight() > 0.02f && Instance->Montage->SlotAnimTracks.Num() > 0
				&& Instance->Montage->SlotAnimTracks[0].AnimTrack.AnimSegments.Num() > 0)
			{
				const UAnimSequenceBase* Ref = Instance->Montage->SlotAnimTracks[0].AnimTrack.AnimSegments[0].GetAnimReference();
				Names += FString::Printf(TEXT("%s:%d "), Ref ? *Ref->GetName() : TEXT("?"), FMath::RoundToInt(Instance->GetWeight() * 4.f));
			}
		}
		// The FullBody slot's total montage weight: below 1 the graph's plain rifle idle (facing away from the wall) shows.
		const float SlotWeight = Anim->GetSlotMontageGlobalWeight(FName(TEXT("FullBody")));
		const FString Signature = Names + Flags + FString::Printf(TEXT(" slot%d"), FMath::RoundToInt(SlotWeight * 10.f));
		if (State.bSawFireClip && Op.bInCover && SlotWeight < 0.9f && !Op.bIsReloading)
		{
			State.MinSlotAfterFire = FMath::Min(State.MinSlotAfterFire, SlotWeight);
		}
		if (Signature != State.LastSignature)
		{
			State.LastSignature = Signature;
			const FString Line = FString::Printf(TEXT("%5.2fs  %s  | %s slot %.2f"), State.TraceTime, *Clips, *Flags, SlotWeight);
			UE_LOG(LogCodexTactics, Display, TEXT("Trace: %s"), *Line);
		}
		if (State.Sequence.Num() == 0 || State.Sequence.Last() != Dominant)
		{
			State.Sequence.Add(Dominant);
		}
		// Tuck-back: after the first fire-stance clip took over, any tuck-back clip with a real weight (not reloading).
		for (const FAnimMontageInstance* Instance : Anim->MontageInstances)
		{
			if (!Instance || !Instance->Montage || Instance->Montage->SlotAnimTracks.Num() == 0
				|| Instance->Montage->SlotAnimTracks[0].AnimTrack.AnimSegments.Num() == 0)
			{
				continue;
			}
			const UAnimSequenceBase* Ref = Instance->Montage->SlotAnimTracks[0].AnimTrack.AnimSegments[0].GetAnimReference();
			const FString Name = Ref ? Ref->GetName() : FString();
			if (IsFireClip(Name) && Instance->GetWeight() > 0.5f)
			{
				State.bSawFireClip = true;
			}
			if (State.bSawFireClip && IsTuckBack(Name) && Instance->GetWeight() > 0.1f && !Op.bIsReloading && Instance->IsActive()
				&& !State.TuckNames.Contains(Name))
			{
				State.TuckNames.Add(Name);
				State.TuckBacks.Add(FString::Printf(TEXT("%s at %.2f s (w%.2f)"), *Name, State.TraceTime, Instance->GetWeight()));
			}
		}
	}

	int32 AimBreaks(const AOperativeCharacter* Op)
	{
		return Op ? Op->GetCornerAimBreakCount(ECornerAimDecision::ReturnNoTargets) + Op->GetCornerAimBreakCount(ECornerAimDecision::DuckForSafety) : 0;
	}

	void BeginTrace(FState& State, const TCHAR* What)
	{
		State.BreaksAtStart = AimBreaks(State.Op.Get());
		State.bTracing = true;
		State.TraceTime = 0.f;
		State.SettledTime = -1.f;
		State.LastSignature.Reset();
		State.bSawFireClip = false;
		State.TuckBacks.Reset();
		State.TuckNames.Reset();
		State.MinSlotAfterFire = 1.f;
		State.Shots = 0;
		State.Sequence.Reset();
		UE_LOG(LogCodexTactics, Display, TEXT("Trace: ---- %s ----"), What);
	}

	void EndTrace(FState& State, const FString& What, bool bWantFireIdle, AOperativeCharacter& Op)
	{
		State.bTracing = false;
		FString Dominant = State.Sequence.Num() > 0 ? State.Sequence.Last() : FString();
		TArray<FString> Short;
		for (const FString& Clip : State.Sequence)
		{
			Short.Add(Clip.Replace(TEXT("anim_M4_"), TEXT("")));
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke: %s - sequence %s"), *What, *FString::Join(Short, TEXT(" -> ")));
		Check(State, AimBreaks(&Op) == State.BreaksAtStart, FString::Printf(TEXT("%s: the corner aim never breaks on the way in (%d breaks)"),
			*What, AimBreaks(&Op) - State.BreaksAtStart));
		Check(State, State.MinSlotAfterFire >= 0.9f, FString::Printf(TEXT("%s: the FullBody slot stays covered in the fire stance (min %.2f)"), *What, State.MinSlotAfterFire));
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke: %s - %d shots during the trace"), *What, State.Shots);
		Check(State, State.TuckBacks.Num() == 0, FString::Printf(TEXT("%s: no tuck back after the fire stance (%s)"), *What,
			State.TuckBacks.Num() > 0 ? *FString::Join(State.TuckBacks, TEXT(", ")) : TEXT("none")));
		int32 StepOuts = 0;
		for (const FString& Clip : State.Sequence)
		{
			StepOuts += Clip.Contains(TEXT("_to_fire")) ? 1 : 0;
		}
		Check(State, StepOuts <= 1, FString::Printf(TEXT("%s: steps out into the fire stance once (%d idle -> fire transitions)"), *What, StepOuts));
		if (bWantFireIdle)
		{
			Check(State, Dominant.Contains(TEXT("fire_idle")) && Op.IsCornerAimActive(),
				FString::Printf(TEXT("%s: ends in the corner fire stance (%s, aim %d)"), *What, *Dominant, Op.IsCornerAimActive() ? 1 : 0));
		}
	}

	FVector Ground(const FState& State, const FVector& Where)
	{
		return FVector(Where.X, Where.Y, State.GroundZ + 95.f);
	}

	bool FindCorner(UWorld* World, const FState& State, FCoverSlot& Out, float FromEdgeCm = 50.f)
	{
		return CoverTraceRules::FindCoverSlotAt(World, State.P + State.F * 380.f - State.R * (300.f - FromEdgeCm) + FVector(0.f, 0.f, 90.f), State.F, Out);
	}

	bool Tick(TWeakObjectPtr<UWorld> WeakWorld, FState& State, float DeltaSeconds)
	{
		DeltaSeconds = FMath::Min(DeltaSeconds, 0.1f);
		State.Time += DeltaSeconds;
		State.StepTime += DeltaSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time > 400.f)
		{
			return World ? Finish(State) : false;
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Op = State.Op.Get();
		if (Squad)
		{
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Member->ColdLevel = 0.f;
				// Threat paths fire at the (frozen) hound behind the wall: the shots play from the corner stance too.
				Member->bTacticalCeaseFire = !(Member == Op && State.Stage == 1 && State.PathIndex < UE_ARRAY_COUNT(Paths) && Paths[State.PathIndex].bThreat);
			}
		}
		if (Op && State.bTracing)
		{
			Sample(State, *Op, DeltaSeconds);
			State.ProbeTimer -= DeltaSeconds;
			if (AEnemyCharacter* Threat = State.Threat.Get(); Threat && Op->bInCover && State.ProbeTimer <= 0.f)
			{
				State.ProbeTimer = 1.f;
				UE_LOG(LogCodexTactics, Display, TEXT("Trace: probe - threat corner target %d, aim residual %.1f deg, hold spot %d, snap pending?, clip %d"),
					Op->IsCornerShotTarget(Threat->GetActorLocation()) ? 1 : 0, Op->GetShotAimResidualDeg(Threat->GetActorLocation()),
					Op->IsCornerHoldSpot() ? 1 : 0, Op->CurrentClip);
			}
		}
		if (State.Stage == 0)
		{
			if (State.StepTime < 3.f)
			{
				return true;
			}
			UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
			Flow->TriggerCombatZone();
			Flow->FinishCutscene();
			Flow->FinishPreparation();
			Op = Squad->GetLeader();
			State.Op = Op;
			{
				FState* Shared = &State;
				Op->OnWeaponFiredNative.AddLambda([Shared](AOperativeCharacter* Shooter, AActor* Target, bool)
				{
					if (Shared->bTracing && Shooter)
					{
						++Shared->Shots;
						UE_LOG(LogCodexTactics, Display, TEXT("Trace: %5.2fs  SHOT at %s (in cover %d, aim %d)"), Shared->TraceTime,
							Target ? *Target->GetName() : TEXT("-"), Shooter->bInCover ? 1 : 0, Shooter->IsCornerAimActive() ? 1 : 0);
					}
				});
			}
			Op->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			State.F = Op->GetActorForwardVector().GetSafeNormal2D();
			State.R = FVector::CrossProduct(FVector::UpVector, State.F);
			State.P = Op->GetActorLocation();
			State.GroundZ = State.P.Z - Op->GetSimpleCollisionHalfHeight();
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				It->Destroy();
			}
			// The wave stays alive with a frozen hound far away.
			if (AEnemyCharacter* Parked = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::FrostHound,
				FVector(State.P.X, State.P.Y, State.GroundZ + 100.f) - State.F * 6000.f, State.F.Rotation()))
			{
				Parked->CustomTimeDilation = 0.f;
				Parked->SetActorTickEnabled(false);
			}
			int32 Index = 0;
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Member->HealthComponent->SetMaxHealth(100000.f);
				if (Member != Op)
				{
					Member->StopOperative();
					Member->TeleportTo(State.P - State.F * 1500.f + State.R * (Index++ % 2 == 0 ? 400.f : -400.f), State.F.Rotation(), false, true);
				}
			}
			const FVector WallCentre = State.P + State.F * 400.f;
			SpawnBlock(World, FVector(WallCentre.X, WallCentre.Y, State.GroundZ + 150.f), State.F.Rotation(), FVector(0.4f, 6.f, 3.f));
			State.Stage = 1;
			State.Step = 0;
			State.StepTime = 0.f;
			return true;
		}
		if (State.PathIndex >= UE_ARRAY_COUNT(Paths))
		{
			return Finish(State);
		}
		const FPath& Path = Paths[State.PathIndex];
		auto NextStep = [&State]() { ++State.Step; State.StepTime = 0.f; };
		switch (State.Step)
		{
		case 0:
		{
			// Reset: out of cover, back to the start, the stance, the threat.
			if (Op->bInCover)
			{
				Op->LeaveCover(TEXT("smoke reset"));
			}
			Op->StopOperative();
			if (AEnemyCharacter* Old = State.Threat.Get())
			{
				Old->Destroy();
			}
			State.Threat.Reset();
			const float Back = Path.Entry == EEntry::Run ? 650.f : 330.f;
			const FVector Start = Path.Entry == EEntry::Shimmy ? State.P + State.F * 250.f + State.R * 150.f
				: State.P + State.F * (380.f - Back) - State.R * 250.f;
			Op->TeleportTo(Ground(State, Start), State.F.Rotation(), false, true);
			// Crouched: he walks in crouched (the high wall stands him up on entry - the entry path traced); the shimmy path
			// crouches at the wall before the shimmy.
			Op->SetStance(Path.bCrouched && Path.Entry != EEntry::Shimmy ? EOperativeStance::Crouching : EOperativeStance::Standing);
			if (Path.bThreat)
			{
				// Behind the wall in the corner stance's line, frozen (a known threat, no fight).
				// A spitter far behind the wall in the corner stance's line: it keeps its distance and trades fire with him.
				const FVector Where = State.P + State.F * 1150.f - State.R * 380.f;
				if (AEnemyCharacter* Spitter = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::Spitter,
					FVector(Where.X, Where.Y, State.GroundZ + 100.f), (-State.F).Rotation()))
				{
					Spitter->GetHealthComponent()->SetMaxHealth(100000.f);
					State.Threat = Spitter;
				}
			}
			NextStep();
			return true;
		}
		case 1:
		{
			if (State.StepTime < 1.5f)
			{
				return true;
			}
			if (Path.Entry == EEntry::Shimmy)
			{
				// Into the middle of the wall first, settle, then shimmy to the edge.
				FCoverSlot Middle;
				const bool bFound = CoverTraceRules::FindCoverSlotAt(World, State.P + State.F * 380.f + State.R * 100.f + FVector(0.f, 0.f, 90.f), State.F, Middle);
				Check(State, bFound && Op->OrderTakeCover(Middle, false) == EOperativeOrderResult::Accepted, FString::Printf(TEXT("%s: into the middle of the wall"), Path.Name));
				NextStep();
				return true;
			}
			FCoverSlot Corner;
			const bool bFound = FindCorner(World, State, Corner, Path.SlotFromEdgeCm);
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: %s - slot %.0f cm from the right-hand edge (measured %.0f)"), Path.Name, Path.SlotFromEdgeCm,
				Corner.bRightEdgeExposed ? Corner.RightEdgeDistanceCm : -1.f);
			const FString What = FString::Printf(TEXT("%s: cover order to the right-hand corner"), Path.Name);
			Check(State, bFound && Op->OrderTakeCover(Corner, Path.Entry == EEntry::Run) == EOperativeOrderResult::Accepted, What);
			BeginTrace(State, Path.Name);
			State.Step = 3;
			State.StepTime = 0.f;
			return true;
		}
		case 2:
		{
			// Shimmy path: settled in the middle -> the shimmy order to the corner.
			if ((!Op->bInCover || State.StepTime < 3.f) && State.StepTime < 10.f)
			{
				return true;
			}
			if (Path.bCrouched && Op->GetStance() != EOperativeStance::Crouching)
			{
				Op->SetStance(EOperativeStance::Crouching);
				State.StepTime = 1.f; // 2 s for the stance clip
				return true;
			}
			FCoverSlot Corner;
			const bool bFound = FindCorner(World, State, Corner);
			// The shimmy order itself ends the hold at the old spot (an explicit order); the trace starts with it.
			const bool bShimmyOk = bFound && Op->bInCover && Op->OrderShimmyTo(Corner) == EOperativeOrderResult::Accepted;
			BeginTrace(State, Path.Name);
			Check(State, bShimmyOk, FString::Printf(TEXT("%s: shimmy order to the corner"), Path.Name));
			NextStep();
			return true;
		}
		case 3:
		{
			// Wait until he is in cover at the corner and the movement settled, then 3 s more.
			const bool bSettled = Op->bInCover && !Op->bShimmying && Op->GetVelocity().SizeSquared2D() < 4.f;
			if (bSettled && State.SettledTime < 0.f)
			{
				State.SettledTime = State.StepTime;
			}
			if (!bSettled)
			{
				State.SettledTime = -1.f;
			}
			if ((State.SettledTime < 0.f || State.StepTime - State.SettledTime < 3.f) && State.StepTime < 15.f)
			{
				return true;
			}
			Check(State, Op->bInCover && Op->bAtCoverCorner, FString::Printf(TEXT("%s: in cover at the corner (%.1f s)"), Path.Name, State.StepTime));
			EndTrace(State, Path.Name, true, *Op);
			if (Path.Entry == EEntry::StanceSwitch)
			{
				BeginTrace(State, TEXT("stand -> crouch at the edge"));
				Op->SetStance(EOperativeStance::Crouching);
				NextStep();
				return true;
			}
			++State.PathIndex;
			State.Step = 0;
			State.StepTime = 0.f;
			return true;
		}
		case 4:
		{
			if (State.StepTime < 4.f)
			{
				return true;
			}
			EndTrace(State, TEXT("stand -> crouch at the edge"), true, *Op);
			BeginTrace(State, TEXT("crouch -> stand at the edge"));
			Op->SetStance(EOperativeStance::Standing);
			NextStep();
			return true;
		}
		case 5:
		{
			if (State.StepTime < 4.f)
			{
				return true;
			}
			EndTrace(State, TEXT("crouch -> stand at the edge"), true, *Op);
			++State.PathIndex;
			State.Step = 0;
			State.StepTime = 0.f;
			return true;
		}
		default:
			return Finish(State);
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		// Every frame (the trace must see one-frame blips).
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float DeltaSeconds)
		{
			return Tick(WeakWorld, *State, DeltaSeconds);
		}), 0.f);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.CornerEntrySmoke"),
		TEXT("Dev check: entering a wall edge by every path goes straight into the corner fire stance (no tuck back); per-frame clip trace; PASS / FAIL."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
