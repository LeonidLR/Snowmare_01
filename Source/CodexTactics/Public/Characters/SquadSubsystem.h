#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Characters/OperativeMovementRules.h"
#include "Characters/SquadFormation.h"
#include "Characters/FirePostureRules.h"
#include "Formation/FormationBypassRules.h"
#include "Formation/FormationCornerRules.h"
#include "Formation/FormationCoverRules.h"
#include "Formation/FormationGuardRules.h"
#include "Formation/FormationMenuRules.h"
#include "Formation/FormationTerrainRules.h"
#include "Formation/FormationTypes.h"
#include "Formation/FormationWallEndRules.h"
#include "Formation/FormationWallGroupRules.h"
#include "Formation/FormationWallRules.h"
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

/** Rules v2.1 bypass (FormationBypassRules::ChooseSide): one decision of the leader's side round a free-standing wall (logs, smokes). */
struct FFormationBypassRecord
{
	/** The wall as the rules saw it (the world's box collision). */
	FFormationWall Wall;
	/** The leader's position and his destination when it was decided. */
	FVector2D From = FVector2D::ZeroVector;
	FVector2D To = FVector2D::ZeroVector;
	FFormationBypassChoice Choice;
	/** Known threats the decision counted (within bypass.threat_memory_s). */
	int32 Threats = 0;
	/** The leader is steered through the choice's corners (false: the path left as the nav mesh made it). */
	bool bSteered = false;
	/** World time of the decision, s. */
	double Time = 0.0;
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
	TOptional<EFormationTerrainClass> GetFormationTerrainClass() const { return Sectors.Terrain.State.Current; }
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

	/**
	 * The slot this frame's formation follow step gave a follower (Z = his feet height): his formation slot, or - when it is off the navmesh -
	 * the stand-in ResolveReachableSlot chose (rules v2.3 contract 13.5: kept slot.min_spacing from the slots given out before him). False
	 * when the follow step did not resolve one for him this frame (the leader, no role, console off, ...). What he really walks to; the bot's
	 * pace check (checks.ts 6) measures the rear man against it.
	 */
	bool GetFormationGivenSlot(const AOperativeCharacter* Operative, FVector& OutSlot) const;

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

	// --- Rules v2.2 wall group (Codex.Formation.Sectors; user playtest 2026-10-25; SquadSubsystemWallGroup.cpp) ---

	/**
	 * The ONE squad-level "re-allocate the wall group" call. The squad stands at a wall with the leader in a real cover slot (the group cover
	 * order's cover switch / wrap / slot path): his own cover facing or corner aim is a FIXED look; FormationWallGroup::Allocate
	 * (FormationCoverRules::AllocateStatic) gives each follower the look that closes the open arc - nobody looks into a wall, one covers the
	 * rear - and the pose to hold it: his cover side along the wall (AOperativeCharacter::SetCoverLookSide), or the back-to-wall pose facing
	 * out (LeaveCoverForWallPose) when the look points away from the wall. NewMembers (the followers taken along) join the group; Why is
	 * logged. Fired by the group cover order (ACodexTacticsPlayerController::OrderGroupCover: the formation wall layout - whose looks are
	 * the user's wall rule already -, the cover switch, the wrap), the leader's arrival (the switch's run-in, the wrap's goal), a member's
	 * (re-)entry (UpdateGroupCoverWait's release, the prone fallbacks, any cover entry of a member). The allocation runs once the leader is
	 * in his slot and again while the group settles. Group mode only (no Solo / box selection, the exploration formation); ends with the
	 * leader's next order, the leader out of cover, Solo, a fight, the console 0. Codex.Formation.Sectors 0: nothing (the old facing).
	 */
	void ReallocateWallGroup(AOperativeCharacter* GroupLeader, const TArray<AOperativeCharacter*>& NewMembers, const TCHAR* Why);

	/** AOperativeCharacter::EnterCover: the wall group's leader / a member entered a slot (the switch / wrap arrival, a re-entry). */
	void NotifyWallGroupCoverEntered(AOperativeCharacter* Operative, bool bSwitchArrival, bool bWrapArrival);

