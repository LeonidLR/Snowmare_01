#include "UI/MinimapSubsystem.h"

#include "AI/NavigationSystemBase.h"
#include "Camera/BuildingExitMarker.h"
#include "Camera/CameraZoneVolume.h"
#include "Camera/InteriorExitSubsystem.h"
#include "Camera/PlayerCameraManager.h"
#include "Camera/TacticalCameraPawn.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/EnemyGhostActor.h"
#include "Environment/EnvironmentTrace.h"
#include "Combat/EnemyRegistrySubsystem.h"
#include "Combat/HealthComponent.h"
#include "Combat/SightRules.h"
#include "Combat/TacticalSightSubsystem.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Core/CodexTacticsGameMode.h"
#include "Core/MissionSessionSubsystem.h"
#include "Data/WaveConfigTypes.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/GameInstance.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Interactables/ProximityMineActor.h"
#include "Interactables/TripwireActor.h"
#include "Misc/App.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "TextureResource.h"
#include "Tutorial/TutorialMarkerActor.h"
#include "Tutorial/TutorialSubsystem.h"
#include "UI/GameMessageSubsystem.h"

namespace MinimapSubsystemLocal
{
	TAutoConsoleVariable<int32> CVarDefaultPanel(
		TEXT("Codex.Minimap.DefaultPanel"), 0,
		TEXT("Top-right panel before the player chose one this session: 0 = messages, 1 = minimap."));
	TAutoConsoleVariable<int32> CVarCaptureSize(
		TEXT("Codex.Minimap.CaptureSize"), 2048,
		TEXT("Longer side (pixels) of each floor's minimap capture (render target, RGBA8)."));
	TAutoConsoleVariable<int32> CVarCapturePasses(
		TEXT("Codex.Minimap.CapturePasses"), 4,
		TEXT("Capture passes per floor at load (one per frame: exposure / lighting settle; the last one is kept)."));

	TAutoConsoleVariable<int32> CVarTopDown(
		TEXT("Codex.Minimap.TopDown"), 1,
		TEXT("Minimap / full map projection: 1 = straight down, map up = the level's start camera forward (game designer 2026-10-21); ")
		TEXT("0 = the old isometric view at the start camera's pitch. The dev command CodexTactics.MinimapRecapture applies a change."));

	TAutoConsoleVariable<float> CVarExposureBias(
		TEXT("Codex.Minimap.ExposureBias"), 0.f,
		TEXT("Exposure compensation (stops) added to the minimap captures (the dev command CodexTactics.MinimapRecapture applies a change)."));

	constexpr float MinimapPanelMargin = 16.f;
	constexpr float MinimapHeaderHeight = 22.f;
	constexpr float MinimapWalkableNormalZ = 0.7f;
	const FName NoMinimapTag(TEXT("NoMinimap"));

	/**
	 * Object types the map's traces see as ground / geometry: static AND dynamic (movable static-mesh actors and Blueprint
	 * pieces default to WorldDynamic - the lake's floor slab under the hut is one, 2026-10-21: with WorldStatic only, nothing
	 * was found under its roof and the roof stayed on the map); IsSolid filters what blocks visibility.
	 */
	FCollisionObjectQueryParams GroundObjects()
	{
		FCollisionObjectQueryParams Objects;
		Objects.AddObjectTypesToQuery(ECC_WorldStatic);
		Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
		return Objects;
	}

	/**
	 * Solid ground / geometry for the map: blocks visibility (object-type traces also report trigger volumes and other
	 * overlap-only boxes; a roof the cutaway has opened ignores visibility too).
	 */
	bool IsSolid(const FHitResult& Hit)
	{
		const UPrimitiveComponent* Component = Hit.GetComponent();
		return Component && Component->GetCollisionResponseToChannel(ECC_Visibility) == ECR_Block;
	}

	double NowSeconds(const UWorld* World)
	{
		return World ? World->GetRealTimeSeconds() : FPlatformTime::Seconds();
	}

	/** The feed's newest message time (0 without messages). */
	double LatestMessageTime(const UWorld* World)
	{
		const UGameMessageSubsystem* Messages = World ? World->GetSubsystem<UGameMessageSubsystem>() : nullptr;
		return Messages && !Messages->GetHistory().IsEmpty() ? Messages->GetHistory().Last().PostedAt : 0.0;
	}

	bool IsOperativeAlive(const AOperativeCharacter* Operative)
	{
		return Operative && (!Operative->HealthComponent || Operative->HealthComponent->IsAlive());
	}

	float FeetZ(const AActor& Actor)
	{
		return Actor.GetActorLocation().Z - Actor.GetSimpleCollisionHalfHeight();
	}

	/**
	 * Map facing of a world direction (unit, image space with square pixels: the HUD draws it as a screen direction). Before
	 * 2026-10-21 this was the UV delta, skewed by the image's Width / Height when they differ (yaws other than 45 deg).
	 */
	FVector2D MapFacing(const FMinimapIsoProjection& Projection, const FVector& Direction)
	{
		return Projection.MapDirection(Direction);
	}
}

bool UMinimapSubsystem::IsTopDownEnabled()
{
	return MinimapSubsystemLocal::CVarTopDown.GetValueOnGameThread() != 0;
}

bool UMinimapSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UMinimapSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	int32 Mode = MinimapSubsystemLocal::CVarDefaultPanel.GetValueOnGameThread();
	if (const UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr)
	{
		if (const UMissionSessionSubsystem* Session = GameInstance->GetSubsystem<UMissionSessionSubsystem>(); Session && Session->MinimapPanelMode >= 0)
		{
			Mode = Session->MinimapPanelMode;
		}
	}
	PanelMode = Mode == 1 ? EMinimapPanelMode::Map : EMinimapPanelMode::Messages;
}

void UMinimapSubsystem::Deinitialize()
{
	if (CaptureActor)
	{
		CaptureActor->Destroy();
		CaptureActor = nullptr;
	}
	Super::Deinitialize();
}

TStatId UMinimapSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMinimapSubsystem, STATGROUP_Tickables);
}

UMinimapSubsystem* UMinimapSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UMinimapSubsystem>() : nullptr;
}

// --- Panel / full map --------------------------------------------------------------------------------------------------

void UMinimapSubsystem::SetPanelMode(EMinimapPanelMode Mode)
{
	if (PanelMode == Mode)
	{
		return;
	}
	PanelMode = Mode;
	LastSeenMessageTime = MinimapSubsystemLocal::LatestMessageTime(GetWorld());
	if (UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr)
	{
		if (UMissionSessionSubsystem* Session = GameInstance->GetSubsystem<UMissionSessionSubsystem>())
		{
			Session->MinimapPanelMode = Mode == EMinimapPanelMode::Map ? 1 : 0;
		}
	}
	ApplyLayout();
}

EMinimapPanelMode UMinimapSubsystem::TogglePanelMode()
{
	SetPanelMode(PanelMode == EMinimapPanelMode::Map ? EMinimapPanelMode::Messages : EMinimapPanelMode::Map);
	return PanelMode;
}

void UMinimapSubsystem::SetFullMapOpen(bool bOpen)
{
	if (bFullMapOpen == bOpen)
	{
		return;
	}
	bFullMapOpen = bOpen;
	if (bOpen)
	{
		bFullViewInitialised = false; // opens fitted to the whole level
	}
	ApplyLayout();
}

bool UMinimapSubsystem::ToggleFullMap()
{
	SetFullMapOpen(!bFullMapOpen);
	return bFullMapOpen;
}

int32 UMinimapSubsystem::GetUnreadMessages() const
{
	const UGameMessageSubsystem* Messages = GetWorld() ? GetWorld()->GetSubsystem<UGameMessageSubsystem>() : nullptr;
	if (!Messages || PanelMode == EMinimapPanelMode::Messages)
	{
		return 0;
	}
	int32 Count = 0;
	for (const FGameMessage& Message : Messages->GetHistory())
	{
		Count += Message.PostedAt > LastSeenMessageTime ? 1 : 0;
	}
	return Count;
}

