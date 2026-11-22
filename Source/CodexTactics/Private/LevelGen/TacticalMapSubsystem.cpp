#include "LevelGen/TacticalMapSubsystem.h"

#include "CodexTactics.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace TacticalMapSubsystemLocal
{
	static TAutoConsoleVariable<int32> CVarDraw(
		TEXT("Codex.TacticalMap.Draw"),
		0,
		TEXT("TACTICAL_MAP P1: 1 draws the tactical map round the view (places red by score, drop spots orange, windows cyan, roof spots magenta, ")
		TEXT("hiding spots green / thin line = stealth-only, ladder-only roofs yellow); 0 (default) off."),
		ECVF_Default);

	static FAutoConsoleCommandWithWorld CmdTacticalMapDump(
		TEXT("Codex.TacticalMap.Dump"),
		TEXT("Logs the tactical map of this level: the counts, the top places and the danger at the player's pawn."),
		FConsoleCommandWithWorldDelegate::CreateStatic([](UWorld* World)
		{
			if (const UTacticalMapSubsystem* Tactical = World ? World->GetSubsystem<UTacticalMapSubsystem>() : nullptr)
			{
				Tactical->Dump();
			}
			else
			{
				UE_LOG(LogCodexTactics, Display, TEXT("[TacticalMap] no tactical map subsystem in this world"));
			}
		}));

	FColor ScoreColor(float Score)
	{
		const uint8 G = static_cast<uint8>(FMath::Clamp(1.f - Score, 0.f, 1.f) * 200.f);
		return FColor(255, G, 0);
	}

	FVector YawArrow(float YawDeg, float Length)
	{
		const float R = FMath::DegreesToRadians(YawDeg);
		return FVector(FMath::Cos(R) * Length, FMath::Sin(R) * Length, 0.f);
	}

	bool Near(const FVector& A, const FVector& B, float Radius)
	{
		return FVector::DistSquared2D(A, B) <= FMath::Square(Radius);
	}
}

bool UTacticalMapSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UTacticalMapSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UTacticalMapSubsystem, STATGROUP_Tickables);
}

bool UTacticalMapSubsystem::IsTickable() const
{
	return bLoaded && IsDrawEnabled();
}

bool UTacticalMapSubsystem::IsDrawEnabled()
{
	return TacticalMapSubsystemLocal::CVarDraw.GetValueOnGameThread() != 0;
}

void UTacticalMapSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	LoadConfig();
	const FString MapName = UWorld::RemovePIEPrefix(InWorld.GetMapName());
	const FString Path = TacticalMapRules::GetDefaultPathForMap(MapName);
	if (MapName.IsEmpty() || !FPaths::FileExists(Path))
	{
		return; // no tactical file: an empty map, every query answers "nothing here"
	}
	LoadFromFile(Path);
}

void UTacticalMapSubsystem::Deinitialize()
{
	Clear();
	Super::Deinitialize();
}

void UTacticalMapSubsystem::LoadConfig()
{
	Config = FTacticalRuntimeConfig();
	const FString Path = TacticalMapRules::GetConfigPath();
	FString Json;
	if (!FPaths::FileExists(Path) || !FFileHelper::LoadFileToString(Json, *Path))
	{
		UE_LOG(LogCodexTactics, Verbose, TEXT("[TacticalMap] no %s: default query settings"), *Path);
		return;
	}
	TArray<FString> Errors;
	FTacticalRuntimeConfig Parsed;
	if (TacticalMapRules::ParseConfig(Json, Parsed, Errors))
	{
		Config = Parsed;
	}
	for (const FString& Error : Errors)
	{
		UE_LOG(LogCodexTactics, Warning, TEXT("[TacticalMap] %s: %s"), *Path, *Error);
	}
}

