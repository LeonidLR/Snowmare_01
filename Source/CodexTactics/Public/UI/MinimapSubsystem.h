#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "UI/MinimapRules.h"
#include "MinimapSubsystem.generated.h"

class AActor;
class AOperativeCharacter;
class ASceneCapture2D;
class ATutorialMarkerActor;
class AInteractableActor;
class FJsonObject;
class UPrimitiveComponent;
class UTexture2D;
class UTextureRenderTarget2D;

/** What a live minimap icon stands for. */
enum class EMinimapIconKind : uint8
{
	Operative,
	Enemy,
	Objective,
	Trap,
	Door
};

/** One icon drawn over the captured map image (projected with the capture's own iso projection). */
struct CODEXTACTICS_API FMinimapIcon
{
	EMinimapIconKind Kind = EMinimapIconKind::Operative;
	FVector World = FVector::ZeroVector;
	FVector2D MapUV = FVector2D::ZeroVector;
	/** Facing on the map (unit, map UV space); zero = none. */
	FVector2D MapFacing = FVector2D::ZeroVector;
	FLinearColor Color = FLinearColor::White;
	float Alpha = 1.f;
	/** The selected operative (the leader). */
	bool bHighlight = false;
	/** Part of a box selection. */
	bool bSelected = false;
	/** On another floor than the one shown (drawn dimmed). */
	bool bOtherFloor = false;
	TWeakObjectPtr<const AActor> Actor;
	FString Label;
};

/** One floor's static image. */
struct FMinimapFloorCapture
{
	TWeakObjectPtr<UTextureRenderTarget2D> Target;
	/** Components left out of this floor's capture (geometry above it). */
	TArray<TWeakObjectPtr<UPrimitiveComponent>> Hidden;
	/** Height of this floor's walking plane (fog quad, click fallback), world Z. */
	float PlaneZ = 0.f;
	/** Capture passes still to render (exposure / lighting settle over a few frames). */
	int32 PassesLeft = 0;
	/** Game-thread time of its CaptureScene calls, ms. */
	double CaptureMs = 0.0;
	/** Rendered at least once (false headless: -nullrhi). */
	bool bRendered = false;
};

/** Result of a click on the minimap / full map. */
enum class EMinimapClick : uint8
{
	/** Not over the map: the world gets the click. */
	None,
	/** The panel header: MAP / MESSAGES switched. */
	Header,
	/** LMB: the camera moved there. */
	Camera,
	/** RMB: a move order there (the normal order path decides). */
	Move,
	/** Over the map but nothing to do (outside the image, no ground). */
	Consumed
};

/**
 * Minimap (user decisions 2026-10-18, docs/design/MINIMAP.md; UE-only): in the top-right panel (switched with the message
 * feed, F2 / the header) or full screen (Tab; F3 in turn-based combat where Tab is "next operative"). The level is
 * captured ONCE per floor at load (orthographic SceneCapture2D at the level start camera's yaw / pitch; characters,
 * markers, effects hidden; per floor the geometry above it left out), live icons are projected over it with the same
 * matrix: squad (leader highlighted, facing arrow), enemies while seen (last known spot fading), tutorial objectives,
 * spotted traps, building doors, the camera's view footprint. Fog of war per floor (reveal discs round the operatives,
 * saved). Floors: level JSON "minimap.floors_cm" or auto-detected from stacked walkable surfaces; the selected operative's
 * floor is shown. Wheel = zoom, LMB = camera there, RMB = move order there (through the normal click path: tutorial locks,
 * confinement, combat dispatch). The HUD draws it (ACodexTacticsHUD::DrawMinimap*); the controller routes the input.
 */
