// USquadSubsystem: formation rules v2.1 bypass "B" in the runtime (behind Codex.Formation.Sectors + Codex.Formation.V21). The leader's own
// move order in exploration (group mode, no box selection) whose straight way is blocked by a FREE-STANDING wall: FormationBypassRules::
// ChooseSide picks the side - the shortest by default; with a known threat (seen, or heard / last known within bypass.threat_memory_s) the
// side where the wall stands between the squad and the threat, when that detour is at most bypass.max_detour longer - and the leader's
// order is steered through the chosen side's corners (BypassRadius off the wall: room for the column to wrap the end), then on to its own
// destination (AOperativeCharacter::SteerOwnMoveVia). His order stays his: its destination, its serial; his next order drops the steering.
// User decision 2026-10-25 "B" (docs/design/FORMATION_SECTORS.md v2.1; contract docs/design/FORMATION_CONTRACT.md 10.2).
// Godot reference: none (UE-only; reference simulation Tools/TrainingGround/src/movement/sim.ts planLeg).

#include "Characters/SquadSubsystem.h"

#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "CodexTactics.h"
#include "Combat/EnemyRegistrySubsystem.h"
#include "Combat/TacticalSightSubsystem.h"
#include "Engine/World.h"
#include "Formation/FormationGuardRuntimeRules.h"
#include "Formation/FormationRuntimeRules.h"
#include "Formation/FormationTuning.h"
#include "NavigationSystem.h"

namespace SquadSubsystemFormationBypassLocal
{
	/** The leg's walls are gathered this far to each side of the straight way (and past its ends), cm. */
	constexpr float BypassCorridorCm = 600.f;
	/** Squad sight off (Codex.Sight 0 / its phase): an enemy this close in a clear line of sight from an operative is a known threat, cm. */
	constexpr float FallbackThreatSightCm = 3000.f;
	/** A waypoint must project onto the nav mesh within this box, cm. */
	const FVector WaypointProjectExtent(80.f, 80.f, 250.f);

	float BypassFeetZ(const AActor& Actor)
	{
		return static_cast<float>(Actor.GetActorLocation().Z) - Actor.GetSimpleCollisionHalfHeight();
	}
}

void USquadSubsystem::ReportFormationThreat(const FVector& Location)
{
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	Sectors.ReportedThreats.Add({ FVector2D(Location), Now });
	// Old reports drop out (twice the memory: the rules filter by age anyway).
	const double Keep = 2.0 * FormationTuning::Get().BypassThreatMemoryS;
	Sectors.ReportedThreats.RemoveAll([Now, Keep](const TPair<FVector2D, double>& Each) { return Now - Each.Value > Keep; });
	UE_LOG(LogCodexTactics, Display, TEXT("[Formation] threat reported at (%.0f, %.0f)"), Location.X, Location.Y);
}

bool USquadSubsystem::GetLastFormationBypass(FFormationBypassRecord& OutRecord) const
{
	if (Sectors.BypassCount == 0)
	{
		return false;
	}
	OutRecord = Sectors.LastBypass;
	return true;
}

TArray<FFormationThreat> USquadSubsystem::GatherFormationThreats() const
{
	using namespace SquadSubsystemFormationBypassLocal;
	TArray<FFormationThreat> Threats;
	UWorld* World = GetWorld();
	if (!World)
	{
		return Threats;
	}
	const double Now = World->GetTimeSeconds();
	for (const TPair<FVector2D, double>& Report : Sectors.ReportedThreats)
	{
		Threats.Add({ Report.Key, static_cast<float>(Now - Report.Value) });
	}
	// The squad's own sight and hearing: the enemies shown now and the last-known silhouettes (seen / heard).
	const UTacticalSightSubsystem* Sight = World->GetSubsystem<UTacticalSightSubsystem>();
	TArray<FSquadKnownEnemy> Known;
	if (Sight && Sight->GetSquadKnownEnemies(Known))
	{
		for (const FSquadKnownEnemy& Each : Known)
		{
			Threats.Add({ FVector2D(Each.Location), Each.SecondsSince });
		}
		return Threats;
	}
	// The squad's sight does not run (Codex.Sight 0): an enemy an operative has a clear line of sight to now.
	for (const AEnemyCharacter* Enemy : UEnemyRegistrySubsystem::GetEnemyCharacters(World))
	{
		if (!Enemy || Enemy->IsDying())
		{
			continue;
		}
		for (const TWeakObjectPtr<AOperativeCharacter>& Member : Members)
		{
			const AOperativeCharacter* Operative = Member.Get();
			if (Operative && FVector::Dist2D(Operative->GetActorLocation(), Enemy->GetActorLocation()) <= FallbackThreatSightCm && Sight
				&& Sight->HasClearSight(UTacticalSightSubsystem::EyePoint(*Operative), UTacticalSightSubsystem::ProfilePoint(*Enemy)))
			{
				Threats.Add({ FVector2D(Enemy->GetActorLocation()), 0.f });
				break;
			}
		}
	}
	return Threats;
}