bool UTacticalMapSubsystem::LoadFromFile(const FString& Path)
{
	Clear();
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *Path))
	{
		UE_LOG(LogCodexTactics, Warning, TEXT("[TacticalMap] cannot read %s"), *Path);
		return false;
	}
	TArray<FString> Errors;
	FTacticalMapFile Parsed;
	const bool bOk = TacticalMapRules::Parse(Json, Parsed, Errors);
	for (const FString& Error : Errors)
	{
		UE_LOG(LogCodexTactics, Warning, TEXT("[TacticalMap] %s: %s"), *Path, *Error);
	}
	if (!bOk)
	{
		return false;
	}
	File = MoveTemp(Parsed);
	LoadedPath = Path;
	bLoaded = true;
	UE_LOG(LogCodexTactics, Display, TEXT("[TacticalMap] %s: %d places, %d drop spots, %d windows, %d overwatch roofs, %d hiding spots, %d ladder-only roofs, %d walkable roofs, %d open crossings, %d goal sites (%s, hash %s)"),
		*FPaths::GetCleanFilename(Path), File.Chokepoints.Num(), File.DropSpots.Num(), File.Windows.Num(), File.OverwatchRoofs.Num(), File.HidingSpots.Num(),
		File.LadderOnlyRoofs.Num(), File.WalkableRoofs.Num(), File.OpenCrossings.Num(), File.GoalSites.Num(), *File.Layout, *File.GeometryHash);
	return true;
}

void UTacticalMapSubsystem::Clear()
{
	File = FTacticalMapFile();
	LoadedPath.Reset();
	bLoaded = false;
}

void UTacticalMapSubsystem::Dump() const
{
	if (!bLoaded)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[TacticalMap] no tactical map on this level (empty map)"));
		return;
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[TacticalMap] %s (layout %s, hash %s, %s / %s, ladder-only factor %.2f)"), *LoadedPath, *File.Layout, *File.GeometryHash,
		*File.Weather, *File.TimeOfDay, File.Thresholds.LadderOnlyFactor);
	UE_LOG(LogCodexTactics, Display, TEXT("  places %d, drop spots %d, windows %d, overwatch roofs %d, hiding spots %d, ladder-only roofs %d, walkable roofs %d, open crossings %d, goal sites %d"),
		File.Chokepoints.Num(), File.DropSpots.Num(), File.Windows.Num(), File.OverwatchRoofs.Num(), File.HidingSpots.Num(), File.LadderOnlyRoofs.Num(),
		File.WalkableRoofs.Num(), File.OpenCrossings.Num(), File.GoalSites.Num());
	for (int32 Index = 0; Index < FMath::Min(10, File.Chokepoints.Num()); ++Index)
	{
		const FTacticalChokepoint& Zone = File.Chokepoints[Index];
		UE_LOG(LogCodexTactics, Display, TEXT("  %s %s %.3f at %s width %.1f m, %s%s"), *Zone.Id, *Zone.Kind.ToString(), Zone.Score, *Zone.Location.ToCompactString(),
			Zone.WidthM, *Zone.Alternative, Zone.DetourM >= 0.f ? *FString::Printf(TEXT(" %.1f m"), Zone.DetourM) : TEXT(""));
	}
	const UWorld* World = GetWorld();
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (const APawn* Pawn = PC ? PC->GetPawn() : nullptr)
	{
		const FTacticalDanger Danger = DangerAt(Pawn->GetNavAgentLocation(), ETacticalSide::Squad);
		UE_LOG(LogCodexTactics, Display, TEXT("  danger at the pawn %s: %.2f (%s)"), *Pawn->GetActorLocation().ToCompactString(), Danger.Danger,
			*TacticalMapRules::JoinKinds(Danger.Kinds));
	}
}

void UTacticalMapSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!IsTickable())
	{
		return;
	}
	const UWorld* World = GetWorld();
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (!PC)
	{
		return;
	}
	FVector ViewLocation;
	FRotator ViewRotation;
	PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
	DrawDebug(ViewLocation);
}