UCLASS()
class CODEXTACTICS_API UMinimapSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	static UMinimapSubsystem* Get(const UObject* WorldContext);

	// --- Panel / full map -------------------------------------------------------------------------------------------

	EMinimapPanelMode GetPanelMode() const { return PanelMode; }
	void SetPanelMode(EMinimapPanelMode Mode);
	/** F2 / a header click: MAP <-> MESSAGES (remembered for the session). Returns the new mode. */
	EMinimapPanelMode TogglePanelMode();
	bool IsFullMapOpen() const { return bFullMapOpen; }
	void SetFullMapOpen(bool bOpen);
	bool ToggleFullMap();

	/** Feed messages posted since the panel last showed them (the header badge in map mode). */
	int32 GetUnreadMessages() const;

	/** The HUD's layout every frame (viewport pixels; the feed's width for the header in messages mode). */
	void UpdateLayout(const FVector2D& ViewportSize, float FeedWidthPx);
	/** Smokes (no viewport headless): fixes the layout to this viewport. */
	void SetViewportSizeForTesting(const FVector2D& ViewportSize);
	FVector2D GetViewportSize() const { return Viewport; }
	/** The panel's header strip (MAP | MESSAGES), screen pixels. */
	FBox2D GetHeaderRect() const { return HeaderRect; }
	/** The corner map's view (valid in map mode). */
	const FMinimapView& GetPanelView() const { return PanelView; }
	const FMinimapView& GetFullView() const { return FullView; }
	/** The view that takes clicks now: the full map while open, else the corner map (map mode). */
	const FMinimapView* GetActiveView() const;

	/** The cursor is over the map / its header (clicks, wheel and edge scrolling belong to the map). */
	bool IsScreenPointOverMap(const FVector2D& Screen) const;
	/** Wheel over the map zooms it; false when the cursor is elsewhere. */
	bool HandleWheel(const FVector2D& Screen, float Notches);
	/**
	 * A click at Screen: the header toggles the panel; LMB on the image = OutWorld for the camera; RMB = OutWorld for a move
	 * order (on the navmesh when possible). The caller acts on Camera / Move.
	 */
	EMinimapClick HandleClick(const FVector2D& Screen, bool bRightButton, FVector& OutWorld);
	/** The world point seen at a map UV on the shown floor (trace along the view, hidden geometry ignored). */
	bool MapToWorld(const FVector2D& MapUV, FVector& OutWorld) const;

	// --- Content ----------------------------------------------------------------------------------------------------

	/** Captures done (or created headless), floors and fog set up. */
	bool IsReady() const { return bReady; }
	const FMinimapIsoProjection& GetProjection() const { return Projection; }
	int32 GetFloorCount() const { return 1 + UpperFloorBases.Num(); }
	const TArray<float>& GetUpperFloorBases() const { return UpperFloorBases; }
	/** Floors from the level JSON (true) or auto-detected. */
	bool AreFloorsFromData() const { return bFloorsFromData; }
	/** The floor the map shows: the selected operative's. */
	int32 GetDisplayedFloor() const { return DisplayedFloor; }
	int32 GetOperativeFloor(const AOperativeCharacter* Operative) const;
	UTextureRenderTarget2D* GetCapture(int32 Floor) const;
	const FMinimapFloorCapture* GetCaptureInfo(int32 Floor) const;
	/** Is this component left out of Floor's capture? */
	bool IsHiddenOnFloor(int32 Floor, const UPrimitiveComponent* Component) const;
	const FMinimapFogGrid& GetFog() const { return Fog; }
	/** The fog overlay texture of a floor (null headless). */
	UTexture2D* GetFogTexture(int32 Floor) const;
	/** The fog quad's world corners (XY bounds at the floor's plane): min-min, max-min, max-max, min-max. */
	TArray<FVector> GetFogQuad(int32 Floor) const;
	/** Live icons now (positions from the actors, enemy memory from the last update). */
	void BuildIcons(TArray<FMinimapIcon>& OutIcons) const;
	/** The main camera's ground footprint on the shown floor, map UV (empty without a camera). */
	TArray<FVector2D> GetCameraFootprint() const;

	/** Redetects the floors and recaptures every floor (debug command / level changed); the fog stays if its grid fits. */
	void Rebuild();

	// --- Save -------------------------------------------------------------------------------------------------------

	TSharedRef<FJsonObject> CaptureSave() const;
	void RestoreSave(const FJsonObject& State);

	// --- Stats (smokes, docs) ---------------------------------------------------------------------------------------

	/** Floor detection + hidden lists, ms. */
	double GetSetupMs() const { return SetupMs; }
	/** Average Tick work, microseconds. */
	double GetAverageTickUs() const { return AverageTickUs; }
	/** Average BuildIcons time (HUD per frame), microseconds. */
	double GetAverageIconUs() const { return AverageIconUs; }
	/** HUD: the canvas draw time of the map this frame (stats only). */
	void ReportDrawTime(double Microseconds);
	double GetAverageDrawUs() const { return AverageDrawUs; }
	int32 GetHiddenPieceCount(int32 Floor) const;

	/** Fog reveal radius round each operative, cm. */
	static constexpr float RevealRadiusCm = 1500.f;
	/** Fog cell, cm. */
	static constexpr float FogCellCm = 200.f;
	/** Updates (fog, floors, enemy sight), s. */
	static constexpr float UpdateInterval = 0.25f;
	/** The last known enemy spot fades over this, s. */
	static constexpr float EnemyFadeSeconds = 12.f;
	/** Outside the sight system (exploration) an operative sees an enemy on the map within this, cm. */
	static constexpr float ExplorationSightCm = 2500.f;

