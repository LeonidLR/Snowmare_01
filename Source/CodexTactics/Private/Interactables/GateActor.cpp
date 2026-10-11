#include "Interactables/GateActor.h"
#include "CodexTactics.h"
#include "Core/CodexTacticsGameMode.h"
#include "Components/BoxComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/WaveConfigTypes.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/InteractableActor.h"
#include "Quests/QuestSubsystem.h"
#include "UI/GameMessageSubsystem.h"
#include "UI/OverheadLabel.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/** Godot gate.gd stops within 0.05 m of the target. */
	constexpr float ArrivalTolerance = 5.f;

	TAutoConsoleVariable<int32> CVarGateOpenMode(
		TEXT("Codex.Gate.OpenMode"), -1,
		TEXT("Blast gate opening: -1 = level JSON gate_open_mode (default generator_waves), 0 = instant (terminal opens it), 1 = generator + waves."),
		ECVF_Default);

	TAutoConsoleVariable<float> CVarGateProgressPerSecond(
		TEXT("Codex.Gate.ProgressPerSecond"), -1.f,
		TEXT("Blast gate creep per second of wave fight with the generator up (0..1 progress); < 0 = level JSON gate_progress_per_second (default 0.004)."),
		ECVF_Default);
}

AGateActor::AGateActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	auto MakeDoor = [this](const TCHAR* BoxName, const TCHAR* MeshName, TObjectPtr<UStaticMeshComponent>& OutMesh)
	{
		UBoxComponent* Door = CreateDefaultSubobject<UBoxComponent>(BoxName);
		Door->SetupAttachment(RootComponent);
		Door->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Door->SetCanEverAffectNavigation(true);
		OutMesh = CreateDefaultSubobject<UStaticMeshComponent>(MeshName);
		OutMesh->SetupAttachment(Door);
		OutMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		if (CubeMesh.Succeeded())
		{
			OutMesh->SetStaticMesh(CubeMesh.Object);
		}
		return Door;
	};
	LeftDoor = MakeDoor(TEXT("LeftDoor"), TEXT("LeftDoorMesh"), LeftDoorMesh);
	RightDoor = MakeDoor(TEXT("RightDoor"), TEXT("RightDoorMesh"), RightDoorMesh);

	// Warning beacon: placeholder engine shapes; BeaconMaterial / the mesh are the slots for the final look (TANDEM request 19).
	BeaconPivot = CreateDefaultSubobject<USceneComponent>(TEXT("BeaconPivot"));
	BeaconPivot->SetupAttachment(RootComponent);
	BeaconPivot->SetMobility(EComponentMobility::Movable);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> LensMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	BeaconMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BeaconMesh"));
	BeaconMesh->SetupAttachment(BeaconPivot);
	BeaconMesh->SetMobility(EComponentMobility::Movable);
	BeaconMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BeaconMesh->SetCanEverAffectNavigation(false);
	BeaconMesh->SetRelativeScale3D(FVector(0.3f));
	if (LensMesh.Succeeded())
	{
		BeaconMesh->SetStaticMesh(LensMesh.Object);
	}
	BeaconLight = CreateDefaultSubobject<USpotLightComponent>(TEXT("BeaconLight"));
	BeaconLight->SetupAttachment(BeaconPivot);
	BeaconLight->SetMobility(EComponentMobility::Movable);
	BeaconLight->SetOuterConeAngle(22.f);
	BeaconLight->SetInnerConeAngle(8.f);
	BeaconLight->SetCastShadows(false);
	BeaconLight->SetVisibility(false);
}

void AGateActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	const FVector Extent = DoorSize * 0.5f;
	for (UBoxComponent* Door : { LeftDoor.Get(), RightDoor.Get() })
	{
		Door->SetBoxExtent(Extent);
	}
	LeftDoor->SetRelativeLocation(FVector(0.f, -Extent.Y, Extent.Z));
	RightDoor->SetRelativeLocation(FVector(0.f, Extent.Y, Extent.Z));
	LeftDoorMesh->SetRelativeScale3D(DoorSize / 100.f);
	RightDoorMesh->SetRelativeScale3D(DoorSize / 100.f);

	BeaconPivot->SetRelativeLocation(BeaconOffset);
	if (BeaconMaterial)
	{
		BeaconMesh->SetMaterial(0, BeaconMaterial);
	}
	BeaconLight->SetLightColor(BeaconColor);
	BeaconLight->SetIntensity(BeaconIntensity);
	BeaconLight->SetAttenuationRadius(BeaconRadius);
}