	/** The wall group is in force (ReallocateWallGroup). */
	bool HasWallGroup() const { return WallGroup.bActive; }
	/** Operative's look and pose from the last wall-group allocation; false when he has none. */
	bool GetWallGroupLook(const AOperativeCharacter* Operative, float& OutYaw, EWallGroupLook& OutKind) const;
	/** How many allocations the wall group in force has run (smokes). */
	int32 GetWallGroupAllocationCount() const { return WallGroup.Allocations; }
	/** How many members' (re-)entries (into the slot after the leader, a prone fallback) re-allocated the wall group in force (smokes). */
	int32 GetWallGroupReentryCount() const { return WallGroup.Reentries; }

	/** Rules v2.2 wall slide (FormationWallRules::Slide) in progress: the wall layout side-steps along its wall. */
	bool IsFormationSliding() const { return Sectors.bSlide; }
	/** Wall slides started since the level began (smokes). */
	int32 GetFormationSlideCount() const { return Sectors.SlideCount; }
	/** Bug A 2026-10-26: the held wall layout is the corner stack at a free wall end (or the slide runs into one). */
	bool IsFormationCornerStack() const { return Sectors.bCornerStack || (Sectors.bSlide && Sectors.bSlideStack); }
	/**
	 * Bug C 2026-10-26 (the squad's guide lines): the squad in group mode stands in the formation wall layout and Destination lies along its
	 * wall or round its end (within slide.max_off_wall of the wall's body) -> the paths a move there would take, one polyline per member,
	 * the leader first: a wall slide (to the corner stack too) - each member's line from where he stands to his slide target; else the
	 * leader's navmesh path (round the wall's end). False (none) otherwise.
	 */
	bool PreviewFormationMove(const FVector& Destination, TArray<TArray<FVector>>& OutPaths) const;
	/**
	 * The squad's corner peek (user bug report 2026-10-26: "one operative at a wall shows the peek silhouette at its corner and peeks on a
	 * click; the squad in formation does not"): the squad in group mode stands in the formation wall layout (nobody in a cover slot, the
	 * leader not in cover - else the single operative's peek) -> who peeks round the end of its wall at EdgeOnFace (the exposed end on the
	 * face line; FormationWallEnd::PlanSquadPeek: the corner stack's point man - the member nearest that end, K second - else the member
	 * nearest it). False when the squad cannot peek as a group. SquadSubsystemFormationPeek.cpp.
	 */
	bool PlanFormationCornerPeek(const FVector& EdgeOnFace, AOperativeCharacter*& OutPeeker, FFormationSquadPeek& OutPlan) const;
	/**
	 * The squad's corner peek order (the click on the end of the squad's wall): Peeker - PlanFormationCornerPeek's - takes CornerSlot (his own
	 * cover order; the corner stand point) and, once in cover, the manual corner peek round that end (AOperativeCharacter::OrderCornerPeek, the
	 * single operative's order); the others form the v2.4 corner stack there (the leader's move into it: the wall slide, K second, the rear man
	 * back along the wall) or, without a stack, hold. Over with the leader's next order, a leader switch or the end of the wall layout (the
	 * peeker then follows the leader out of his cover as any follower in cover). False: nothing was ordered.
	 */
	bool OrderFormationCornerPeek(AOperativeCharacter* Peeker, const FCoverSlot& CornerSlot, const FVector& EdgeOnFace);
	/** The squad's corner peek in force: its peeker (null when none) - smokes. */
	AOperativeCharacter* GetFormationPeeker() const { return Sectors.Peek.Peeker.Get(); }
	/** Squad corner peeks whose peek order was given (the peeker reached his corner) since the level began - smokes. */
	int32 GetFormationPeekCount() const { return Sectors.PeekCount; }
	/** The last coverage allocation on the move (FormationCoverRules::Allocate): its rear guard; false when the allocator did not run. */
	bool GetFormationRearGuard(EFormationRole& OutRole) const;

	// --- Rules v2.1 in the runtime (Codex.Formation.Sectors + Codex.Formation.V21; SquadSubsystemFormationGuard.cpp, SquadSubsystemFormationBypass.cpp) ---

