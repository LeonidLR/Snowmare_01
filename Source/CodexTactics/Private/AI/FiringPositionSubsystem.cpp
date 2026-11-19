// Firing position service (smart enemies T-05). See FiringPositionSubsystem.h.

#include "AI/FiringPositionSubsystem.h"

#include "AI/PmcCommanderRules.h"
#include "AI/PmcDefenceRules.h"
#include "AI/PmcSquadSubsystem.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Interactables/TurretActor.h"
#include "LevelGen/TacticalMapSubsystem.h"
#include "NavigationSystem.h"
#include "Tactics/CoverRules.h"
#include "Tactics/CoverTraceRules.h"

namespace FiringPositionSubsystemLocal
{
	/** The nearest-to-ignore set of a real shot line: every operative and turret (AOperativeCharacter::EvaluateShotLine, AddShotLineAllies). */
	void AddShotLineIgnored(const UWorld* World, FCollisionQueryParams& Params)
	{
		for (TActorIterator<AOperativeCharacter> It(World); It; ++It)
		{
			Params.AddIgnoredActor(*It);
		}
		for (TActorIterator<ATurretActor> It(World); It; ++It)
		{
			Params.AddIgnoredActor(*It);
		}
	}

	float YawOf(const FVector& From, const FVector& To)
	{
		return FMath::RadiansToDegrees(FMath::Atan2(static_cast<float>(To.Y - From.Y), static_cast<float>(To.X - From.X)));
	}

	float YawDelta(float A, float B)
	{
		return FMath::Abs(FMath::FindDeltaAngleDegrees(A, B));
	}

	/** A cover candidate may be tried as a position only within this CoverQualityAgainst (FindCoverNear's 0.35). */
	constexpr float MinCoverQuality = 0.35f;
	/** A rim step is on the walkable surface when the navmesh has a point this close (planar), cm. */
	constexpr float WalkSurfaceCm = 12.f;
	/** A cover wall tried again within this cell is the same slot, cm. */
	constexpr float SlotCellCm = 150.f;
}

struct UFiringPositionSubsystem::FFiringScanContext
{
	UWorld* World = nullptr;
	FCollisionQueryParams Params;
	FVector Centre = FVector::ZeroVector;
	float Radius = 0.f;
	float BandMin = 0.f;
	float BandMax = 0.f;
	/** The standable sample points of the area, lifted to the aim height. */
	TArray<FVector> Aims;
	const UTacticalMapSubsystem* Map = nullptr;
	float FloorToleranceCm = 150.f;
	int32 Traces = 0;
	int32 RoofFaults = 0;
};

bool UFiringPositionSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

UFiringPositionSubsystem* UFiringPositionSubsystem::Get(const UWorld* World)
{
	return World ? World->GetSubsystem<UFiringPositionSubsystem>() : nullptr;
}

const FFiringPositionConfig& UFiringPositionSubsystem::GetConfig() const
{
	if (!bConfigLoaded)
	{
		UFiringPositionSubsystem* Mutable = const_cast<UFiringPositionSubsystem*>(this);
		Mutable->Config = FiringPositionRules::LoadConfig();
		Mutable->bConfigLoaded = true;
	}
	return Config;
}

void UFiringPositionSubsystem::SetConfig(const FFiringPositionConfig& InConfig)
{
	Config = InConfig;
	bConfigLoaded = true;
	Cache.Reset();
}

void UFiringPositionSubsystem::InvalidateCache()
{
	Cache.Reset();
}

FString UFiringPositionSubsystem::AreaKey(const FVector& AreaCenter, float AreaRadiusCm, float BandMinCm, float BandMaxCm) const
{
	return FString::Printf(TEXT("%d,%d,%d|%d|%d,%d"), FMath::RoundToInt(static_cast<float>(AreaCenter.X) / 50.f), FMath::RoundToInt(static_cast<float>(AreaCenter.Y) / 50.f),
		FMath::RoundToInt(static_cast<float>(AreaCenter.Z) / 50.f), FMath::RoundToInt(AreaRadiusCm / 50.f), FMath::RoundToInt(BandMinCm / 100.f), FMath::RoundToInt(BandMaxCm / 100.f));
}