void UMinimapSubsystem::UpdateLayout(const FVector2D& ViewportSize, float FeedWidthPx)
{
	if (!bViewportForced && ViewportSize.X > 1.f && ViewportSize.Y > 1.f)
	{
		Viewport = ViewportSize;
	}
	FeedWidth = FeedWidthPx;
	ApplyLayout();
}

void UMinimapSubsystem::SetViewportSizeForTesting(const FVector2D& ViewportSize)
{
	bViewportForced = true;
	Viewport = ViewportSize;
	FeedWidth = ViewportSize.X * 0.38f;
	ApplyLayout();
}

FMinimapView UMinimapSubsystem::MakePanelView() const
{
	using namespace MinimapSubsystemLocal;
	const float Width = FMath::Clamp(Viewport.X * 0.26f, 240.f, 560.f);
	const float Height = Width * 0.72f;
	const FVector2D Origin(Viewport.X - MinimapPanelMargin - Width, MinimapPanelMargin + MinimapHeaderHeight + 2.f);
	FVector2D Center(0.5f, 0.5f);
	const USquadSubsystem* Squad = GetWorld() ? GetWorld()->GetSubsystem<USquadSubsystem>() : nullptr;
	if (const AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr)
	{
		Center = Projection.WorldToMap(Leader->GetActorLocation());
	}
	FMinimapView View = FMinimapView::Make(Origin, FVector2D(Width, Height), Center, PanelSpanU, Projection.GetAspect());
	View.CenterUV = MinimapRules::ClampCenter(View.CenterUV, View.SpanU, View.SpanV);
	return View;
}

void UMinimapSubsystem::ApplyLayout()
{
	using namespace MinimapSubsystemLocal;
	PanelView = MakePanelView();
	const float HeaderWidth = PanelMode == EMinimapPanelMode::Map ? PanelView.ScreenSize.X : FMath::Max(FeedWidth, 200.f);
	HeaderRect = FBox2D(FVector2D(Viewport.X - MinimapPanelMargin - HeaderWidth, MinimapPanelMargin), FVector2D(Viewport.X - MinimapPanelMargin, MinimapPanelMargin + MinimapHeaderHeight));

	const FVector2D FullOrigin(Viewport.X * 0.06f, Viewport.Y * 0.09f);
	const FVector2D FullSize(Viewport.X * 0.88f, Viewport.Y * 0.70f);
	const float FitSpan = FMinimapView::FitSpanU(FullSize, Projection.GetAspect());
	if (!bFullViewInitialised)
	{
		FullView = FMinimapView::Make(FullOrigin, FullSize, FVector2D(0.5f, 0.5f), FitSpan, Projection.GetAspect());
		bFullViewInitialised = true;
	}
	else
	{
		const FVector2D Center = FullView.CenterUV;
		FullView = FMinimapView::Make(FullOrigin, FullSize, Center, FMath::Min(FullView.SpanU, FitSpan), Projection.GetAspect());
		FullView.CenterUV = MinimapRules::ClampCenter(FullView.CenterUV, FullView.SpanU, FullView.SpanV);
	}
}

const FMinimapView* UMinimapSubsystem::GetActiveView() const
{
	if (!bReady)
	{
		return nullptr;
	}
	if (bFullMapOpen)
	{
		return &FullView;
	}
	return PanelMode == EMinimapPanelMode::Map ? &PanelView : nullptr;
}

bool UMinimapSubsystem::IsScreenPointOverMap(const FVector2D& Screen) const
{
	if (bFullMapOpen)
	{
		return bReady && FullView.Contains(Screen);
	}
	if (HeaderRect.bIsValid && HeaderRect.IsInside(Screen))
	{
		return true;
	}
	const FMinimapView* View = GetActiveView();
	return View && View->Contains(Screen);
}

bool UMinimapSubsystem::HandleWheel(const FVector2D& Screen, float Notches)
{
	const FMinimapView* View = GetActiveView();
	if (!View || !View->Contains(Screen) || Projection.WidthCm <= 0.f)
	{
		return false;
	}
	const float MinSpan = FMath::Clamp(1500.f / Projection.WidthCm, 0.01f, 1.f);
	if (bFullMapOpen)
	{
		const float FitSpan = FMinimapView::FitSpanU(FullView.ScreenSize, Projection.GetAspect());
		const float NewSpan = MinimapRules::StepZoomSpan(FullView.SpanU, Notches, MinSpan, FitSpan);
		FullView = MinimapRules::ZoomAround(FullView, Screen, NewSpan, Projection.GetAspect());
		FullView.CenterUV = MinimapRules::ClampCenter(FullView.CenterUV, FullView.SpanU, FullView.SpanV);
	}
	else
	{
		PanelSpanU = MinimapRules::StepZoomSpan(PanelSpanU, Notches, MinSpan, 1.f);
		ApplyLayout(); // the corner map stays centred on the selected operative
	}
	return true;
}

EMinimapClick UMinimapSubsystem::HandleClick(const FVector2D& Screen, bool bRightButton, FVector& OutWorld)
{
	if (!bFullMapOpen && HeaderRect.bIsValid && HeaderRect.IsInside(Screen))
	{
		if (!bRightButton)
		{
			TogglePanelMode();
		}
		return EMinimapClick::Header;
	}
	const FMinimapView* View = GetActiveView();
	if (!View || !View->Contains(Screen))
	{
		return EMinimapClick::None;
	}
	const FVector2D UV = View->ScreenToMap(Screen);
	if (UV.X < 0.f || UV.Y < 0.f || UV.X > 1.f || UV.Y > 1.f || !MapToWorld(UV, OutWorld))
	{
		return EMinimapClick::Consumed;
	}
	if (bRightButton)
	{
		if (UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
		{
			FNavLocation OnNav;
			if (Nav->ProjectPointToNavigation(OutWorld, OnNav, FVector(300.f, 300.f, 400.f)))
			{
				OutWorld = OnNav.Location;
			}
		}
		return EMinimapClick::Move;
	}
	return EMinimapClick::Camera;
}

bool UMinimapSubsystem::MapToWorld(const FVector2D& MapUV, FVector& OutWorld) const
{
	const UWorld* World = GetWorld();
	if (!World || !bReady)
	{
		return false;
	}
	const FVector Forward = Projection.GetForward();
	const float Reach = FMath::Max(Projection.WidthCm, Projection.HeightCm) + 20000.f;
	const FVector OnImage = Projection.MapToImagePlane(MapUV);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MinimapClick), false);
	if (const FMinimapFloorCapture* Capture = GetCaptureInfo(DisplayedFloor))
	{
		for (const TWeakObjectPtr<UPrimitiveComponent>& Hidden : Capture->Hidden)
		{
			if (Hidden.IsValid())
			{
				Params.AddIgnoredComponent(Hidden.Get());
			}
		}
	}
	FVector Start = OnImage - Forward * Reach;
	const FVector End = OnImage + Forward * Reach;
	const FMinimapFloorCapture* Shown = GetCaptureInfo(DisplayedFloor);
	if (Shown && Shown->ClipZ.IsSet() && Projection.IsTopDown())
	{
		Start.Z = FMath::Min(Start.Z, Shown->ClipZ.GetValue()); // what the clipped image shows starts below its clip height
	}
	for (int32 Step = 0; Step < 8; ++Step)
	{
		FHitResult Hit;
		if (!World->LineTraceSingleByObjectType(Hit, Start, End, MinimapSubsystemLocal::GroundObjects(), Params))
		{
			break;
		}
		if (MinimapSubsystemLocal::IsSolid(Hit))
		{
			OutWorld = Hit.ImpactPoint;
			return true;
		}
		Params.AddIgnoredComponent(Hit.GetComponent());
		Start = Hit.ImpactPoint;
	}
	const FMinimapFloorCapture* Capture = GetCaptureInfo(DisplayedFloor);
	OutWorld = Projection.MapToWorldOnPlane(MapUV, Capture ? Capture->PlaneZ : GroundPlaneZ);
	return true;
}

