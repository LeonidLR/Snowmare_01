#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Survival/WarmIdleRules.h"
#include "WarmIdleSubsystem.generated.h"

class AActor;
class AOperativeCharacter;
class UHeatSourceComponent;
struct FTacticalHeatConfig;

/**
 * Weather idle poses of the operatives (user request 2026-10-12, user design for the fire roles the same day; UE-only).
 * Exploration only (WarmIdleRules::IsExplorationPhase); every decision is cosmetic and gives way to any order at once:
 *  - cold idle: cold >= WarmIdleColdThreshold, idle WarmIdleDelaySeconds, out of any warm zone -> the shivering set on the spot;
 *  - barrel: idle operatives in a burning barrel's warm zone walk to hand-warming places around it (BarrelStandDistance) and face it;
 *  - campfire / gas stove: the operatives in its warm zone are given roles (WarmIdleRules::AssignFireRoles): the coldest sit on the
 *    inner ring (at least WarmMinSitters, plus everyone >= WarmExtraSitterCold), the rest stand on the outer ring warming their
 *    hands; roles are re-evaluated only when someone arrives / leaves or the fire state changes (no flicker as the cold drops).
 * Spots are spread around the fire (WarmIdleRules::PickSlot), checked against the NavMesh and blocking geometry, and everyone faces
 * the fire. An order (AOperativeCharacter::GetMoveOrderSerial) frees only the ordered operative; a parked follower keeps his spot
 * while the leader stays in that warm zone (ShouldFollowerHoldWarmSpot, read by USquadSubsystem). A threat, combat start, knockdown,
 * cover or a task ends the pose (UOperativeAnimInstance plays the End clip / blends out).
 * Squad gathers at the fire (user decision 2026-10-12, FTacticalHeatConfig::bWarmPullFollowers): while the leader holds a warm spot at a
 * barrel / campfire / stove, USquadSubsystem::UpdateFollower asks GetFollowerPullTarget and walks the followers to that source's ring
 * instead of their formation slots (exploration formation only; solo / guard / explicit orders / threats / combat are not pulled);
 * once they stand in the zone the normal flow above gives them roles (the coldest sit) and spots.
 */
UCLASS()
class CODEXTACTICS_API UWarmIdleSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** Runs one decision step now with DeltaSeconds (smokes; the tick runs it every EvaluateInterval). */
	void Evaluate(float DeltaSeconds);

	/** A parked follower keeps his warm spot while the leader is within the warm zone of that spot's heat source. */
	static bool ShouldFollowerHoldWarmSpot(const AOperativeCharacter& Follower, const AOperativeCharacter& Leader);

	/** Where a pulled follower goes (GetFollowerPullTarget). */
	struct FFollowerPull
	{
		/** A spot on the source's outer ring (or a point inside its warm zone when no ring is built). */
		FVector Target = FVector::ZeroVector;
		/** The heat source's location (to face it). */
		FVector Fire = FVector::ZeroVector;
		/** At the target (or standing inside the zone): the squad parks him there and the normal flow takes over. */
		bool bArrived = false;
	};

	/**
	 * Squad gathers at the fire (WarmIdleRules::ShouldPullFollower): true when Follower, an exploration-formation follower, is to walk
	 * to the heat source Leader is warming at instead of his formation slot. An explicit order to him (GetMoveOrderSerial) drops him from
	 * the pull until the leader leaves that fire. Called from USquadSubsystem::UpdateFollower at the repath rate.
	 */
	bool GetFollowerPullTarget(AOperativeCharacter& Follower, const AOperativeCharacter& Leader, FFollowerPull& OutPull);

	/** The source Follower is pulled to now (smokes / debug); nullptr when none. */
	AActor* GetFollowerPullSource(const AOperativeCharacter* Follower) const;

	/** Decision state of one operative (smokes / debug). */
	struct FDebugState
	{
		EWarmIdlePose Pose = EWarmIdlePose::None;
		EWarmFireRole Role = EWarmFireRole::Unassigned;
		EWarmHeatKind Heat = EWarmHeatKind::None;
		TWeakObjectPtr<AActor> Source;
		bool bHasSpot = false;
		bool bInnerRing = false;
		bool bArrived = false;
		FVector Spot = FVector::ZeroVector;
		float IdleSeconds = 0.f;
	};
	FDebugState GetDebugState(const AOperativeCharacter* Operative) const;

	/** Decisions per second (the walk / pose requests); threats and phase changes are seen within this. */
	float EvaluateInterval = 0.1f;

	/** An attack this recent counts as a threat, s. */
	float ThreatHoldSeconds = 5.f;

	/** The operative must face the fire within this before the pose starts, degrees. */
	float FacingToleranceDegrees = 25.f;

