#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Combat/KnockdownTypes.h"
#include "Ladders/LadderRules.h"
#include "Ladders/LadderTypes.h"
#include "LadderClimbComponent.generated.h"

class ALadderActor;
class ACharacter;
class UAnimMontage;
class UAnimSequence;
class UAnimInstance;

/** Per-frame rung IK data for the anim instance (LADDERS.md 6.2): world rung centres + alphas + the grip offsets. */
struct FLadderIKFrame
{
	bool bActive = false;
	FVector RungCentre[static_cast<int32>(ELadderLimb::Count)];
	float Alpha[static_cast<int32>(ELadderLimb::Count)] = { 0.f, 0.f, 0.f, 0.f };
	/** Away from the wall (towards the climber), across the ladder, up. */
	FVector Outward = FVector::ForwardVector;
	FVector Across = FVector::RightVector;
	/** Half the rail spacing minus a margin: a grip stays between the rails. */
	float HalfWidth = 24.f;
	/** Hand / foot target offsets from the rung centre: along Outward and up, cm. */
	FVector2D HandOffset = FVector2D(4.f, -2.f);
	FVector2D FootOffset = FVector2D(-6.f, 5.f);
};

/**
 * The climb (docs/design/LADDERS.md section 5): one state machine (LadderRules::Transition) driven by the ladder's smart nav link
 * - a click on the other level builds a path through the link, the path's "link reached" hands the climber over here, the climb
 * runs by itself and the path resumes at the other end (no climb axis, no W / S). The montages play IN PLACE on the FullBody slot
 * (root motion ignored) and are positioned by this component every tick; the actor follows the clip's extracted root track,
 * scaled per axis to the exact target, and is snapped to Floor + k * Pitch at every loop boundary (rung-index snapping). The top
 * transitions turn the climber's OWN collision off (never the wall's, like the vault), with an end-fit check. One interrupt path,
 * AbortClimb, for montage interrupts (the montage delegates), death, knockdown (-> the fall, 7.7), save / load, EndPlay, timeouts.
 * On AOperativeCharacter (default subobject); enemies are a later phase (7.5). UE-only system (no Godot reference).
 */
