#include "Environment/SnowInteractionSubsystem.h"

#include "Camera/CameraZoneVolume.h"
#include "Camera/PlayerCameraManager.h"
#include "Camera/TacticalCameraPawn.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "CodexTactics.h"
#include "Engine/Canvas.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Environment/SnowInteractionRules.h"
#include "Environment/SnowStampSet.h"
#include "Environment/WeatherPresenterActor.h"
#include "Environment/WeatherSnowFxRules.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Misc/App.h"
#include "Misc/PackageName.h"
#include "Scalability.h"
#include "SnowInteractionStats.h"
#include "Subsystems/CodexEventBus.h"

DEFINE_STAT(STAT_SnowInteraction_Tick);
DEFINE_STAT(STAT_SnowInteraction_Poll);
DEFINE_STAT(STAT_SnowInteraction_Draw);
DEFINE_STAT(STAT_SnowInteraction_Passes);
DEFINE_STAT(STAT_SnowInteraction_Stamps);
DEFINE_STAT(STAT_SnowInteraction_Dropped);
DEFINE_STAT(STAT_SnowInteraction_Walkers);

namespace SnowInteractionLocal
{
	TAutoConsoleVariable<int32> CVarSnowTracks(
		TEXT("Codex.Env.SnowTracks"), 1,
		TEXT("Interactive snow (docs/systems/environment-weather.md section 14): 1 = walkers / vehicles / dragged objects mark RT_SnowInteraction, ")
		TEXT("0 = off (MPC_Environment.SnowTrackWindow.w = 0). Visual only."),
		ECVF_Default);

	TAutoConsoleVariable<int32> CVarSnowTracksRes(
		TEXT("Codex.Env.SnowTracksRes"), 0,
		TEXT("Interactive snow RT resolution: 0 = auto (Low effects quality 1024 / 48 m window, else 2048 / 64 m), or 1024 | 2048 | 4096."),
		ECVF_Scalability);

	TAutoConsoleVariable<int32> CVarSnowTracksAnyGround(
		TEXT("Codex.Env.SnowTracksAnyGround"), 1,
		TEXT("Interactive snow ground gate: 1 = any static mesh floor takes stamps (the levels have no landscape yet), 0 = only a landscape or a ")
		TEXT("floor tagged SnowGround."),
		ECVF_Default);

	TAutoConsoleVariable<float> CVarSnowTracksFadeScale(
		TEXT("Codex.Env.SnowTracksFadeScale"), 1.f,
		TEXT("Debug: multiplies the interactive snow fade rate (SnowTracksSmoke uses it to see the fade in seconds)."),
		ECVF_Cheat);

	const TCHAR* RenderTargetPath = TEXT("/Game/Environment/Weather/Snow/Tracks/RT_SnowInteraction.RT_SnowInteraction");
	const TCHAR* StampMaterialPath = TEXT("/Game/Environment/Weather/Snow/Tracks/M_SnowStamp.M_SnowStamp");
	const TCHAR* FadeMaterialPath = TEXT("/Game/Environment/Weather/Snow/Tracks/M_SnowInteraction_Fade.M_SnowInteraction_Fade");
	const TCHAR* ShiftMaterialPath = TEXT("/Game/Environment/Weather/Snow/Tracks/M_SnowTracks_Shift.M_SnowTracks_Shift");
	const TCHAR* StampSetPath = TEXT("/Game/Environment/Weather/Snow/Tracks/DA_SnowStamps.DA_SnowStamps");
	const TCHAR* CollectionPath = TEXT("/Game/Environment/Weather/MPC/MPC_Environment.MPC_Environment");

	// Material / MPC parameter names (Scripts/Editor/create_snow_interaction_assets.py, create_environment_mpc.py).
	const FName ParamSource(TEXT("Source"));
	const FName ParamOffset(TEXT("Offset"));
	const FName ParamFadeR(TEXT("FadeR"));
	const FName ParamFadeG(TEXT("FadeG"));
	const FName ParamWindow(TEXT("SnowTrackWindow"));
	const FName ParamRefill(TEXT("SnowRefill"));
	const FName ParamFresh(TEXT("FreshSnow"));