bool UFiringPositionSubsystem::ProjectOnNav(const FVector& Point, const FVector& Extent, FVector& Out) const
{
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!Nav)
	{
		Out = Point; // no navigation in this world: nothing to check against
		return true;
	}
	FNavLocation OnNav;
	if (!Nav->ProjectPointToNavigation(Point, OnNav, Extent))
	{
		return false;
	}
	Out = OnNav.Location;
	return true;
}

bool UFiringPositionSubsystem::FindWalkSurface(const FVector& Point, float HorizontalCm, float VerticalCm, FVector& Out) const
{
	if (UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
	{
		FNavLocation OnNav;
		if (Nav->ProjectPointToNavigation(Point, OnNav, FVector(HorizontalCm, HorizontalCm, VerticalCm)) && FVector::Dist2D(OnNav.Location, Point) <= HorizontalCm)
		{
			Out = OnNav.Location;
			return true;
		}
	}
	else
	{
		Out = Point; // no navigation in this world: nothing to check against
		return true;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(FiringPositionFloor), false);
	FiringPositionSubsystemLocal::AddShotLineIgnored(World, Params);
	FHitResult Hit;
	if (World->LineTraceSingleByChannel(Hit, Point + FVector(0.f, 0.f, 30.f), Point - FVector(0.f, 0.f, 30.f), ECC_Visibility, Params) && !Hit.bStartPenetrating
		&& Hit.ImpactNormal.Z > 0.7f)
	{
		Out = FVector(Point.X, Point.Y, Hit.ImpactPoint.Z);
		return true;
	}
	return false;
}

bool UFiringPositionSubsystem::LineBlocked(FFiringScanContext& Context, const FVector& From, const FVector& To) const
{
	FHitResult Hit;
	++Context.Traces;
	return Context.World->LineTraceSingleByChannel(Hit, From, To, ECC_Visibility, Context.Params);
}

void UFiringPositionSubsystem::BuildContext(FFiringScanContext& Context, const FVector& AreaCenter, float AreaRadiusCm, float BandMinCm, float BandMaxCm) const
{
	Context.World = GetWorld();
	Context.Params = FCollisionQueryParams(SCENE_QUERY_STAT(FiringPositionLine), false);
	FiringPositionSubsystemLocal::AddShotLineIgnored(Context.World, Context.Params);
	Context.Centre = AreaCenter;
	Context.Radius = AreaRadiusCm;
	Context.BandMin = BandMinCm;
	Context.BandMax = BandMaxCm;
	Context.Map = Context.World ? Context.World->GetSubsystem<UTacticalMapSubsystem>() : nullptr;
	if (Context.Map && Context.Map->IsLoaded())
	{
		Context.FloorToleranceCm = Context.Map->GetConfig().FloorToleranceCm;
	}
	else
	{
		Context.Map = nullptr;
	}
	const FFiringPositionConfig& Cfg = GetConfig();
	for (const FVector& Sample : FiringPositionRules::AreaSamplePoints(AreaCenter, AreaRadiusCm, Cfg.AreaSamples))
	{
		// A sample is where a target could STAND: off the navmesh (inside a building, a wall) it would not be a target.
		FVector OnNav;
		if (ProjectOnNav(Sample, FVector(100.f, 100.f, 150.f), OnNav) && FMath::Abs(OnNav.Z - AreaCenter.Z) <= Context.FloorToleranceCm)
		{
			Context.Aims.Add(OnNav + FVector(0.f, 0.f, Cfg.SampleHeightCm));
		}
	}
}

int32 UFiringPositionSubsystem::CountClear(FFiringScanContext& Context, const FVector& Origin, float SectorYaw, float SectorHalfFovDeg, FVector* OutMeanAim,
	FVector* OutFirstClearAim) const
{
	int32 Hits = 0;
	FVector Sum = FVector::ZeroVector;
	for (const FVector& Aim : Context.Aims)
	{
		if (!FiringPositionRules::SampleInBand(static_cast<float>(FVector::Dist2D(Origin, Aim)), Context.BandMin, Context.BandMax, 0.f))
		{
			continue;
		}
		if (SectorHalfFovDeg > 0.f && FiringPositionSubsystemLocal::YawDelta(FiringPositionSubsystemLocal::YawOf(Origin, Aim), SectorYaw) > SectorHalfFovDeg)
		{
			continue;
		}
		if (!LineBlocked(Context, Origin, Aim))
		{
			if (Hits == 0 && OutFirstClearAim)
			{
				*OutFirstClearAim = Aim;
			}
			++Hits;
			Sum += Aim;
		}
	}
	if (OutMeanAim && Hits > 0)
	{
		*OutMeanAim = Sum / static_cast<double>(Hits);
	}
	return Hits;
}

void UFiringPositionSubsystem::FinishCandidate(FFiringScanContext& Context, FFiringCandidate& Candidate, float Protect, float Escape) const
{
	const FFiringPositionConfig& Cfg = GetConfig();
	FFiringPosition& Pos = Candidate.Position;
	Pos.bValid = Pos.CoveredShare >= Cfg.MinCoveredShare && Pos.CoveredShare > 0.f;
	FFiringScoreInputs Inputs;
	Inputs.Fire = Pos.CoveredShare;
	Inputs.Protect = Protect;
	Inputs.Escape = Escape;
	Inputs.Danger = Context.Map ? Context.Map->DangerAt(Pos.StandPoint, ETacticalSide::Pmc).Danger : 0.f;
	Candidate.Score = Pos.bValid ? FiringPositionRules::ScoreF2(Inputs, Cfg.DangerWeight) : 0.f;
}

void UFiringPositionSubsystem::GatherRoofs(FFiringScanContext& Context, TArray<FFiringCandidate>& Out)
{
	if (!Context.Map)
	{
		return;
	}
	const FFiringPositionConfig& Cfg = GetConfig();
	const float RingMin = FMath::Max(0.f, Context.BandMin - Context.Radius);
	const float RingMax = Context.BandMax > 0.f ? Context.BandMax + Context.Radius : TNumericLimits<float>::Max();
	const TArray<float> Steps = FiringPositionRules::RimSteps(Cfg);
	for (const FTacticalOverwatchRoof& Roof : Context.Map->GetFile().OverwatchRoofs)
	{
		for (int32 SpotIndex = 0; SpotIndex < Roof.Spots.Num(); ++SpotIndex)
		{
			const FTacticalOverwatchSpot& Spot = Roof.Spots[SpotIndex];
			const float Distance = static_cast<float>(FVector::Dist2D(Spot.Location, Context.Centre));
			if (Distance < RingMin || Distance > RingMax)
			{
				continue;
			}
			FVector Walk;
			if (!FindWalkSurface(Spot.Location, 60.f, 40.f, Walk))
			{
				continue; // not on the roof's walkable surface: no man stands there
			}
			const FVector Dir = (Context.Centre - Walk).GetSafeNormal2D();
			const FString SourceId = FString::Printf(TEXT("%s#%d"), *Roof.Building, SpotIndex);
			++LastStats.RoofSpots;
			bool bFound = false;
			// The crouched shot first (the protection a parapet gives), then the standing one; each walks the rim steps outwards.
			for (const EOperativeStance Stance : { EOperativeStance::Crouching, EOperativeStance::Standing })
			{
				for (const float Step : Steps)
				{
					FVector Stand = Walk;
					if (Step > 0.f)
					{
						FVector OnSurface;
						if (!FindWalkSurface(Walk + Dir * Step, FiringPositionSubsystemLocal::WalkSurfaceCm, 40.f, OnSurface))
						{
							break; // the roof's walkable surface ends here: no further step
						}
						Stand = OnSurface;
					}
					const FVector Origin = Stand + FVector(0.f, 0.f, Cfg.MuzzleHeight(Stance));
					FVector Mean = FVector::ZeroVector;
					const int32 Hits = CountClear(Context, Origin, 0.f, 0.f, &Mean);
					const float Share = FiringPositionRules::CoveredShare(Hits, Context.Aims.Num());
					if (Share < Cfg.MinCoveredShare || Hits <= 0)
					{
						continue;
					}
					FFiringCandidate Candidate;
					FFiringPosition& Pos = Candidate.Position;
					Pos.Kind = EFiringPositionKind::RoofEdge;
					Pos.StandPoint = Stand;
					Pos.Stance = Stance;
					Pos.FacingYaw = FiringPositionSubsystemLocal::YawOf(Origin, Mean);
					Pos.CoveredShare = Share;
					Pos.SourceId = SourceId;
					Pos.FireOrigin = Origin;
					Pos.RimStepCm = Step;
					FinishCandidate(Context, Candidate, FiringPositionRules::RoofProtectOf(Roof.Level, Cfg),
						FiringPositionRules::RoofEscape(Roof.Access.Num(), Roof.bLadderOnly, Cfg));
					Out.Add(Candidate);
					bFound = true;
					break;
				}
				if (bFound)
				{
					break;
				}
			}
			if (!bFound)
			{
				++Context.RoofFaults;
				UE_LOG(LogCodexTactics, Display, TEXT("[FiringPos] roof %s: parapet blocks every stance (spot %s, steps up to %.0f cm)"), *SourceId, *Spot.Location.ToCompactString(),
					Cfg.RimStepMaxCm);
			}
		}
	}
}

void UFiringPositionSubsystem::GatherWindows(FFiringScanContext& Context, TArray<FFiringCandidate>& Out)
{
	if (!Context.Map)
	{
		return;
	}
	const FFiringPositionConfig& Cfg = GetConfig();
	const float HalfFov = Context.Map->GetConfig().WindowHalfFovDeg;
	const float RingMin = FMath::Max(0.f, Context.BandMin - Context.Radius);
	const float RingMax = Context.BandMax > 0.f ? Context.BandMax + Context.Radius : TNumericLimits<float>::Max();
	for (const FTacticalWindow& Window : Context.Map->GetFile().Windows)
	{
		const float Distance = static_cast<float>(FVector::Dist2D(Window.Stand, Context.Centre));
		if (Distance < RingMin || Distance > RingMax)
		{
			continue;
		}
		FVector Stand;
		if (!ProjectOnNav(Window.Stand, FVector(80.f, 80.f, 60.f), Stand))
		{
			continue;
		}
		++LastStats.Windows;
		const FVector Origin = Stand + FVector(0.f, 0.f, Cfg.MuzzleStandingCm);
		FVector Mean = FVector::ZeroVector;
		const int32 Hits = CountClear(Context, Origin, Window.Yaw, HalfFov, &Mean);
		FFiringCandidate Candidate;
		FFiringPosition& Pos = Candidate.Position;
		Pos.Kind = EFiringPositionKind::Window;
		Pos.StandPoint = Stand;
		Pos.Stance = EOperativeStance::Standing; // a crouch would hide him under the sill (PmcSquadSubsystem's window post)
		Pos.FacingYaw = Window.Yaw;
		Pos.CoveredShare = FiringPositionRules::CoveredShare(Hits, Context.Aims.Num());
		Pos.SourceId = Window.Id;
		Pos.FireOrigin = Origin;
		FinishCandidate(Context, Candidate, Cfg.WindowProtect, Cfg.EscapeOneWay);
		if (Candidate.Position.bValid)
		{
			Out.Add(Candidate);
		}
	}
}

void UFiringPositionSubsystem::GatherCover(FFiringScanContext& Context, TArray<FFiringCandidate>& Out)
{
	using namespace FiringPositionSubsystemLocal;
	const FFiringPositionConfig& Cfg = GetConfig();
	const float RingMin = FMath::Max(0.f, Context.BandMin - Context.Radius);
	const float RingMax = Context.BandMax > 0.f ? Context.BandMax + Context.Radius : RingMin + 3000.f;
	// The polar grid of scan points over the ring, thinned to MaxScanPoints (a deterministic stride).
	TArray<FVector> Raw;
	int32 RingIndex = 0;
	for (float Radius = RingMin + Cfg.ScanStepCm * 0.5f; Radius <= RingMax; Radius += Cfg.ScanStepCm, ++RingIndex)
	{
		const int32 Count = FMath::Max(1, FMath::CeilToInt(2.f * PI * Radius / Cfg.ScanStepCm));
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const float Angle = 2.f * PI * (static_cast<float>(Index) + (RingIndex % 2 == 0 ? 0.f : 0.5f)) / static_cast<float>(Count);
			Raw.Add(Context.Centre + FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0.f));
		}
	}
	const int32 Stride = FMath::Max(1, FMath::CeilToInt(static_cast<float>(Raw.Num()) / static_cast<float>(Cfg.MaxScanPoints)));
	TSet<FString> Seen;
	int32 Slots = 0;
	for (int32 Index = 0; Index < Raw.Num() && Slots < Cfg.MaxCoverSlots; Index += Stride)
	{
		FVector OnNav;
		if (!ProjectOnNav(Raw[Index], FVector(150.f, 150.f, 150.f), OnNav) || FMath::Abs(OnNav.Z - Context.Centre.Z) > Context.FloorToleranceCm)
		{
			continue;
		}
		++LastStats.ScanPoints;
		const FVector RayOrigin = OnNav + FVector(0.f, 0.f, 90.f);
		for (int32 Ray = 0; Ray < Cfg.ScanRays && Slots < Cfg.MaxCoverSlots; ++Ray)
		{
			const FVector Direction = FRotator(0.f, 360.f * static_cast<float>(Ray) / static_cast<float>(Cfg.ScanRays), 0.f).Vector();
			FCoverSlot Slot;
			auto Prefilter = [&](const FHitResult& Hit)
			{
				// Cheap: the wall must stand between the slot and the area (CoverQualityAgainst) and the wall's cell must be new.
				FVector Normal(Hit.ImpactNormal.X, Hit.ImpactNormal.Y, 0.f);
				if (FMath::Abs(Hit.ImpactNormal.Z) > 0.3f || !Normal.Normalize())
				{
					return false;
				}
				if (FVector::DotProduct(Normal, Direction) > 0.f)
				{
					Normal = -Normal;
				}
				const FVector Approx = Hit.ImpactPoint + Normal * 45.f;
				if (PmcCommanderRules::CoverQualityAgainst(Approx, Normal, Context.Centre) < MinCoverQuality)
				{
					return false;
				}
				const FString Cell = FString::Printf(TEXT("%s|%d,%d,%d"), Hit.GetActor() ? *Hit.GetActor()->GetPathName() : TEXT(""), FMath::FloorToInt(static_cast<float>(Approx.X) / SlotCellCm),
					FMath::FloorToInt(static_cast<float>(Approx.Y) / SlotCellCm), FMath::FloorToInt(static_cast<float>(Approx.Z) / SlotCellCm));
				bool bAlready = false;
				Seen.Add(Cell, &bAlready);
				return !bAlready;
			};
			if (!UPmcSquadSubsystem::CoverSlotAlongRay(Context.World, RayOrigin, Direction, Cfg.ScanReachCm, Context.Params, Slot, Prefilter))
			{
				continue;
			}
			++Slots;
			if (FMath::Abs(Slot.WorldLocation.Z - Context.Centre.Z) > Context.FloorToleranceCm)
			{
				continue;
			}
			const float Distance = static_cast<float>(FVector::Dist2D(Slot.WorldLocation, Context.Centre));
			if (Distance < RingMin || Distance > RingMax)
			{
				continue;
			}
			const float Quality = PmcCommanderRules::CoverQualityAgainst(Slot.WorldLocation, Slot.WallNormal, Context.Centre);
			if (Quality < MinCoverQuality)
			{
				continue;
			}
			const FString WallId = Slot.WallActor.IsValid() ? Slot.WallActor->GetPathName() : FString();
			FFiringCandidate Best;
			float BestShare = 0.f;
			FVector BestMean = FVector::ZeroVector;
			if (Slot.Height == ECoverHeight::HighCover)
			{
				// A full wall: fire only round an exposed corner, the lean point is the muzzle (CoverRules::CornerLeanPoint).
				for (const ECoverFacing Facing : { ECoverFacing::Left, ECoverFacing::Right })
				{
					if (!Slot.IsEdgeExposed(Facing))
					{
						continue;
					}
					const FVector Muzzle = Slot.WorldLocation + FVector(0.f, 0.f, Cfg.MuzzleStandingCm);
					const FVector Origin = CoverRules::CornerLeanPoint(Slot, Facing, Muzzle, CoverRules::GetConfig().CornerPeekOffsetCm);
					FVector Mean = FVector::ZeroVector;
					const float Share = FiringPositionRules::CoveredShare(CountClear(Context, Origin, 0.f, 0.f, &Mean), Context.Aims.Num());
					if (Share > BestShare)
					{
						BestShare = Share;
						BestMean = Mean;
						Best.Position.Kind = EFiringPositionKind::Corner;
						Best.Position.Stance = EOperativeStance::Standing;
						Best.Position.FireOrigin = Origin;
					}
				}
			}
			else
			{
				// A low wall: over the top, crouched first (the protection), then standing.
				for (const EOperativeStance Stance : { EOperativeStance::Crouching, EOperativeStance::Standing })
				{
					const FVector Origin = Slot.WorldLocation + FVector(0.f, 0.f, Cfg.MuzzleHeight(Stance));
					FVector Mean = FVector::ZeroVector;
					const float Share = FiringPositionRules::CoveredShare(CountClear(Context, Origin, 0.f, 0.f, &Mean), Context.Aims.Num());
					if (Share > BestShare)
					{
						BestShare = Share;
						BestMean = Mean;
						Best.Position.Kind = EFiringPositionKind::LowCover;
						Best.Position.Stance = Stance;
						Best.Position.FireOrigin = Origin;
					}
				}
			}
			if (BestShare <= 0.f)
			{
				continue;
			}
			Best.Position.StandPoint = Slot.WorldLocation;
			Best.Position.FacingYaw = YawOf(Best.Position.FireOrigin, BestMean);
			Best.Position.CoveredShare = BestShare;
			Best.Position.CoverKey = PmcDefenceRules::CoverObjectKey(WallId, Slot.WallPoint);
			Best.Position.SourceId = FString::Printf(TEXT("%s@%s"), Slot.WallActor.IsValid() ? *Slot.WallActor->GetName() : TEXT("wall"), *Slot.WallPoint.ToCompactString());
			FinishCandidate(Context, Best, Quality, Best.Position.Kind == EFiringPositionKind::Corner ? Cfg.EscapeSecondWay : Cfg.EscapeOneWay);
			if (Best.Position.bValid)
			{
				Out.Add(Best);
			}
		}
	}
}

