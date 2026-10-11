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
};

/** One row of the command registry: id -> default key label, section and description (the future Controls screen). */
struct CODEXTACTICS_API FPlayerCommandInfo
{
	FName Id;
	/** Default binding as shown on hint cards, e.g. "B", "Alt+V", "Ctrl+Click". */
	const TCHAR* KeyLabel = TEXT("");
	/** Camera / Orders / Combat / Survival / Time / System. */
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
	/** W / A / S / D, screen edge or MMB drag: the camera starts panning. */
	extern CODEXTACTICS_API const FName CameraPan;
	/** Q / E, arrows or RMB drag: the camera turns. */
	extern CODEXTACTICS_API const FName CameraRotate;
	/** Mouse wheel: zoom in / out. */
	extern CODEXTACTICS_API const FName CameraZoom;

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

	// --- Cover ----------------------------------------------------------------------------------------------------
	/** First click on a wall: the cover ghost (preview). */
	extern CODEXTACTICS_API const FName CoverPreview;
	/** Second click on the ghost: the operative takes the cover slot (wall hug / low cover). */
	extern CODEXTACTICS_API const FName CoverEnter;
	/** Reported with CoverEnter when the slot has an exposed corner (fire from the corner). */
	extern CODEXTACTICS_API const FName CoverCorner;
	/** A click along the operative's own wall: shimmy. */
	extern CODEXTACTICS_API const FName CoverShimmy;
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

	// --- System ---------------------------------------------------------------------------------------------------
	/** F5: quick save. */
	extern CODEXTACTICS_API const FName QuickSave;
	/** F9: quick load. */
	extern CODEXTACTICS_API const FName QuickLoad;

	/** Every command with its default key label (registry order = Controls screen order). */
	CODEXTACTICS_API const TArray<FPlayerCommandInfo>& GetRegistry();

	/** The registry row of Id, nullptr when unknown. */
	CODEXTACTICS_API const FPlayerCommandInfo* Find(FName Id);

	/**
	 * Pattern match used by the tutorial: exact id, "*" (anything), or a "Prefix.*" / "Prefix*" wildcard.
	 * Case-insensitive (FName).
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

	/** Broadcasts UCodexEventBus::OnGameplayEvent (no-op without a game instance) and logs it. */
	CODEXTACTICS_API void Raise(const UObject* WorldContext, FName EventName, AActor* Subject = nullptr);
}