	/** Loads Path when its package exists (a missing package would log load warnings). */
	UObject* LoadIfPresent(const TCHAR* Path)
	{
		const FSoftObjectPath Soft(Path);
		if (Soft.IsNull() || !FPackageName::DoesPackageExist(Soft.GetLongPackageName()))
		{
			return nullptr;
		}
		return Soft.TryLoad();
	}
}

bool USnowInteractionSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	// Nothing to draw without a renderer (-nullrhi smokes / bots / a dedicated server): not even created, zero cost there.
	return Super::ShouldCreateSubsystem(Outer) && FApp::CanEverRender() && !IsRunningDedicatedServer();
}

bool USnowInteractionSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

USnowInteractionSubsystem* USnowInteractionSubsystem::Get(const UObject* Context)
{
	const UWorld* World = Context ? Context->GetWorld() : nullptr;
	return World ? World->GetSubsystem<USnowInteractionSubsystem>() : nullptr;
}

bool USnowInteractionSubsystem::IsFeatureEnabled()
{
	return SnowInteractionLocal::CVarSnowTracks.GetValueOnGameThread() != 0;
}

bool USnowInteractionSubsystem::IsAnyGroundEnabled()
{
	return SnowInteractionLocal::CVarSnowTracksAnyGround.GetValueOnGameThread() != 0;
}

TStatId USnowInteractionSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USnowInteractionSubsystem, STATGROUP_SnowInteraction);
}

void USnowInteractionSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	bBegunPlay = true;
	bAssetsReady = LoadAssets();
	RefreshShelters();
	if (UCodexEventBus* Bus = UCodexEventBus::Get(&InWorld))
	{
		Bus->OnSoldierDowned.AddUniqueDynamic(this, &USnowInteractionSubsystem::HandleSoldierDowned);
		Bus->OnGameplayEventDetailed.AddUniqueDynamic(this, &USnowInteractionSubsystem::HandleGameplayEventDetailed);
		bBoundEvents = true;
	}
	BindEventSources(InWorld);
	if (bAssetsReady)
	{
		// The RT asset outlives the world (PIE, a restart reloads the level): every world starts with untouched snow.
		ClearTracks();
	}
	UE_LOG(LogCodexTactics, Verbose, TEXT("SnowInteraction: begin play, assets %s, stamp set %s"), bAssetsReady ? TEXT("ok") : TEXT("missing"),
		bStampSetFromAsset ? TEXT("DA_SnowStamps") : TEXT("C++ defaults"));
}

void USnowInteractionSubsystem::Deinitialize()
{
	if (bBoundEvents)
	{
		if (UCodexEventBus* Bus = UCodexEventBus::Get(GetWorld()))
		{
			Bus->OnSoldierDowned.RemoveDynamic(this, &USnowInteractionSubsystem::HandleSoldierDowned);
			Bus->OnGameplayEventDetailed.RemoveDynamic(this, &USnowInteractionSubsystem::HandleGameplayEventDetailed);
		}
		bBoundEvents = false;
	}
	UnbindEventSources();
	if (bAssetsReady && RenderTarget)
	{
		ClearTracks();
		if (RenderTarget->SizeX != AssetSize.X || RenderTarget->SizeY != AssetSize.Y)
		{
			RenderTarget->ResizeTarget(AssetSize.X, AssetSize.Y); // the asset keeps its authored size (PIE shares it with the editor)
		}
	}
	bActive = false;
	bBegunPlay = false;
	Trackers.Reset();
	PendingStamps.Reset();
	PendingShots.Reset();
	Super::Deinitialize();
}