	/**
	 * The formation asked for this frame: the hold-G menu's manual choice (SetFormationMenuChoice), else the terrain's (ChooseFormation).
	 * GetFormationKind is the formation in EFFECT - it differs while the wall guard falls back (FormationGuardRules::Resolve: the mirror
	 * echelon, or the trail column along the leader's path; the request comes back by itself after classify.hold_distance of path).
	 */
	EFormationKind GetFormationRequested() const { return Sectors.Requested; }
	/** The wall guard's level this frame (the request / its mirror echelon / the trail column). */
	EFormationGuardLevel GetFormationGuardLevel() const { return Sectors.Guard.Level; }
	/** The slots are the trail column's (FormationShapeRules::TrailSlots on the leader's recorded path) - the guard's level 2 or the turn fold. */
	bool IsFormationTrail() const { return Sectors.bTrail; }
	/**
	 * HUD line (English, labels.ts guardHudText): the formation in effect ("Diamond"), or "Echelon left -> Column (wall)" while the guard
	 * falls back; empty while the formation sectors do not drive the squad.
	 */
	FString GetFormationHudText() const;

	/**
	 * The hold-G menu's choice (FormationMenuRules; unset = Auto: the terrain decides). A manual formation holds until Auto (too few
	 * members -> Auto, FormationMenuRules::Resolve); "All forward" = the terrain's formation with everybody looking at 12. The radial
	 * menu itself is not built yet: the dev console Codex.Formation.Menu sets it.
	 */
	void SetFormationMenuChoice(const TOptional<EFormationMenuItem>& Choice);
	TOptional<EFormationMenuItem> GetFormationMenuChoice() const { return Sectors.MenuChoice; }

	/**
	 * Rules v2.1 bypass "B": a spot where a threat was seen / heard just now (the smokes' threat points; the squad's own sight and
	 * hearing feed the same list through UTacticalSightSubsystem::GetSquadKnownEnemies). Known for bypass.threat_memory_s.
	 */
	void ReportFormationThreat(const FVector& Location);
	/** Forgets the reported threats (smokes between runs). */
	void ClearFormationThreats() { Sectors.ReportedThreats.Reset(); }
	/** The last bypass decision (the leader's side round a free-standing wall); false when none was taken since the level began. */
	bool GetLastFormationBypass(FFormationBypassRecord& OutRecord) const;
	/** Bypass decisions taken since the level began (smokes). */
	int32 GetFormationBypassCount() const { return Sectors.BypassCount; }
	/** The leader is being steered round a wall's end (the bypass waypoints). */
	bool IsFormationBypassSteering() const { return Sectors.Bypass.bActive; }