// --- Setup -------------------------------------------------------------------------------------------------------------

void UMinimapSubsystem::Tick(float DeltaTime)
{
	UWorld* World = GetWorld();
	if (!World || !World->HasBegunPlay())
	{
		return;
	}
	if (!bReady)
	{
		TrySetup();
		return;
	}
	if (PanelMode == EMinimapPanelMode::Messages)
	{
		LastSeenMessageTime = MinimapSubsystemLocal::LatestMessageTime(World);
	}
	ApplyLayout();
	RunCapturePasses();
	const double Now = MinimapSubsystemLocal::NowSeconds(World);
	if (LastUpdateTime < 0.0 || Now - LastUpdateTime >= UpdateInterval)
	{
		LastUpdateTime = Now;
		const double Start = FPlatformTime::Seconds();
		UpdateTick();
		const double Us = (FPlatformTime::Seconds() - Start) * 1.0e6;
		AverageTickUs = AverageTickUs <= 0.0 ? Us : AverageTickUs * 0.9 + Us * 0.1;
	}
}

bool UMinimapSubsystem::TrySetup()
{
	UWorld* World = GetWorld();
	const USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
	if (!Squad || !Squad->GetLeader())
	{
		return false;
	}
	const double Now = MinimapSubsystemLocal::NowSeconds(World);
	if (SetupStartTime < 0.0)
	{
		SetupStartTime = Now;
	}
	if (Now - SetupStartTime < 1.0)
	{
		return false; // the level settles (navmesh build, squad placement)
	}
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (Nav && Nav->IsNavigationBuildInProgress() && Now - SetupStartTime < 10.0)
	{
		return false;
	}
	Rebuild();
	return bReady;
}

FBox UMinimapSubsystem::ComputeArea() const
{
	UWorld* World = GetWorld();
	FBox2D Flat(ForceInit);
	ACodexTacticsGameMode* GameMode = World->GetAuthGameMode<ACodexTacticsGameMode>();
	const ULevelConfigAsset* Level = GameMode ? GameMode->PeekLevelConfig() : nullptr;
	if (Level && Level->Config.MinimapBoundsCm.bIsValid)
	{
		Flat = Level->Config.MinimapBoundsCm;
	}
	float MinZ = TNumericLimits<float>::Max();
	float MaxZ = TNumericLimits<float>::Lowest();
	if (!Flat.bIsValid)
	{
		for (TActorIterator<ANavMeshBoundsVolume> It(World); It; ++It)
		{
			const FBox Box = It->GetComponentsBoundingBox(true);
			if (Box.IsValid)
			{
				Flat += FBox2D(FVector2D(Box.Min), FVector2D(Box.Max));
				MinZ = FMath::Min(MinZ, static_cast<float>(Box.Min.Z));
				MaxZ = FMath::Max(MaxZ, static_cast<float>(Box.Max.Z));
			}
		}
	}
	if (!Flat.bIsValid)
	{
		// No navmesh bounds: the level's static geometry (huge pieces such as sky spheres left out).
		for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
		{
			const FBox Box = It->GetComponentsBoundingBox(false);
			if (Box.IsValid && Box.GetSize().GetMax() < 200000.f)
			{
				Flat += FBox2D(FVector2D(Box.Min), FVector2D(Box.Max));
			}
		}
	}
	if (!Flat.bIsValid)
	{
		const USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		const FVector Leader = Squad && Squad->GetLeader() ? Squad->GetLeader()->GetActorLocation() : FVector::ZeroVector;
		Flat = FBox2D(FVector2D(Leader) - FVector2D(5000.f), FVector2D(Leader) + FVector2D(5000.f));
	}
	if (MinZ > MaxZ)
	{
		const USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		const float LeaderZ = Squad && Squad->GetLeader() ? Squad->GetLeader()->GetActorLocation().Z : 0.f;
		MinZ = LeaderZ - 3000.f;
		MaxZ = LeaderZ + 5000.f;
	}
	return FBox(FVector(Flat.Min, MinZ), FVector(Flat.Max, MaxZ));
}

void UMinimapSubsystem::SurfacesBelow(const FVector2D& XY, float TopZ, float BottomZ, TArray<float>& OutZ, const UPrimitiveComponent* Ignore) const
{
	OutZ.Reset();
	const UWorld* World = GetWorld();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MinimapSurfaces), false);
	if (Ignore)
	{
		Params.AddIgnoredComponent(Ignore);
	}
	const FCollisionObjectQueryParams Objects = MinimapSubsystemLocal::GroundObjects();
	FVector Start(XY, TopZ);
	const FVector End(XY, BottomZ);
	for (int32 Step = 0; Step < 8 && Start.Z > BottomZ; ++Step)
	{
		FHitResult Hit;
		if (!World->LineTraceSingleByObjectType(Hit, Start, End, Objects, Params))
		{
			break;
		}
		if (Hit.ImpactNormal.Z >= MinimapSubsystemLocal::MinimapWalkableNormalZ && !Hit.bStartPenetrating && MinimapSubsystemLocal::IsSolid(Hit))
		{
			OutZ.Add(Hit.ImpactPoint.Z);
		}
		if (Hit.GetComponent())
		{
			Params.AddIgnoredComponent(Hit.GetComponent());
		}
		Start.Z = Hit.ImpactPoint.Z - 1.f;
	}
}

float UMinimapSubsystem::HeightAboveLowest(const FVector2D& XY, float Feet) const
{
	TArray<float> Surfaces;
	SurfacesBelow(XY, Feet + 40.f, Feet - 3000.f, Surfaces);
	if (Surfaces.IsEmpty())
	{
		return 0.f;
	}
	return Feet - FMath::Min(Surfaces);
}