bool USnowInteractionSubsystem::LoadAssets()
{
	using namespace SnowInteractionLocal;
	RenderTarget = Cast<UTextureRenderTarget2D>(LoadIfPresent(RenderTargetPath));
	StampMaterial = Cast<UMaterialInterface>(LoadIfPresent(StampMaterialPath));
	UMaterialInterface* Fade = Cast<UMaterialInterface>(LoadIfPresent(FadeMaterialPath));
	UMaterialInterface* Shift = Cast<UMaterialInterface>(LoadIfPresent(ShiftMaterialPath));
	Collection = Cast<UMaterialParameterCollection>(LoadIfPresent(CollectionPath));
	StampSet = Cast<USnowStampSet>(LoadIfPresent(StampSetPath));
	bStampSetFromAsset = StampSet != nullptr;
	Stamps = StampSet ? StampSet->Stamps : FSnowStampTable();
	Tuning = StampSet ? StampSet->Tuning : FSnowInteractionTuning();
	if (!RenderTarget || !StampMaterial || !Fade || !Shift)
	{
		UE_LOG(LogCodexTactics, Warning, TEXT("SnowInteraction: assets missing under /Game/Environment/Weather/Snow/Tracks (RT %d, stamp %d, fade %d, shift %d): ")
			TEXT("interactive snow off (Scripts/Editor/create_snow_interaction_assets.py)"), RenderTarget != nullptr, StampMaterial != nullptr,
			Fade != nullptr, Shift != nullptr);
		return false;
	}
	// The canvas would draw with the default material while the shaders compile (editor): wait for them once here.
	StampMaterial->EnsureIsComplete();
	Fade->EnsureIsComplete();
	Shift->EnsureIsComplete();
	AssetSize = FIntPoint(RenderTarget->SizeX, RenderTarget->SizeY);
	FadeMaterial = UMaterialInstanceDynamic::Create(Fade, this);
	ShiftToScratch = UMaterialInstanceDynamic::Create(Shift, this);
	ShiftBack = UMaterialInstanceDynamic::Create(Shift, this);
	ShiftToScratch->SetTextureParameterValue(ParamSource, RenderTarget);
	ShiftBack->SetVectorParameterValue(ParamOffset, FLinearColor::Transparent);
	return true;
}

void USnowInteractionSubsystem::Activate()
{
	EnsureResolution();
	Window.bValid = false;
	Trackers.Reset();
	PendingStamps.Reset();
	PendingShots.Reset();
	SincePoll = SinceFade = SinceClamp = 0.f;
	ClearTracks();
	bActive = true;
}

void USnowInteractionSubsystem::Deactivate()
{
	bActive = false;
	Window.bValid = false;
	Trackers.Reset();
	PendingStamps.Reset();
	PendingShots.Reset();
}

bool USnowInteractionSubsystem::EnsureResolution()
{
	const int32 Wanted = SnowInteractionRules::ResolveResolution(SnowInteractionLocal::CVarSnowTracksRes.GetValueOnGameThread(),
		Scalability::GetQualityLevels().EffectsQuality);
	if (Wanted == Resolution && Scratch)
	{
		return false;
	}
	Resolution = Wanted;
	if (RenderTarget->SizeX != Wanted || RenderTarget->SizeY != Wanted)
	{
		RenderTarget->ResizeTarget(Wanted, Wanted);
	}
	if (!Scratch)
	{
		Scratch = NewObject<UTextureRenderTarget2D>(this, TEXT("SnowInteractionScratch"), RF_Transient);
		Scratch->RenderTargetFormat = RenderTarget->RenderTargetFormat;
		Scratch->ClearColor = FLinearColor::Transparent;
		Scratch->bAutoGenerateMips = false;
		Scratch->AddressX = TA_Clamp;
		Scratch->AddressY = TA_Clamp;
		Scratch->InitAutoFormat(Wanted, Wanted);
		Scratch->UpdateResourceImmediate(true);
		ShiftBack->SetTextureParameterValue(SnowInteractionLocal::ParamSource, Scratch);
	}
	else if (Scratch->SizeX != Wanted)
	{
		Scratch->ResizeTarget(Wanted, Wanted);
	}
	Window = FSnowTrackWindow();
	Window.Resolution = Wanted;
	Window.SizeCm = SnowInteractionRules::GetWindowSizeCm(Wanted);
	Trackers.Reset();
	ClearTracks();
	return true;
}

void USnowInteractionSubsystem::ClearTracks()
{
	UWorld* World = GetWorld();
	if (World && RenderTarget)
	{
		UKismetRenderingLibrary::ClearRenderTarget2D(World, RenderTarget, FLinearColor::Transparent);
	}
}