void AGateActor::BeginPlay()
{
	Super::BeginPlay();
	LeftMeshRest = LeftDoorMesh->GetRelativeLocation();
	RightMeshRest = RightDoorMesh->GetRelativeLocation();
	if (UQuestSubsystem* Quests = GetWorld()->GetSubsystem<UQuestSubsystem>())
	{
		Quests->OnGateOpened.AddDynamic(this, &AGateActor::HandleGateOpened);
	}
}

AGateActor* AGateActor::FindLevelGate(const UWorld* World)
{
	if (World)
	{
		for (TActorIterator<AGateActor> It(const_cast<UWorld*>(World)); It; ++It)
		{
			return *It;
		}
	}
	return nullptr;
}

FGateProgressConfig AGateActor::GetProgressConfig() const
{
	FGateProgressConfig Config;
	const UWorld* World = GetWorld();
	const ACodexTacticsGameMode* GameMode = World ? World->GetAuthGameMode<ACodexTacticsGameMode>() : nullptr;
	if (const ULevelConfigAsset* Level = GameMode ? GameMode->GetActiveLevelConfig() : nullptr)
	{
		Config = Level->Config.Gate;
	}
	const int32 Mode = CVarGateOpenMode.GetValueOnGameThread();
	if (Mode == 0 || Mode == 1)
	{
		Config.Mode = Mode == 0 ? EGateOpenMode::Instant : EGateOpenMode::GeneratorWaves;
	}
	const float Rate = CVarGateProgressPerSecond.GetValueOnGameThread();
	if (Rate >= 0.f)
	{
		Config.ProgressPerSecond = Rate;
	}
	return Config;
}

bool AGateActor::IsGeneratorPowering() const
{
	UWorld* World = GetWorld();
	const UQuestSubsystem* Quests = World ? World->GetSubsystem<UQuestSubsystem>() : nullptr;
	if (!Quests || !Quests->IsGeneratorRunning())
	{
		return false;
	}
	for (TActorIterator<AInteractableActor> It(World); It; ++It)
	{
		if (It->ObjectType == EInteractableType::Generator && It->bGeneratorBroken)
		{
			return false;
		}
	}
	return true;
}

void AGateActor::OpenGate()
{
	if (bOpening || bOpen)
	{
		return;
	}
	const float HalfWidth = DoorSize.Y * 0.5f;
	LeftTargetY = -HalfWidth - OpenDistance;
	RightTargetY = HalfWidth + OpenDistance;
	Progress = 1.f;
	bOpening = true;
	SetActorTickEnabled(true);
	SetStatus(EGateProgressStatus::Opening);
}

void AGateActor::UnlockProgress()
{
	if (bOpen || bOpening || bProgressUnlocked)
	{
		return;
	}
	bProgressUnlocked = true;
	SetActorTickEnabled(true);
	UE_LOG(LogCodexTactics, Log, TEXT("Gate %s: servos unlocked, paced by the generator and the waves (progress %.0f%%)"), *GetName(), Progress * 100.f);
}