void UMinimapSubsystem::DetectFloors(const FBox& InArea)
{
	UWorld* World = GetWorld();
	const FVector Size = InArea.GetSize();
	// Coarse pass (<= 6 m apart, so any building of 6 m or more gets a column; <= 160 x 160), then every coarse column that
	// found a walkable surface stacked over another one is refined on a 2 m grid round it (floor slabs need several samples).
	const float Spacing = FMath::Max(FMath::Clamp(FMath::Max(Size.X, Size.Y) / 96.f, 200.f, 600.f), FMath::Max(Size.X, Size.Y) / 160.f);
	const int32 CountX = FMath::Clamp(FMath::CeilToInt(Size.X / Spacing), 1, 160);
	const int32 CountY = FMath::Clamp(FMath::CeilToInt(Size.Y / Spacing), 1, 160);
	constexpr float FineStep = 200.f;
	TArray<float> Stacked;
	TArray<FVector> StackedPoints;
	TArray<float> Grounds;
	float MaxSurface = TNumericLimits<float>::Lowest();
	TArray<float> Surfaces;
	int32 Columns = 0;
	const auto Sample = [&](const FVector2D& XY, bool bCoarse) -> bool
	{
		++Columns;
		SurfacesBelow(XY, InArea.Max.Z + 200.f, InArea.Min.Z - 200.f, Surfaces);
		if (Surfaces.IsEmpty())
		{
			return false;
		}
		const float Lowest = FMath::Min(Surfaces);
		if (bCoarse)
		{
			Grounds.Add(Lowest);
		}
		MaxSurface = FMath::Max(MaxSurface, FMath::Max(Surfaces));
		bool bStacked = false;
		for (const float Z : Surfaces)
		{
			if (Z - Lowest >= FloorParams.StoreyMinCm)
			{
				Stacked.Add(Z);
				StackedPoints.Add(FVector(XY, Z));
				bStacked = true;
			}
		}
		return bStacked;
	};
	TArray<FVector2D> Refine;
	for (int32 Y = 0; Y < CountY; ++Y)
	{
		for (int32 X = 0; X < CountX; ++X)
		{
			const FVector2D XY(InArea.Min.X + (X + 0.5f) * Size.X / CountX, InArea.Min.Y + (Y + 0.5f) * Size.Y / CountY);
			if (Sample(XY, true))
			{
				Refine.Add(XY);
			}
		}
	}
	TSet<FIntPoint> Fine;
	for (const FVector2D& Centre : Refine)
	{
		const FIntPoint Min(FMath::FloorToInt((Centre.X - Spacing - InArea.Min.X) / FineStep), FMath::FloorToInt((Centre.Y - Spacing - InArea.Min.Y) / FineStep));
		const FIntPoint Max(FMath::CeilToInt((Centre.X + Spacing - InArea.Min.X) / FineStep), FMath::CeilToInt((Centre.Y + Spacing - InArea.Min.Y) / FineStep));
		for (int32 Y = Min.Y; Y <= Max.Y; ++Y)
		{
			for (int32 X = Min.X; X <= Max.X; ++X)
			{
				bool bSeen = false;
				Fine.Add(FIntPoint(X, Y), &bSeen);
				if (!bSeen && Fine.Num() <= 40000)
				{
					Sample(FVector2D(InArea.Min.X + (X + 0.5f) * FineStep, InArea.Min.Y + (Y + 0.5f) * FineStep), false);
				}
			}
		}
	}
	UE_LOG(LogCodexTactics, Display, TEXT("Minimap: floor scan %d columns (%dx%d coarse every %.0f cm, %d refined)"), Columns, CountX, CountY, Spacing, Refine.Num());
	if (!Grounds.IsEmpty())
	{
		Grounds.Sort();
		GroundPlaneZ = Grounds[Grounds.Num() / 2];
		Area = FBox(FVector(InArea.Min.X, InArea.Min.Y, Grounds[0] - 50.f), FVector(InArea.Max.X, InArea.Max.Y, FMath::Max(MaxSurface, Grounds.Last()) + 300.f));
	}
	else
	{
		const USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		GroundPlaneZ = Squad && Squad->GetLeader() ? MinimapSubsystemLocal::FeetZ(*Squad->GetLeader()) : 0.f;
		Area = FBox(FVector(InArea.Min.X, InArea.Min.Y, GroundPlaneZ - 100.f), FVector(InArea.Max.X, InArea.Max.Y, GroundPlaneZ + 600.f));
	}

	ACodexTacticsGameMode* GameMode = World->GetAuthGameMode<ACodexTacticsGameMode>();
	const ULevelConfigAsset* Level = GameMode ? GameMode->PeekLevelConfig() : nullptr;
	bFloorsFromData = Level && !Level->Config.MinimapFloorsCm.IsEmpty();
	if (bFloorsFromData)
	{
		UpperFloorBases = MinimapRules::NormalizeFloorBases(Level->Config.MinimapFloorsCm, FloorParams);
		return;
	}
	const TArray<float> Candidates = MinimapRules::DetectFloorBases(Stacked, FloorParams);
	UE_LOG(LogCodexTactics, Display, TEXT("Minimap: %d stacked samples, candidate floors [%s]"), Stacked.Num(),
		*FString::JoinBy(Candidates, TEXT(", "), [](float Z) { return FString::Printf(TEXT("%.0f"), Z); }));
	// A floor counts only where the squad can walk: a navmesh path from the leader to one of its samples (flat roofs and
	// closed lofts are stacked walkable surfaces too).
	UpperFloorBases.Reset();
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	const USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
	const AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
	for (const float Base : Candidates)
	{
		bool bReachable = !Nav || !Leader;
		int32 Tries = 0;
		for (const FVector& Point : StackedPoints)
		{
			if (bReachable || Tries >= 6)
			{
				break;
			}
			if (FMath::Abs(Point.Z - Base) > FloorParams.BinCm)
			{
				continue;
			}
			++Tries;
			FNavLocation OnNav;
			if (!Nav->ProjectPointToNavigation(Point + FVector(0.f, 0.f, 40.f), OnNav, FVector(60.f, 60.f, 100.f)) || FMath::Abs(OnNav.Location.Z - Point.Z) > 80.f)
			{
				continue;
			}
			const UNavigationPath* Path = Nav->FindPathToLocationSynchronously(World, Leader->GetActorLocation(), OnNav.Location);
			bReachable = Path && Path->IsValid() && !Path->IsPartial();
		}
		if (bReachable)
		{
			UpperFloorBases.Add(Base);
		}
		else
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Minimap: stacked surface at %.0f cm left out (no navmesh path from the squad)"), Base);
		}
	}
}

bool UMinimapSubsystem::IsCaptureHiddenActor(const AActor* Actor) const
{
	if (!Actor)
	{
		return false;
	}
	if (Actor->IsA<APawn>() || Actor->IsA<ATutorialMarkerActor>() || Actor->IsA<AEnemyGhostActor>() || Actor->IsA<AProximityMineActor>()
		|| Actor->IsA<ATripwireActor>() || Actor->IsA<ASceneCapture2D>() || Actor->ActorHasTag(MinimapSubsystemLocal::NoMinimapTag))
	{
		return true;
	}
	// Runtime-spawned helpers (pings, aim rings, previews, feedback) are not level geometry; spawned static meshes are.
	return !Actor->IsNetStartupActor() && !Actor->IsA<AStaticMeshActor>();
}

void UMinimapSubsystem::BuildHiddenLists()
{
	UWorld* World = GetWorld();
	const int32 Floors = GetFloorCount();
	Captures.SetNum(Floors);
	for (int32 Floor = 0; Floor < Floors; ++Floor)
	{
		Captures[Floor].Hidden.Reset();
		Captures[Floor].PlaneZ = Floor == 0 ? GroundPlaneZ : UpperFloorBases[Floor - 1];
		Captures[Floor].ClipZ = MinimapRules::FloorClipZ(Floor, UpperFloorBases, Projection.IsTopDown(), FloorParams);
	}
	const float GroundMin = Area.Min.Z + 50.f;
	TArray<float> Surfaces;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (IsCaptureHiddenActor(*It) || It->IsHidden())
		{
			continue;
		}
		TInlineComponentArray<UStaticMeshComponent*> Meshes(*It);
		for (UStaticMeshComponent* Mesh : Meshes)
		{
			if (!Mesh || !Mesh->IsRegistered() || !Mesh->IsVisible())
			{
				continue;
			}
			const FBox Box = Mesh->Bounds.GetBox();
			const float Bottom = Box.Min.Z;
			// Cheap prune: a piece that cannot stand clearance-high above the lowest ground, nor above an upper floor, stays.
			const bool bMaybeFloating = Bottom - GroundMin >= FloorParams.CutClearanceCm;
			if (!bMaybeFloating)
			{
				continue;
			}
			SurfacesBelow(FVector2D(Box.GetCenter()), Bottom - 2.f, GroundMin - 500.f, Surfaces, Mesh);
			const float AboveGround = Surfaces.IsEmpty() ? 0.f : Bottom - FMath::Min(Surfaces);
			for (int32 Floor = 0; Floor < Floors; ++Floor)
			{
				if (MinimapRules::HideOnFloor(Floor, Bottom, AboveGround, UpperFloorBases, FloorParams))
				{
					Captures[Floor].Hidden.Add(Mesh);
				}
			}
		}
	}
}