void USquadSubsystem::PlanFormationBypass(AOperativeCharacter& LeaderRef)
{
	using namespace SquadSubsystemFormationBypassLocal;
	EndFormationBypass(TEXT("a new order"));
	if (!FormationGuardRuntime::IsEnabled() || HasMultiSelection() || !LeaderRef.HasOwnMoveOrder())
	{
		return;
	}
	const FVector2D From(LeaderRef.GetActorLocation());
	const FVector2D To(LeaderRef.GetLastMoveDestination());
	const double Length = FVector2D::Distance(From, To);
	if (Length < FormationGuardRuntime::BypassMinLegCm)
	{
		return;
	}
	// The walls round the straight way (a box along it), then the first free-standing one that blocks it.
	const FVector2D Mid = (From + To) * 0.5;
	const FVector2D Half(0.5 * Length + BypassCorridorCm, BypassCorridorCm);
	const TArray<FFormationWall> Walls = GatherFormationWalls(Mid, Half, FormationMath::YawOf(To - From), BypassFeetZ(LeaderRef) + FormationRuntime::ProbeHeightCm);
	const TOptional<int32> Blocking = FormationGuardRuntime::FirstBlockingWall(Walls, From, To, FormationGuardRuntime::AgentRadiusCm, /*bFreeStandingOnly*/ true);
	if (!Blocking.IsSet())
	{
		return;
	}
	const FFormationConfig& Config = FormationTuning::Get();
	const TArray<FFormationThreat> Threats = GatherFormationThreats();
	FFormationBypassRecord Record;
	Record.Wall = Walls[Blocking.GetValue()];
	Record.From = From;
	Record.To = To;
	Record.Choice = FormationBypassRules::ChooseSide(Record.Wall, From, To, Threats, Config);
	Record.Threats = Threats.FilterByPredicate([&Config](const FFormationThreat& Each) { return Each.AgeS >= 0.f && Each.AgeS <= Config.BypassThreatMemoryS; }).Num();
	Record.Time = GetWorld()->GetTimeSeconds();
	// Steer through the chosen side's corners when they lie on the nav mesh (else the nav mesh's own way stays).
	const TArray<FVector2D> Corners = FormationGuardRuntime::BypassWaypoints(Record.Choice);
	TArray<FVector2D> Waypoints;
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	bool bOnNav = Nav != nullptr && Corners.Num() > 0;
	for (const FVector2D& Corner : Corners)
	{
		FNavLocation OnNav;
		if (!Nav || !Nav->ProjectPointToNavigation(FVector(Corner.X, Corner.Y, LeaderRef.GetActorLocation().Z), OnNav, WaypointProjectExtent))
		{
			bOnNav = false;
			break;
		}
		Waypoints.Add(FVector2D(OnNav.Location));
	}
	if (bOnNav && LeaderRef.SteerOwnMoveVia(FVector(Waypoints[0].X, Waypoints[0].Y, LeaderRef.GetActorLocation().Z)))
	{
		Record.bSteered = true;
		Sectors.Bypass.bActive = true;
		Sectors.Bypass.Serial = LeaderRef.GetMoveOrderSerial();
		Sectors.Bypass.Waypoints = MoveTemp(Waypoints);
		Sectors.Bypass.Index = 0;
	}
	Sectors.LastBypass = Record;
	++Sectors.BypassCount;
	UE_LOG(LogCodexTactics, Display, TEXT("[Formation] bypass %d: wall (%.0f, %.0f)-(%.0f, %.0f) on the %s (%s, detour %.0f %%, the shortest on the %s), %d known threats, %d corners%s"),
		Sectors.BypassCount, Record.Wall.A.X, Record.Wall.A.Y, Record.Wall.B.X, Record.Wall.B.Y, FormationGuardRuntime::WallSideText(Record.Choice.WallOn),
		FormationGuardRuntime::BypassReasonText(Record.Choice.Reason), 100.f * Record.Choice.Detour, FormationGuardRuntime::WallSideText(Record.Choice.Shortest),
		Record.Threats, Corners.Num(), Record.bSteered ? TEXT(", steered") : TEXT(", not steered (off the nav mesh)"));
}

void USquadSubsystem::UpdateFormationBypass(AOperativeCharacter& LeaderRef)
{
	if (!Sectors.Bypass.bActive)
	{
		return;
	}
	if (LeaderRef.GetMoveOrderSerial() != Sectors.Bypass.Serial)
	{
		EndFormationBypass(TEXT("a new order"));
		return;
	}
	if (!LeaderRef.HasOwnMoveOrder())
	{
		EndFormationBypass(TEXT("the order ended"));
		return;
	}
	const FVector2D Here(LeaderRef.GetActorLocation());
	int32 Next = FormationGuardRuntime::AdvanceWaypoint(Sectors.Bypass.Waypoints, Sectors.Bypass.Index, Here);
	if (Next == Sectors.Bypass.Index)
	{
		if (LeaderRef.IsOwnMoveSteered())
		{
			return; // on his way to the current waypoint
		}
		Next = Sectors.Bypass.Index + 1; // his walk there ended by itself (reached / failed: the order already went on) - the next one
	}
	Sectors.Bypass.Index = Next;
	if (Next >= Sectors.Bypass.Waypoints.Num())
	{
		LeaderRef.ResumeOwnMove(); // past the last corner: on to his destination
		EndFormationBypass(TEXT("round the wall"));
		return;
	}
	const FVector2D& Waypoint = Sectors.Bypass.Waypoints[Next];
	if (!LeaderRef.SteerOwnMoveVia(FVector(Waypoint.X, Waypoint.Y, LeaderRef.GetActorLocation().Z)))
	{
		EndFormationBypass(TEXT("the next corner cannot be reached"));
	}
}

void USquadSubsystem::EndFormationBypass(const TCHAR* Why)
{
	if (!Sectors.Bypass.bActive)
	{
		return;
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[Formation] bypass steering over at waypoint %d / %d: %s"), Sectors.Bypass.Index, Sectors.Bypass.Waypoints.Num(), Why);
	Sectors.Bypass = FFormationSectorState::FBypassSteer();
}