UCLASS(ClassGroup = (CodexTactics), meta = (BlueprintSpawnableComponent))
class CODEXTACTICS_API ULadderClimbComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	ULadderClimbComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// --- Clips (LADDERS.md 4; the UE5 Manny clips play on the operative's UE4 skeleton by bone name) ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CodexTactics|Ladder")
	TSoftObjectPtr<UAnimSequence> UpLoopClip;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CodexTactics|Ladder")
	TSoftObjectPtr<UAnimSequence> ClimbOutClip;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CodexTactics|Ladder")
	TSoftObjectPtr<UAnimSequence> IntoFromTopClip;

	/** The anim graph's full-body slot (an enemy's UEnemyAnimInstance::OneShotSlot is used on an enemy body). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CodexTactics|Ladder")
	FName MontageSlot = TEXT("FullBody");

	/** A climbing enemy waits this long at a leased ladder, then its AI decides afresh (it may queue again), s. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CodexTactics|Ladder", meta = (ClampMin = "0.5"))
	float EnemyQueueSeconds = 6.f;

	// --- Tunables (LADDERS.md 7.9) ---

	/** Clip play rate on the ladder (1 = the clips' speed: ~41 cm/s up the loop). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CodexTactics|Ladder", meta = (ClampMin = "0.1"))
	float ClimbPlayRate = 1.f;

	/** The glide from the link start onto the hang line: at least this long, s ... */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CodexTactics|Ladder", meta = (ClampMin = "0.05"))
	float AlignMinSeconds = 0.35f;

	/** ... and no faster than this, cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CodexTactics|Ladder", meta = (ClampMin = "10"))
	float AlignSpeed = 220.f;

	/** Turn rate of the align glides, deg/s. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CodexTactics|Ladder", meta = (ClampMin = "10"))
	float AlignTurnRate = 540.f;

	/**
	 * Going down from the top (user feedback 2026-10-23: "he goes down with his BACK to the ladder for the first seconds"): he walks to the
	 * ladder head, turns in place until he faces the ladder (back to the drop), and climbs down backwards - the ClimbOutTop clip played in
	 * reverse (its body faces the ladder throughout). Off = the old IntoFromTop clip, whose 180 deg turn runs while the body already sinks.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CodexTactics|Ladder")
	bool bDescendFacingLadder = true;

	/** The turn in place at the ladder head before going down: at least this long, s (feet planted). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CodexTactics|Ladder", meta = (ClampMin = "0.1"))
	float TopTurnMinSeconds = 0.5f;

	/** Blend between the loop and the top clips, s (IntoFromTop always starts with blend-in 0). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CodexTactics|Ladder", meta = (ClampMin = "0"))
	float ClipBlendSeconds = 0.2f;

	/** Blend out of the climb at either end, s. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CodexTactics|Ladder", meta = (ClampMin = "0"))
	float ExitBlendSeconds = 0.3f;

	/** A knocked-down / killed climber leaves the hang line this far before he falls, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CodexTactics|Ladder", meta = (ClampMin = "0"))
	float DropAwayCm = 20.f;

	/** Fall damage on landing (user decision 2026-10-22: safe 1 m, 0.1 HP / cm, max 40). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CodexTactics|Ladder")
	float FallSafeHeightCm = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CodexTactics|Ladder")
	float FallDamagePerCm = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CodexTactics|Ladder")
	float FallDamageMax = 40.f;

	/** Rung IK targets: the wrist this far towards the climber / up from the rung centre; the ball of the foot likewise. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CodexTactics|Ladder IK")
	FVector2D HandGripOffset = FVector2D(4.f, -2.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CodexTactics|Ladder IK")
	FVector2D FootGripOffset = FVector2D(-6.f, 5.f);

	/** Rung IK on / off (the smokes compare). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CodexTactics|Ladder IK")
	bool bRungIK = true;

	/**
	 * This body's offset from the clips' hang line on the loop, cm: X away from the rungs (towards the climber), Y up. Zero for the
	 * Manny-proportioned bodies the clips were made on; a body whose arms cannot reach the hand rungs (the Marksman's short torso) hangs
	 * a little higher / closer. Faded in with the rung IK on the loop, out at the transitions.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CodexTactics|Ladder IK")
	FVector2D BodyHangOffset = FVector2D::ZeroVector;

	// --- API ---

	/**
	 * The path reached Ladder's link (its smart-link callback, or this component's own polling). Refused -> the path ends with a
	 * feed line; leased / not standing -> he waits at the link; else the climb starts next tick. True when handled.
	 */
	bool NotifyLinkReached(ALadderActor* Ladder, bool bGoingUp);

	/** The one interrupt path (LADDERS.md 5.3). Death / Knockdown drop him to the bottom; the rest place him at the nearest end. */
	void AbortClimb(ELadderAbortReason Reason);

	/** A knockdown while climbing: he falls to the bottom and is knocked down on landing, with fall damage (7.7). */
	bool DropForKnockdown(EKnockdownDirection Direction, EKnockdownCause Cause);

	/** A new order mid-climb: he finishes the climb, then walks there (7.1). */
	void SetPendingDestination(const FVector& Destination);
	void ClearPendingDestination() { bHasPendingDestination = false; }

	ELadderClimbState GetState() const { return State; }

	/** Loads the clips (once) and extracts their root tracks; false when one is missing (that body cannot climb). */
	bool EnsureClips() { return LoadClips(); }

	/** The climb owns the body: any state but None, or an entry pending for the next tick (the owner's AI waits). */
	bool IsBusy() const { return State != ELadderClimbState::None || bEntryPending; }

	/** The slot the climb clips play on for this body. */
	FName GetClipSlot() const;

	/** On the ladder or moving onto / off it, or falling off it (not None / Queue). */
	bool IsOnLadder() const { return LadderRules::IsOnLadderState(State); }

	/** Climbing proper (on the ladder, not falling off it). */
	bool IsClimbing() const { return IsOnLadder() && State != ELadderClimbState::Dropping; }

	bool IsDropping() const { return State == ELadderClimbState::Dropping; }
	bool IsQueued() const { return State == ELadderClimbState::Queue; }

	ALadderActor* GetLadder() const { return Ladder.Get(); }
	const LadderRules::FLoopPos& GetLoopPos() const { return LoopPos; }
	bool IsGoingUp() const { return bGoingUp; }

	/** Where a save taken now puts him: the link point of the end he is heading to (7.8); his location when not climbing. */
	FVector GetSaveLocation() const;

	/**
	 * The floor he counts on for the roof cutaway / minimap (LADDERS.md 7.3): on the ladder (or falling off it) the spot where the climb
	 * started - the floor he left - until ExitTop / ExitBottom ends; his location otherwise.
	 */
	FVector GetFloorReferenceLocation() const;

	/** Rung IK for the anim instance (game thread). */
	const FLadderIKFrame& GetIKFrame() const { return IKFrame; }

	/** The montage playing the climb now (smokes). */
	UAnimMontage* GetCurrentMontage() const { return CurrentMontage.Get(); }

	// --- Stats (smokes) ---
	int32 GetClimbsUp() const { return ClimbsUp; }
	int32 GetClimbsDown() const { return ClimbsDown; }
	float GetLastDropHeight() const { return LastDropHeight; }
	float GetLastFallDamage() const { return LastFallDamage; }
	ELadderRefusal GetLastRefusal() const { return LastRefusal; }
	ELadderAbortReason GetLastAbort() const { return LastAbort; }
	float GetLoopLength() const { return LoopLength; }

	/** The align glide's walking speed now (the legs walk it; 0 = standing / turning), cm/s. */
	float GetAlignWalkSpeed() const { return AlignWalkSpeed; }