void AGateActor::RestoreOpen(bool bInOpen)
{
	const float HalfWidth = DoorSize.Y * 0.5f;
	const float Offset = bInOpen ? OpenDistance : 0.f;
	FVector Left = LeftDoor->GetRelativeLocation();
	FVector Right = RightDoor->GetRelativeLocation();
	Left.Y = -HalfWidth - Offset;
	Right.Y = HalfWidth + Offset;
	LeftDoor->SetRelativeLocation(Left);
	RightDoor->SetRelativeLocation(Right);
	LeftTargetY = Left.Y;
	RightTargetY = Right.Y;
	bOpening = false;
	bOpen = bInOpen;
	for (UBoxComponent* Door : { LeftDoor.Get(), RightDoor.Get() })
	{
		Door->SetCollisionEnabled(bInOpen ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics);
		Door->SetCanEverAffectNavigation(!bInOpen);
	}
	if (bInOpen)
	{
		Progress = 1.f;
		Status = bProgressUnlocked ? EGateProgressStatus::Open : Status;
	}
	SetBeaconOn(false);
	SetActorTickEnabled(!bInOpen && bProgressUnlocked);
}

void AGateActor::RestoreProgress(bool bUnlocked, float InProgress, bool bInFinalWaveCleared)
{
	bFinalWaveCleared = bInFinalWaveCleared;
	bProgressUnlocked = bUnlocked && UsesGeneratorPacing();
	if (bOpen)
	{
		Progress = 1.f;
		Status = bProgressUnlocked ? EGateProgressStatus::Open : EGateProgressStatus::Locked;
		SetActorTickEnabled(false);
		return;
	}
	Progress = FMath::Clamp(InProgress, 0.f, 1.f);
	Status = bProgressUnlocked ? EGateProgressStatus::Idle : EGateProgressStatus::Locked;
	bSilentStatus = true; // the first evaluation after the load posts no HQ line
	SetActorTickEnabled(bProgressUnlocked || bOpening);
}

void AGateActor::SetBeaconOn(bool bOn)
{
	bBeaconSpinning = bOn;
	BeaconLight->SetVisibility(bOn);
}

void AGateActor::UpdateBeacon(float DeltaSeconds, bool bGeneratorWorking)
{
	const bool bSpin = GateProgressRules::ShouldBeaconSpin(bProgressUnlocked, bOpen, bOpening, bGeneratorWorking);
	if (bSpin != bBeaconSpinning)
	{
		SetBeaconOn(bSpin);
	}
	if (bSpin)
	{
		BeaconYaw += BeaconSpinSpeed * DeltaSeconds;
		BeaconPivot->SetRelativeRotation(FRotator(0.f, FMath::Fmod(BeaconYaw, 360.f), 0.f));
	}
}

bool AGateActor::IsBeaconLightOn() const
{
	return BeaconLight->IsVisible();
}

bool AGateActor::AreLeavesClosed() const
{
	const float HalfWidth = DoorSize.Y * 0.5f;
	return FMath::IsNearlyEqual(LeftDoor->GetRelativeLocation().Y, -HalfWidth, 0.1f)
		&& FMath::IsNearlyEqual(RightDoor->GetRelativeLocation().Y, HalfWidth, 0.1f)
		&& LeftDoorMesh->GetRelativeLocation().Equals(LeftMeshRest, 0.1f) && RightDoorMesh->GetRelativeLocation().Equals(RightMeshRest, 0.1f);
}

void AGateActor::SetStatus(EGateProgressStatus NewStatus)
{
	const bool bSilent = bSilentStatus;
	bSilentStatus = false;
	if (NewStatus == Status)
	{
		return;
	}
	const EGateProgressStatus Previous = Status;
	Status = NewStatus;
	if (bSilent || !bProgressUnlocked)
	{
		return;
	}
	UE_LOG(LogCodexTactics, Log, TEXT("Gate %s: %s -> %s (%s)"), *GetName(), *UEnum::GetValueAsString(Previous),
		*UEnum::GetValueAsString(NewStatus), *GetStatusText());
	FText Speaker;
	FText Text;
	if (GateProgressRules::GetRadioLine(Previous, NewStatus, bFinalWaveCleared, Speaker, Text))
	{
		if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
		{
			Messages->PostMessage(Speaker, Text);
		}
	}
}

