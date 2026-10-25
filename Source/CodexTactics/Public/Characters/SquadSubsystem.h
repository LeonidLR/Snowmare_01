#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Characters/OperativeMovementRules.h"
#include "Characters/SquadFormation.h"
#include "Characters/FirePostureRules.h"
#include "Formation/FormationTerrainRules.h"
#include "Formation/FormationTypes.h"
#include "GameFlow/GameFlowTypes.h"
#include "Tactics/CoverTypes.h"
#include "SquadSubsystem.generated.h"

class AOperativeCharacter;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSquadLeaderChanged, AOperativeCharacter*, NewLeader);

/**
 * Group cover leader priority (game designer 2026-10-23): what one group cover order keeps for its waiting members - the free slots of
 * the same wall nobody was given and the unused prone spots behind the cover (a member whose slot was taken while he waited gets the
 * nearest free spare, else a prone spot).
 */
struct FGroupCoverSpares
{
	TArray<FCoverSlot> Slots;
	TArray<bool> SlotUsed;
	TArray<FVector> ProneSpots;
	TArray<bool> ProneUsed;
	/** Facing of a prone member (towards the cover). */
	float FaceYaw = 0.f;
};

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

	/**
	 * Game designer play-test 2026-10-22: after a fight that returns to exploring the followers keep the spots they fought from
	 * until the leader moves or the player orders Regroup (B; bug 2026-10-23) - SquadOrderRules::ArmsPostFightHold / KeepsPostFightHold.
	 */
	bool IsPostFightHoldActive() const { return bPostFightHold; }

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

	/**
	 * True while formation following is on: exploration and after the final fight (PostCombat, bug 2026-10-23; SquadOrderRules::IsFormationPhase).
	 * From preparation to the fight's end the operatives act individually.
	 */
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

	/**
	 * Group cover leader priority (game designer 2026-10-23): instead of running straight into his slot, Member runs to his wait point
	 * (GroupCoverRules::ComputeWaitPoint, ~1.6 m out from the slot) and waits there until CoverLeader has taken his cover (or CoverLeader's order
	 * failed / he is down: GroupCoverRules::ShouldReleaseToSlot), then takes Slot (a run); a slot taken meanwhile -> the nearest free
	 * spare slot, else prone behind the cover (Spares). bHoldSlot: SetHoldGroupCoverSlot on the slot order. False when no wait point
	 * is reachable (the caller sends him into the slot at once). Dropped by a new order to him, Regroup or turn-based combat.
	 */
	bool OrderGroupCoverWait(AOperativeCharacter* Member, const FCoverSlot& Slot, AOperativeCharacter* CoverLeader, bool bHoldSlot,
		const TSharedRef<FGroupCoverSpares>& Spares);

	/** He waits off the wall for the leader of his group cover order (on the way to the wait point or standing there). */
	bool IsWaitingForGroupCoverLeader(const AOperativeCharacter* Operative) const;

	// --- Formation look sectors (Codex.Formation.Sectors; docs/design/FORMATION_SECTORS.md, FORMATION_CONTRACT.md 7; SquadSubsystemFormation.cpp) ---

	/**
	 * The formation in force (console on, exploration formation): picked by the terrain round the leader (FormationTerrainRules::Classify
	 * with hysteresis over a trace probe, FormationRuntime::ChooseFormation: the wall layout only on arrival) or the wall order.
	 */
	EFormationKind GetFormationKind() const { return Sectors.Formation; }
	/** Walk (patrol) / run / halt / wall. */
	EFormationMode GetFormationMode() const { return Sectors.Mode; }
	/** The terrain class in force (unset before the first probe). */
	TOptional<EFormationTerrainClass> GetFormationTerrainClass() const { return Sectors.Terrain.Class; }
	/** The free passage width round the leader (FormationTerrainRules::PassageWidth), unset when a side is open. */
	TOptional<float> GetFormationPassageWidth() const { return Sectors.PassageWidth; }
	/** The formation heading (the leader's smoothed travel direction), UE yaw. */
	float GetFormationHeadingYaw() const { return Sectors.HeadingYaw; }
	/** Members in the formation (the leader and his followers, at most 4 roles). */
	int32 GetFormationMemberCount() const { return Sectors.MemberCount; }
	/** True while the console is on and this frame drove the formation (exploration formation, group mode). */
	bool IsFormationSectorsActive() const { return Sectors.bActiveThisFrame; }

	/** Operative's formation role this frame (the leader K; a follower by his slot / the wall order); false when he has none. */
	bool GetFormationRole(const AOperativeCharacter* Operative, EFormationRole& OutRole) const;

	/** Operative's formation slot (Z = his feet height) and look yaw this frame; false when he has none (no role, console off, ...). */
	bool GetFormationTarget(const AOperativeCharacter* Operative, FVector& OutSlot, float& OutLookYaw) const;

	/** A group cover order in group mode runs as the formation wall layout (OrderFormationWall) and has not ended. */
	bool HasFormationWallOrder() const { return Sectors.bWallOrder; }

	/** The outward normal's yaw of the wall layout in force (the automatic one or the order); false when none. */
	bool GetFormationWallNormalYaw(float& OutYaw) const;

	/**
	 * FORMATION_SECTORS.md "Wall" (console on): a group cover order given in group mode (Solo off, no box selection, exploration
	 * formation; not planned in the pause, not a cover switch / wrap) - nobody looks through the wall: Direct (the leader, K) runs to the
	 * clicked slot, Others (his formation followers) to the FormationWallRules::Layout slots along the same wall 1 m apart (the 4th at the
	 * roomier end); each turns to his layout yaw within 1 m of his slot (facing his path before), then crouches facing OUT, back to the
	 * wall (the baseline pose; AOperativeCharacter::IsInFormationWallPose). Ends with the leader's next order (the others follow him
	 * again, standing up). False (nothing ordered) when the layout cannot be made (no wall behind the slot, more than 4 members).
	 */
	bool OrderFormationWall(AOperativeCharacter* Direct, const TArray<AOperativeCharacter*>& Others, const FCoverSlot& Slot);

	/** Smokes / teleports: the formation heading snaps to Forward, the terrain history and the slot velocities start afresh, a wall order ends. */
	void ResetFormationState(const FVector& Forward);

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
		/** Formation sectors: his slot at the last repath and when (the slot's velocity for the follower speed, contract 7). */
		FVector2D PrevSectorSlot = FVector2D::ZeroVector;
		double PrevSectorSlotTime = -1.0;
	};

	/** Formation look sectors: the state over frames (SquadSubsystemFormation.cpp). */
	struct FFormationSectorState
	{
		FFormationTerrainTracker Terrain;
		EFormationKind Formation = EFormationKind::Wedge;
		/** The formation the leader last walked in (kept while a wall ahead is still a corner to turn). */
		EFormationKind MoveFormation = EFormationKind::Wedge;
		EFormationMode Mode = EFormationMode::Halt;
		TOptional<float> PassageWidth;
		float HeadingYaw = 0.f;
		bool bHeadingValid = false;
		float PatrolTime = 0.f;
		/** Seconds the leader has stood still (the wall layout only on arrival). */
		float LeaderStillSeconds = 0.f;
		float ProbeTimeRemaining = 0.f;
		/** The probe walls (thin segments at the trace hits), the leader position and heading they were traced from. */
		TArray<FFormationWall> ProbeWalls;
		FVector2D ProbeOrigin = FVector2D::ZeroVector;
		float ProbeHeadingYaw = 0.f;
		/** The raw class of the last probe (FormationRuntime::AcceptProbe). */
		TOptional<EFormationTerrainClass> LastProbeRaw;
		int32 MemberCount = 1;
		bool bActiveThisFrame = false;
		/** This frame's slots and look yaws by role (Slots / LookYaw, or the wall layout). */
		TArray<FFormationSlot> FrameSlots;
		/** The wall layout in force (the automatic one on arrival or the order's). */
		bool bWallLayoutValid = false;
		TArray<FFormationSlot> WallSlots;
		FVector2D WallAnchor = FVector2D::ZeroVector;
		float WallNormalYaw = 0.f;
		/** The group cover order in group mode as the wall layout (OrderFormationWall). */
		bool bWallOrder = false;
		TWeakObjectPtr<AOperativeCharacter> WallOrderLeader;
		uint32 WallOrderSerial = 0;
		float WallOrderSeconds = 0.f;
		TMap<TWeakObjectPtr<AOperativeCharacter>, EFormationRole> WallOrderRoles;
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

	/** A group cover member waiting for the leader (OrderGroupCoverWait). */
	struct FGroupCoverWaitOrder
	{
		TWeakObjectPtr<AOperativeCharacter> Leader;
		FCoverSlot Slot;
		FVector WaitPoint = FVector::ZeroVector;
		/** AOperativeCharacter::GetMoveOrderSerial after the walk to the wait point: another value = a new order to him. */
		uint32 Serial = 0;
		float WaitSeconds = 0.f;
		float LeaderIdleSeconds = 0.f;
		bool bHoldSlot = false;
		TSharedPtr<FGroupCoverSpares> Spares;
	};

	/** Waiting members: released into their slots once the leader is in cover (SquadSubsystemGroupCover.cpp). */
	void UpdateGroupCoverWait(float DeltaTime);
	void DropGroupCoverWait(const AOperativeCharacter* Operative, const TCHAR* Why);
	/** Sends him into his slot (or the nearest free spare / a prone spot when it was taken). */
	void ReleaseGroupCoverWait(AOperativeCharacter& Member, const FGroupCoverWaitOrder& Order, const TCHAR* Why);
	/** Another operative is in / going to a cover slot within the occupied radius of Point (or a body stands there). */
	bool IsGroupCoverPointTaken(const FVector& Point, const AOperativeCharacter* Except) const;

	/** The group cover prone orders: arrival -> face the cover -> prone -> hold; dropped by a new order (SquadSubsystemGroupCover.cpp). */
	void UpdateGroupCoverProne(float DeltaTime);
	void DropGroupCoverProne(const AOperativeCharacter* Operative, const TCHAR* Why);
	/** ExecutePlannedOrders: the planned prone walks just started (Planned -> Moving), the others dropped. */
	void StartPlannedGroupCoverProne();

	void RebuildFollowers();
	/** The follower part of Tick (formation follow); Tick applies the frame's formation looks / pace after it. */
	void TickFormationFollow(float DeltaTime);
	/** Formation sectors: heading, probe, terrain class, formation, mode, this frame's slots / looks, the leader's look and pace. */
	void UpdateFormationSectors(AOperativeCharacter& LeaderRef, bool bLeaderMoving, float DeltaTime);
	/** Sets every member's formation look / pace from this frame (none for a member not in it). */
	void ApplyFormationFrame();
	/** v1.2: the leader's move destination for Classify (at_wall only on arrival); unset before his first move order. */
	static TOptional<FVector2D> LeaderDestination(const AOperativeCharacter& LeaderRef);
	/** v1.2: the weather is a blizzard (FormationFor's blizzard tight diamond). */
	bool IsFormationBlizzard() const;
	/** The one adapter over FFormationTerrainTracker::Step (rules v2 changes it again). */
	EFormationTerrainClass StepFormationTerrain(const AOperativeCharacter& LeaderRef, float DeltaTime);
	/** Traces the probe rays round the leader into Sectors.ProbeWalls. */
	void ProbeFormationWalls(const AOperativeCharacter& LeaderRef);
	/**
	 * Finds the wall face along TowardsWall from Origin within MaxDistance (its point, outward normal) and its free room each side
	 * (left / right of a man facing it); false when there is none.
	 */
	bool BuildFormationWall(const FVector& Origin, const FVector2D& TowardsWall, float MaxDistance, FVector2D& OutFace, float& OutNormalYaw,
		float& OutRoomLeft, float& OutRoomRight) const;
	/** The wall layout for the members at a wall (Others' roles by minimal travel when bAssignRoles); false when no wall is found. */
	bool ComputeWallLayout(const FVector& Origin, const FVector2D& TowardsWall, float MaxDistance, const FVector2D& ClickedSlot, int32 MemberCount,
		const AOperativeCharacter* FourthMan);
	/** The wall order: the members' arrival (crouch facing out), its end on the leader's next order / Solo / the formation off. */
	void UpdateFormationWallOrder(AOperativeCharacter& LeaderRef, float DeltaTime);
	void EndFormationWallOrder(const TCHAR* Why);
	/** A member of the wall order at his slot and turned: the baseline pose (crouch facing out). */
	void SettleFormationWallPose(AOperativeCharacter& Member);
	/**
	 * Formation sectors part of UpdateFollower: his slot / look this frame, the facing, the speed with the slot's velocity lead.
	 * Returns false when the old slot math should run instead (no role).
	 */
	bool UpdateSectorFollower(FFollowerState& Follower, AOperativeCharacter& Operative, AOperativeCharacter& LeaderRef, bool bLeaderMoving, float DeltaTime,
		float TimeSeconds);
	/** His role this frame (the wall order's, else by his follower slot); unset = none. */
	TOptional<EFormationRole> SectorRoleOf(const FFollowerState& Follower) const;
	const FFormationSlot* FindFrameSlot(EFormationRole Role) const;
	/** The looks this frame (applied to every member at the end of Tick; a member not in it gets none) and the leader's patrol pace. */
	TMap<TWeakObjectPtr<AOperativeCharacter>, float> FrameLooks;
	TWeakObjectPtr<AOperativeCharacter> FramePaceLeader;
	float FramePace = 1.f;
	FFormationSectorState Sectors;
	void UpdateSlotSwap(float DeltaTime);
	void UpdateFollower(FFollowerState& Follower, AOperativeCharacter& LeaderRef, bool bLeaderMoving, float DeltaTime, float TimeSeconds);
	bool IsSlotPathBlocked(const AOperativeCharacter& LeaderRef, const FVector& SlotLocation) const;
	/**
	 * A follower in cover / lying behind it on a group cover order (rev 5g, 2026-10-20): holds it (true) or leaves it to follow the
	 * leader walking away (stands up from a squad-given low stance; true while that clip roots him). Distance = to his slot, cm.
	 */
	/** Squad gathers at the fire (2026-10-12): walks him to the warm source's ring the leader warms at; true when that pull drives him. */
	bool TryWarmPull(FFollowerState& Follower, AOperativeCharacter& Operative, const AOperativeCharacter& LeaderRef);
	bool HoldsCoverOrLeavesIt(FFollowerState& Follower, AOperativeCharacter& Operative, const AOperativeCharacter& LeaderRef, bool bLeaderMoving,
		float Distance);
	/**
	 * Bug 2026-10-23: a formation slot off the navmesh (inside a wall, the generator, a fallen mate's remains crate) was unreachable
	 * for good - while the leader stood still the follower never came back. Returns Slot when it lies on the navmesh, else the
	 * nearest candidate round the leader that does (SquadFormation::ComputeFallbackSlots), else Slot.
	 */
	FVector ResolveReachableSlot(const AOperativeCharacter& LeaderRef, const FVector& Slot) const;

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
	TMap<TWeakObjectPtr<AOperativeCharacter>, FGroupCoverWaitOrder> GroupCoverWaitOrders;
	/** Operatives with a deferred planned shot -> seconds of real-time fight since the release. */
	TMap<TWeakObjectPtr<AOperativeCharacter>, float> DeferredShots;
	ECodexCombatMode LastCombatMode = ECodexCombatMode::None;
	ECodexGamePhase LastPhase = ECodexGamePhase::Exploration;
	/** The fight just ended: the formation leaves the followers where they stand until the leader moves. */
	bool bPostFightHold = false;

	ESquadFirePosture SquadPosture = FirePostureRules::DefaultPosture;
	bool bSquadProvoked = false;
};