TArray<FFiringCandidate> UFiringPositionSubsystem::Gather(const FVector& AreaCenter, float AreaRadiusCm, float BandMinCm, float BandMaxCm)
{
	const double Start = FPlatformTime::Seconds();
	++GatherCount;
	LastStats = FFiringGatherStats();
	FFiringScanContext Context;
	BuildContext(Context, AreaCenter, AreaRadiusCm, BandMinCm, BandMaxCm);
	TArray<FFiringCandidate> Out;
	if (!Context.World || Context.Aims.IsEmpty())
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[FiringPos] area %s r %.0f cm: no standable sample point (off the navmesh?): no positions"), *AreaCenter.ToCompactString(), AreaRadiusCm);
		return Out;
	}
	GatherRoofs(Context, Out);
	GatherWindows(Context, Out);
	GatherCover(Context, Out);
	Out.StableSort([](const FFiringCandidate& A, const FFiringCandidate& B)
	{
		if (A.Score != B.Score)
		{
			return A.Score > B.Score;
		}
		return A.Position.SourceId.Compare(B.Position.SourceId, ESearchCase::CaseSensitive) < 0;
	});
	LastStats.Valid = Out.Num();
	LastStats.CoverSlots = Out.FilterByPredicate([](const FFiringCandidate& C) { return C.Position.Kind == EFiringPositionKind::Corner || C.Position.Kind == EFiringPositionKind::LowCover; }).Num();
	LastStats.Traces = Context.Traces;
	LastStats.Milliseconds = (FPlatformTime::Seconds() - Start) * 1000.0;
	UE_LOG(LogCodexTactics, Display,
		TEXT("[FiringPos] area %s r %.0f cm band %.0f..%.0f cm: %d valid (%d cover, %d roof spots tried, %d windows tried, %d scan points), %d roof faults, %d traces, %.1f ms"),
		*AreaCenter.ToCompactString(), AreaRadiusCm, BandMinCm, BandMaxCm, LastStats.Valid, LastStats.CoverSlots, LastStats.RoofSpots, LastStats.Windows, LastStats.ScanPoints,
		Context.RoofFaults, LastStats.Traces, LastStats.Milliseconds);
	return Out;
}

