#pragma once

#include "CoreMinimal.h"
#include "PlayerCommands.generated.h"

/**
 * What a logical player command was about (who / what / where). Filled by the input handler that issued it; every field
 * is optional.
 */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FPlayerCommandContext
{
	GENERATED_BODY()

	/** The operative that carries the order out (usually the leader / the active turn-based unit). */
	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Commands")
	TObjectPtr<AActor> Operative = nullptr;

	/** The object / enemy the order targets. */
	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Commands")
	TObjectPtr<AActor> Target = nullptr;

	/** World point of the order (move / placement / cover slot), zero when none. */
	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Commands")
	FVector Location = FVector::ZeroVector;

	/** Alt held: the whole squad. */
	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Commands")
	bool bSquadWide = false;

	/** Planned in the tactical pause (runs on the release). */
	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Commands")
	bool bPlanned = false;

	/**
	 * > 0: this is not a new command but a HOLD tick of a continuous one (a pan / drag that is still going on): the seconds
	 * since the previous tick (PlayerCommands::ReportHold). The tutorial adds them up for "complete.hold_s"; it never counts
	 * a tick as a new use.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Commands")
	float HeldSeconds = 0.f;
};

/**
 * Details of a gameplay event (GameplayEvents::RaiseWithContext -> UCodexEventBus::OnGameplayEventDetailed): who, what,
 * where, and a short kind ("High" / "Low" cover). Every field is optional.
 */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FGameplayEventContext
{
	GENERATED_BODY()

	/** The operative / object the event is about (also the Subject of OnGameplayEvent). */
	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Events")
	TObjectPtr<AActor> Subject = nullptr;

	/** Stable id of the subject (actor name) - the tutorial counts distinct operatives by it. */
	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Events")
	FName SubjectId;

	/** The other actor involved (the cover's wall / barricade, the trap, ...). */
	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Events")
	TObjectPtr<AActor> Target = nullptr;

	/** Id of Target or of the place (wall actor name, empty when none). */
	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Events")
	FName TargetId;

	/** Short kind of the event: cover "High" / "Low", corner side "Left" / "Right". */
	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Events")
	FName Kind;

	/** World point (the cover slot), zero when none. */
	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Events")
	FVector Location = FVector::ZeroVector;
};

/**
 * One row of the command registry: id -> default key label, section and description. THE table the F1 Controls screen
 * (UCodexControlsScreen) is generated from, so the registry and the screen never diverge.
 */
struct CODEXTACTICS_API FPlayerCommandInfo
{
	FName Id;
	/** Default binding as shown on hint cards, e.g. "B", "Alt+V", "Ctrl+Click". */
	const TCHAR* KeyLabel = TEXT("");
	/** One of PlayerCommands::GetSections() (the Controls screen section it is listed under). */
	const TCHAR* Section = TEXT("");
	const TCHAR* Description = TEXT("");
};

/**
 * Logical player commands (UX brief 03 §2.2 W1 / W2): every key / click handler that carries an order out reports ONE id
 * through PlayerCommands::Report (the choke point). The tutorial counts these ids, never raw keys, so a later remap or a
 * gamepad changes nothing in the step data. Reporting never changes what the handler does.
 * Ids are "Section.Verb" names; tutorial step data may use a trailing "*" wildcard ("Stance.*", "Deploy.*").
 * UE-only (no Godot reference: Godot had no tutorial layer).
 */
namespace PlayerCommands
{
	// --- Camera ---------------------------------------------------------------------------------------------------
	// Continuous inputs (pans, drags) report ONCE per action when it starts, then ReportHold ticks while it lasts, so the
	// tutorial can ask for "complete.hold_s" of real use. Discrete inputs (Q / E, wheel) report once per press / notch.
	/** W / A / S / D: a keyboard pan starts ("Camera.Pan.Keys"). */
	extern CODEXTACTICS_API const FName CameraPanKeys;
	/** The cursor pushed to the screen edge: an edge pan starts ("Camera.Pan.Edge"). */
	extern CODEXTACTICS_API const FName CameraPanEdge;
	/** MMB drag: a drag pan starts ("Camera.Pan.Drag"). */
	extern CODEXTACTICS_API const FName CameraPanDrag;
	/** Q / E: one 45 degree camera step per press ("Camera.Rotate.Keys"). */
	extern CODEXTACTICS_API const FName CameraRotateKeys;
	/** RMB drag: a free-look drag starts ("Camera.Rotate.Mouse"). */
	extern CODEXTACTICS_API const FName CameraRotateMouse;
	/** Mouse wheel: one notch of zoom in / out ("Camera.Zoom"). */
	extern CODEXTACTICS_API const FName CameraZoom;
	/**
	 * M inside a building (a camera zone with a fixed interior camera): the interior camera <-> the normal top-down view
	 * ("Camera.InteriorView"; rev 5g, never locked).
	 */
	extern CODEXTACTICS_API const FName CameraInteriorView;
	/** A click on an EXIT arrow at a door of the building whose interior camera shows: walk out ("Order.ExitBuilding"; never locked). */
	extern CODEXTACTICS_API const FName ExitBuilding;
	/** Minimap (2026-10-18, docs/design/MINIMAP.md): F2 / the panel header - the top-right panel MAP <-> MESSAGES ("Map.Panel"). */
	extern CODEXTACTICS_API const FName MapPanel;
	/** Tab (outside turn-based combat) / F3: the full-screen map opens / closes ("Map.Full"). */
	extern CODEXTACTICS_API const FName MapFull;
	/** LMB on the map: the camera moves there ("Map.Camera"). */
	extern CODEXTACTICS_API const FName MapCamera;
	/** RMB on the map: the selection walks there (registry row only - reported as Order.Move by the ground-click path). */
	extern CODEXTACTICS_API const FName MapMove;
	/** The wheel over the map zooms it ("Map.Zoom"). */
	extern CODEXTACTICS_API const FName MapZoom;

	// --- Orders and selection -----------------------------------------------------------------------------------
	/** LMB on the ground: the leader (or the selected group) walks there (planned in the tactical pause). */
	extern CODEXTACTICS_API const FName Move;
	/** Double click on the ground: sprint there. */
	extern CODEXTACTICS_API const FName Sprint;
	/** Shift + click on the ground: the leader watches that sector. */
	extern CODEXTACTICS_API const FName WatchSector;
	/** Keys 1-4 / a click on an operative / the action-bar slot: that operative leads. */
	extern CODEXTACTICS_API const FName SelectMember;
	/** LMB drag: box selection of a group. */
	extern CODEXTACTICS_API const FName SelectBox;
	/** LMB on an object: the leader walks up to it (its action menu opens on arrival). */
	extern CODEXTACTICS_API const FName Interact;

	// --- Stance and squad ---------------------------------------------------------------------------------------
	/** Z / C / V: the selected operative stands / crouches / goes prone. */
	extern CODEXTACTICS_API const FName StanceStand;
	extern CODEXTACTICS_API const FName StanceCrouch;
	extern CODEXTACTICS_API const FName StanceProne;
	/** Alt + Z / C / V: the whole squad. */
	extern CODEXTACTICS_API const FName SquadStanceStand;
	extern CODEXTACTICS_API const FName SquadStanceCrouch;
	extern CODEXTACTICS_API const FName SquadStanceProne;
	/** B: solo on (the others hold their spots). */
	extern CODEXTACTICS_API const FName SoloOn;
	/** B again: solo off - the squad regroups into the formation. */
	extern CODEXTACTICS_API const FName Regroup;
	/** T: the leader guards his spot / rejoins the formation. */
	extern CODEXTACTICS_API const FName Guard;
	/** Ctrl + T: Commander Mode (autonomous squad combat) on / off. */
	extern CODEXTACTICS_API const FName Autonomy;
	/** Shift + RMB: hold this object / spot at all costs. */
	extern CODEXTACTICS_API const FName HoldLine;
	/** , / . / slash: fire posture passive / defensive / aggressive (Alt = squad, see FPlayerCommandContext::bSquadWide). */
	extern CODEXTACTICS_API const FName PosturePassive;
	extern CODEXTACTICS_API const FName PostureDefensive;
	extern CODEXTACTICS_API const FName PostureAggressive;
	/**
	 * Stealth lure (user decisions 2026-10-18): 5 / the action bar WHISTLE - the selected operative whistles (a noise at his
	 * spot); reported when he whistled. Only exploring undetected; a tutorial lock target (Demo 01: locked until a later lesson).
	 */
	extern CODEXTACTICS_API const FName StealthWhistle;
	/** 6 / the action bar THROW: the stone throw aim starts ("Stealth.Throw" is reported when the stone is thrown). */
	extern CODEXTACTICS_API const FName StealthThrow;

	// --- Cover ----------------------------------------------------------------------------------------------------
	/** First click on a wall: the cover ghost (preview). */
	extern CODEXTACTICS_API const FName CoverPreview;
	/** Second click on the ghost: the operative takes the cover slot (wall hug / low cover). */
	extern CODEXTACTICS_API const FName CoverEnter;
	/** Reported with CoverEnter when the slot has an exposed corner (fire from the corner). */
	extern CODEXTACTICS_API const FName CoverCorner;
	/** A click along the operative's own wall: shimmy. */
	extern CODEXTACTICS_API const FName CoverShimmy;
	/**
	 * In high cover, a click on the exposed END (edge) of his own wall: he shimmies to that corner and holds the corner-aim pose
	 * (leaning out, rifle up) until another order, also with no enemy around (user request 2026-10-13, Demo 01 rev 5c).
	 */
	extern CODEXTACTICS_API const FName CoverCornerPeek;
	/**
	 * In cover at a wall EDGE, a double-click on ANOTHER cover within range: he dashes there and arrives through the run-in
	 * transition (game designer 2026-10-20, Tactics/CoverSwitchRules). Locked -> the ordinary cover order (Cover.Enter).
	 */
	extern CODEXTACTICS_API const FName CoverSwitch;
	/** N: corner lean / blind fire. */
	extern CODEXTACTICS_API const FName CoverFireMode;

	// --- Objects, items, traps ------------------------------------------------------------------------------------
	/** Push / relocate an object (barrel, barricade, crate, ...): ordered (or planned) to a new spot. */
	extern CODEXTACTICS_API const FName Relocate;
	/** Barrel menu "Ignite": light a barrel with a match (ordered or planned). */
	extern CODEXTACTICS_API const FName LightBarrel;
	/** Any other confirmed object action (quest objects, generator, pick up, ...). */
	extern CODEXTACTICS_API const FName UseObject;
	/** U: heat pack on the leader. */
	extern CODEXTACTICS_API const FName UseHeatPack;
	/** H / J / K / L: medkit / canned food / bread / chocolate. */
	extern CODEXTACTICS_API const FName UseItem;
	/** A supply crate's loot dialog opened. */
	extern CODEXTACTICS_API const FName LootCrate;
	/** An item (or everything) taken from a crate. */
	extern CODEXTACTICS_API const FName LootTake;
	/** Defusal confirmed on a hidden / level mine. */
	extern CODEXTACTICS_API const FName DisarmMine;
	/** Defusal confirmed on a trapped crate / object. */
	extern CODEXTACTICS_API const FName DisarmTrap;
	/** Disarm confirmed on a tripwire. */
	extern CODEXTACTICS_API const FName DisarmTripwire;
	/** "Set trap (N)": a grenade trap on an object. */
	extern CODEXTACTICS_API const FName SetTrap;
	/** F: set-up from the supply (placement starts). */
	extern CODEXTACTICS_API const FName DeployStart;
	/** A deployable confirmed on its spot (ordered or planned; turn-based grid placement too). */
	extern CODEXTACTICS_API const FName DeployMine;
	extern CODEXTACTICS_API const FName DeployBarricade;
	extern CODEXTACTICS_API const FName DeployTurret;
	extern CODEXTACTICS_API const FName DeployTripwire;
	/** Campfire / gas stove / anything else set up from the supply. */
	extern CODEXTACTICS_API const FName DeployOther;

	// --- Combat ---------------------------------------------------------------------------------------------------
	/** Ctrl + click: targeted shot (enemy priority, barrel, mine, crate, trapped object). */
	extern CODEXTACTICS_API const FName TargetedShot;
	/** Ctrl + click on a point with nothing to aim at (2026-10-19): the selection fires there at once (a tutorial lock under "Attack.*"). */
	extern CODEXTACTICS_API const FName QuickFire;
	/** Plain click on a live enemy during a wave: priority target. */
	extern CODEXTACTICS_API const FName PriorityTarget;
	/** G: grenade in hand / throw aim. */
	extern CODEXTACTICS_API const FName GrenadeAim;
	/** LMB while aiming: the grenade is thrown. */
	extern CODEXTACTICS_API const FName GrenadeThrow;
	/** Shift + G: signal flare aim. */
	extern CODEXTACTICS_API const FName FlareAim;
	/** X: next weapon. */
	extern CODEXTACTICS_API const FName CycleWeapon;
	/** R (outside placement / the grid fight): reload. */
	extern CODEXTACTICS_API const FName Reload;

	// --- Time -----------------------------------------------------------------------------------------------------
	/** Space tap: tactical pause on. */
	extern CODEXTACTICS_API const FName TacticalPause;
	/** Space tap in the pause: the plan runs (pause released). */
	extern CODEXTACTICS_API const FName PauseRelease;
	/** Space hold 1.5 s: turn-based fight entered. */
	extern CODEXTACTICS_API const FName TurnBasedEnter;
	/** Space hold in the turn-based fight: back to real time. */
	extern CODEXTACTICS_API const FName TurnBasedExit;
	/** Enter: end the squad's turn. */
	extern CODEXTACTICS_API const FName EndTurn;
	/** Tab: next operative in the turn-based fight. */
	extern CODEXTACTICS_API const FName NextUnit;
	/** Ctrl + click on the grid: attack. */
	extern CODEXTACTICS_API const FName TurnBasedAttack;
	/** Plain click on the grid: select / walk. */
	extern CODEXTACTICS_API const FName TurnBasedOrder;

	// --- Vehicles (APC P1, 2026-10-12) -----------------------------------------------------------------------------
	/** Y: the selected operative(s) board the nearest vehicle (Alt + Y: the whole squad; FPlayerCommandContext::bSquadWide). */
	extern CODEXTACTICS_API const FName VehicleBoard;
	/** Y with everyone addressed aboard: they get out (Alt + Y: the whole squad). */
	extern CODEXTACTICS_API const FName VehicleDisembark;
	/** LMB on the ground with the leader aboard: the vehicle drives there (double click: full speed). */
	extern CODEXTACTICS_API const FName VehicleDrive;
	/** Ctrl + click on an enemy with the leader aboard: the turret's priority target (P2); on a barrel / mine / trapped object: one cannon shell at it (P4). */
	extern CODEXTACTICS_API const FName VehicleTurretFire;
	/** Y near a disabled vehicle: the Engineer goes to repair it (P4). */
	extern CODEXTACTICS_API const FName VehicleRepair;

	// --- System ---------------------------------------------------------------------------------------------------
	/** F5: quick save. */
	extern CODEXTACTICS_API const FName QuickSave;
	/** F9: quick load. */
	extern CODEXTACTICS_API const FName QuickLoad;
	/** F1: the Controls reference screen (a UI hotkey: listed here for the screen, never reported). */
	extern CODEXTACTICS_API const FName ControlsScreen;
	/**
	 * "Item.Give": an item dragged from the inventory drawer onto a squad mate (his model or his action-bar portrait) is handed
	 * over; with Shift held a heat pack goes into his kit even when he is cold (rev 5d). Reported when the hand-over is requested.
	 */
	extern CODEXTACTICS_API const FName GiveItem;
	/** "Inventory.Open": the inventory drawer opened (I / action bar); a tutorial lock target (Demo 01: locked until the heat pack lesson). */
	extern CODEXTACTICS_API const FName InventoryOpen;
	/** "View.VisionCones": the player toggled the enemy vision cones (O / pause menu), Commandos style. */
	extern CODEXTACTICS_API const FName ToggleVisionCones;
	/**
	 * "View.SquadVisionCones": the player toggled the OWN SQUAD's vision cones (Shift+O / pause menu, 2026-10-20). Locked exactly when O is (the
	 * handler asks the lock of View.VisionCones: the Demo 01 tutorial unlocks both with the A4 cones card).
	 */
	extern CODEXTACTICS_API const FName ToggleSquadVisionCones;
	/** Esc: the pause menu (listed here for the screen, never reported). */
	extern CODEXTACTICS_API const FName PauseMenu;
	/**
	 * Enter (tap): the next line of the shown conversation - the story dialogue window or a tutorial radio / walk-and-talk
	 * (user request 2026-10-13). Never locked by the tutorial.
	 */
	extern CODEXTACTICS_API const FName DialogueNext;
	/** Enter held ~0.6 s / the SKIP button on the radio strip: the whole conversation ends as if it had played out. */
	extern CODEXTACTICS_API const FName DialogueSkip;
	/**
	 * Rev 5i: SKIP TUTORIAL (the button at the mission start / the pause menu entry, after a Yes / No confirm): the tutorial is off for
	 * this run, every lock and confinement is released. No hotkey; never locked.
	 */
	extern CODEXTACTICS_API const FName SkipTutorial;

	/** Every command with its default key label (registry order = Controls screen order). */
	CODEXTACTICS_API const TArray<FPlayerCommandInfo>& GetRegistry();

	/** The Controls screen sections in display order: "Movement & camera", "Squad", "Stances", "Combat", ... */
	CODEXTACTICS_API const TArray<const TCHAR*>& GetSections();

	/** Overrides the key label of a command (a rebound key); an empty label restores the default. */
	CODEXTACTICS_API void SetKeyLabelOverride(FName Id, const FString& KeyLabel);

	/** The key label to show for Id now: the rebound one, else the registry default; empty for an unknown id. */
	CODEXTACTICS_API FString GetKeyLabel(FName Id);

	/** The registry row of Id, nullptr when unknown. */
	CODEXTACTICS_API const FPlayerCommandInfo* Find(FName Id);

	/**
	 * Pattern match used by the tutorial: exact id, "*" (anything), or a "Prefix.*" / "Prefix*" wildcard. A wildcard is a
	 * plain prefix match, so it spans any number of "." segments ("Camera.*" and "Camera.Pan.*" both match
	 * "Camera.Pan.Keys"); an id without a wildcard matches exactly only. Case-insensitive (FName).
	 */
	CODEXTACTICS_API bool Matches(const FString& Pattern, FName CommandId);

	/** "[B]" for a registered id, "[Alt+V]" for a literal key, "" for an empty token. */
	CODEXTACTICS_API FString FormatKeyToken(const FString& Token);

	/**
	 * Replaces "{key:<CommandId>}" tokens in Text with the command's key cap ("[B]"); unknown ids keep the id in brackets.
	 */
	CODEXTACTICS_API FString ResolveKeyTokens(const FString& Text);

	/**
	 * THE choke point: logs the command and broadcasts UCodexEventBus::OnPlayerCommand (the tutorial listens there).
	 * Safe without a game instance (unit tests): then only the log line.
	 */
	CODEXTACTICS_API void Report(const UObject* WorldContext, FName CommandId, const FPlayerCommandContext& Context = FPlayerCommandContext());

	/**
	 * A hold tick of a continuous command (key pan, edge pan, drags): DeltaSeconds of real time during which the action was
	 * still going on. Broadcast on the same bus with FPlayerCommandContext::HeldSeconds set; no log line (every frame).
	 */
	CODEXTACTICS_API void ReportHold(const UObject* WorldContext, FName CommandId, float DeltaSeconds);

	/** Report with only the acting operative set. */
	CODEXTACTICS_API void ReportBy(const UObject* WorldContext, FName CommandId, AActor* Operative, AActor* Target = nullptr, bool bPlanned = false);
}

/**
 * Gameplay events the tutorial can trigger on / complete with ("event" triggers / completions): raised by the systems
 * where they happen, broadcast as UCodexEventBus::OnGameplayEvent. Names are free-form in the data; these are the ones
 * the code raises today (HANDOFF §6 «Tutorial»).
 */
namespace GameplayEvents
{
	/** A relocated object was set down on its new spot (barrel push done). */
	extern CODEXTACTICS_API const FName RelocationDone;
	/** A carry / push was refused (too cold / too wounded to lift). */
	extern CODEXTACTICS_API const FName RelocationRefused;
	/** A barrel caught fire from a match. */
	extern CODEXTACTICS_API const FName BarrelLit;
	/** A hidden mine was spotted (UCodexEventBus::OnMineSpotted). */
	extern CODEXTACTICS_API const FName MineSpotted;
	/** A provision / heat pack was used (UCodexEventBus::OnItemUsed). */
	extern CODEXTACTICS_API const FName ItemUsed;
	/** An operative's cold tier rose to Chills or worse ("ColdTier.Chills" / ".Freezing" / ... are raised too). */
	extern CODEXTACTICS_API const FName ColdTierChanged;
	/** An operative fell for real (UCodexEventBus::OnSoldierDowned). */
	extern CODEXTACTICS_API const FName SoldierDowned;
	/** A lethal hit in a lesson beat only downed the operative (tutorial rule). */
	extern CODEXTACTICS_API const FName LessonDowned;
	/** The bottom dialogue window closed. */
	extern CODEXTACTICS_API const FName DialogueFinished;
	/** A wave / ambush fight started (phase WaveCombat). */
	extern CODEXTACTICS_API const FName CombatStarted;
	/** The fight ended (wave cleared / post combat). */
	extern CODEXTACTICS_API const FName CombatEnded;
	/** A hidden PMC tripwire was spotted (Subject = the spotter). */
	extern CODEXTACTICS_API const FName TripwireSpotted;
	/**
	 * Rev 5h: any hidden trap (level mine, PMC tripwire) was spotted (Subject = the spotter). Also "TrapSpotted.Peek" when a corner
	 * peek revealed it and "TrapSpotted.<SpotEventTag>" for a trap of a tagged group (AInteractableActor::SpotEventTag).
	 */
	extern CODEXTACTICS_API const FName TrapSpotted;
	/** A tripwire was disarmed (Subject = the disarmer). */
	extern CODEXTACTICS_API const FName TripwireDisarmed;
	/** A tripwire's pin was pulled (Subject = whoever crossed it, none for a failed disarm). */
	extern CODEXTACTICS_API const FName TripwireTriggered;
	/**
	 * "TripwireTriggered.Enemy" (2026-10-19, the A4 ambush lesson part 1): the pin was pulled by an ENEMY walking into the wire
	 * (Subject = that enemy); raised right after TripwireTriggered.
	 */
	extern CODEXTACTICS_API const FName TripwireTriggeredByEnemy;
	/**
	 * "StealthLure.Heard" (2026-10-19, the A4 ambush lesson part 2): an enemy heard a whistle / a stone and investigates it now
	 * (started or moved its search there; Subject = the enemy, Target = the operative who made the noise, Kind = "Whistle" /
	 * "Throw", Location = the noise). Once per enemy per noise.
	 */
	extern CODEXTACTICS_API const FName LureHeard;
	/**
	 * "EnemyKilled" (2026-10-21, the A4 close-shot lesson): an enemy died (Subject = the enemy, Target = the operative whose attack killed
	 * it - matched by the attacker name - or none, Kind = "Squad" (an operative's shot / blow / grenade), "Trap" (a mine / wire / charge
	 * blast) or "Other", Location = where it died). Raised as "EnemyKilled.<Kind>" FIRST and then as "EnemyKilled", so a step completing on
	 * the specific kill finishes before a step reacting to any kill of that enemy sees it.
	 */
	extern CODEXTACTICS_API const FName EnemyKilled;
	/** An operative settled in a cover slot (arrived and pressed to the wall; not on the order). Also "CoverEntered.High" / ".Low". */
	extern CODEXTACTICS_API const FName CoverEntered;
	/** An operative left his cover (moved off, ordered elsewhere, knocked out of it). */
	extern CODEXTACTICS_API const FName CoverLeft;
	/** An operative finished a side-step along the same wall. */
	extern CODEXTACTICS_API const FName CoverShimmy;
	/** An operative in high cover reached the exposed corner on his facing side. */
	extern CODEXTACTICS_API const FName CornerReached;
	/**
	 * A manual corner peek (Cover.CornerPeek) began: he leans out at the corner in the corner-aim pose (Subject = him, Kind =
	 * "Left" / "Right"; once per order).
	 */
	extern CODEXTACTICS_API const FName CornerPeek;
	/** An operative climbed into a vehicle (Subject = him, Target = the vehicle, Kind = his seat "Driver" / "Gunner" / "Passenger"). */
	extern CODEXTACTICS_API const FName VehicleBoarded;
	/** An operative climbed out (Subject = him, Target = the vehicle). */
	extern CODEXTACTICS_API const FName VehicleDisembarked;
	/** A drive order started (Subject = the driver, Target = the vehicle, Location = the goal). */
	extern CODEXTACTICS_API const FName VehicleDriveStarted;
	/** The vehicle reached its goal (Subject = the driver, Target = the vehicle). */
	extern CODEXTACTICS_API const FName VehicleArrived;
	/** A drive could not reach the clicked spot (no road / too narrow / blocked). */
	extern CODEXTACTICS_API const FName VehiclePathBlocked;
	/**
	 * A player command was refused because the tutorial locks it (UTutorialSubsystem::CheckCommandLocked; the handler degrades
	 * it, e.g. a locked sprint walks). Also raised as "Tutorial.Locked.<CommandId>" ("Tutorial.Locked.Order.Sprint").
	 */
	extern CODEXTACTICS_API const FName TutorialLocked;
	/**
	 * An item went from a crate / ground pile into an operative's kit (loot click, Take all, a loot line dragged onto him).
	 * Subject = the collector, Target = the container, Kind = the item ("HeatPack", "Medkit", ...); also raised as
	 * "ItemLooted.<Kind>" ("ItemLooted.HeatPack").
	 */
	extern CODEXTACTICS_API const FName ItemLooted;
	/**
	 * An item went from one squad mate's kit into another's (a drawer drag onto him; a heat pack cracked on a cold mate is
	 * ItemUsed instead). Subject = the giver, Target = the recipient, Kind = the item ("HeatPack", ...); also raised as
	 * "ItemGiven.<Kind>" ("ItemGiven.HeatPack"). Rev 5d (2026-10-14).
	 */
	extern CODEXTACTICS_API const FName ItemGiven;
	/** The turret fired (Subject = the gunner, Target = the enemy, Kind = "Cannon" per shell / "MachineGun" per second of fire). */
	extern CODEXTACTICS_API const FName VehicleTurretFired;
	/** The turret killed an enemy (Subject = the gunner, Target = the enemy, Kind = "Cannon" / "MachineGun"). */
	extern CODEXTACTICS_API const FName VehicleTurretKill;
	/** A small enemy died under the wheels (Subject = the driver, Target = the enemy, Kind = its archetype). */
	extern CODEXTACTICS_API const FName VehicleRunOver;
	/** The hull took damage (P4; Subject = the attacker or nullptr, Target = the vehicle, Kind = the source "SmallMelee" / "HeavyMelee" / "Mine" / ...). */
	extern CODEXTACTICS_API const FName VehicleDamaged;
	/** The hull reached 0 HP: disabled, the crew ejected (Subject = the last attacker or nullptr, Target = the vehicle). */
	extern CODEXTACTICS_API const FName VehicleDisabled;
	/** A disabled hull was repaired (Subject = the repairer, Target = the vehicle). */
	extern CODEXTACTICS_API const FName VehicleRepaired;
	/** An operative stepped onto a one-at-a-time bridge (ABridgeCrossingVolume; Subject = the operative). */
	extern CODEXTACTICS_API const FName BridgeEntered;
	/** The bridge was full: an operative was stopped at its edge ("The bridge won't hold two!"; Subject = the operative). */
	extern CODEXTACTICS_API const FName BridgeRefused;
	/** An operative left the bridge on the far side (Subject = the operative). */
	extern CODEXTACTICS_API const FName BridgeCrossed;
	/** A standing / crouched operative was stopped at a low fence ("Too low - go prone"; Subject = the operative). */
	extern CODEXTACTICS_API const FName CrawlGapRefused;
	/** A prone operative crawled under a low fence and came out on the far side (Subject = the operative). */
	extern CODEXTACTICS_API const FName CrawlGapPassed;
	/** The player toggled the enemy vision cones ("VisionConesToggled.On" / ".Off" are raised too). */
	extern CODEXTACTICS_API const FName VisionConesToggled;
	/** The player toggled the squad's vision cones ("SquadVisionConesToggled.On" / ".Off" are raised too). */
	extern CODEXTACTICS_API const FName SquadVisionConesToggled;

	/** Broadcasts UCodexEventBus::OnGameplayEvent (no-op without a game instance) and logs it. */
	CODEXTACTICS_API void Raise(const UObject* WorldContext, FName EventName, AActor* Subject = nullptr);

	/**
	 * Broadcasts OnGameplayEvent (EventName, Context.Subject) and OnGameplayEventDetailed (EventName, Context); fills
	 * SubjectId / TargetId from the actors when empty.
	 */
	CODEXTACTICS_API void RaiseWithContext(const UObject* WorldContext, FName EventName, const FGameplayEventContext& Context);

	/** "ItemLooted" and "ItemLooted.<ItemKind>" for Count > 0 units of ItemKind taken by Collector from Container. */
	CODEXTACTICS_API void RaiseItemLooted(const UObject* WorldContext, AActor* Collector, AActor* Container, FName ItemKind, int32 Count);

	/** "ItemGiven" and "ItemGiven.<ItemKind>" for Count > 0 units of ItemKind handed from Giver to Recipient. */
	CODEXTACTICS_API void RaiseItemGiven(const UObject* WorldContext, AActor* Giver, AActor* Recipient, FName ItemKind, int32 Count);
}
