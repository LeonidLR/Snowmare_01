#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Characters/OperativeMovementRules.h"
#include "Characters/SquadFormation.h"
#include "SquadSubsystem.generated.h"

class AOperativeCharacter;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSquadLeaderChanged, AOperativeCharacter*, NewLeader);

/**
 * The player's squad: roster, current leader, and the triangle formation that followers keep in exploration.
 * Followers get a NavMesh move to their slot every RepathInterval; slot math lives in SquadFormation.
 * Godot reference: player.gd (_process_follower_movement, formation slots, stance/sprint sync),
 * main.gd (_select_squad_member_by_index, _set_entire_squad_stance).
 */
UCLASS()
class CODEXTACTICS_API USquadSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** Adds an operative to the roster (sorted by SquadIndex); the first one becomes leader. */
	void RegisterOperative(AOperativeCharacter* Operative);
	void UnregisterOperative(AOperativeCharacter* Operative);

	/** Makes the roster member at RosterIndex (0-based, keys 1..N) the leader. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Squad")
	bool SetLeaderByIndex(int32 RosterIndex);

	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Squad")
	bool SetLeader(AOperativeCharacter* NewLeader);

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Squad")
	AOperativeCharacter* GetLeader() const { return Leader.Get(); }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Squad")
	TArray<AOperativeCharacter*> GetMembers() const;

	/** Sets the stance of every squad member (Alt + stance key). */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Squad")
	void SetSquadStance(EOperativeStance Stance);

	/**
	 * While holding, followers stop and keep their positions instead of following the leader
	 * (Godot: perimeter hold while the leader explores a camera zone alone).
	 */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Squad")
	void SetFollowersHolding(bool bHold);

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Squad")
	bool AreFollowersHolding() const { return bFollowersHolding; }

	/** Formation slot of a follower, or INDEX_NONE for the leader / unknown actors. */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Squad")
	int32 GetFormationSlot(const AOperativeCharacter* Operative) const;

	UPROPERTY(BlueprintAssignable, Category = "CodexTactics|Squad")
	FOnSquadLeaderChanged OnLeaderChanged;

	/** Formation tuning; replaced from balance data in Phase 2. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Squad")
	FSquadFormationConfig FormationConfig;

private:
	struct FFollowerState
	{
		TWeakObjectPtr<AOperativeCharacter> Operative;
		int32 Slot = 0;
		float ColumnTimeRemaining = 0.f;
		float RepathTimeRemaining = 0.f;
		bool bParked = false;
	};

	void RebuildFollowers();
	void UpdateSlotSwap(float DeltaTime);
	void UpdateFollower(FFollowerState& Follower, AOperativeCharacter& LeaderRef, bool bLeaderMoving, float DeltaTime, float TimeSeconds);
	bool IsSlotPathBlocked(const AOperativeCharacter& LeaderRef, const FVector& SlotLocation) const;

	TArray<TWeakObjectPtr<AOperativeCharacter>> Members;
	TWeakObjectPtr<AOperativeCharacter> Leader;
	TArray<FFollowerState> Followers;
	FVector FormationHeading = FVector::ZeroVector;
	float SlotSwapCooldownRemaining = 0.f;
	bool bFollowersHolding = false;
};