const TArray<FFiringCandidate>& UFiringPositionSubsystem::GetCandidates(const FVector& AreaCenter, float AreaRadiusCm, float BandMinCm, float BandMaxCm)
{
	const FString Key = AreaKey(AreaCenter, AreaRadiusCm, BandMinCm, BandMaxCm);
	if (const TArray<FFiringCandidate>* Cached = Cache.Find(Key))
	{
		return *Cached;
	}
	return Cache.Add(Key, Gather(AreaCenter, AreaRadiusCm, BandMinCm, BandMaxCm));
}

TArray<FFiringPosition> UFiringPositionSubsystem::PositionsCovering(const FVector& AreaCenter, float AreaRadiusCm, float BandMinCm, float BandMaxCm, int32 N,
	const TArray<FString>& ExcludeCoverKeys)
{
	const TArray<FFiringCandidate>& Candidates = GetCandidates(AreaCenter, AreaRadiusCm, BandMinCm, BandMaxCm);
	TArray<FFiringPosition> Result;
	if (N <= 0)
	{
		for (const FFiringCandidate& Candidate : Candidates)
		{
			if (Candidate.Position.bValid && !(Candidate.Position.CoverKey.Len() > 0 && ExcludeCoverKeys.Contains(Candidate.Position.CoverKey)))
			{
				Result.Add(Candidate.Position);
			}
		}
		return Result;
	}
	for (const int32 Index : FiringPositionRules::PickN(Candidates, N, ExcludeCoverKeys, GetConfig()))
	{
		Result.Add(Candidates[Index].Position);
	}
	return Result;
}

