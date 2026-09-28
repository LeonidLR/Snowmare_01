#include "Interactables/InteractableActor.h"
#include "Characters/OperativeCharacter.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Interactables/HeatSourceComponent.h"
#include "Quests/QuestSubsystem.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "InteractableActor"

namespace
{
	/** Stand-off from the box surface when approaching, cm. */
	constexpr float ApproachStandOff = 60.f;
}

AInteractableActor::AInteractableActor()
{
	PrimaryActorTick.bCanEverTick = false;

	Box = CreateDefaultSubobject<UBoxComponent>(TEXT("Box"));
	RootComponent = Box;
	Box->SetBoxExtent(FVector(50.f, 50.f, 50.f));
	Box->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	Box->SetCanEverAffectNavigation(true);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Box);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (CubeMesh.Succeeded())
	{
		Mesh->SetStaticMesh(CubeMesh.Object);
	}

	HeatSource = CreateDefaultSubobject<UHeatSourceComponent>(TEXT("HeatSource"));
	HeatSource->SetupAttachment(Box);
}

void AInteractableActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	// Placeholder visual fills the collision box (engine cube is 100 cm).
	Mesh->SetRelativeScale3D(Box->GetUnscaledBoxExtent() / 50.f);
}

void AInteractableActor::BeginPlay()
{
	Super::BeginPlay();

	if (ObjectType == EInteractableType::Generator)
	{
		if (UQuestSubsystem* Quests = GetWorld()->GetSubsystem<UQuestSubsystem>())
		{
			Quests->OnGeneratorStarted.AddDynamic(this, &AInteractableActor::HandleGeneratorStarted);
		}
	}
}

void AInteractableActor::Interact(AOperativeCharacter* User)
{
	if (UQuestSubsystem* Quests = GetWorld()->GetSubsystem<UQuestSubsystem>())
	{
		Quests->InteractWith(ObjectType, this);
	}
}

void AInteractableActor::ExecuteAction(AOperativeCharacter* User)
{
	Interact(User);
}

FActionMenuRequest AInteractableActor::BuildActionMenu(const AOperativeCharacter* Leader) const
{
	const UQuestSubsystem* Quests = GetWorld()->GetSubsystem<UQuestSubsystem>();
	if (!Quests)
	{
		return FActionMenuRequest();
	}
	const FText Squad = LOCTEXT("SquadSpeaker", "Отряд");
	const FText Cancel = LOCTEXT("Cancel", "Отмена");
	const FText Close = LOCTEXT("Close", "Закрыть");

	switch (ObjectType)
	{
	case EInteractableType::Canister:
		if (Quests->HasEmptyCanister() || Quests->HasFuelCanister())
		{
			return FActionMenuRequest::MakeMessage(Squad, LOCTEXT("CanisterOwned", "Канистра уже у нас в инвентаре."));
		}
		return FActionMenuRequest::MakeMenu(LOCTEXT("CanisterTitle", "🛢️ Пустая канистра"),
			LOCTEXT("CanisterDesc", "Взять пустую канистру для топлива?"), LOCTEXT("CanisterTake", "Взять канистру"), Cancel, false);

	case EInteractableType::Vehicle:
	{
		const FText Title = LOCTEXT("VehicleTitle", "🚜 Брошенный БМП-2");
		if (Quests->HasFuelCanister() || Quests->IsGeneratorRunning())
		{
			return FActionMenuRequest::MakeMessage(Squad, LOCTEXT("VehicleDrained", "Топливо из бака БМП уже слито в канистру."));
		}
		if (Quests->HasEmptyCanister())
		{
			return FActionMenuRequest::MakeMenu(Title,
				LOCTEXT("VehicleDrainDesc", "Слить дизельное топливо из бака БМП в канистру?\n(Топливо наполнит канистру для запуска генератора)"),
				LOCTEXT("VehicleDrain", "Слить дизель в канистру"), Cancel, false);
		}
		return FActionMenuRequest::MakeMenu(Title,
			LOCTEXT("VehicleNoCanDesc", "Топливный бак БМП полон солярки, но у отряда нет подходящей емкости, чтобы её слить."),
			LOCTEXT("VehicleNoCan", "Нужна емкость"), Close, true);
	}

	case EInteractableType::Generator:
	{
		const FText Title = LOCTEXT("GeneratorTitle", "⚡ Резервный дизель-генератор");
		if (Quests->IsGeneratorRunning())
		{
			return FActionMenuRequest::MakeMessage(Squad, LOCTEXT("GeneratorRunning", "Генератор уже запущен на полную мощность и обогревает территорию."));
		}
		if (Quests->HasFuelCanister())
		{
			return FActionMenuRequest::MakeMenu(Title,
				LOCTEXT("GeneratorFuelDesc", "Залить дизельное топливо из канистры в генератор и запустить его?\n(Создаст обширную зону тепла и подаст ток на пульт гермоворот)"),
				LOCTEXT("GeneratorFuel", "Залить бензин"), Cancel, false);
		}
		return FActionMenuRequest::MakeMenu(Title,
			LOCTEXT("GeneratorDryDesc", "Генератор сухой и обесточен. Сначала слейте дизель из БМП в канистру."),
			LOCTEXT("GeneratorDry", "Требуется топливо"), Close, true);
	}

	case EInteractableType::GateTerminal:
	{
		const FText Title = LOCTEXT("TerminalTitle", "🎛️ Пульт управления воротами");
		if (Quests->IsGatePowered())
		{
			return FActionMenuRequest::MakeMessage(Squad, LOCTEXT("TerminalPowered", "Питание на ворота уже подано. Створки разблокированы."));
		}
		if (Quests->IsGeneratorRunning())
		{
			return FActionMenuRequest::MakeMenu(Title,
				LOCTEXT("TerminalOpenDesc", "Подать высокое напряжение на сервоприводы и открыть гермоворота?"),
				LOCTEXT("TerminalOpen", "Открыть ворота"), Cancel, false);
		}
		return FActionMenuRequest::MakeMenu(Title,
			LOCTEXT("TerminalNoPowerDesc", "Основная электросеть обесточена. Сначала заправьте и запустите резервный генератор."),
			LOCTEXT("TerminalNoPower", "Нет питания"), Close, true);
	}

	default:
		// The gate itself is not clickable in Godot (only its terminal).
		return FActionMenuRequest();
	}
}

float AInteractableActor::GetDistanceTo(const FVector& Location) const
{
	FVector Closest;
	const float Distance = Box->GetClosestPointOnCollision(Location, Closest);
	return Distance >= 0.f ? Distance : FVector::Dist(Location, GetActorLocation());
}

FVector AInteractableActor::GetApproachPoint(const FVector& FromLocation) const
{
	FVector Closest;
	if (Box->GetClosestPointOnCollision(FromLocation, Closest) < 0.f)
	{
		Closest = GetActorLocation();
	}
	const FVector Outward = (FromLocation - Closest).GetSafeNormal2D();
	return Closest + Outward * ApproachStandOff;
}

void AInteractableActor::HandleGeneratorStarted()
{
	HeatSource->SetHeatActive(true);
}

#undef LOCTEXT_NAMESPACE