private:
	struct FEnemyMemory
	{
		FVector Location = FVector::ZeroVector;
		FVector Facing = FVector::ForwardVector;
		double LastSeen = -1.0;
		bool bSeen = false;
	};
	struct FDoor
	{
		FVector Location = FVector::ZeroVector;
		FVector Outward = FVector::ForwardVector;
	};

	bool TrySetup();
	void DetectFloors(const FBox& Area);
	void BuildHiddenLists();
	void SetupCaptures();
	void RunCapturePasses();
	void SetupFog(const FBox2D& Area);
	void UpdateFogTextures();
	void UpdateTick();
	void RefreshStaticLists();
	void UpdateEnemyMemory();
	FBox ComputeArea() const;
	/** Walkable surfaces (world Z, top first) under XY between TopZ and BottomZ. */
	void SurfacesBelow(const FVector2D& XY, float TopZ, float BottomZ, TArray<float>& OutZ, const UPrimitiveComponent* Ignore = nullptr) const;
	/** Height of FeetZ above the lowest walkable surface under XY. */
	float HeightAboveLowest(const FVector2D& XY, float FeetZ) const;
	FMinimapView MakePanelView() const;
	bool IsCaptureHiddenActor(const AActor* Actor) const;
	void ApplyLayout();
	bool ShouldTick() const;

	EMinimapPanelMode PanelMode = EMinimapPanelMode::Messages;
	bool bFullMapOpen = false;
	bool bReady = false;
	bool bFloorsFromData = false;
	bool bViewportForced = false;
	double SetupStartTime = -1.0;
	double LastUpdateTime = -1.0;
	double LastStaticRefresh = -1.0;
	double LastSeenMessageTime = 0.0;

	FVector2D Viewport = FVector2D(1280.f, 720.f);
	float FeedWidth = 480.f;
	FBox2D HeaderRect = FBox2D(ForceInit);
	FMinimapView PanelView;
	FMinimapView FullView;
	/** Corner map span (zoom), map U units. */
	float PanelSpanU = 0.25f;
	bool bFullViewInitialised = false;

	FMinimapIsoProjection Projection;
	FBox Area = FBox(ForceInit);
	float GroundPlaneZ = 0.f;
	TArray<float> UpperFloorBases;
	MinimapRules::FFloorParams FloorParams;
	int32 DisplayedFloor = 0;
	TMap<TWeakObjectPtr<const AOperativeCharacter>, int32> OperativeFloors;
	TArray<FMinimapFloorCapture> Captures;
	FIntPoint CaptureSize = FIntPoint(1024, 1024);

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextureRenderTarget2D>> CaptureTargets;
	UPROPERTY(Transient)
	TObjectPtr<ASceneCapture2D> CaptureActor;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UTexture2D>> FogTextures;

	FMinimapFogGrid Fog;
	TArray<uint32> FogTextureRevision;
	/** A save applied before the fog existed (load right after the level start). */
	TSharedPtr<FJsonObject> PendingRestore;

	TMap<TWeakObjectPtr<AActor>, FEnemyMemory> Enemies;
	TArray<TWeakObjectPtr<ATutorialMarkerActor>> Markers;
	TArray<TWeakObjectPtr<AInteractableActor>> Traps;
	TArray<FDoor> Doors;

	double SetupMs = 0.0;
	double AverageTickUs = 0.0;
	mutable double AverageIconUs = 0.0;
	double AverageDrawUs = 0.0;
};