bool UFiringPositionSubsystem::PickCrossfirePair(const FVector& KillZone, float AreaRadiusCm, float BandMinCm, float BandMaxCm, const TArray<FString>& ExcludeCoverKeys,
	FFiringPosition& OutFirst, FFiringPosition& OutSecond)
{
	const TArray<FFiringCandidate>& Candidates = GetCandidates(KillZone, AreaRadiusCm, BandMinCm, BandMaxCm);
	int32 First = INDEX_NONE;
	int32 Second = INDEX_NONE;
	if (!FiringPositionRules::PickCrossfirePair(Candidates, KillZone, ExcludeCoverKeys, GetConfig(), First, Second))
	{
		return false;
	}
	OutFirst = Candidates[First].Position;
	OutSecond = Candidates[Second].Position;
	return true;
}

void UFiringPositionSubsystem::RetracePosition(const FFiringPosition& Position, const FVector& AreaCenter, float AreaRadiusCm, float BandMinCm, float BandMaxCm,
	int32& OutClear, int32& OutSamples, FVector* OutClearFloorPoint) const
{
	FFiringScanContext Context;
	BuildContext(Context, AreaCenter, AreaRadiusCm, BandMinCm, BandMaxCm);
	OutSamples = Context.Aims.Num();
	const bool bWindow = Position.Kind == EFiringPositionKind::Window && Context.Map;
	FVector FirstClear = FVector::ZeroVector;
	OutClear = CountClear(Context, Position.FireOrigin, Position.FacingYaw, bWindow ? Context.Map->GetConfig().WindowHalfFovDeg : 0.f, nullptr, &FirstClear);
	if (OutClearFloorPoint)
	{
		*OutClearFloorPoint = OutClear > 0 ? FirstClear - FVector(0.f, 0.f, GetConfig().SampleHeightCm) : FVector::ZeroVector;
	}
}