void UMinimapSubsystem::SetupCaptures()
{
	UWorld* World = GetWorld();
	const int32 Floors = GetFloorCount();
	const int32 Passes = FMath::Max(1, MinimapSubsystemLocal::CVarCapturePasses.GetValueOnGameThread());
	CaptureSize = Projection.ChooseTextureSize(FMath::Clamp(MinimapSubsystemLocal::CVarCaptureSize.GetValueOnGameThread(), 256, 4096));
	Projection = Projection.WithPixelAspect(CaptureSize.X, CaptureSize.Y);
	CaptureTargets.SetNum(Floors);
	for (int32 Floor = 0; Floor < Floors; ++Floor)
	{
		UTextureRenderTarget2D* Target = CaptureTargets[Floor];
		if (!Target)
		{
			Target = NewObject<UTextureRenderTarget2D>(this, *FString::Printf(TEXT("MinimapFloor%d"), Floor));
			CaptureTargets[Floor] = Target;
		}
		Target->RenderTargetFormat = RTF_RGBA8_SRGB; // FinalColorLDR is sRGB-encoded: decoded when sampled (no double gamma)
		Target->ClearColor = FLinearColor(0.02f, 0.025f, 0.035f, 1.f);
		Target->InitAutoFormat(CaptureSize.X, CaptureSize.Y);
		Target->UpdateResourceImmediate(true);
		Captures[Floor].Target = Target;
		Captures[Floor].PassesLeft = Passes;
		Captures[Floor].CaptureMs = 0.0;
		Captures[Floor].bRendered = false;
	}
	if (!CaptureActor)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.ObjectFlags |= RF_Transient;
		CaptureActor = World->SpawnActor<ASceneCapture2D>(Params);
	}
	if (!CaptureActor)
	{
		return;
	}
	USceneCaptureComponent2D* Capture = CaptureActor->GetCaptureComponent2D();
	const FVector Forward = Projection.GetForward();
	const float BackOff = FMath::Max(Projection.WidthCm, Projection.HeightCm) + 10000.f;
	CaptureActor->SetActorLocationAndRotation(Projection.Center - Forward * BackOff, FRotator(Projection.PitchDeg, Projection.YawDeg, 0.f));
	Capture->ProjectionType = ECameraProjectionMode::Orthographic;
	Capture->OrthoWidth = Projection.WidthCm;
	Capture->bAutoCalculateOrthoPlanes = false;
	Capture->bCaptureEveryFrame = false;
	Capture->bCaptureOnMovement = false;
	Capture->bAlwaysPersistRenderingState = true;
	Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_RenderScenePrimitives;
	Capture->ShowFlags.SetParticles(false);
	Capture->ShowFlags.SetFog(false);
	Capture->ShowFlags.SetVolumetricFog(false);
	Capture->ShowFlags.SetMotionBlur(false);
	Capture->ShowFlags.SetDepthOfField(false);
	Capture->ShowFlags.SetLensFlares(false);
	Capture->ShowFlags.SetDecals(true);
	// The capture stands far outside the level (orthographic back-off), outside any bounded post-process volume: it takes the
	// settings of the volume the player's camera is in (exposure, colour grading), so the map is lit like the game.
	Capture->PostProcessSettings = FPostProcessSettings();
	{
		const APlayerController* PC = World->GetFirstPlayerController();
		const FVector ViewPoint = PC && PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraLocation()
			: (PC && PC->GetPawn() ? PC->GetPawn()->GetActorLocation() : Projection.Center);
		const APostProcessVolume* Best = nullptr;
		for (TActorIterator<APostProcessVolume> It(World); It; ++It)
		{
			APostProcessVolume* Volume = *It;
			if (Volume->bEnabled && Volume->BlendWeight > 0.f && (Volume->bUnbound || Volume->EncompassesPoint(ViewPoint, 0.f, nullptr))
				&& (!Best || Volume->Priority > Best->Priority))
			{
				Best = Volume;
			}
		}
		if (Best)
		{
			Capture->PostProcessSettings = Best->Settings;
			UE_LOG(LogCodexTactics, Display, TEXT("Minimap: capture post-process from %s"), *Best->GetName());
		}
	}
	// A one-off capture has no exposure history (its eye adaptation starts far too bright and never settles in a few
	// passes): a fixed (manual) exposure, tuned by Codex.Minimap.ExposureBias. Gemini's stylized look may replace this
	// (TANDEM request).
	Capture->PostProcessSettings.bOverride_AutoExposureMethod = true;
	Capture->PostProcessSettings.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
	Capture->PostProcessSettings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	Capture->PostProcessSettings.AutoExposureApplyPhysicalCameraExposure = false;
	Capture->PostProcessSettings.bOverride_AutoExposureBias = true;
	Capture->PostProcessSettings.AutoExposureBias = MinimapSubsystemLocal::CVarExposureBias.GetValueOnGameThread();
	Capture->PostProcessBlendWeight = 1.f;
	Capture->HiddenActors.Reset();
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (IsCaptureHiddenActor(*It) && *It != CaptureActor)
		{
			Capture->HiddenActors.Add(*It);
		}
	}
}

void UMinimapSubsystem::RunCapturePasses()
{
	if (!CaptureActor)
	{
		return;
	}
	for (int32 Floor = 0; Floor < Captures.Num(); ++Floor)
	{
		FMinimapFloorCapture& Info = Captures[Floor];
		if (Info.PassesLeft <= 0)
		{
			continue;
		}
		--Info.PassesLeft;
		if (!FApp::CanEverRender() || !Info.Target.IsValid())
		{
			return; // headless (-nullrhi): the targets exist, nothing is drawn
		}
		USceneCaptureComponent2D* Capture = CaptureActor->GetCaptureComponent2D();
		Capture->TextureTarget = Info.Target.Get();
		Capture->HiddenComponents = Info.Hidden;
		// The height clip (top-down upper floors): the near plane of the downward ortho view at ClipZ.
		const bool bClip = Info.ClipZ.IsSet() && Projection.IsTopDown();
		Capture->bOverride_CustomNearClippingPlane = bClip;
		Capture->CustomNearClippingPlane = bClip ? FMath::Max(1.f, static_cast<float>(Capture->GetComponentLocation().Z) - Info.ClipZ.GetValue()) : 0.f;
		const double Start = FPlatformTime::Seconds();
		Capture->CaptureScene();
		CODEX_ENV_TRACE(Log, this, TEXT("minimap capture floor %d (passes left %d)"), Floor, Info.PassesLeft);
		Info.CaptureMs += (FPlatformTime::Seconds() - Start) * 1000.0;
		Info.bRendered = true;
		if (Info.PassesLeft == 0)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Minimap: floor %d captured (%dx%d, %d hidden pieces, %.2f ms game thread)"), Floor,
				CaptureSize.X, CaptureSize.Y, Info.Hidden.Num(), Info.CaptureMs);
		}
		return; // one capture per frame
	}
}

void UMinimapSubsystem::SetupFog(const FBox2D& FlatArea)
{
	const int32 Floors = GetFloorCount();
	FMinimapFogGrid Probe;
	Probe.Init(FlatArea, FogCellCm, Floors);
	const bool bSameGrid = Fog.IsValid() && Fog.GetSize() == Probe.GetSize() && FMath::IsNearlyEqual(Fog.GetCellCm(), Probe.GetCellCm(), 0.5f)
		&& Fog.GetBounds().Min.Equals(Probe.GetBounds().Min, 1.0);
	if (bSameGrid && Fog.GetNumFloors() == Floors)
	{
		return; // keep what was explored
	}
	TArray<FString> Kept;
	if (bSameGrid)
	{
		for (int32 Floor = 0; Floor < Fog.GetNumFloors(); ++Floor)
		{
			Kept.Add(Fog.Encode(Floor));
		}
	}
	Fog = MoveTemp(Probe);
	for (int32 Floor = 0; Floor < Kept.Num() && Floor < Floors; ++Floor)
	{
		Fog.Decode(Floor, Kept[Floor]);
	}
	FogTextures.Reset();
	FogTextureRevision.Reset();
	if (!FApp::CanEverRender())
	{
		return;
	}
	for (int32 Floor = 0; Floor < Floors; ++Floor)
	{
		UTexture2D* Texture = UTexture2D::CreateTransient(Fog.GetSize().X, Fog.GetSize().Y, PF_B8G8R8A8, *FString::Printf(TEXT("MinimapFog%d"), Floor));
		if (Texture)
		{
			Texture->SRGB = false;
			Texture->Filter = TF_Bilinear;
			Texture->AddressX = TA_Clamp;
			Texture->AddressY = TA_Clamp;
			Texture->UpdateResource();
		}
		FogTextures.Add(Texture);
		FogTextureRevision.Add(MAX_uint32);
	}
}