void UTacticalMapSubsystem::DrawDebug(const FVector& ViewLocation) const
{
#if ENABLE_DRAW_DEBUG
	using namespace TacticalMapSubsystemLocal;
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const float Radius = Config.DrawRadiusCm, Lift = Config.DrawLiftCm;
	const FVector Up(0.f, 0.f, Lift);
	for (const FTacticalChokepoint& Zone : File.Chokepoints)
	{
		const bool bVisible = Zone.Bounds.bIsValid
			? Zone.Bounds.ComputeSquaredDistanceToPoint(FVector2D(ViewLocation.X, ViewLocation.Y)) <= FMath::Square(static_cast<double>(Radius))
			: Near(Zone.Location, ViewLocation, Radius);
		if (!bVisible)
		{
			continue;
		}
		const FColor Color = ScoreColor(Zone.Score);
		for (int32 I = 0; I < Zone.Poly.Num(); ++I)
		{
			const FVector2D A = Zone.Poly[I], B = Zone.Poly[(I + 1) % Zone.Poly.Num()];
			DrawDebugLine(World, FVector(A, Zone.Location.Z) + Up, FVector(B, Zone.Location.Z) + Up, Color, false, -1.f, 0, 4.f);
		}
		for (int32 I = 1; I < Zone.Line.Num(); ++I)
		{
			DrawDebugLine(World, Zone.Line[I - 1] + Up, Zone.Line[I] + Up, Color, false, -1.f, 0, 1.5f);
		}
		DrawDebugString(World, Zone.Location + FVector(0.f, 0.f, 120.f), FString::Printf(TEXT("%s %s %.2f"), *Zone.Id, *Zone.Kind.ToString(), Zone.Score), nullptr, Color, -1.f, true);
	}
	for (const FTacticalDropSpot& Spot : File.DropSpots)
	{
		if (Near(Spot.Land, ViewLocation, Radius))
		{
			DrawDebugDirectionalArrow(World, Spot.Edge + Up, Spot.Land + Up, 80.f, FColor::Orange, false, -1.f, 0, 3.f);
		}
	}
	for (const FTacticalWindow& W : File.Windows)
	{
		if (Near(W.Location, ViewLocation, Radius))
		{
			DrawDebugBox(World, W.Stand + FVector(0.f, 0.f, 100.f), FVector(20.f), FColor::Cyan, false, -1.f, 0, 2.f);
			DrawDebugDirectionalArrow(World, W.Stand + FVector(0.f, 0.f, 100.f), W.Stand + FVector(0.f, 0.f, 100.f) + YawArrow(W.Yaw, 200.f), 40.f, FColor::Cyan, false, -1.f, 0, 2.f);
		}
	}
	for (const FTacticalOverwatchRoof& Roof : File.OverwatchRoofs)
	{
		for (const FTacticalOverwatchSpot& Spot : Roof.Spots)
		{
			if (Near(Spot.Location, ViewLocation, Radius))
			{
				const FVector At = Spot.Location + FVector(0.f, 0.f, 60.f);
				DrawDebugSphere(World, At, 40.f, 8, FColor::Magenta, false, -1.f, 0, 2.f);
				DrawDebugDirectionalArrow(World, At, At + YawArrow(Spot.FacingYaw, 400.f), 60.f, FColor::Magenta, false, -1.f, 0, 3.f);
			}
		}
	}
	for (const FTacticalHidingSpot& Spot : File.HidingSpots)
	{
		if (!Near(Spot.Location, ViewLocation, Radius))
		{
			continue;
		}
		const FColor Color = Spot.bStealthOnly ? FColor(120, 200, 120) : FColor::Green;
		DrawDebugCircle(World, Spot.Location + Up, 50.f, 12, Color, false, -1.f, 0, 2.f, FVector(1, 0, 0), FVector(0, 1, 0), false);
		if (Spot.bStealthOnly)
		{
			DrawDebugLine(World, Spot.Location + Up, Spot.ThreatLocation + Up, Color, false, -1.f, 0, 0.5f);
		}
		else
		{
			DrawDebugLine(World, Spot.Location + Up, Spot.ThreatLocation + Up, Color, false, -1.f, 0, 1.5f);
		}
	}
	for (const FTacticalLadderRoof& Roof : File.LadderOnlyRoofs)
	{
		if (!Near(Roof.Center, ViewLocation, Radius))
		{
			continue;
		}
		for (int32 I = 0; I < Roof.Poly.Num(); ++I)
		{
			const FVector2D A = Roof.Poly[I], B = Roof.Poly[(I + 1) % Roof.Poly.Num()];
			DrawDebugLine(World, FVector(A, Roof.Z) + Up, FVector(B, Roof.Z) + Up, FColor::Yellow, false, -1.f, 0, 3.f);
		}
	}
#endif
}