TArray<FFiringTraceDiag> UFiringPositionSubsystem::DiagnoseRoofSpot(const FVector& SpotFeet, float FacingYaw, const FVector& Target) const
{
	(void)FacingYaw;
	TArray<FFiringTraceDiag> Out;
	UWorld* World = GetWorld();
	if (!World)
	{
		return Out;
	}
	const FFiringPositionConfig& Cfg = GetConfig();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(FiringPositionDiag), false);
	FiringPositionSubsystemLocal::AddShotLineIgnored(World, Params);
	const FVector Dir = (Target - SpotFeet).GetSafeNormal2D();
	const FVector Aim = Target + FVector(0.f, 0.f, Cfg.SampleHeightCm);
	for (const EOperativeStance Stance : { EOperativeStance::Standing, EOperativeStance::Crouching, EOperativeStance::Prone })
	{
		for (const float Step : FiringPositionRules::RimSteps(Cfg))
		{
			FFiringTraceDiag Diag;
			Diag.Stance = Stance;
			Diag.StepCm = Step;
			FVector Stand = SpotFeet + Dir * Step;
			FVector OnSurface;
			if (FindWalkSurface(Stand, FiringPositionSubsystemLocal::WalkSurfaceCm, 40.f, OnSurface))
			{
				Diag.bOnWalkSurface = true;
				Stand = OnSurface;
			}
			else
			{
				Diag.bOnWalkSurface = false;
			}
			Diag.Muzzle = Stand + FVector(0.f, 0.f, Cfg.MuzzleHeight(Stance));
			Diag.Aim = Aim;
			FHitResult Hit;
			Diag.bBlocked = World->LineTraceSingleByChannel(Hit, Diag.Muzzle, Aim, ECC_Visibility, Params);
			if (Diag.bBlocked)
			{
				Diag.HitActor = GetNameSafe(Hit.GetActor());
				Diag.HitComponent = GetNameSafe(Hit.GetComponent());
				Diag.HitDistanceCm = Hit.Distance;
				Diag.HitHeightCm = static_cast<float>(Hit.ImpactPoint.Z - SpotFeet.Z);
				// The top of what was hit: straight down on it, just inside its face.
				const FVector Inside = Hit.ImpactPoint + (Aim - Diag.Muzzle).GetSafeNormal2D() * 5.f;
				FHitResult Top;
				if (World->LineTraceSingleByChannel(Top, FVector(Inside.X, Inside.Y, SpotFeet.Z + 500.f), FVector(Inside.X, Inside.Y, SpotFeet.Z - 5.f), ECC_Visibility, Params))
				{
					Diag.HitTopCm = static_cast<float>(Top.ImpactPoint.Z - SpotFeet.Z);
				}
			}
			Out.Add(Diag);
		}
	}
	return Out;
}