private:
	/** The clip's root track in the climber frame (X forward to the wall, Y right, Z up), from its first frame, at 30 Hz. */
	struct FRootTrack
	{
		TArray<FVector> Samples;
		float Length = 0.f;
		float SampleRate = 30.f;
		FVector Sample(float Time) const;
		FVector End() const { return Samples.Num() ? Samples.Last() : FVector::ZeroVector; }
	};

	bool LoadClips();
	FRootTrack ExtractTrack(UAnimSequence* Clip) const;
	/**
	 * Takes the clip's own lift of its last part out of Track (the ClimbOutTop's feet rise with the body after LiftStart although they stand
	 * on the top): from LiftStart the smaller of the two feet's rise is subtracted from the root's height. Returns the lift removed at the end.
	 */
	float RemoveClipLift(UAnimSequence* Clip, FRootTrack& Track, float LiftStart) const;
	/** The up loop's rise share at Time (0..1). */
	float LoopFraction(float Time) const;

	ACharacter* GetCharacter() const;
	UAnimInstance* GetAnimInstance() const;
	float GetHalfHeight() const;

	void StartEntry();
	void Enter(ELadderClimbState NewState, float ExpectedSeconds);
	void Fire(ELadderClimbEvent Event, float ExpectedSeconds = 0.f);

	void TickAlignBottom(float DeltaTime);
	void TickAlignTop(float DeltaTime);
	void TickWarpClip(float DeltaTime);
	void TickLoop(float DeltaTime);
	void TickExitBottom(float DeltaTime);
	void TickDropping(float DeltaTime);
	void PollLinks();

	void BeginEnterTop();
	/** The ClimbOutTop warp between the hang pose on rung Rung and the platform: its scale and where it ends on top (capsule centres). */
	void TopWarp(int32 Rung, FVector& OutHang, FVector& OutTop, FVector& OutScale) const;
	void BeginExitTop();
	/** The platform's edge behind the rung plane at the ladder's top, cm (a trace towards the wall); -1 = none found. */
	float MeasureTopEdge() const;
	/**
	 * On ClimbOutTop (both ways) the knees / feet swing over the platform's edge while still a little under its top (the clip's ladder stood
	 * further off its ledge): pushes him off the wall just enough that no leg bone below the top is past the edge (a few cm, released smoothly).
	 * Base = where the root track puts him. Returns the push, cm.
	 */
	float EdgePushFor(const FVector& Base, float DeltaTime);
	void BeginLoop(bool bUp);
	void BeginExitBottom();
	void FinishClimb();

	/** Plays Clip on MontageSlot paused at Time (the component sets the position every tick). */
	UAnimMontage* PlayClip(UAnimSequence* Clip, float Time, float BlendIn);
	void SetClipTime(float Time);
	/** Stops the climb montage without treating it as an interrupt. */
	void StopClip(float BlendOut);
	void HandleMontageEnded(UAnimMontage* Montage, bool bInterrupted);

	void TakeSnapshot();
	void RestoreSnapshot(bool bFalling);
	void ResumePath();
	void Refuse(ELadderRefusal Refusal);
	void UpdateIKFrame();
	void PlaceAt(const FVector& CapsuleCentre, float Yaw);

	UFUNCTION()
	void HandleOwnerDied(AActor* Victim, const FString& AttackerSource);

	ELadderClimbState State = ELadderClimbState::None;
	float StateTime = 0.f;
	float StateTimeout = 0.f;
	TWeakObjectPtr<ALadderActor> Ladder;
	bool bGoingUp = true;

	/** The link was reached: the climb starts on the next tick (the crowd's link callback is mid-update). */
	bool bEntryPending = false;
	TWeakObjectPtr<ALadderActor> PendingLadder;
	bool bPendingUp = true;
	float QueueRetry = 0.f;
	/** Time spent in Queue (an enemy gives up after EnemyQueueSeconds). */
	float QueueSeconds = 0.f;

	FVector PendingDestination = FVector::ZeroVector;
	bool bHasPendingDestination = false;
	/** The actor location when the climb took the body (GetFloorReferenceLocation). */
	FVector ClimbStartLocation = FVector::ZeroVector;

	LadderRules::FLoopPos LoopPos;
	LadderRules::FLoopPos TargetPos;
	float LoopLength = 0.f;
	/** Up-loop rise share per sample (0 at the start, 1 at the end), 30 Hz. */
	TArray<float> LoopRise;
	FRootTrack ClimbOutTrack;
	FRootTrack IntoFromTopTrack;
	bool bClipsLoaded = false;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> LoadedUpLoop;
	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> LoadedClimbOut;
	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> LoadedIntoFromTop;

	TWeakObjectPtr<UAnimMontage> CurrentMontage;

	/** Align / warp glides. */
	FVector GlideFrom = FVector::ZeroVector;
	FVector GlideTo = FVector::ZeroVector;
	float GlideFromYaw = 0.f;
	float GlideToYaw = 0.f;
	float GlideSeconds = 0.f;
	/** Warped clip: start, per-axis scale in the climber frame, the climber yaw, the clip time. */
	const FRootTrack* WarpTrack = nullptr;
	FVector WarpStart = FVector::ZeroVector;
	FVector WarpScale = FVector::OneVector;
	float WarpYaw = 0.f;
	float ClipTime = 0.f;
	/** The warped clip plays backwards (ClimbOutTop going down). */
	bool bWarpReverse = false;
	/** EdgePushFor: the edge measured at the top transition, the push (cm, away from the wall) and its release spring. */
	float TopEdgeCm = -1.f;
	float EdgePush = 0.f;
	float EdgePushVelocity = 0.f;
	float MaxEdgePush = 0.f;

