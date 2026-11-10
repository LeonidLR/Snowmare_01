#pragma once

#include "CoreMinimal.h"
#include "Characters/FirePostureRules.h"
#include "Characters/OperativeMovementRules.h"
#include "Tactics/CoverTypes.h"

class AActor;
class AOperativeCharacter;

/**
 * The shared squad order catalog (user request 2026-11-03, docs/systems/pmc-squad-ai.md): ONE table of the orders a squad member
 * can be given, used by the player's controller and by the PMC commander (UPmcSquadSubsystem) alike. Every entry = CanDo(context)
 * + Execute(context) on the operative's own API (the "shared component" is AOperativeCharacter itself, so every skill it has -
 * cover, peeks, ladders, drops, stances, weapons, healing - serves both sides). A new skill = (1) the operative implementation,
 * (2) a catalog entry here, (3) a PmcCommanderRules scoring hook + default weight, (4) a smoke + an arena metric.
 * UE-only, no Godot reference. Tested in CodexTactics.AI.SquadOrderCatalog.*.
 */
enum class ESquadOrder : uint8
{
	/** Walk / sprint to Location (OrderMoveTo). */
	Move,
	/** Take the cover slot in Cover: walk to it, then EnterCover (the operative's own run-in handles the entry). */
	TakeCover,
	/** Leave the wall (LeaveCover). */
	LeaveCover,
	/** Stance (SetStance). */
	Stance,
	/** The fighter's own fire posture override (Passive / Defensive / Aggressive). */
	FirePosture,
	/** Fire at Target first while it can be hit (SetManualPriorityTarget). */
	PriorityTarget,
	/** Suppressive fire on Target at Location (OrderSuppress). */
	Suppress,
	/** Cease the suppressive fire. */
	CeaseSuppress,
	/** Spend a medkit on Patient (HealAlly; within reach). */
	Heal,
	/** Reload the weapon in hands. */
	Reload,
	/** Switch to the arsenal weapon WeaponId. */
	SwitchWeapon,
	/** Stop where he stands and face Location (StopAndFace); a zero Location only stops him. */
	Hold,
	/** One rocket at Target: draw the launcher, fire, back to the rifle (OrderLauncherStrike; 2026-11-09). */
	LauncherStrike,
	Count
};

/** What an order needs (every field optional; the entry's CanDo says which it reads). */
struct CODEXTACTICS_API FSquadOrderContext
{
	AOperativeCharacter* Operative = nullptr;
	FVector Location = FVector::ZeroVector;
	AActor* Target = nullptr;
	AOperativeCharacter* Patient = nullptr;
	FCoverSlot Cover;
	EOperativeStance Stance = EOperativeStance::Standing;
	ESquadFirePosture Posture = ESquadFirePosture::Aggressive;
	FString WeaponId;
	bool bSprint = false;
};

/** One row of the catalog. */
struct CODEXTACTICS_API FSquadOrderEntry
{
	ESquadOrder Id = ESquadOrder::Hold;
	const TCHAR* Name = TEXT("");
	const TCHAR* Description = TEXT("");
	/** The order makes sense for this context now (never executes anything). */
	TFunction<bool(const FSquadOrderContext&)> CanDo;
	/** Carries the order out; true when the operative took it. */
	TFunction<bool(const FSquadOrderContext&)> Execute;
};

namespace SquadOrderCatalog
{
	/** Every entry, in ESquadOrder order. */
	CODEXTACTICS_API const TArray<FSquadOrderEntry>& All();

	/** The entry of an order (nullptr for Count). */
	CODEXTACTICS_API const FSquadOrderEntry* Find(ESquadOrder Order);

	/** "Move", "TakeCover", ... */
	CODEXTACTICS_API const TCHAR* Name(ESquadOrder Order);

	/** The order by its name (Count when unknown). */
	CODEXTACTICS_API ESquadOrder Parse(const FString& Name);

	/** The common gate every entry applies first: a valid, alive operative that is not knocked down, raging or panicking. */
	CODEXTACTICS_API bool IsOperativeOrderable(const AOperativeCharacter* Operative);

	CODEXTACTICS_API bool CanDo(ESquadOrder Order, const FSquadOrderContext& Context);

	/** CanDo, then the entry's Execute; false when either refused. */
	CODEXTACTICS_API bool Execute(ESquadOrder Order, const FSquadOrderContext& Context);
}