void AGateActor::TickProgress(float DeltaSeconds)
{
	const FGateProgressConfig Config = GetProgressConfig();
	FGateProgressInputs Inputs;
	Inputs.bUnlocked = bProgressUnlocked;
	Inputs.bGeneratorWorking = IsGeneratorPowering();
	const bool bWasCleared = bFinalWaveCleared;
	if (const UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>())
	{
		Inputs.WaveIndex = Flow->GetWaveIndex();
		Inputs.FinalWave = GateProgressRules::ResolveFinalWave(Config, Flow->GetConfig().TotalWaves);
		Inputs.bFightRunning = GateProgressRules::IsFightRunning(Flow->GetPhase(), Flow->GetCombatMode());
		// Latched: the flow leaves the victory phases, the door's right to open stays.
		bFinalWaveCleared |= GateProgressRules::IsFinalWaveCleared(Flow->GetPhase(), Inputs.WaveIndex, Inputs.FinalWave);
	}
	const bool bJustCleared = bFinalWaveCleared && !bWasCleared;
	Inputs.bFinalWaveCleared = bFinalWaveCleared;
	const FGateProgressStep Step = GateProgressRules::Step(Config, Progress, Inputs, DeltaSeconds);
	if (bJustCleared && Status == EGateProgressStatus::NoPower && Step.Status == EGateProgressStatus::NoPower && !bSilentStatus)
	{
		// The last wave fell while the generator was already down: the status stays, the news does not.
		FText Speaker;
		FText Text;
		UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>();
		if (Messages && GateProgressRules::GetRadioLine(EGateProgressStatus::HoldingForWave, EGateProgressStatus::NoPower, true, Speaker, Text))
		{
			Messages->PostMessage(Speaker, Text);
		}
	}
	if (Step.bOpenFully)
	{
		OpenGate();
		return;
	}
	Progress = Step.Progress;
	UpdateBeacon(DeltaSeconds, Inputs.bGeneratorWorking);
	SetStatus(Step.Status);
}

void AGateActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bOpening)
	{
		UpdateBeacon(DeltaSeconds, true);
	}
	if (!bOpening)
	{
		if (bProgressUnlocked && !bOpen)
		{
			TickProgress(DeltaSeconds);
		}
		return;
	}

	const float Step = OpenSpeed * DeltaSeconds;
	auto Slide = [Step](UBoxComponent* Door, float TargetY)
	{
		FVector Location = Door->GetRelativeLocation();
		Location.Y = FMath::FInterpConstantTo(Location.Y, TargetY, 1.f, Step);
		Door->SetRelativeLocation(Location);
	};
	Slide(LeftDoor, LeftTargetY);
	Slide(RightDoor, RightTargetY);

	if (FMath::Abs(LeftDoor->GetRelativeLocation().Y - LeftTargetY) < ArrivalTolerance)
	{
		bOpening = false;
		bOpen = true;
		SetBeaconOn(false);
		SetActorTickEnabled(false);
		// Architect spec: once open, the leaves stop blocking the passage (also refreshes the NavMesh).
		for (UBoxComponent* Door : { LeftDoor.Get(), RightDoor.Get() })
		{
			Door->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Door->SetCanEverAffectNavigation(false);
		}
		SetStatus(EGateProgressStatus::Open);
	}
}

bool AGateActor::ShouldShowStatus() const
{
	return bProgressUnlocked && !bOpen && GateProgressRules::ShouldShowStatus(Status);
}

FString AGateActor::GetStatusText() const
{
	return GateProgressRules::GetStatusText(Status, Progress);
}

bool AGateActor::GetOverheadLabel(FOverheadLabel& OutLabel) const
{
	if (!ShouldShowStatus())
	{
		return false;
	}
	OutLabel.Text = GetStatusText();
	OutLabel.HeightCm = DoorSize.Z + 80.f;
	OutLabel.Color = Status == EGateProgressStatus::NoPower ? FLinearColor(1.f, 0.3f, 0.25f)
		: (Status == EGateProgressStatus::Opening ? FLinearColor(0.3f, 1.f, 0.45f) : FLinearColor(1.f, 0.8f, 0.3f));
	return true;
}

void AGateActor::HandleGateOpened()
{
	if (UsesGeneratorPacing())
	{
		UnlockProgress();
	}
	else
	{
		OpenGate();
	}
}
