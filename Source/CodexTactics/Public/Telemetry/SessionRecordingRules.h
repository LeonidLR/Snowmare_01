#pragma once

#include "CoreMinimal.h"

enum class EDemoOrderKind : uint8 { Move, Cover, CornerPeek, Shot, QuickFire, Suppress, Stance, Medkit, Breach, Grenade };
enum class EDemoMode : uint8 { Solo, Autonomy, CeaseFire, Pause };
enum class EDemoResult : uint8 { Aborted, Victory, Defeat };

struct FDemoGate
{
	int32 Switch = 1;
	bool bGameWorld = false;
	bool bUnattended = false;
	bool bNullRhi = false;
	bool bNoTelemetry = false;
	bool bCommandlet = false;
	bool bAutomation = false;
};

struct FDemoFacts
{
	EDemoOrderKind Kind = EDemoOrderKind::Move;
	double ThreatDistance = -1.;
	double ThreatDistanceNew = -1.;
	double FlankDegrees = 0.;
	double GoalDistance = -1.;
	double GoalDistanceNew = -1.;
	bool bInCover = false;
	bool bMoving = false;
	bool bCrouch = false;
	bool bCease = false;
	bool bAccepted = true;
};

struct FDemoMapping
{
	const TCHAR* Order = nullptr;
	const TCHAR* Unmapped = TEXT("");
};

struct FDemoThreat
{
	FName Id;
	FName Archetype;
	FVector Position = FVector::ZeroVector;
};

struct FDemoOp
{
	FName Id;
	FName LastOrder;
	FVector NearestThreat = FVector::ZeroVector;
	FVector Position = FVector::ZeroVector;
	double Yaw = 0.;
	double ThreatDistance = -1.;
	double ThreatBearing = 0.;
	double GoalDistance = -1.;
	double LastOrderTime = 0.;
	float Health = 0.f;
	int32 Clip = 0;
	int32 Reserve = 0;
	int32 ThreatCount = 0;
	uint8 Stance = 0;
	bool bAlive = false;
	bool bMoving = false;
	bool bCover = false;
	bool bSelected = false;
	bool bLeader = false;
};

struct FDemoUnit
{
	FName Id;
	FName Role;
	FName Weapon;
	float MaxHealth = 0.f;
	int32 ClipMax = 0;
};

struct FDemoFrame
{
	enum class EType : uint8 { Order, Snapshot, Mode };
	EType Type = EType::Snapshot;
	double Time = 0.;
	double RealTime = 0.;
	int32 FirstOp = 0;
	int32 NumOps = 0;
	int32 FirstThreat = 0;
	int32 NumThreats = 0;
	FDemoFacts Facts;
	FName Actor;
	FName Formation;
	TOptional<FVector> Destination;
	TOptional<FVector> CoverPoint;
	double CoverYaw = 0.;
	uint8 CoverHeight = 0;
	FDemoThreat Target;
	EDemoMode Mode = EDemoMode::Solo;
	bool bModeOn = false;
	bool bSolo = false;
	bool bBoxSelected = false;
	bool bSprint = false;
	bool bPlan = false;
	bool bPaused = false;
	bool bAutonomy = false;
};

struct FDemoSession
{
	FString Map;
	FDateTime StartedUtc;
	FDateTime StartedLocal;
	TOptional<FVector> InitialGoal;
	TArray<FDemoUnit> Units;
	TArray<FDemoFrame> Frames;
	TArray<FDemoOp> Ops;
	TArray<FDemoThreat> Threats;
	FString Mark;
	EDemoResult Result = EDemoResult::Aborted;
	double Duration = 0.;
	int32 AcceptedOrders = 0;
	int32 Alive = 0;
	bool bPie = false;
	bool bImmortal = false;
	bool bInfiniteAmmo = false;
	bool bTruncated = false;
};

namespace SessionRecordingRules
{
	inline constexpr double MinSessionSec = 60.;
	inline constexpr double SnapshotIntervalSec = 1.;
	inline constexpr int32 MaxThreats = 12;
	inline constexpr double MoveDeltaCm = 300.;
	inline constexpr double FlankDeg = 45.;
	inline constexpr double MaxSessionSec = 14400.;

	CODEXTACTICS_API const TCHAR* OffReason(const FDemoGate& Gate);
	CODEXTACTICS_API bool ShouldRecord(const FDemoGate& Gate);
	CODEXTACTICS_API bool ShouldDiscard(double Duration, int32 Accepted, bool bSuppressed);
	CODEXTACTICS_API FString CleanMapName(FString Map);
	CODEXTACTICS_API FString MakeDemoFileName(FDateTime Local, const FString& Map);
	CODEXTACTICS_API const TCHAR* KindName(EDemoOrderKind Kind);
	CODEXTACTICS_API FDemoMapping MapToPmcOrder(const FDemoFacts& Facts);
	CODEXTACTICS_API FVector2D ThreatDistanceBearing(const FVector& Actor, double Yaw, const FVector* Threat);
	CODEXTACTICS_API FString SerializeSession(const FDemoSession& Session, const FString& FileStem);
}
