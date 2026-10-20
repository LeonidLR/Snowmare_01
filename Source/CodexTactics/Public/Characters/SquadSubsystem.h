#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Characters/OperativeMovementRules.h"
#include "Characters/SquadFormation.h"
#include "Characters/FirePostureRules.h"
#include "GameFlow/GameFlowTypes.h"
#include "SquadSubsystem.generated.h"

class AOperativeCharacter;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSquadLeaderChanged, AOperativeCharacter*, NewLeader);

/** Where a group cover member without a slot is in his "prone behind the cover" order (USquadSubsystem::OrderGroupCoverProne). */
enum class ESquadGroupCoverPronePhase : uint8
{
	/** Tactical pause: the walk is planned, it starts on the release. */
	Planned,
	/** Running / walking to the spot. */
	Moving,
	/** At the spot: turning to face the cover before lying down. */
	Facing,
	/** Lying at the spot; held there like a follower in cover. */
	Holding
};

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

	/** Makes NewLeader the leader; bResetGroup drops a box-selected group to the leader alone (Godot select_new_leader). */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Squad")
	bool SetLeader(AOperativeCharacter* NewLeader, bool bResetGroup = true);

	/**
	 * Box selection (Godot main.gd _set_selected_squad): marks the group (rings under the non-leaders, and under the
	 * leader too when more than one is selected); bSwitchLeader makes its first member the leader unless the leader is in it.
	 */
	void SetSelectedGroup(const TArray<AOperativeCharacter*>& Group, bool bSwitchLeader = true);

	/** The box-selected operatives (just the leader when none). */
	TArray<AOperativeCharacter*> GetSelectedGroup() const;

	bool IsGroupSelected(const AOperativeCharacter* Operative) const;

	/** More than one operative selected: ground orders move them all (Godot moving_group). */
	bool HasMultiSelection() const;

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

	/** Toggles solo scout mode (Godot main.gd toggle_solo_mode on B key). */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Squad")
	void ToggleSoloMode();

	/**
	 * Godot toggle_soldier_guard: the operative holds its spot (stops, keeps facing, leaves the formation) or returns to
	 * the formation; posts the radio line.
	 */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Squad")
	void ToggleGuard(AOperativeCharacter* Operative);

	/** Re-assigns the formation slots (after a save-game load changed leaders / guards). */
	void RefreshFormation() { RebuildFollowers(); }

	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Squad")
	void EnterSoloMode();

	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Squad")
	void ExitSoloMode(bool bCausedByLeash = false, float Distance = 0.f);

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Squad")
	bool IsSoloMode() const { return bIsSoloMode; }

	/**
	 * Demo 01 rev 5h (game designer 2026-10-15, every level): a hidden trap was spotted (AInteractableActor::NotifyTrapSpotted).
	 * Everyone stops (the Godot rule); exploring with the formation outside the AUTO mode the squad also switches to Solo
	 * (TrapSpotRules::DecideSquadResponse): each operative holds, a box-selected group drops to the selected operative, so the
	 * next order moves him alone - "Mine spotted - squad holding. Move them one by one." Regroup (B) as usual. A Solo entered
	 * this way has no 25 m leash (a leash regroup would walk the formation through the traps) until the player regroups.
	 */
	void HoldForSpottedTrap(AOperativeCharacter* Spotter, const FText& TrapName);

	/** Solo entered by a spotted trap (no leash; see HoldForSpottedTrap). */
	bool IsTrapSolo() const { return bIsSoloMode && bTrapSolo; }

	/** Save-game load: the trap Solo flag (after the Solo state itself is restored). */
	void RestoreTrapSolo(bool bInTrapSolo) { bTrapSolo = bIsSoloMode && bInTrapSolo; }

	/**
	 * Rev 2026-10-19 (user: "Solo looks ON but the squad moves together" after a load): the saved Solo state set as ONE unit - the mode,
	 * the trap Solo and the followers holding - whatever the world had before the load (the old path only toggled when they differed
	 * and kept a box-selected group); a multi-selection drops to the leader, holding followers stop. No feed line, no crouch (the
	 * saved stances stay).
	 */
	void RestoreSoloForLoad(bool bSolo, bool bInTrapSolo);

	/**
	 * Commander Mode (Sprint 07-A): squad members fight on their own around their move-order anchors
	 * (USquadAutonomySubsystem). Off by default: the classic fully manual control is unchanged.
	 */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Squad")
	bool IsAutonomousSquadCombat() const { return bAutonomousSquadCombat; }

	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Squad")
	void SetAutonomousSquadCombat(bool bEnabled);

	/** Flips Commander Mode (Ctrl + T, CodexTactics.ToggleAutonomousCombat); returns the new state. */
	bool ToggleAutonomousSquadCombat();

	// --- Fire posture (rules of engagement of the automatic fire, user request 2026-10-06; FirePostureRules) ---

	/** The squad-wide posture (operatives without an override follow it). Default Aggressive (the old auto-fire). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Squad|Posture")
	ESquadFirePosture GetSquadPosture() const { return SquadPosture; }

	/** Sets the squad-wide posture and clears every per-operative override (the whole squad then fights this way). */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Squad|Posture")
	void SetSquadPosture(ESquadFirePosture Posture);

	/**
	 * The posture keys / action bar buttons (, . /), user decision 2026-10-06: the SELECTED operative(s) get Posture as
	 * their own override — the box-selected group, else the controlled leader; bSquadWide (Alt + , . /) sets the whole
	 * squad (SetSquadPosture). FirePostureRules::GetOrderScope. Posts the radio line; returns how many changed.
	 */
	int32 ApplyPostureOrder(ESquadFirePosture Posture, bool bSquadWide = false);

	/** The operatives a posture order without Alt changes now: the box-selected group, else the leader. */
	TArray<AOperativeCharacter*> GetPostureOrderTargets() const;

	/** The posture in force for Operative (its override, else the squad's). */
	ESquadFirePosture GetEffectivePosture(const AOperativeCharacter* Operative) const;

	/** Number of living operatives whose override differs from the squad posture (HUD hint). */
	int32 CountPostureOverrides() const;

	/** An operative was attacked (hit or dodged an enemy attack): provokes the squad for Defensive (squad-wide option). */
	void NotifyMemberAttacked(AOperativeCharacter* Operative);

	/** Some squad member was attacked in the current fight. */
	bool IsSquadProvoked() const { return bSquadProvoked; }

	/** Posture tuning (CVars Codex.Posture.DefensiveSquadWide / Codex.Posture.AggressiveExplorationFire). */
	static FFirePostureConfig GetPostureConfig();

	/** Max scout distance from followers in solo mode before auto-exit (cm, 25m matching Godot). */
	static constexpr float SoloModeMaxDistance = 2500.f;

	/**
	 * Rev 5g (game designer 2026-10-14): Operative executes an order of his own - a move, a use walk-up, a push / deploy task
	 * (SquadOrderRules::IsOnOwnOrder). A leader switch / Solo never cancels it; the formation takes him only once it is done.
	 */
	bool IsOnOwnOrder(const AOperativeCharacter* Operative) const;

	/** Formation slot of a follower, or INDEX_NONE for the leader / unknown actors. */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Squad")
	int32 GetFormationSlot(const AOperativeCharacter* Operative) const;

	/**
	 * Tactical pause: plans a move for an operative, clamped to Radius around where it stood when the pause began.
	 * Returns the planned (clamped) destination. Orders run together when the pause is released.
	 */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Squad")
	FVector PlanMove(AOperativeCharacter* Operative, const FVector& Destination, bool bSprint, float Radius);

	/** Where Operative stood when the current tactical pause began; false outside a pause. */
	bool GetPauseOrigin(const AOperativeCharacter* Operative, FVector& OutOrigin) const;

	/** Drops Operative's planned pause move (a planned relocation replaces it). */
	void ClearPlannedOrder(const AOperativeCharacter* Operative);

	/** Number of operatives with a planned pause order. */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Squad")
	int32 GetPlannedOrderCount() const { return PlannedOrders.Num(); }

	/** Operatives whose planned targeted shot could not fire on the pause release and is being retried (smokes). */
	int32 GetDeferredShotCount() const { return DeferredShots.Num(); }

	/** True while formation following is on (exploration only; from preparation on, operatives act individually). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Squad")
	bool IsFormationActive() const;

	// --- Group cover order (game designer 2026-10-20; GroupCoverRules, SquadOrderRules::ShouldJoinGroupCover) -------------

	/**
	 * The operatives who go along when Direct (the direct-order operative = the leader) is ordered into cover: a box selection of
	 * two or more -> the other selected ones; else group mode (Solo off, exploration formation) -> the formation followers not
	 * busy with an order of their own. Empty = the single cover order (Solo, a single selection, a fight without a box). Roster
	 * order.
	 */
	TArray<AOperativeCharacter*> GetGroupCoverMembers(const AOperativeCharacter* Direct) const;

	/**
	 * A group cover member without a slot on the cover (too short for all): he runs (bSprint) to Spot behind it, turns to FaceYaw
	 * (towards the cover) and goes prone there, then holds it like a follower in cover - until the leader walks away in group mode,
	 * Regroup, a new order to him or a stance change. bPlan: the tactical pause plans the walk (PlanMove within Radius, executed on
	 * the release). OutDestination = where he will go. False when the move is refused.
	 */
	bool OrderGroupCoverProne(AOperativeCharacter* Operative, const FVector& Spot, float FaceYaw, bool bSprint, bool bPlan, float Radius,
		FVector& OutDestination);

	/** He has a group cover prone order (planned, on the way, turning or lying there); bHoldingOnly: lying at the spot only. */
	bool HasGroupCoverProne(const AOperativeCharacter* Operative, bool bHoldingOnly = false) const;

	/** The spot of his group cover prone order; false when he has none. */
	bool GetGroupCoverProneSpot(const AOperativeCharacter* Operative, FVector& OutSpot) const;

	UPROPERTY(BlueprintAssignable, Category = "CodexTactics|Squad")
	FOnSquadLeaderChanged OnLeaderChanged;

	/** Formation tuning; replaced from balance data in Phase 2. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Squad")
	FSquadFormationConfig FormationConfig;

protected:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

private:
	struct FPlannedOrder
	{
		FVector Destination = FVector::ZeroVector;
		bool bSprint = false;
	};

	UFUNCTION()
	void HandleGameFlowChanged(ECodexGamePhase Phase, ECodexCombatMode CombatMode);

	/** EnterSoloMode body; bForTrap: the trap hold (no leash, operatives in cover keep their stance, the trap line instead). */
	void EnterSoloModeInternal(bool bForTrap);

	UFUNCTION()
	void HandleTacticalPauseReleased();

	/** Remembers where every operative stands when a tactical pause begins and drops old plans. */
	void BeginOrderPlanning();
	void ExecutePlannedOrders();
	/**
	 * Bug 2026-10-08: re-fires the planned object shots that could not fire on the release (reload / misfire / frozen
	 * weapon) in the real-time fight, within TargetedShotRules::PlannedShotRetrySeconds.
	 */
	void RetryPlannedShots(float DeltaTime);

	struct FFollowerState
	{
		TWeakObjectPtr<AOperativeCharacter> Operative;
		int32 Slot = 0;
		float ColumnTimeRemaining = 0.f;
		float RepathTimeRemaining = 0.f;
		bool bParked = false;
	};

	struct FGroupCoverProneOrder
	{
		FVector Spot = FVector::ZeroVector;
		float FaceYaw = 0.f;
		/** AOperativeCharacter::GetMoveOrderSerial after this order's own move: another value = a new order to him. */
		uint32 Serial = 0;
		ESquadGroupCoverPronePhase Phase = ESquadGroupCoverPronePhase::Moving;
		float PhaseSeconds = 0.f;
		/** The one walk onto the spot after stopping short of it was issued. */
		bool bRetried = false;
	};

	/** The group cover prone orders: arrival -> face the cover -> prone -> hold; dropped by a new order (SquadSubsystemGroupCover.cpp). */
	void UpdateGroupCoverProne(float DeltaTime);
	void DropGroupCoverProne(const AOperativeCharacter* Operative, const TCHAR* Why);
	/** ExecutePlannedOrders: the planned prone walks just started (Planned -> Moving), the others dropped. */
	void StartPlannedGroupCoverProne();

	void RebuildFollowers();
	void UpdateSlotSwap(float DeltaTime);
	void UpdateFollower(FFollowerState& Follower, AOperativeCharacter& LeaderRef, bool bLeaderMoving, float DeltaTime, float TimeSeconds);
	bool IsSlotPathBlocked(const AOperativeCharacter& LeaderRef, const FVector& SlotLocation) const;

	TArray<TWeakObjectPtr<AOperativeCharacter>> Members;
	TWeakObjectPtr<AOperativeCharacter> Leader;
	TArray<TWeakObjectPtr<AOperativeCharacter>> SelectedGroup;
	TArray<FFollowerState> Followers;
	FVector FormationHeading = FVector::ZeroVector;
	float SlotSwapCooldownRemaining = 0.f;
	bool bFollowersHolding = false;
	bool bIsSoloMode = false;
	/** Rev 5h: the Solo was entered by a spotted trap (no leash). */
	bool bTrapSolo = false;
	bool bAutonomousSquadCombat = false;

	TMap<TWeakObjectPtr<AOperativeCharacter>, FVector> PauseOrigins;
	TMap<TWeakObjectPtr<AOperativeCharacter>, FPlannedOrder> PlannedOrders;
	TMap<TWeakObjectPtr<AOperativeCharacter>, FGroupCoverProneOrder> GroupCoverProneOrders;
	/** Operatives with a deferred planned shot -> seconds of real-time fight since the release. */
	TMap<TWeakObjectPtr<AOperativeCharacter>, float> DeferredShots;
	ECodexCombatMode LastCombatMode = ECodexCombatMode::None;

	ESquadFirePosture SquadPosture = FirePostureRules::DefaultPosture;
	bool bSquadProvoked = false;
};