void UMinimapSubsystem::UpdateFogTextures()
{
	if (!FApp::CanEverRender() || !Fog.IsValid())
	{
		return;
	}
	for (int32 Floor = 0; Floor < FogTextures.Num(); ++Floor)
	{
		UTexture2D* Texture = FogTextures[Floor];
		if (!Texture || FogTextureRevision[Floor] == Fog.GetRevision())
		{
			continue;
		}
		FogTextureRevision[Floor] = Fog.GetRevision();
		const FIntPoint Size = Fog.GetSize();
		uint8* Pixels = static_cast<uint8*>(FMemory::Malloc(Size.X * Size.Y * 4));
		for (int32 Y = 0; Y < Size.Y; ++Y)
		{
			for (int32 X = 0; X < Size.X; ++X)
			{
				uint8* Pixel = Pixels + (Y * Size.X + X) * 4;
				const bool bRevealed = Fog.IsCellRevealed(Floor, X, Y);
				// Linear (the texture is not sRGB): a near-black blue, ~8 % of the level's silhouette shows through.
				Pixel[0] = 6; // B
				Pixel[1] = 4; // G
				Pixel[2] = 3; // R
				Pixel[3] = bRevealed ? 0 : 235;
			}
		}
		FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, Size.X, Size.Y);
		Texture->UpdateTextureRegions(0, 1, Region, Size.X * 4, 4, Pixels, [](uint8* SrcData, const FUpdateTextureRegion2D* Regions)
		{
			FMemory::Free(SrcData);
			delete Regions;
		});
	}
}

void UMinimapSubsystem::Rebuild()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const double Start = FPlatformTime::Seconds();
	const FBox Initial = ComputeArea();
	DetectFloors(Initial);
	float Yaw = 45.f;
	float CameraPitch = -35.26f;
	if (const APlayerController* PC = World->GetFirstPlayerController())
	{
		if (const ATacticalCameraPawn* Camera = PC->GetPawn<ATacticalCameraPawn>())
		{
			Yaw = Camera->Config.BaseYaw; // the level's start angle (the player's rotation never changes the map)
			CameraPitch = Camera->Config.Pitch;
		}
	}
	// Straight down by default (2026-10-21): map up = the start camera's forward; Codex.Minimap.TopDown 0 = the old iso view.
	const float Pitch = MinimapRules::CapturePitch(IsTopDownEnabled(), CameraPitch);
	Projection = FMinimapIsoProjection::Fit(Area, Yaw, Pitch, 500.f);
	BuildHiddenLists();
	SetupCaptures();
	// The fog covers everything the image shows (on the ground plane), so outside the explored part nothing of the capture
	// shows through, also beyond the mapped area.
	FBox2D FogArea(FVector2D(Area.Min), FVector2D(Area.Max));
	TArray<float> Planes = UpperFloorBases;
	Planes.Add(GroundPlaneZ);
	for (const float PlaneZ : Planes)
	{
		for (const FVector2D& Corner : { FVector2D(0.f, 0.f), FVector2D(1.f, 0.f), FVector2D(1.f, 1.f), FVector2D(0.f, 1.f) })
		{
			FogArea += FVector2D(Projection.MapToWorldOnPlane(Corner, PlaneZ));
		}
	}
	SetupFog(FogArea);
	if (PanelSpanU <= 0.f || !bReady)
	{
		PanelSpanU = FMath::Clamp(6000.f / FMath::Max(Projection.WidthCm, 1.f), 0.01f, 1.f);
	}
	bFullViewInitialised = false;
	Doors.Reset();
	if (UInteriorExitSubsystem* Exits = World->GetSubsystem<UInteriorExitSubsystem>())
	{
		for (TActorIterator<ACameraZoneVolume> It(World); It; ++It)
		{
			for (const FInteriorExit& Exit : Exits->FindExits(*It, It->GetActorLocation()))
			{
				Doors.Add({ Exit.DoorPoint, Exit.Outward });
			}
		}
	}
	for (TActorIterator<ABuildingExitMarker> It(World); It; ++It)
	{
		if (!Doors.ContainsByPredicate([&It](const FDoor& Door) { return FVector::Dist2D(Door.Location, It->GetActorLocation()) < 100.f; }))
		{
			Doors.Add({ It->GetActorLocation(), It->GetOutwardDirection() });
		}
	}
	SetupMs = (FPlatformTime::Seconds() - Start) * 1000.0;
	bReady = true;
	if (PendingRestore.IsValid())
	{
		const TSharedPtr<FJsonObject> State = PendingRestore;
		PendingRestore.Reset();
		RestoreSave(*State);
	}
	RefreshStaticLists();
	UpdateTick();
	ApplyLayout();
	UE_LOG(LogCodexTactics, Display, TEXT("Minimap: area %.0f x %.0f m, %d floor(s) (%s: %s), projection yaw %.1f pitch %.1f %.0f x %.0f m, capture %dx%d, fog %dx%d cells of %.0f cm, %d door(s), setup %.1f ms"),
		Area.GetSize().X / 100.f, Area.GetSize().Y / 100.f, GetFloorCount(), bFloorsFromData ? TEXT("level JSON") : TEXT("auto"),
		*FString::JoinBy(UpperFloorBases, TEXT(", "), [](float Z) { return FString::Printf(TEXT("%.0f"), Z); }),
		Projection.YawDeg, Projection.PitchDeg, Projection.WidthCm / 100.f, Projection.HeightCm / 100.f, CaptureSize.X, CaptureSize.Y,
		Fog.GetSize().X, Fog.GetSize().Y, Fog.GetCellCm(), Doors.Num(), SetupMs);
}

// --- Live state --------------------------------------------------------------------------------------------------------

void UMinimapSubsystem::UpdateTick()
{
	UWorld* World = GetWorld();
	const USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
	if (!Squad)
	{
		return;
	}
	for (const AOperativeCharacter* Member : Squad->GetMembers())
	{
		if (!MinimapSubsystemLocal::IsOperativeAlive(Member))
		{
			continue;
		}
		const float Feet = MinimapSubsystemLocal::FeetZ(*Member);
		const FVector2D XY(Member->GetActorLocation());
		const int32 Previous = OperativeFloors.FindRef(Member);
		const int32 Selected = UpperFloorBases.IsEmpty() ? 0 : MinimapRules::SelectFloor(Feet, HeightAboveLowest(XY, Feet), UpperFloorBases, Previous, FloorParams);
		// LADDERS.md 7.3: on a ladder the floor he climbed from until he is off it (the floor switches once he stands on the roof).
		const int32 Floor = MinimapRules::FloorWithLadder(Member->IsClimbingLadder() && OperativeFloors.Contains(Member), Previous, Selected);
		OperativeFloors.Add(Member, Floor);
		Fog.RevealDisc(Floor, XY, RevealRadiusCm);
	}
	for (auto It = OperativeFloors.CreateIterator(); It; ++It)
	{
		if (!It->Key.IsValid())
		{
			It.RemoveCurrent();
		}
	}
	DisplayedFloor = FMath::Clamp(GetOperativeFloor(Squad->GetLeader()), 0, GetFloorCount() - 1);
	UpdateEnemyMemory();
	UpdateFogTextures();
	const double Now = MinimapSubsystemLocal::NowSeconds(World);
	if (LastStaticRefresh < 0.0 || Now - LastStaticRefresh >= 1.0)
	{
		RefreshStaticLists();
	}
}

int32 UMinimapSubsystem::GetOperativeFloor(const AOperativeCharacter* Operative) const
{
	const int32* Floor = Operative ? OperativeFloors.Find(Operative) : nullptr;
	return Floor ? *Floor : 0;
}

void UMinimapSubsystem::RefreshStaticLists()
{
	UWorld* World = GetWorld();
	LastStaticRefresh = MinimapSubsystemLocal::NowSeconds(World);
	Markers.Reset();
	for (TActorIterator<ATutorialMarkerActor> It(World); It; ++It)
	{
		Markers.Add(*It);
	}
	Traps.Reset();
	for (TActorIterator<AInteractableActor> It(World); It; ++It)
	{
		if (It->IsA<AProximityMineActor>() || It->IsA<ATripwireActor>())
		{
			Traps.Add(*It);
		}
	}
}