private:
	struct FMemberState
	{
		uint32 OrderSerial = 0;
		bool bSerialKnown = false;
		float IdleSeconds = 0.f;
		EWarmIdlePose Pose = EWarmIdlePose::None;
		TWeakObjectPtr<AActor> Source;
		EWarmHeatKind Heat = EWarmHeatKind::None;
		EWarmFireRole Role = EWarmFireRole::Unassigned;
		bool bInnerRing = false;
		int32 Slot = INDEX_NONE;
		bool bWalkIssued = false;
		bool bArrived = false;
		int32 WalkFailures = 0;
		/** Gave up on this source (no reachable spot): no new attempt until he leaves its zone. */
		TWeakObjectPtr<AActor> GaveUpSource;
	};

	/** The squad pull of one follower (see GetFollowerPullTarget). */
	struct FPullState
	{
		uint32 OrderSerial = 0;
		bool bSerialKnown = false;
		/** World time of the last GetFollowerPullTarget call: the squad stopped asking (solo / held / guard / a fight) -> the pull lapses. */
		double LastQuery = 0.0;
		TWeakObjectPtr<AActor> Source;
		float Reach = 0.f;
		FVector Target = FVector::ZeroVector;
		/** Reserved outer-ring slot (INDEX_NONE: none), so two pulled followers do not pick the same one. */
		int32 Slot = INDEX_NONE;
		/** Ordered away from this fire: not pulled again until the leader leaves its warm zone. */
		TWeakObjectPtr<AActor> OptOutSource;
		float OptOutReach = 0.f;
	};

	struct FGroup
	{
		TWeakObjectPtr<AActor> Source;
		EWarmHeatKind Kind = EWarmHeatKind::None;
		FVector Center = FVector::ZeroVector;
		float Reach = 0.f;
		bool bRingsBuilt = false;
		TArray<FVector> Inner;
		TArray<FVector> Outer;
		TArray<bool> InnerBlocked;
		TArray<bool> OuterBlocked;
		TArray<TWeakObjectPtr<AOperativeCharacter>> Members;
	};

	/** The heat source covering Operative (barrel / campfire / stove preferred over other sources, the nearest first). */
	UHeatSourceComponent* FindHeatSource(const AOperativeCharacter& Operative, EWarmHeatKind& OutKind) const;
	static EWarmHeatKind ClassifySource(const UHeatSourceComponent& Source);
	void BuildRings(FGroup& Group, const FTacticalHeatConfig& Config, const FVector& FirstMember) const;
	bool IsSpotUsable(const FVector& Spot, const AActor* SourceActor) const;
	void UpdateGroup(FGroup& Group, const TArray<AOperativeCharacter*>& Candidates, const FTacticalHeatConfig& Config);
	void AssignSlot(FGroup& Group, AOperativeCharacter& Operative, FMemberState& State);
	void DriveMember(FGroup& Group, AOperativeCharacter& Operative, FMemberState& State, const FTacticalHeatConfig& Config);
	void Release(AOperativeCharacter& Operative, FMemberState& State);
	/** An enemy in his cone, a combat target or a recent hit. */
	bool IsThreatened(const AOperativeCharacter& Operative, double Now, bool bExploration) const;
	/** Panic, rage, a use order or a relocation task of his own. */
	bool IsBusy(const AOperativeCharacter& Operative) const;
	/** The outer-ring slot (or a point in the zone) Follower walks to at Source. */
	FVector ChoosePullTarget(const AOperativeCharacter& Follower, const AActor& Source, float Reach, int32& OutSlot) const;
	void RequestPose(AOperativeCharacter& Operative, FMemberState& State, EWarmIdlePose Pose, EWarmExitReason Reason);

	TMap<TWeakObjectPtr<AOperativeCharacter>, FMemberState> States;
	TMap<TWeakObjectPtr<AActor>, FGroup> Groups;
	TMap<TWeakObjectPtr<AOperativeCharacter>, FPullState> Pulls;
	float Accumulator = 0.f;
};