void USnowInteractionSubsystem::DebugRunExtraPasses(int32 FadePasses, int32 ShiftPasses)
{
	UWorld* World = GetWorld();
	if (!bActive || !World || !Window.bValid)
	{
		return;
	}
	FadeMaterial->SetScalarParameterValue(SnowInteractionLocal::ParamFadeR, 1.f);
	FadeMaterial->SetScalarParameterValue(SnowInteractionLocal::ParamFadeG, 1.f);
	for (int32 Pass = 0; Pass < FadePasses; ++Pass)
	{
		UKismetRenderingLibrary::DrawMaterialToRenderTarget(World, RenderTarget, FadeMaterial);
	}
	for (int32 Pass = 0; Pass < ShiftPasses; ++Pass)
	{
		RunShift(FVector2D::ZeroVector);
	}
}

void USnowInteractionSubsystem::AddStamp(const FSnowStamp& Stamp)
{
	if (bActive)
	{
		PendingStamps.Add(Stamp);
	}
}

AWeatherPresenterActor* USnowInteractionSubsystem::FindPresenter()
{
	AWeatherPresenterActor* Found = Presenter.Get();
	UWorld* World = GetWorld();
	if (!Found && World && World->GetTimeSeconds() >= NextPresenterSearch)
	{
		// The presenter is spawned once at the environment's start: a missing one is looked up at most once a second.
		Found = AWeatherPresenterActor::Find(World);
		Presenter = Found;
		NextPresenterSearch = World->GetTimeSeconds() + 1.;
	}
	return Found;
}

void USnowInteractionSubsystem::RefreshShelters()
{
	Shelters.Reset();
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	// Map-placed camera zones (no runtime spawns): gathered at begin play and every few seconds, never per walker.
	for (TActorIterator<ACameraZoneVolume> It(World); It; ++It)
	{
		if (It->Environment == ECameraZoneEnvironment::Closed || It->Environment == ECameraZoneEnvironment::Shelter)
		{
			Shelters.Add(*It);
		}
	}
}

bool USnowInteractionSubsystem::IsSheltered(const FVector& Location) const
{
	for (const TWeakObjectPtr<ACameraZoneVolume>& Zone : Shelters)
	{
		const ACameraZoneVolume* Volume = Zone.Get();
		if (Volume && Volume->ContainsLocation(Location))
		{
			return true;
		}
	}
	return false;
}

bool USnowInteractionSubsystem::GetViewFocus(FVector& OutFocus, float& OutYaw) const
{
	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	const APlayerCameraManager* CameraManager = PC ? PC->PlayerCameraManager.Get() : nullptr;
	if (!CameraManager)
	{
		return false;
	}
	const FRotator Rotation = CameraManager->GetCameraRotation();
	OutYaw = static_cast<float>(Rotation.Yaw);
	if (const ATacticalCameraPawn* Tactical = Cast<ATacticalCameraPawn>(CameraManager->GetViewTarget()))
	{
		OutFocus = Tactical->GetViewCentre();
	}
	else
	{
		// A cinematic camera: the point 15 m below the camera along the view (no trace).
		const FVector Location = CameraManager->GetCameraLocation();
		OutFocus = WeatherSnowFxRules::ComputeViewFocus(Location, Rotation.Vector(), Location.Z - 1500., 2500.);
	}
	return true;
}

void USnowInteractionSubsystem::UpdateWindow(const FVector& Focus, float ViewYaw)
{
	const double Step = SnowInteractionRules::GetSnapStepCm(Window.Resolution, Window.SizeCm);
	const FVector2D Centre = SnowInteractionRules::ComputeWindowCentre(Focus, ViewYaw, Tuning.WindowLeadCm);
	const FVector2D Origin = SnowInteractionRules::UpdateWindowOrigin(Window.Origin, Window.bValid, Centre, Window.SizeCm, Step);
	if (!Window.bValid)
	{
		Window.Origin = Origin;
		Window.bValid = true;
		return;
	}
	if (Origin.Equals(Window.Origin, 0.01))
	{
		return;
	}
	const FIntPoint Shift = SnowInteractionRules::ComputeShiftTexels(Window.Origin, Origin, Window.Resolution, Window.SizeCm);
	if (FMath::Abs(Shift.X) >= Window.Resolution || FMath::Abs(Shift.Y) >= Window.Resolution)
	{
		ClearTracks(); // a jump past the whole window (a level teleport, a cinematic): nothing to keep
	}
	else
	{
		RunShift(FVector2D(Shift.X, Shift.Y) / static_cast<double>(Window.Resolution));
		++Metrics.ShiftPasses;
	}
	Window.Origin = Origin;
}