static_assert(UMinimapSubsystem::EnemyFadeSeconds == SightRules::LastKnownFadeSeconds,
	"the minimap's last-known enemy icon and the exploration silhouette fade together (2026-10-19)");

void UMinimapSubsystem::UpdateEnemyMemory()
{
	UWorld* World = GetWorld();
	const double Now = MinimapSubsystemLocal::NowSeconds(World);
	const UTacticalSightSubsystem* Sight = World->GetSubsystem<UTacticalSightSubsystem>();
	// 2026-10-19: the squad's sight runs while exploring too - the map marks seen exactly what the world shows (eye height by stance).
	const bool bSightActive = Sight && Sight->IsSquadSightActive();
	const USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
	TArray<const AOperativeCharacter*, TInlineAllocator<8>> Watchers;
	for (const AOperativeCharacter* Member : Squad ? Squad->GetMembers() : TArray<AOperativeCharacter*>())
	{
		if (MinimapSubsystemLocal::IsOperativeAlive(Member))
		{
			Watchers.Add(Member);
		}
	}
	TSet<const AActor*> Present;
	for (AActor* Enemy : UEnemyRegistrySubsystem::GetTaggedEnemies(World))
	{
		if (!Enemy)
		{
			continue;
		}
		if (const AEnemyCharacter* Character = Cast<AEnemyCharacter>(Enemy); Character && Character->IsDying())
		{
			continue;
		}
		Present.Add(Enemy);
		bool bSeen = false;
		if (!Enemy->IsHidden())
		{
			if (bSightActive)
			{
				bSeen = Sight->IsVisibleToSquad(Enemy);
			}
			else
			{
				for (const AOperativeCharacter* Watcher : Watchers)
				{
					if (FVector::DistSquared(Watcher->GetActorLocation(), Enemy->GetActorLocation()) > FMath::Square(ExplorationSightCm))
					{
						continue;
					}
					if (!Sight || Sight->HasClearSight(UTacticalSightSubsystem::EyePoint(*Watcher), UTacticalSightSubsystem::ProfilePoint(*Enemy)))
					{
						bSeen = true;
						break;
					}
				}
			}
		}
		FEnemyMemory& Memory = Enemies.FindOrAdd(Enemy);
		Memory.bSeen = bSeen;
		if (bSeen)
		{
			Memory.Location = Enemy->GetActorLocation();
			Memory.Facing = Enemy->GetActorForwardVector();
			Memory.LastSeen = Now;
		}
	}
	for (auto It = Enemies.CreateIterator(); It; ++It)
	{
		if (!It->Key.IsValid() || !Present.Contains(It->Key.Get()))
		{
			It.RemoveCurrent();
		}
	}
}

void UMinimapSubsystem::BuildIcons(TArray<FMinimapIcon>& OutIcons) const
{
	using namespace MinimapSubsystemLocal;
	const double Start = FPlatformTime::Seconds();
	OutIcons.Reset();
	const UWorld* World = GetWorld();
	if (!World || !bReady)
	{
		return;
	}
	const double Now = NowSeconds(World);
	// Objectives first (under everything), then doors, traps, enemies, the squad on top.
	// 2026-10-20: the tutorial's current navigation target is highlighted (exactly one; added when its marker is not listed yet - the
	// runtime guide beacon appears between two static-list refreshes).
	const UTutorialSubsystem* Tutorial = World->GetSubsystem<UTutorialSubsystem>();
	const FTutorialGuideView* Guide = Tutorial && Tutorial->GetGuideView().bValid ? &Tutorial->GetGuideView() : nullptr;
	bool bGuideListed = false;
	for (const TWeakObjectPtr<ATutorialMarkerActor>& Marker : Markers)
	{
		if (Marker.IsValid() && Marker->IsMarkerShown())
		{
			FMinimapIcon& Icon = OutIcons.AddDefaulted_GetRef();
			Icon.Kind = EMinimapIconKind::Objective;
			Icon.World = Marker->GetActorLocation();
			Icon.Color = Marker->Color;
			Icon.Actor = Marker.Get();
			Icon.bHighlight = Guide && Guide->Marker.Get() == Marker.Get();
			bGuideListed |= Icon.bHighlight;
		}
	}
	if (Guide && !bGuideListed)
	{
		FMinimapIcon& Icon = OutIcons.AddDefaulted_GetRef();
		Icon.Kind = EMinimapIconKind::Objective;
		Icon.World = Guide->Location;
		Icon.Color = Guide->Marker.IsValid() ? Guide->Marker->Color : FLinearColor(1.f, 0.55f, 0.08f);
		Icon.Actor = Guide->Marker.Get();
		Icon.bHighlight = true;
	}
	for (const FDoor& Door : Doors)
	{
		const int32 DoorFloor = UpperFloorBases.IsEmpty() ? 0 : MinimapRules::SelectFloor(Door.Location.Z, 1000.f, UpperFloorBases, 0, FloorParams);
		if (!Fog.IsRevealed(DoorFloor, FVector2D(Door.Location)) && !Fog.IsRevealed(0, FVector2D(Door.Location)))
		{
			continue;
		}
		FMinimapIcon& Icon = OutIcons.AddDefaulted_GetRef();
		Icon.Kind = EMinimapIconKind::Door;
		Icon.World = Door.Location;
		Icon.MapFacing = MapFacing(Projection, Door.Outward);
		Icon.Color = FLinearColor(0.35f, 1.f, 0.45f);
		Icon.Label = TEXT("EXIT");
	}
	for (const TWeakObjectPtr<AInteractableActor>& Trap : Traps)
	{
		if (Trap.IsValid() && Trap->UsesDisarmApproach())
		{
			FMinimapIcon& Icon = OutIcons.AddDefaulted_GetRef();
			Icon.Kind = EMinimapIconKind::Trap;
			Icon.World = Trap->GetActorLocation();
			Icon.Color = FLinearColor(1.f, 0.55f, 0.1f);
			Icon.Actor = Trap.Get();
		}
	}
	for (const TPair<TWeakObjectPtr<AActor>, FEnemyMemory>& Entry : Enemies)
	{
		const AActor* Enemy = Entry.Key.Get();
		const FEnemyMemory& Memory = Entry.Value;
		const float Alpha = MinimapRules::EnemyIconAlpha(Memory.bSeen, Memory.LastSeen >= 0.0 ? Now - Memory.LastSeen : -1.0, EnemyFadeSeconds);
		if (!Enemy || Alpha <= 0.f)
		{
			continue;
		}
		FMinimapIcon& Icon = OutIcons.AddDefaulted_GetRef();
		Icon.Kind = EMinimapIconKind::Enemy;
		Icon.World = Memory.bSeen ? Enemy->GetActorLocation() : Memory.Location;
		Icon.MapFacing = Memory.bSeen ? MapFacing(Projection, Enemy->GetActorForwardVector()) : FVector2D::ZeroVector;
		Icon.Color = FLinearColor(1.f, 0.18f, 0.12f);
		Icon.Alpha = Alpha;
		Icon.Actor = Enemy;
	}
	if (const USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>())
	{
		const AOperativeCharacter* Leader = Squad->GetLeader();
		const TArray<AOperativeCharacter*> Selected = Squad->HasMultiSelection() ? Squad->GetSelectedGroup() : TArray<AOperativeCharacter*>();
		for (const AOperativeCharacter* Member : Squad->GetMembers())
		{
			if (!IsOperativeAlive(Member) || Member->IsHidden())
			{
				continue;
			}
			FMinimapIcon& Icon = OutIcons.AddDefaulted_GetRef();
			Icon.Kind = EMinimapIconKind::Operative;
			Icon.World = Member->GetActorLocation();
			Icon.MapFacing = MapFacing(Projection, Member->GetActorForwardVector());
			Icon.Color = Member->BodyColor;
			Icon.bHighlight = Member == Leader;
			Icon.bSelected = Selected.Contains(Member);
			Icon.bOtherFloor = GetOperativeFloor(Member) != DisplayedFloor;
			Icon.Actor = Member;
			Icon.Label = Member->DisplayName.ToString().Left(1);
		}
	}
	for (FMinimapIcon& Icon : OutIcons)
	{
		Icon.MapUV = Projection.WorldToMap(Icon.World);
	}
	const double Us = (FPlatformTime::Seconds() - Start) * 1.0e6;
	AverageIconUs = AverageIconUs <= 0.0 ? Us : AverageIconUs * 0.95 + Us * 0.05;
}

