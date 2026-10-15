#pragma once

#include "CoreMinimal.h"
#include "TrapSpotRules.generated.h"

/**
 * What the squad does when a hidden trap (a level mine, a PMC tripwire) is spotted. Demo 01 rev 5h (game designer play-test
 * 2026-10-15, every level): "the squad stops, but the next click moves everyone again in formation and someone steps on it".
 */
enum class ETrapSpotResponse : uint8
{
	/** Everyone stops (the old rule, Godot _on_mine_spotted); the formation carries on with the next order. */
	HoldOnly,
	/** Everyone stops and the squad switches to Solo: each operative holds, the next order goes only to the selected one. */
	HoldAndSolo
};

/**
 * Rev 5h: the corner peek reveals hidden traps. While an operative leans out round a corner (the corner aim of a manual peek
 * or of the corner hold), every hidden trap inside his peek sight cone with a line of sight is spotted at once - with
 * certainty, far beyond the proximity spot range (mine 4.5 m, Medic-Sapper +1.5 m). BP_Operative Class Defaults ->
 * Cover|Peek (AOperativeCharacter::PeekSpotConfig).
 */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FTrapPeekSpotConfig
{
	GENERATED_BODY()

	/** Off: a peek reveals nothing (only the proximity rule spots traps). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Cover|Peek")
	bool bEnabled = true;

	/** How far the peek sees a hidden trap, cm (planar, from the lean-out muzzle round the corner). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Cover|Peek", meta = (ClampMin = "0"))
	float RangeCm = 1200.f;

	/** Half the sight cone round the corner-aim direction, degrees (30 = a 60 degree cone). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Cover|Peek", meta = (ClampMin = "0", ClampMax = "180"))
	float HalfAngleDeg = 30.f;

	/** A wall / rock between the peek and the trap hides it (visibility trace to TargetHeightCm above the trap). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Cover|Peek")
	bool bRequireLineOfSight = true;

	/** The point of the trap the line of sight goes to, cm above its actor location (a mine lies flush with the snow). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Cover|Peek", meta = (ClampMin = "0"))
	float TargetHeightCm = 20.f;

	/** How often a peeking operative looks for traps, s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Cover|Peek", meta = (ClampMin = "0.02"))
	float IntervalSeconds = 0.2f;
};

/** Pure rules of trap spotting (rev 5h); tested in CodexTactics.Interactables.TrapSpot.*. */
namespace TrapSpotRules
{
	/**
	 * The squad's response to a spotted hidden trap. Solo only while exploring with the formation (bFormationActive: from the
	 * preparation on operatives act on their own anyway, and the scan is off in the wave fight), never in the AUTO mode
	 * (Commander Mode, bAutonomy: its paths already go round known traps - UNavArea_Tripwire, rev 5e) and never for a squad of
	 * one (LivingMembers <= 1).
	 */
	CODEXTACTICS_API ETrapSpotResponse DecideSquadResponse(bool bFormationActive, bool bAutonomy, int32 LivingMembers);

	/**
	 * Target lies inside the peek sight cone: planar distance from Origin <= RangeCm and the planar angle between Direction and
	 * the line to Target <= HalfAngleDeg. A target at the origin is not "in" the cone.
	 */
	CODEXTACTICS_API bool IsInPeekSightCone(const FVector& Origin, const FVector& Direction, const FVector& Target, float RangeCm,
		float HalfAngleDeg);

	/** The tutorial / gameplay event raised for a spotted trap of a group: "TrapSpotted.<Tag>" (none for an empty tag). */
	CODEXTACTICS_API FName GroupEventName(FName SpotEventTag);

	/** Tutorial condition stat "traps_spotted.<Tag>": the tag, or NAME_None when Stat is not of that form. */
	CODEXTACTICS_API FName ParseSpottedStatTag(FName Stat);
}