void USnowInteractionSubsystem::RunShift(const FVector2D& OffsetUV)
{
	SCOPE_CYCLE_COUNTER(STAT_SnowInteraction_Passes);
	UWorld* World = GetWorld();
	if (!World || !Scratch)
	{
		return;
	}
	// new(uv) = old(uv + offset): RT -> scratch with the offset (zero outside, clamped), then scratch -> RT as it is.
	ShiftToScratch->SetVectorParameterValue(SnowInteractionLocal::ParamOffset, FLinearColor(static_cast<float>(OffsetUV.X), static_cast<float>(OffsetUV.Y), 0.f, 0.f));
	UKismetRenderingLibrary::DrawMaterialToRenderTarget(World, Scratch, ShiftToScratch);
	UKismetRenderingLibrary::DrawMaterialToRenderTarget(World, RenderTarget, ShiftBack);
}

void USnowInteractionSubsystem::RunFade(float Dt)
{
	SCOPE_CYCLE_COUNTER(STAT_SnowInteraction_Passes);
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const float Scaled = Dt * FMath::Max(SnowInteractionLocal::CVarSnowTracksFadeScale.GetValueOnGameThread(), 0.f);
	const float FadeR = SnowInteractionRules::ComputeFadeFactor(Scaled, Tuning.FadeBaseRate, Tuning.FadeSnowBoost, SnowRefill);
	const float FadeG = SnowInteractionRules::ComputeFadeFactor(Scaled, Tuning.FadeBaseRate * Tuning.BermFadeScale, Tuning.FadeSnowBoost, SnowRefill);
	FadeMaterial->SetScalarParameterValue(SnowInteractionLocal::ParamFadeR, FadeR);
	FadeMaterial->SetScalarParameterValue(SnowInteractionLocal::ParamFadeG, FadeG);
	UKismetRenderingLibrary::DrawMaterialToRenderTarget(World, RenderTarget, FadeMaterial);
	++Metrics.FadePasses;
}

void USnowInteractionSubsystem::FlushStamps(const FVector2D& Focus)
{
	if (PendingStamps.IsEmpty())
	{
		return;
	}
	SCOPE_CYCLE_COUNTER(STAT_SnowInteraction_Draw);
	UWorld* World = GetWorld();
	const int32 Dropped = SnowInteractionRules::CapStamps(PendingStamps, Focus, Tuning.MaxStampsPerFlush);
	TArray<FCanvasUVTri> Triangles;
	Triangles.Reserve(PendingStamps.Num() * 2);
	const double Res = Window.Resolution;
	int32 Culled = 0;
	for (const FSnowStamp& Stamp : PendingStamps)
	{
		FVector2D Corners[4];
		SnowInteractionRules::BuildStampQuad(Stamp, Corners);
		FVector2D Pixel[4];
		FBox2D Bounds(ForceInit);
		for (int32 Index = 0; Index < 4; ++Index)
		{
			Pixel[Index] = SnowInteractionRules::WorldToUV(Corners[Index], Window.Origin, Window.SizeCm) * Res;
			Bounds += Pixel[Index];
		}
		if (Bounds.Max.X < 0. || Bounds.Max.Y < 0. || Bounds.Min.X > Res || Bounds.Min.Y > Res)
		{
			++Culled;
			continue;
		}
		const FLinearColor Color = SnowInteractionRules::EncodeStampColor(Stamp.Shape, Stamp.Depth, Stamp.Berm);
		FCanvasUVTri& A = Triangles.AddDefaulted_GetRef();
		A.V0_Pos = Pixel[0]; A.V0_UV = FVector2D(0., 0.); A.V0_Color = Color;
		A.V1_Pos = Pixel[1]; A.V1_UV = FVector2D(1., 0.); A.V1_Color = Color;
		A.V2_Pos = Pixel[2]; A.V2_UV = FVector2D(1., 1.); A.V2_Color = Color;
		FCanvasUVTri& B = Triangles.AddDefaulted_GetRef();
		B.V0_Pos = Pixel[0]; B.V0_UV = FVector2D(0., 0.); B.V0_Color = Color;
		B.V1_Pos = Pixel[2]; B.V1_UV = FVector2D(1., 1.); B.V1_Color = Color;
		B.V2_Pos = Pixel[3]; B.V2_UV = FVector2D(0., 1.); B.V2_Color = Color;
	}
	const int32 Drawn = Triangles.Num() / 2;
	if (Drawn > 0 && World)
	{
		UCanvas* Canvas = nullptr;
		FVector2D Size;
		FDrawToRenderTargetContext Context;
		UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(World, RenderTarget, Canvas, Size, Context);
		if (Canvas)
		{
			Canvas->K2_DrawMaterialTriangle(StampMaterial, MoveTemp(Triangles));
		}
		UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(World, Context);
		++Metrics.DrawCalls;
	}
	Metrics.StampsDrawn += Drawn;
	Metrics.StampsDropped += Dropped;
	Metrics.StampsCulled += Culled;
	INC_DWORD_STAT_BY(STAT_SnowInteraction_Stamps, Drawn);
	INC_DWORD_STAT_BY(STAT_SnowInteraction_Dropped, Dropped);
	PendingStamps.Reset();
}