	/** The corner watch this frame (FormationCornerRules::Watch: the member looking round a wall end and his look); empty when none. */
	const TArray<FFormationCornerLook>& GetFormationCornerLooks() const { return Sectors.CornerLooks; }
	/** The world walls the v2.1 rules see round the leader (box collision -> FormationGuardRuntime::WallsFromBox). */
	const TArray<FFormationWall>& GetFormationWorldWalls() const { return Sectors.WorldWalls; }
	/**
	 * Rules v2.4 (user bug report 2026-10-26): the wall layout in force stands at a wall END and its looks were spread over the open arc round
	 * it (FormationWallRules::EndLooks); OutBasePct / OutPct = the open-arc share the face looks / the spread looks hold (smokes, the HUD log).
	 */
	bool IsFormationWallEndSpread(float& OutBasePct, float& OutPct) const
	{
		OutBasePct = Sectors.WallEndBasePct;
		OutPct = Sectors.WallEndPct;
		return Sectors.bWallLayoutValid && Sectors.bWallEndSpread;
	}
	/**
	 * Rules v2.5 (user rule 2026-10-26, the covered approach to the wall): this frame's approach looks (FormationWallRules::Approach) while the
	 * squad walks into the wall layout - the leader runs in and looks along the wall, the rear cover, the flank men; empty once everyone is at
	 * his slot (the plain layout) or with Codex.Formation.Approach 0.
	 */
	const TArray<FFormationApproachLook>& GetFormationApproachLooks() const { return Sectors.ApproachLooks; }
	/** The wall layout in force (slots and looks by role; empty when none) - smokes. */
	const TArray<FFormationSlot>& GetFormationWallSlots() const { return Sectors.WallSlots; }

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
		/** Rules v2.2: the rear guard's last formation move direction while he catches up (FormationRuntime::RearGuardCatchUpLook). */
		float CatchUpMoveYaw = 0.f;
		bool bCatchUp = false;
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
		/** The leader's position last tick (the path the v2 hysteresis counts). */
		FVector2D LastLeaderPos = FVector2D::ZeroVector;
		bool bLastLeaderPosValid = false;
		/** The raw class of the last probe (FormationRuntime::AcceptProbe). */
		TOptional<EFormationTerrainClass> LastProbeRaw;
		int32 MemberCount = 1;
		bool bActiveThisFrame = false;
		/** The leader's position at the last heading update (a jump = a teleport snaps the heading to his facing). */
		FVector2D HeadingTickPos = FVector2D::ZeroVector;
		float HeadingTickYaw = 0.f;
		bool bHeadingTickPosValid = false;
		/** Codex.Formation.Sectors last tick (a switch snaps the heading). */
		bool bWasEnabled = true;
		/** This frame's slots and look yaws by role (Slots / LookYaw, or the wall layout). */
		TArray<FFormationSlot> FrameSlots;
		/** The wall layout in force (the automatic one on arrival or the order's). */
		bool bWallLayoutValid = false;
		TArray<FFormationSlot> WallSlots;
		/** Rules v2.4: WallSlots' looks were spread over a wall end's open arc (EndLooks); the shares the face / the spread looks hold, %. */
		bool bWallEndSpread = false;
		/** Rules v2.5: the covered approach into the wall layout - its held roles (unset = not begun) and this frame's looks (empty = none). */
		TOptional<FFormationApproachState> Approach;
		TArray<FFormationApproachLook> ApproachLooks;
		float WallEndBasePct = 100.f;
		float WallEndPct = 100.f;
		FVector2D WallAnchor = FVector2D::ZeroVector;
		float WallNormalYaw = 0.f;
		/** The group cover order in group mode as the wall layout (OrderFormationWall). */
		bool bWallOrder = false;
		TWeakObjectPtr<AOperativeCharacter> WallOrderLeader;
		uint32 WallOrderSerial = 0;
		float WallOrderSeconds = 0.f;
		TMap<TWeakObjectPtr<AOperativeCharacter>, EFormationRole> WallOrderRoles;
		/** The wall order's members crouch at their slots (the group cover order); false = a held wall layout after a slide from the automatic one. */
		bool bWallOrderCrouch = true;
		/** The leader's move order serial last tick (a new order while the wall layout stands may be a slide). */
		uint32 LastLeaderSerial = 0;
		bool bLastLeaderSerialValid = false;
		/** A new leader order seen, decided once his move runs (a stand-up clip defers it: its destination is not known before). */
		bool bLeaderOrderPending = false;
		float LeaderOrderPendingSeconds = 0.f;
		/** Rules v2.2 wall slide (FormationWallRules::Slide): the wall (its face line along the measured room), the layout it started from. */
		bool bSlide = false;
		FFormationWall SlideWall;
		TArray<FFormationSlot> SlideStartSlots;
		FVector2D SlideDestination = FVector2D::ZeroVector;
		FVector2D SlideLeaderTarget = FVector2D::ZeroVector;
		/** The leader's serial of the slide's own move (his next order ends / restarts it). */
		uint32 SlideSerial = 0;
		float SlideSeconds = 0.f;
		int32 SlideCount = 0;
		/** The slide came from the group cover order's wall layout (it ends crouched again) or the automatic one. */
		bool bSlideFromOrder = false;
		/** The 4th / the pair's partner ends the slide on the right end (keeps it when it has room, contract 11.5). */
		bool bSlideEndRight = true;
		/** Bug A 2026-10-26: this slide runs to a free wall end (FormationWallEnd::FindStackEnd) and ends in the corner stack. */
		bool bSlideStack = false;
		FFormationStackEnd SlideStackEnd;
		/** The held wall layout is the corner stack at a free wall end (the point man peeks round it); its end. */
		bool bCornerStack = false;
		FFormationStackEnd CornerStackEnd;
		/** Rules v2.2 look probes round each follower's slot (FormationCoverRules::Allocate's walls next to the slots). */
		TArray<FFormationWall> LookWalls;
		/** The last coverage allocation on the move: its rear guard. */
		TOptional<EFormationRole> RearGuard;

