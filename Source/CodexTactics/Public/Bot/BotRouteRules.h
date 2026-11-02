#pragma once

#include "CoreMinimal.h"
#include "Bot/BotCoverRules.h"

class FJsonObject;

/**
 * The playtest bot's planned ROUTE (game designer 2026-11-01, Training Ground "Combat lab", docs/design/COMBAT_LAB.md section 7;
 * UE-only, no Godot reference): every map gets a route the bot leads the squad along in formation, and the run report says whether it
 * managed to walk it. The level JSON "bot_route" block (LevelJsonRules::ParseLevel -> FLevelCombatConfig::BotRoute) holds the waypoints;
 * FBotRouteTracker is the pure bookkeeping of one run (reached / skipped waypoints, stuck events, laps), tested in
 * CodexTactics.Bot.Route.*. UPlaytestBotSubsystem (PlaytestBotRouteMode.cpp) turns its commands into clicks.
 */

/** What the bot does when the leader reached a waypoint. */
enum class EBotRouteAction : uint8
{
	/** Nothing: on to the next waypoint. */
	Walk,
	/** A click on the wall face at WallPoint (the group cover order = the formation wall layout), then HoldSeconds there. */
	Wall,
	/** The leader alone (Solo) climbs the ladder nearest the waypoint, stays HoldSeconds on the roof and climbs down. */
	Climb
};

/** One waypoint of the route (world cm, planar). */
struct CODEXTACTICS_API FBotRouteWaypoint
{
	FVector2D At = FVector2D::ZeroVector;
	FString Label;
	EBotRouteAction Action = EBotRouteAction::Walk;
	/** Wall: the point on the wall's face the bot clicks (traced at knee height from At). */
	FVector2D WallPoint = FVector2D::ZeroVector;
	/** Wall / Climb: seconds the squad stays at the wall / the leader on the roof. */
	float HoldSeconds = 3.f;
	/**
	 * "z" (generated maps, 2026-11-02): the floor height of the waypoint's level, cm. Set: the bot clicks just above that level (a
	 * waypoint under a roof is never a click on the roof) and the waypoint counts as reached only with the leader's feet within
	 * FBotRouteConfig::ReachHeightCm of it (a roof point straight above a floor point). Unset: the leader's own level, any height.
	 */
	bool bHasZ = false;
	float Z = 0.f;
	/** "drop": the leg to this waypoint drops through that roof / floor hole (a drop nav link; the bot reports how it went). */
	FString Drop;
};

/** The level JSON "bot_route" block (defaults = the 2026-11-01 combat lab values). */
struct CODEXTACTICS_API FBotRouteConfig
{
	FString Name;
	/** The last waypoint leads back to the first (a lap ends when the first is reached again). */
	bool bLoop = true;
	/** The leader within this of a waypoint has reached it, cm. */
	float ReachRadiusCm = 250.f;
	/** A waypoint with a "z" is reached only with the leader's feet within this of it, cm. */
	float ReachHeightCm = 150.f;
	/** The leader moved less than StuckMoveCm for this long (route not paused) = stuck, s. */
	float StuckAfterSeconds = 12.f;
	float StuckMoveCm = 150.f;
	/** A leg is stuck too when it takes longer than LegBaseSeconds + its straight length at LegMinSpeedCm (a detour that never ends). */
	float LegBaseSeconds = 25.f;
	float LegMinSpeedCm = 100.f;
	/** Stuck events re-order the move this many times; the next one gives the waypoint up (skipped). */
	int32 MaxRetries = 2;
	/**
	 * A fight that starts while walking holds the route at most this long when the squad found no wall to take cover at (it halts where it
	 * stands and fights), s; 0 = walk on fighting.
	 */
	float FightHoldSeconds = 20.f;
	/** Cover at a wall first on contact ("fight_cover"; game designer decision 5, 2026-11-01 night; BotCoverRules). */
	FBotCoverParams Cover;
	/** A wall / climb action gives up after this long, s. */
	float ActionTimeoutSeconds = 45.f;
	TArray<FBotRouteWaypoint> Points;