void UFiringPositionSubsystem::Dump(const FVector& Centre, float AreaRadiusCm, float BandMinCm, float BandMaxCm, int32 Count)
{
	const TArray<FFiringPosition> Positions = PositionsCovering(Centre, AreaRadiusCm, BandMinCm, BandMaxCm, Count);
	UE_LOG(LogCodexTactics, Display, TEXT("[FiringPos] dump: area %s r %.0f cm band %.0f..%.0f cm -> %d positions (of %d valid)"), *Centre.ToCompactString(), AreaRadiusCm,
		BandMinCm, BandMaxCm, Positions.Num(), GetCandidates(Centre, AreaRadiusCm, BandMinCm, BandMaxCm).Num());
	for (const FFiringPosition& Position : Positions)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("%s"), *FiringPositionRules::LogLine(Position));
	}
}

namespace FiringPositionSubsystemLocal
{
	static FAutoConsoleCommandWithWorldAndArgs CmdFiringPositions(
		TEXT("CodexTactics.FiringPositions"),
		TEXT("T-05: logs the firing positions covering an area round the first player pawn (or the squad leader): <radius_m> [band_min_m band_max_m] [count]. Defaults 10 m, band 15..60 m, 12."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
		{
			UFiringPositionSubsystem* Service = UFiringPositionSubsystem::Get(World);
			if (!Service)
			{
				UE_LOG(LogCodexTactics, Display, TEXT("[FiringPos] no firing position subsystem in this world"));
				return;
			}
			FVector Centre = FVector::ZeroVector;
			if (const USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>(); Squad && Squad->GetLeader())
			{
				Centre = Squad->GetLeader()->GetActorLocation() - FVector(0.f, 0.f, Squad->GetLeader()->GetSimpleCollisionHalfHeight());
			}
			const float RadiusM = Args.Num() > 0 ? FCString::Atof(*Args[0]) : 10.f;
			const float BandMinM = Args.Num() > 2 ? FCString::Atof(*Args[1]) : 15.f;
			const float BandMaxM = Args.Num() > 2 ? FCString::Atof(*Args[2]) : 60.f;
			const int32 Count = Args.Num() > 3 ? FCString::Atoi(*Args[3]) : 12;
			Service->Dump(Centre, RadiusM * 100.f, BandMinM * 100.f, BandMaxM * 100.f, Count);
		}));
}