public:
	/** The largest edge push of the last climb (smokes), cm. */
	float GetMaxEdgePush() const { return MaxEdgePush; }

private:
	/** AlignTop: walk to the ladder head (0), then the turn in place (1). */
	int32 AlignPhase = 0;
	float AlignPhaseStart = 0.f;
	float WalkSeconds = 0.f;
	float TurnSeconds = 0.f;
	float AlignWalkSpeed = 0.f;

	/** Snapshot restored on every exit (LADDERS.md 2: movement mode, rotation flags, braking, gravity, root motion, weapon). */
	struct FSnapshot
	{
		bool bValid = false;
		uint8 MovementMode = 1;
		float GravityScale = 1.f;
		float BrakingFlying = 0.f;
		bool bOrientToMovement = false;
		bool bControllerDesiredRotation = false;
		uint8 RootMotionMode = 0;
		bool bWeaponVisible = true;
	} Snapshot;

	/** Knockdown drop. */
	float DropFeetZ = 0.f;
	/** A body without collision (a killed enemy's capsule is switched off) falls by code onto this floor. */
	float DropFloorZ = 0.f;
	float DropSpeed = 0.f;
	bool bDropFloorFound = false;
	bool bKnockdownOnLanding = false;
	EKnockdownDirection DropDirection = EKnockdownDirection::Back;
	EKnockdownCause DropCause = EKnockdownCause::None;

	FLadderIKFrame IKFrame;
	/** The rung IK fades in on the loop and out at the transitions (0..1). */
	float IKWeight = 0.f;
	float PollTimer = 0.f;

	int32 ClimbsUp = 0;
	int32 ClimbsDown = 0;
	float LastDropHeight = 0.f;
	float LastFallDamage = 0.f;
	ELadderRefusal LastRefusal = ELadderRefusal::None;
	ELadderAbortReason LastAbort = ELadderAbortReason::None;
};