	/** At least two waypoints. */
	bool IsValid() const { return Points.Num() >= 2; }
	/** Straight-line length of one lap (the closing leg back to the first waypoint included on a loop), cm. */
	float LapLengthCm() const;
};

/** One stuck event of the run (the report's "where / why the bot got stuck"). */
struct CODEXTACTICS_API FBotRouteStuck
{
	float Time = 0.f;
	FVector2D Position = FVector2D::ZeroVector;
	int32 Waypoint = 0;
	FString Label;
	/** The order the leader had (FBotRouteTracker::NotifyOrder). */
	FString LastOrder;
	/** no_move | leg_timeout | order_failed:<why> | action_failed:<why>. */
	FString Reason;
	/** The waypoint was given up after this event. */
	bool bSkipped = false;
};

/**
 * One drop through a roof / floor hole (a waypoint with "drop"; generated maps 2026-11-02): how the squad got down. bFell = the leader
 * fell (walked off the rim through the builder's drop nav link); bScripted = the leg got stuck and the bot put the squad down there itself.
 */
struct CODEXTACTICS_API FBotRouteDrop
{
	int32 Waypoint = 0;
	FString Label;
	FString Via;
	bool bFell = false;
	bool bScripted = false;
	bool bReached = false;
	/** From the leg's order to the waypoint reached (or given up), s. */
	float Seconds = 0.f;
};

/** One finished lap. */
struct CODEXTACTICS_API FBotRouteLap
{
	float Seconds = 0.f;
	int32 Reached = 0;
	int32 Skipped = 0;
};

/** What the tracker asks the bot to do this tick. */
enum class EBotRouteCommand : uint8
{
	/** Keep going. */
	None,
	/** Order the move to GetTarget() (the first order of a leg, or after a skipped waypoint). */
	Move,
	/** The leader is stuck: order the move to GetTarget() again. */
	Retry,
	/** The waypoint is reached and has an action: run it, then FinishAction. */
	StartAction,
	/** Not a loop and the last waypoint is done. */
	Finished
};

/** The world as the tracker sees it each tick. */
struct CODEXTACTICS_API FBotRouteInput
{
	float Time = 0.f;
	FVector2D Leader = FVector2D::ZeroVector;
	/** The leader's feet height, cm (checked against a waypoint's "z"). */
	float LeaderFeetZ = 0.f;
	/** No stuck clock this tick (a fight hold, a knockdown, the leader on a ladder). */
	bool bPaused = false;
};

/** The pure bookkeeping of one route run (CodexTactics.Bot.Route.*). */
class CODEXTACTICS_API FBotRouteTracker
{
public:
	/** Starts the route at its first waypoint. */
	void Reset(const FBotRouteConfig& InConfig, float Time, const FVector2D& Leader);

	/** One step: reached / stuck / skipped bookkeeping and the command for the bot. */
	EBotRouteCommand Tick(const FBotRouteInput& In);

	/** The bot issued Order (the report's "last order"). */
	void NotifyOrder(const FString& Order) { LastOrder = Order; }
	/** The order could not be issued (no ground under the waypoint, no controller): a stuck event at once. */
	EBotRouteCommand NotifyOrderFailed(const FBotRouteInput& In, const FString& Why);
	/** The action of the reached waypoint is over: bOk false records a stuck event (action_failed:<Why>, the waypoint counts as skipped). */
	EBotRouteCommand FinishAction(const FBotRouteInput& In, bool bOk, const FString& Why);