void USnowInteractionSubsystem::WriteParameters()
{
	UWorld* World = GetWorld();
	UMaterialParameterCollectionInstance* Instance = World && Collection ? World->GetParameterCollectionInstance(Collection) : nullptr;
	if (!Instance)
	{
		return;
	}
	const FLinearColor WindowParam = SnowInteractionRules::MakeWindowParam(Window.Origin, Window.SizeCm, bActive && Window.bValid);
	if (WindowParam != LastWindowParam)
	{
		Instance->SetVectorParameterValue(SnowInteractionLocal::ParamWindow, WindowParam);
		LastWindowParam = WindowParam;
	}
	if (!FMath::IsNearlyEqual(SnowRefill, LastRefillWritten, 1e-4f))
	{
		Instance->SetScalarParameterValue(SnowInteractionLocal::ParamRefill, SnowRefill);
		LastRefillWritten = SnowRefill;
	}
	// FreshSnow moves slowly: written when it changed by 0.001 or reached an end.
	if (FMath::Abs(FreshSnow - LastFreshWritten) >= 1e-3f || ((FreshSnow == 0.f || FreshSnow == 1.f) && FreshSnow != LastFreshWritten))
	{
		Instance->SetScalarParameterValue(SnowInteractionLocal::ParamFresh, FreshSnow);
		LastFreshWritten = FreshSnow;
	}
}