		// --- Rules v2.1 (SquadSubsystemFormationGuard.cpp / SquadSubsystemFormationBypass.cpp) ---
		/** The formation asked for (menu / terrain) and the wall guard's state over frames. */
		EFormationKind Requested = EFormationKind::Wedge;
		FFormationGuardState Guard;
		/** The slots are the trail column's this frame. */
		bool bTrail = false;
		/** Rules v2.3 part 7: the trail's direction at each trail slot this frame (the allocator's per-place Travel). */
		TArray<FFormationRoleYaw> TrailTravel;
		/** Rules v2.3 part 7: the allocator's over-the-shoulder members on the move (ClearLook's Limit; FormationGuardRuntime::ShoulderLimit). */
		TArray<FFormationRoleYaw> ShoulderLooks;
		/** The leader's path this tick (StepFormationTerrain), cm. */
		float LastStepDistance = 0.f;
		/** The leader's recorded path, oldest first (FormationGuardRuntime::PushTrail). */
		TArray<FVector2D> Trail;
		/** The formation (+ trail flag) the followers' places were last assigned for: a change re-assigns them (contract 10.6). */
		EFormationKind PlacesFormation = EFormationKind::Wedge;
		bool bPlacesTrail = false;
		bool bPlacesKeyValid = false;
		/** The world's box walls round the leader, where they were gathered and how long ago. */
		TArray<FFormationWall> WorldWalls;
		FVector2D WorldWallsCenter = FVector2D::ZeroVector;
		float WorldWallsAge = 0.f;
		bool bWorldWallsValid = false;
		/** Slicing the pie (FormationCornerRules::Watch). */
		FFormationCornerState Corner;
		TArray<FFormationCornerLook> CornerLooks;
		/** The hold-G menu's choice (unset = Auto) and its "All forward". */
		TOptional<EFormationMenuItem> MenuChoice;
		bool bAllForward = false;
		/** The bypass steering of the leader's own order through the corners of the chosen side. */
		struct FBypassSteer
		{
			bool bActive = false;
			/** The leader's move order serial the bypass belongs to (another order drops it). */
			uint32 Serial = 0;
			TArray<FVector2D> Waypoints;
			int32 Index = 0;
		};
		FBypassSteer Bypass;
		/** Threat spots reported (ReportFormationThreat) and when, world time. */
		TArray<TPair<FVector2D, double>> ReportedThreats;
		FFormationBypassRecord LastBypass;
		int32 BypassCount = 0;
		/** The squad's corner peek (OrderFormationCornerPeek; SquadSubsystemFormationPeek.cpp). */
		struct FSquadPeek
		{
			TWeakObjectPtr<AOperativeCharacter> Peeker;
			/** The leader who ordered it (a switch ends it). */
			TWeakObjectPtr<AOperativeCharacter> Leader;
			/** The wall's end on its face line and the side it lies on from the corner slot. */
			FVector EdgeOnFace = FVector::ZeroVector;
			ECoverFacing Side = ECoverFacing::Right;
			/** The leader's move order serial the peek belongs to (another order ends it; the squad's move into the stack is adopted once). */
			uint32 LeaderSerial = 0;
			/** The peek order is still due (he walks to the corner). */
			bool bPending = false;
			/** The leader's move into the corner stack has not become the slide / the stack yet. */
			bool bAwaitStack = false;
			float Seconds = 0.f;
		};
		FSquadPeek Peek;
		int32 PeekCount = 0;
	};

	/** Rules v2.2 wall group (ReallocateWallGroup; SquadSubsystemWallGroup.cpp). */
	struct FWallGroupState
	{
		bool bActive = false;
		TWeakObjectPtr<AOperativeCharacter> Leader;
		/** The leader's move order serial when the group was (re)started: another one = his next order ends it. */
		uint32 LeaderSerial = 0;
		TArray<TWeakObjectPtr<AOperativeCharacter>> Members;
		/** Members this group took out of their cover slot into the back-to-wall pose. */
		TArray<TWeakObjectPtr<AOperativeCharacter>> WallPose;
		TMap<TWeakObjectPtr<AOperativeCharacter>, FWallGroupLookResult> Looks;
		bool bDirty = false;
		float RecheckSeconds = 0.f;
		float LeaderOutSeconds = 0.f;
		float Seconds = 0.f;
		int32 Allocations = 0;
		int32 Reentries = 0;
		FString Why;
	};
	FWallGroupState WallGroup;
	/** The wall group per tick: its end, the members' state, the allocation (dirty / every RecheckSeconds) and its poses. */
	void UpdateWallGroup(float DeltaTime);
	void EndWallGroup(const TCHAR* Why);
	/** The wall group's member re-entered (his slot after the leader, a prone fallback): ReallocateWallGroup when he is a member. */
	void ReallocateWallGroupForMember(AOperativeCharacter& Member, const TCHAR* Why);
	/** Runs FormationWallGroup::Allocate for the group as it stands and applies the cover sides / wall poses / prone looks. */
	void AllocateWallGroupNow();
	/** UpdateFollower: a member the wall group holds in the back-to-wall pose stands still facing his look (true = handled). */
	bool HoldsWallGroupPose(FFollowerState& Follower, AOperativeCharacter& Operative);
	/** Thin wall segments from rays round Positions at Height above their feet (look / wall-group probes), cm. */
	TArray<FFormationWall> TraceLookWalls(const TArray<FVector>& Positions, float HeightAboveFeet, int32 Rays, float Length) const;

	/** Rules v2.2 wall slide: a new leader order from the wall layout - starts the slide when its destination lies along the same wall. */
	bool TryStartFormationSlide(AOperativeCharacter& LeaderRef, bool bFromOrder);
	/** The slide per tick (targets / looks by the leader's progress); on arrival the wall layout again. */
	void UpdateFormationSlide(AOperativeCharacter& LeaderRef, float DeltaTime);
	void EndFormationSlide(AOperativeCharacter& LeaderRef, bool bArrived, const TCHAR* Why);
	/** The wall layout round the leader at his wall (the face behind his back: -WallNormalYaw); keeps the roles of an order. */
	bool HoldWallLayoutAt(AOperativeCharacter& LeaderRef, float WallNormalYaw, bool bCrouch, const TOptional<bool>& KeepEndRight);
	/**
	 * Bug A 2026-10-26: the corner stack held at a free wall end (FormationWallEnd::StackSlots from StartSlots) as the wall order the squad
	 * keeps until the leader's next order (crouched when bCrouch); the roles of an order are kept, else the follower slots' roles.
	 */
	void HoldCornerStack(AOperativeCharacter& LeaderRef, const TArray<FFormationSlot>& StartSlots, const FFormationStackEnd& End, bool bCrouch);
	/**
	 * Bug D 2026-10-26: the leader switched while the wall layout stands - the wall order (and a running slide) goes on with the new leader:
	 * the layout again round him at the same wall (K in the middle), the order's crouch kept, the places by minimal travel.
	 */
	void PassWallOrderToLeader(AOperativeCharacter& NewLeader);
	/** The held wall layout's roles (WallOrderRoles) for the followers by minimal travel to Slots' places (K first; a wall order's own roles kept). */
	void AssignWallRolesByTravel(const TArray<FFormationSlot>& Slots);
	/** Bug B 2026-10-26: Slots (FormationWallRules::Layout's, K first) fitted to the world at the wall (FormationWallEnd::FitLayout). */
	TArray<FFormationSlot> FitWallLayoutToWorld(const TArray<FFormationSlot>& Slots, float NormalYaw, float FeetZ) const;
	/** The world walls round Center within HalfExtentCm at the formation probe height over FeetZ (GatherFormationWalls). */
	TArray<FFormationWall> GatherWallsAround(const FVector2D& Center, float HalfExtentCm, float FeetZ) const;
	/** The squad's corner peek per tick: the peek order once the peeker is in cover at the corner; its end (SquadSubsystemFormationPeek.cpp). */
	void UpdateFormationCornerPeek(AOperativeCharacter& LeaderRef, float DeltaTime);
	void EndFormationCornerPeek(const TCHAR* Why);
	/** Operative is the squad corner peek's peeker (the formation leaves him to his corner: no follow, no wall pose, no sector look). */
	bool IsFormationPeeker(const AOperativeCharacter* Operative) const { return Operative && Sectors.Peek.Peeker.Get() == Operative; }
	/** The patrol / halt looks of FrameSlots from FormationCoverRules::Allocate (rules v2.2); false when the allocator does not apply. */
	bool AllocateFormationLooks(const AOperativeCharacter& LeaderRef, const FVector2D& LeaderPos);
	/**
	 * Places, not fixed slots (contract 10.6 / 11.6): the followers' slot indices (= roles) re-assigned by minimal travel to this frame's
	 * places when that saves FormationRuntime::ReassignGainCm (after a turn the man nearest the rear place becomes the rear man); rules
	 * v2.1: also whenever the formation in effect changes (a fold, the mirror, the trail column on / off), whatever the saving.
	 */
	void ReassignFormationPlaces();

	// --- Rules v2.1 in the runtime (SquadSubsystemFormationGuard.cpp) ---
	/** The leader's trail (PushTrail; a teleport starts it afresh) and the world walls round him (refreshed as he walks). */
	void UpdateFormationTrailAndWalls(const AOperativeCharacter& LeaderRef, float DeltaTime);
	/**
	 * Box-collision walls (FormationGuardRuntime::WallsFromBox) of the blocking world geometry crossing the horizontal plane PlaneZ inside
	 * the rectangle Center +- HalfExtents turned by Yaw (static and dynamic world objects; pawns and boxes not standing upright left out).
	 */
	TArray<FFormationWall> GatherFormationWalls(const FVector2D& Center, const FVector2D& HalfExtents, float Yaw, float PlaneZ) const;
	/**
	 * The leader's path ahead for the guard / turn fold: [his position, the rest of his nav path, the bypass's remaining waypoints, his
	 * destination]; [his position] when he is not walking an order of his own.
	 */
	TArray<FVector2D> LeaderPathAhead(const AOperativeCharacter& LeaderRef, bool bLeaderMoving) const;
	/** The v2.1 guard step: Sectors.Requested -> Sectors.Formation (in effect) and Sectors.bTrail (FormationGuardRules::Resolve / Effective). */
	void StepFormationGuard(const AOperativeCharacter& LeaderRef, bool bLeaderMoving, int32 Count);
	/** The trail column's slots and looks this frame (FormationShapeRules::TrailSlots, LookYaw column by each man's room). */
	void BuildTrailFrameSlots(const FVector2D& LeaderPos, int32 Count);
	/** Slicing the pie: FormationCornerRules::Watch on this frame's places; the watchers' looks replace their slot yaws (walk mode). */
	void ApplyFormationCornerWatch(const FVector2D& LeaderPos);
	/** v2.1.1 (user "6A"): Look through FormationSectorRules::ClearLook unless the member is exempt (FormationGuardRuntime::ShouldClearLook). */
	float ClearFormationLook(const AOperativeCharacter& Member, EFormationRole Role, bool bLeader, float Look);
	/**
	 * Rules v2.3 (contract 13.1): each member's last ClearLook result - ClearLook's held look (hysteresis) the next frame; reset when ClearLook
	 * does not apply to him (FormationGuardRuntime::RememberClearLook) or his look is not computed in a frame (AgeClearHeld each tick).
	 */
	TMap<TWeakObjectPtr<AOperativeCharacter>, FFormationClearHeld> ClearHeld;

	// --- Rules v2.1 bypass "B" (SquadSubsystemFormationBypass.cpp) ---
	/** A new own order of the leader: the side round the first free-standing wall on his way (FormationBypassRules::ChooseSide), steered. */
	void PlanFormationBypass(AOperativeCharacter& LeaderRef);
	/** The steering per tick: the next waypoint once near the current one, the destination after the last; dropped by a new order. */
	void UpdateFormationBypass(AOperativeCharacter& LeaderRef);
	void EndFormationBypass(const TCHAR* Why);
	/** The threats the squad knows now (reported + seen / heard enemies, UTacticalSightSubsystem), with their ages. */
	TArray<FFormationThreat> GatherFormationThreats() const;

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
	EFormationTerrainClass StepFormationTerrain(const AOperativeCharacter& LeaderRef);
	/** Traces the probe rays round the leader into Sectors.ProbeWalls. */
	void ProbeFormationWalls(const AOperativeCharacter& LeaderRef);
	/**
	 * Finds the wall face along TowardsWall from Origin within MaxDistance (its point, outward normal) and its free room each side
	 * (left / right of a man facing it); false when there is none.
	 */
	bool BuildFormationWall(const FVector& Origin, const FVector2D& TowardsWall, float MaxDistance, FVector2D& OutFace, float& OutNormalYaw,
		float& OutRoomLeft, float& OutRoomRight, float RoomMaxCm = 400.f) const;
	/** The wall layout for the members at a wall (Others' roles by minimal travel when bAssignRoles); false when no wall is found. */
	bool ComputeWallLayout(const FVector& Origin, const FVector2D& TowardsWall, float MaxDistance, const FVector2D& ClickedSlot, int32 MemberCount,
		const AOperativeCharacter* FourthMan);
	/**
	 * Rules v2.4 (user bug report 2026-10-26, contract 14): at a wall END the wall layout's looks (Sectors.WallSlots, just laid out) are spread
	 * over the open arc round it - FormationWallRules::EndLooks over the world's box walls round the layout plus the traced face measured out to
	 * wall_end.probe_dist (so a long wall of complex collision still reads as a face); computed once per layout (no flicker). Codex.Formation.WallEnd 0
	 * = the face looks everywhere (v2.3).
	 */
	void ApplyFormationWallEnd(const FVector& Origin, const FVector2D& TowardsWall, float MaxDistance, const TArray<EFormationRole>& FixedRoles = TArray<EFormationRole>());
	/**
	 * Rules v2.5 (user rule 2026-10-26, contract 15): the covered approach into the wall layout - FormationWallRules::Approach over the places'
	 * actual positions (the leader K, the followers by their wall roles) and the world walls; its looks replace this frame's slot looks
	 * (unset = the runner faces his run). Codex.Formation.Approach 0 = everyone walks in facing his travel (v2.4).
	 */
	void ApplyFormationApproach(const AOperativeCharacter& LeaderRef);
	/** Rules v2.5: this follower role's approach duty this frame (unset = none). */
	TOptional<EFormationApproachDuty> FormationApproachDuty(EFormationRole Role) const;
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
	/** The slot each follower's sector follow step resolved this frame (GetFormationGivenSlot). */
	TMap<TWeakObjectPtr<AOperativeCharacter>, FVector2D> FrameGivenByMember;
	TWeakObjectPtr<AOperativeCharacter> FramePaceLeader;
	float FramePace = 1.f;
	FFormationSectorState Sectors;
	/** Formation sectors (bug 2026-10-26): each former leader's own facing and the world time he stopped leading (SetLeader). */
	TMap<TWeakObjectPtr<AOperativeCharacter>, TPair<float, double>> LeaderFacingMemory;
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
	 * nearest candidate round the leader that does (SquadFormation::ComputeFallbackSlots), else Slot. bLeaderSide (rules v2.1, contract 7): a
	 * candidate must also be on the leader's side - nothing blocks the straight line from him to it (a walkable point behind a wall splits
	 * the squad).
	 */
	FVector ResolveReachableSlot(const AOperativeCharacter& LeaderRef, const FVector& Slot, bool bLeaderSide = false,
		const TOptional<FVector>& Here = TOptional<FVector>()) const;

	/**
	 * Rules v2.3 (contract 13.5): the slots given out this formation tick (the leader's position, then each resolved slot): a stand-in for
	 * an unwalkable slot keeps slot.min_spacing from them (SquadFormation::ChooseFallbackSlot). Reset by TickFormationFollow.
	 */
	mutable TArray<FVector2D> FrameGivenSlots;
	/** Rules v2.3 (contract 13.5 "and from the walkable slots"): this frame's walkable formation slots into FrameGivenSlots before the follow loop. */
	void SeedFrameGivenSlots(const AOperativeCharacter& LeaderRef);

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