	bool IsInAction() const { return bInAction; }
	bool IsFinished() const { return bFinished; }
	int32 GetIndex() const { return Index; }
	const FBotRouteWaypoint* GetWaypoint() const { return Config.Points.IsValidIndex(Index) ? &Config.Points[Index] : nullptr; }
	FVector2D GetTarget() const { return GetWaypoint() ? GetWaypoint()->At : FVector2D::ZeroVector; }
	const FString& GetLastOrder() const { return LastOrder; }
	const TArray<FBotRouteStuck>& GetStucks() const { return Stucks; }
	const TArray<FBotRouteLap>& GetLaps() const { return Laps; }
	int32 GetRetries() const { return Retries; }
	/** Waypoints reached / skipped in the lap under way. */
	int32 GetLapReached() const { return LapReached; }
	int32 GetLapSkipped() const { return LapSkipped; }
	/** Waypoints reached / skipped over the whole run. */
	int32 GetTotalReached() const { return TotalReached; }
	int32 GetTotalSkipped() const { return TotalSkipped; }
	/** Planar distance the leader walked, cm. */
	float GetDistanceCm() const { return DistanceCm; }
	/** Some lap was finished with no waypoint skipped. */
	bool IsRouteCompleted() const;
	/** 1 once a lap is done with no skip; else the best share of the route's waypoints reached in one lap (0..1). */
	float GetProgress() const;
	/** The furthest waypoint reached in the first lap (INDEX_NONE before any). */
	int32 GetFurthestFirstLap() const { return FurthestFirstLap; }
	const FBotRouteConfig& GetConfig() const { return Config; }
	/** The leader stands at Waypoint: within the reach radius and, when it has a "z", the reach height. */
	bool IsAt(const FBotRouteWaypoint& Waypoint, const FBotRouteInput& In) const;

private:
	EBotRouteCommand Advance(const FBotRouteInput& In, bool bReached);
	EBotRouteCommand Stuck(const FBotRouteInput& In, const FString& Reason);
	void StartLeg(const FBotRouteInput& In);

	FBotRouteConfig Config;
	int32 Index = 0;
	bool bOrdered = false;
	bool bStarted = false;
	bool bInAction = false;
	bool bFinished = false;
	int32 Retries = 0;
	FVector2D LastLeader = FVector2D::ZeroVector;
	FVector2D ProgressPos = FVector2D::ZeroVector;
	float ProgressTime = 0.f;
	float LastTime = 0.f;
	float LegSeconds = 0.f;
	float LegLimitSeconds = 0.f;
	float LapStartTime = 0.f;
	int32 LapProcessed = 0;
	int32 LapReached = 0;
	int32 LapSkipped = 0;
	int32 TotalReached = 0;
	int32 TotalSkipped = 0;
	int32 BestLapReached = 0;
	int32 FurthestFirstLap = INDEX_NONE;
	float DistanceCm = 0.f;
	FString LastOrder;
	TArray<FBotRouteStuck> Stucks;
	TArray<FBotRouteLap> Laps;
};

namespace BotRouteRules
{
	/** "walk" | "wall" | "climb" (any case); false when unknown. */
	CODEXTACTICS_API bool ParseAction(const FString& Name, EBotRouteAction& Out);
	CODEXTACTICS_API const TCHAR* ActionName(EBotRouteAction Action);

	/**
	 * The level JSON "bot_route" object: {"name", "loop", "reach_radius_cm", "stuck_after_sec", "stuck_move_cm", "leg_base_sec",
	 * "leg_min_speed_cm", "reach_height_cm", "max_retries", "fight_hold_sec", "action_timeout_sec", "fight_cover": {"enabled", "search_cm", "hold_sec", "min_wall_cm",
	 * "min_height_cm", "min_threat_cm", "min_facing_cos"}, "points": [{"at": [x, y], "z", "label", "action": "walk" | "wall" |
	 * "climb", "wall": [x, y], "hold_sec", "drop": "<hole id>"}]}. Absent keys keep the defaults (clamped to sane ranges). False + OutError on fewer than two points,
	 * a point without "at": [x, y], an unknown action or a "wall" action without "wall": [x, y].
	 */
	CODEXTACTICS_API bool Parse(const FJsonObject& Route, FBotRouteConfig& OutConfig, FString* OutError = nullptr);
}