void USnowInteractionSubsystem::Tick(float DeltaTime)
{
	SCOPE_CYCLE_COUNTER(STAT_SnowInteraction_Tick);
	Super::Tick(DeltaTime);
	UWorld* World = GetWorld();
	if (!World || !bAssetsReady)
	{
		return;
	}
	const uint64 StartCycles = FPlatformTime::Cycles64();
	const double Now = World->GetTimeSeconds();
	const float Dt = FMath::Max(DeltaTime, 0.f);

	// Weather side (visual only): the refill follows the blended preset's snowfall; FreshSnow rises while it snows.
	AWeatherPresenterActor* Weather = FindPresenter();
	const bool bVisuals = Weather && Weather->IsApplying() && AWeatherPresenterActor::AreVisualsEnabled();
	SnowRefill = bVisuals ? FMath::Clamp(Weather->GetLastFrame().Preset.SnowIntensity, 0.f, 1.f) : 0.f;
	FreshSnow = bVisuals
		? SnowInteractionRules::StepFreshSnow(FreshSnow, AWeatherPresenterActor::ComputeIsSnowing(SnowRefill), Dt, Tuning.FreshSnowRiseSeconds,
			Tuning.FreshSnowFadeSeconds)
		: 0.f;

	const bool bWant = bVisuals && IsFeatureEnabled();
	if (bWant && !bActive)
	{
		Activate();
	}
	else if (!bWant && bActive)
	{
		Deactivate();
	}

	if (bActive)
	{
		EnsureResolution(); // Codex.Env.SnowTracksRes changed: resized, cleared, the window re-placed below
		FVector Focus;
		float Yaw = 0.f;
		if (GetViewFocus(Focus, Yaw))
		{
			UpdateWindow(Focus, Yaw);
			LastFocus = FVector2D(Focus.X, Focus.Y);
		}
		if (Window.bValid)
		{
			const float PollStep = 1.f / FMath::Max(Tuning.PollHz, 1.f);
			SincePoll += Dt;
			if (SincePoll >= PollStep)
			{
				SincePoll = FMath::Min(SincePoll - PollStep, PollStep);
				PollWalkers(Now);
			}
			ProcessShots(); // phase 3: the queued bullet-dent traces (capped per frame)
			SinceFade += Dt;
			const float FadeRate = Tuning.FadeBaseRate * (1.f + Tuning.FadeSnowBoost * SnowRefill)
				* FMath::Max(SnowInteractionLocal::CVarSnowTracksFadeScale.GetValueOnGameThread(), 0.f);
			if (SnowInteractionRules::IsFadeDue(SinceFade, Tuning.FadeHz, FadeRate))
			{
				RunFade(SinceFade);
				SinceFade = 0.f;
			}
			SinceClamp += Dt;
			if (SinceClamp >= Tuning.ClampPassSeconds)
			{
				SinceClamp = 0.f;
				RunShift(FVector2D::ZeroVector);
				++Metrics.ClampPasses;
			}
			FlushStamps(LastFocus);
		}
	}
	if (Now >= NextShelterRefresh)
	{
		RefreshShelters();
		NextShelterRefresh = Now + 10.;
	}
	WriteParameters();
	++Metrics.Frames;
	Metrics.CpuSeconds += FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - StartCycles);
}

void USnowInteractionSubsystem::AddImprint(const FSnowStampSpec& Spec, const AActor& Actor)
{
	const FVector Location = Actor.GetActorLocation();
	AddStamp(SnowInteractionRules::MakePrint(Spec, FVector2D(Location.X, Location.Y), static_cast<float>(Actor.GetActorRotation().Yaw)));
}

void USnowInteractionSubsystem::HandleSoldierDowned(AOperativeCharacter* Soldier)
{
	if (!bActive || !Soldier)
	{
		return;
	}
	if (IsCharacterOnSnow(*Soldier) || IsActorOverSnow(*Soldier))
	{
		AddImprint(Stamps.OperativeDeath, *Soldier);
	}
}

void USnowInteractionSubsystem::HandleGameplayEventDetailed(FName EventName, const FGameplayEventContext& Context)
{
	if (bActive && EventName == GameplayEvents::Explosion)
	{
		HandleExplosion(Context.Kind, Context.Location, Context.Radius, Context.Subject);
		return;
	}
	if (!bActive || EventName != GameplayEvents::EnemyKilled)
	{
		return;
	}
	const AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Context.Subject);
	if (Enemy && Enemy->GetArchetype() == EEnemyArchetype::CryoDrone)
	{
		return; // a drone has no body to leave in the snow
	}
	const AActor* Body = Context.Subject;
	const FVector Location = !Context.Location.IsNearlyZero() ? Context.Location : (Body ? Body->GetActorLocation() : FVector::ZeroVector);
	if (!Body && Context.Location.IsNearlyZero())
	{
		return;
	}
	const bool bOnSnow = Enemy ? (IsCharacterOnSnow(*Enemy) || IsActorOverSnow(*Enemy)) : (Body ? IsActorOverSnow(*Body) : IsAnyGroundEnabled());
	if (!bOnSnow)
	{
		return;
	}
	const FSnowStampSpec& Spec = Enemy ? SnowInteractionRules::GetEnemyImprint(Enemy->GetArchetype(), Stamps) : Stamps.EnemyImprint;
	const float Yaw = Body ? static_cast<float>(Body->GetActorRotation().Yaw) : 0.f;
	AddStamp(SnowInteractionRules::MakePrint(Spec, FVector2D(Location.X, Location.Y), Yaw));
}