TArray<FVector2D> UMinimapSubsystem::GetCameraFootprint() const
{
	TArray<FVector2D> Out;
	const UWorld* World = GetWorld();
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (!bReady || !PC || !PC->PlayerCameraManager)
	{
		return Out;
	}
	float PlaneZ = GroundPlaneZ;
	if (DisplayedFloor > 0 && UpperFloorBases.IsValidIndex(DisplayedFloor - 1))
	{
		PlaneZ = UpperFloorBases[DisplayedFloor - 1];
	}
	else if (const USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>(); Squad && Squad->GetLeader())
	{
		PlaneZ = MinimapSubsystemLocal::FeetZ(*Squad->GetLeader());
	}
	const float Aspect = Viewport.Y > 1.f ? Viewport.X / Viewport.Y : 16.f / 9.f;
	for (const FVector& Corner : MinimapRules::FrustumFootprint(PC->PlayerCameraManager->GetCameraLocation(), PC->PlayerCameraManager->GetCameraRotation(),
		PC->PlayerCameraManager->GetFOVAngle(), Aspect, PlaneZ, 15000.f))
	{
		Out.Add(Projection.WorldToMap(Corner));
	}
	return Out;
}

// --- Queries -----------------------------------------------------------------------------------------------------------

UTextureRenderTarget2D* UMinimapSubsystem::GetCapture(int32 Floor) const
{
	return Captures.IsValidIndex(Floor) ? Captures[Floor].Target.Get() : nullptr;
}

const FMinimapFloorCapture* UMinimapSubsystem::GetCaptureInfo(int32 Floor) const
{
	return Captures.IsValidIndex(Floor) ? &Captures[Floor] : nullptr;
}

bool UMinimapSubsystem::IsHiddenOnFloor(int32 Floor, const UPrimitiveComponent* Component) const
{
	return Captures.IsValidIndex(Floor) && Component
		&& Captures[Floor].Hidden.ContainsByPredicate([Component](const TWeakObjectPtr<UPrimitiveComponent>& Hidden) { return Hidden.Get() == Component; });
}

int32 UMinimapSubsystem::GetHiddenPieceCount(int32 Floor) const
{
	return Captures.IsValidIndex(Floor) ? Captures[Floor].Hidden.Num() : 0;
}

UTexture2D* UMinimapSubsystem::GetFogTexture(int32 Floor) const
{
	return FogTextures.IsValidIndex(Floor) ? FogTextures[Floor].Get() : nullptr;
}

TArray<FVector> UMinimapSubsystem::GetFogQuad(int32 Floor) const
{
	const float Z = Captures.IsValidIndex(Floor) ? Captures[Floor].PlaneZ : GroundPlaneZ;
	const FVector2D Min = Fog.GetBounds().Min;
	const FVector2D Max = Min + FVector2D(Fog.GetSize()) * Fog.GetCellCm();
	return { FVector(Min.X, Min.Y, Z), FVector(Max.X, Min.Y, Z), FVector(Max.X, Max.Y, Z), FVector(Min.X, Max.Y, Z) };
}

void UMinimapSubsystem::ReportDrawTime(double Microseconds)
{
	AverageDrawUs = AverageDrawUs <= 0.0 ? Microseconds : AverageDrawUs * 0.95 + Microseconds * 0.05;
}

// --- Save --------------------------------------------------------------------------------------------------------------

TSharedRef<FJsonObject> UMinimapSubsystem::CaptureSave() const
{
	TSharedRef<FJsonObject> State = MakeShared<FJsonObject>();
	if (!Fog.IsValid() && PendingRestore.IsValid())
	{
		// Loaded before the level's map was set up (a load right after the level start): what will be restored is the state.
		State->Values = PendingRestore->Values;
		State->SetStringField(TEXT("panel"), PanelMode == EMinimapPanelMode::Map ? TEXT("map") : TEXT("messages"));
		return State;
	}
	State->SetStringField(TEXT("panel"), PanelMode == EMinimapPanelMode::Map ? TEXT("map") : TEXT("messages"));
	if (Fog.IsValid())
	{
		TSharedRef<FJsonObject> FogState = MakeShared<FJsonObject>();
		FogState->SetNumberField(TEXT("cell_cm"), Fog.GetCellCm());
		FogState->SetNumberField(TEXT("min_x"), Fog.GetBounds().Min.X);
		FogState->SetNumberField(TEXT("min_y"), Fog.GetBounds().Min.Y);
		FogState->SetNumberField(TEXT("size_x"), Fog.GetSize().X);
		FogState->SetNumberField(TEXT("size_y"), Fog.GetSize().Y);
		TArray<TSharedPtr<FJsonValue>> Floors;
		for (int32 Floor = 0; Floor < Fog.GetNumFloors(); ++Floor)
		{
			Floors.Add(MakeShared<FJsonValueString>(Fog.Encode(Floor)));
		}
		FogState->SetArrayField(TEXT("floors"), Floors);
		State->SetObjectField(TEXT("fog"), FogState);
	}
	return State;
}

void UMinimapSubsystem::RestoreSave(const FJsonObject& State)
{
	FString Panel;
	if (State.TryGetStringField(TEXT("panel"), Panel))
	{
		SetPanelMode(Panel == TEXT("map") ? EMinimapPanelMode::Map : EMinimapPanelMode::Messages);
	}
	const TSharedPtr<FJsonObject>* FogState = nullptr;
	if (!State.TryGetObjectField(TEXT("fog"), FogState) || !FogState || !FogState->IsValid())
	{
		return;
	}
	if (!Fog.IsValid())
	{
		PendingRestore = MakeShared<FJsonObject>();
		PendingRestore->Values = State.Values; // applied once the level's fog exists
		return;
	}
	double Cell = 0.0;
	double MinX = 0.0;
	double MinY = 0.0;
	int32 SizeX = 0;
	int32 SizeY = 0;
	(*FogState)->TryGetNumberField(TEXT("cell_cm"), Cell);
	(*FogState)->TryGetNumberField(TEXT("min_x"), MinX);
	(*FogState)->TryGetNumberField(TEXT("min_y"), MinY);
	(*FogState)->TryGetNumberField(TEXT("size_x"), SizeX);
	(*FogState)->TryGetNumberField(TEXT("size_y"), SizeY);
	if (SizeX != Fog.GetSize().X || SizeY != Fog.GetSize().Y || !FMath::IsNearlyEqual(Cell, static_cast<double>(Fog.GetCellCm()), 0.5)
		|| !FMath::IsNearlyEqual(MinX, Fog.GetBounds().Min.X, 1.0) || !FMath::IsNearlyEqual(MinY, Fog.GetBounds().Min.Y, 1.0))
	{
		UE_LOG(LogCodexTactics, Warning, TEXT("Minimap: the saved fog (%dx%d) does not fit this level's grid (%dx%d) - kept as explored now"),
			SizeX, SizeY, Fog.GetSize().X, Fog.GetSize().Y);
		return;
	}
	const TArray<TSharedPtr<FJsonValue>>* Floors = nullptr;
	if ((*FogState)->TryGetArrayField(TEXT("floors"), Floors))
	{
		for (int32 Floor = 0; Floor < Floors->Num() && Floor < Fog.GetNumFloors(); ++Floor)
		{
			FString Data;
			if ((*Floors)[Floor].IsValid() && (*Floors)[Floor]->TryGetString(Data))
			{
				Fog.Decode(Floor, Data);
			}
		}
	}
}
