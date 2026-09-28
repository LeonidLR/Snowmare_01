#include "Interactables/InteractableActor.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "Combat/HealthComponent.h"
#include "EngineUtils.h"
#include "UI/GameMessageSubsystem.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Interactables/HeatSourceComponent.h"
#include "Interactables/TurretActor.h"
#include "TimerManager.h"
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
	GeneratorHealth = GeneratorMaxHealth;

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
	// Godot _on_action_confirmed: a trapped object is defused first (the operative crouches to work on it).
	if (bTrapped && User)
	{
		if (User->GetStance() == EOperativeStance::Standing)
		{
			User->SetStance(EOperativeStance::Crouching);
		}
		AttemptDefusal(User);
		return;
	}
	PerformAction(User);
}

void AInteractableActor::PerformAction(AOperativeCharacter* User)
{
	const UQuestSubsystem* Quests = GetWorld()->GetSubsystem<UQuestSubsystem>();
	const bool bRunning = Quests && Quests->IsGeneratorRunning();
	if (ObjectType == EInteractableType::Generator && User
		&& (bGeneratorBroken || (GeneratorHealth < GeneratorMaxHealth && bRunning)))
	{
		// Godot _on_action_confirmed 1.6: repair the diesel generator (engineer 2.5 s, others 5 s).
		const float Seconds = User->SquadRole == EOperativeRole::Engineer ? 2.5f : 5.f;
		const FRotator Facing = (GetActorLocation() - User->GetActorLocation()).Rotation();
		User->StopOperative();
		User->SetActorRotation(FRotator(0.f, Facing.Yaw, 0.f));
		User->SetStance(EOperativeStance::Crouching);
		PostLine(User->DisplayName, FText::Format(LOCTEXT("GeneratorRepairing", "⚡ {0}: «Восстанавливаем топливную магистраль генератора ({1}с)...»"),
			User->DisplayName, FText::AsNumber(Seconds, &FNumberFormattingOptions().SetMinimumFractionalDigits(1).SetMaximumFractionalDigits(1))));
		FTimerHandle Handle;
		GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateUObject(this, &AInteractableActor::FinishGeneratorRepair,
			TWeakObjectPtr<AOperativeCharacter>(User)), Seconds, false);
		return;
	}
	Interact(User);
}

void AInteractableActor::PostLine(const FText& Speaker, const FText& Text) const
{
	if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
	{
		Messages->PostMessage(Speaker, Text);
	}
}

FDefusalChance AInteractableActor::GetDefusalChance(const AOperativeCharacter* Operative) const
{
	if (!Operative)
	{
		return FDefusalChance();
	}
	return DeployableRules::CalculateDefusal(Operative->SquadRole, Operative->GetStance(), Operative->Luck, Operative->ColdLevel,
		FailedDefusalAttempts, bDefusalWarned);
}

FText AInteractableActor::DescribeDefusal(const AOperativeCharacter* Operative) const
{
	const FDefusalChance Odds = GetDefusalChance(Operative);
	const FText Status = Odds.bDangerous
		? LOCTEXT("HighRisk", "⚠️ ВЫСОКИЙ РИСК ВЗРЫВА!")
		: FText::Format(LOCTEXT("SuccessChance", "Шанс успеха: ~{0}%"), FMath::FloorToInt(Odds.Chance));
	return FText::Format(LOCTEXT("Performer", "Исполнитель: {0} (Поза: {1}) | {2}"),
		Operative ? Operative->DisplayName : LOCTEXT("Soldier", "Боец"),
		DeployableRules::GetDefusalStanceName(Operative ? Operative->GetStance() : EOperativeStance::Standing), Status);
}

EDefusalResult AInteractableActor::AttemptDefusal(AOperativeCharacter* Operative)
{
	const FText Name = Operative ? Operative->DisplayName : LOCTEXT("Soldier", "Боец");
	const ETrapFlavor Flavor = GetTrapFlavor();
	if (!bTrapped)
	{
		PostLine(Name, Flavor == ETrapFlavor::Turret ? LOCTEXT("TurretSafe", "Турель безопасна — растяжек нет.")
			: Flavor == ETrapFlavor::Crate ? LOCTEXT("CrateSafe", "Ящик безопасен — растяжек нет.")
			: Flavor == ETrapFlavor::Mine ? LOCTEXT("MineSafe", "Мина уже обезврежена или безопасна.")
			: (Flavor == ETrapFlavor::Barricade ? LOCTEXT("BarricadeSafe", "Баррикада безопасна — мин-ловушек нет.")
				: LOCTEXT("ObjectSafe", "Объект безопасен — мин-ловушек нет.")));
		return EDefusalResult::Success;
	}

	const FDefusalChance Odds = GetDefusalChance(Operative);
	const EDefusalResult Result = DeployableRules::ResolveDefusal(Odds, bDefusalWarned, FailedDefusalAttempts,
		FMath::FRand() * 100.f, FMath::FRand() * 100.f);
	const bool bStanding = !Operative || Operative->GetStance() == EOperativeStance::Standing;

	switch (Result)
	{
	case EDefusalResult::Warning:
	{
		const bool bCold = Odds.ColdPenalty > 0.f;
		if (Flavor == ETrapFlavor::Turret)
		{
			PostLine(Name, FText::Format(LOCTEXT("TurretWarn", "⚠️ {0}: «Турель заминирована растяжкой! {1}, {2} — подорвёмся! Нужно согреться или присесть!»"),
				Name, bCold ? LOCTEXT("ColdFingers", "пальцы коченеют") : LOCTEXT("WireHinge", "проволока растяжки взведена на шарнире"),
				bStanding ? LOCTEXT("TurretStanding", "стоя к заряду не подберусь") : LOCTEXT("PoseDanger", "в такой позе опасно")));
		}
		else if (Flavor == ETrapFlavor::Crate)
		{
			PostLine(Name, FText::Format(LOCTEXT("CrateWarn", "⚠️ {0}: «Разминирование ящика крайне рискованно! {1}, {2} — подорвёмся и спалим весь лут! Нужно согреться или хотя бы присесть!»"),
				Name, bCold ? LOCTEXT("ColdFingers", "пальцы коченеют") : LOCTEXT("MineUnstable", "механизм слишком нестабилен"),
				bStanding ? LOCTEXT("CrateStanding", "стоя к детонатору не подберусь") : LOCTEXT("PoseDanger", "в такой позе опасно")));
		}
		else if (Flavor == ETrapFlavor::Mine)
		{
			PostLine(Name, FText::Format(LOCTEXT("MineWarn", "⚠️ {0}: «Разминирование выглядит крайне опасным! {1}, {2} — подорвёмся! Нужно согреться или хотя бы лечь на землю!»"),
				Name, bCold ? LOCTEXT("ColdFingers", "пальцы коченеют") : LOCTEXT("MineUnstable", "механизм слишком нестабилен"),
				bStanding ? LOCTEXT("MineStanding", "стоя к ней не подберусь") : LOCTEXT("PoseDanger", "в такой позе опасно")));
		}
		else
		{
			const FText ColdHint = bCold ? LOCTEXT("ColdFingers", "пальцы коченеют")
				: (Flavor == ETrapFlavor::Barricade ? LOCTEXT("WireTight", "проволока растяжки сильно натянута")
					: LOCTEXT("WireArmed", "проволока растяжки взведена на корпусе"));
			const FText Pose = bStanding ? LOCTEXT("ChargeStanding", "стоя к заряду не подобраться") : LOCTEXT("PoseDanger", "в такой позе опасно");
			PostLine(Name, FText::Format(Flavor == ETrapFlavor::Barricade
				? LOCTEXT("BarricadeWarn", "⚠️ {0}: «Баррикада заминирована растяжкой! {1}, {2} — подорвёмся! Нужно согреться или присесть!»")
				: LOCTEXT("ObjectWarn", "⚠️ {0}: «Объект заминирован растяжкой! {1}, {2} — подорвёмся! Нужно согреться или присесть!»"),
				Name, ColdHint, Pose));
		}
		break;
	}
	case EDefusalResult::Success:
		bTrapped = false;
		bDefused = true;
		PostLine(Name, Flavor == ETrapFlavor::Turret
			? FText::Format(LOCTEXT("TurretDefused", "✅ {0} успешно обезвредил(а) растяжку на боевой турели!"), Name)
			: Flavor == ETrapFlavor::Crate
			? FText::Format(LOCTEXT("CrateDefused", "✅ {0} успешно обезвредил(а) растяжку на ящике снабжения!"), Name)
			: Flavor == ETrapFlavor::Mine
			? FText::Format(LOCTEXT("MineDefused", "✅ {0} успешно обезвредил(а) мину!"), Name)
			: (Flavor == ETrapFlavor::Barricade
				? FText::Format(LOCTEXT("BarricadeDefused", "✅ {0} успешно обезвредил(а) растяжку на баррикаде!"), Name)
				: FText::Format(LOCTEXT("ObjectDefused", "✅ {0} успешно обезвредил(а) растяжку на объекте ({1})!"), Name, DisplayName)));
		break;
	case EDefusalResult::Detonation:
		PostLine(Name, Flavor == ETrapFlavor::Turret ? LOCTEXT("TurretBoom", "💥 Срыв чеки ловушки на турели! Прогремел взрыв!")
			: Flavor == ETrapFlavor::Crate ? LOCTEXT("CrateBoom", "💥 Срыв чеки растяжки! Ловушка на ящике сдетонировала, всё содержимое уничтожено!")
			: Flavor == ETrapFlavor::Mine ? LOCTEXT("MineBoom", "💥 Срыв взрывателя! Мина сдетонировала при попытке разминирования!")
			: (Flavor == ETrapFlavor::Barricade ? LOCTEXT("BarricadeBoom", "💥 Срыв чеки на баррикаде! Ловушка сдетонировала!")
				: LOCTEXT("ObjectBoom", "💥 Срыв чеки ловушки на объекте! Взрыв!")));
		DetonateTrap(false, Name);
		break;
	default:
		PostLine(Name, FText::Format(Flavor == ETrapFlavor::Turret
			? LOCTEXT("TurretSlip", "⚠️ {0}: «Щёлк! Скоба сместилась, но детонатор не сработал! Следующая ошибка приведёт к взрыву!»")
			: Flavor == ETrapFlavor::Crate
			? LOCTEXT("CrateSlip", "⚠️ {0}: «Щёлк! Растяжка натянулась, но взрыватель не сработал! Следующий срыв подорвёт ящик!»")
			: Flavor == ETrapFlavor::Mine
			? LOCTEXT("MineSlip", "⚠️ {0}: «Щёлк! Детонатор заклинило, попытка сорвалась! Повторный срыв вызовет подрыв!»")
			: (Flavor == ETrapFlavor::Barricade
				? LOCTEXT("BarricadeSlip", "⚠️ {0}: «Щёлк! Растяжка сместилась, взрыватель уцелел! Осторожнее!»")
				: LOCTEXT("ObjectSlip", "⚠️ {0}: «Щёлк! Растяжка сместилась, детонатор не сработал! Следующая оплошность вызовет подрыв!»")),
			Name));
		break;
	}
	return Result;
}

void AInteractableActor::ApplyBlast(float EnemyDamageBase, float SquadDamageBase, float Radius, float ArmorPenetration, EDamageType DamageType,
	const FText& Source, const FText& SquadLine, EStatusEffect Status, float StatusDuration, float StatusTickDamage)
{
	const FVector Center = GetActorLocation();
	// Enemies (Godot group "enemies"; UE actors tagged Enemy with a health component).
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		AActor* Candidate = *It;
		if (!Candidate || !Candidate->ActorHasTag(FName(TEXT("Enemy"))))
		{
			continue;
		}
		UHealthComponent* Health = Candidate->FindComponentByClass<UHealthComponent>();
		const float EnemyDamage = DeployableRules::GetBlastDamage(EnemyDamageBase, FVector::Dist(Center, Candidate->GetActorLocation()), Radius,
			DeployableRules::EnemyFalloff);
		if (Health && Health->IsAlive() && EnemyDamage > 0.f)
		{
			FDamageSpec Spec;
			Spec.Amount = EnemyDamage;
			Spec.DamageType = DamageType;
			Spec.ArmorPenetration = ArmorPenetration;
			Spec.AttackerSource = Source.ToString();
			Spec.StatusEffect = Status;
			Spec.StatusDuration = StatusDuration;
			Spec.StatusTickDamage = StatusTickDamage;
			Health->TakeDamage(Spec);
		}
	}
	// Friendly fire on the squad.
	if (const USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>())
	{
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			UHealthComponent* Health = Member ? Member->HealthComponent.Get() : nullptr;
			if (!Health || !Health->IsAlive())
			{
				continue;
			}
			const float SquadDamage = DeployableRules::GetBlastDamage(SquadDamageBase,
				FVector::Dist(Center, Member->GetActorLocation()), Radius, DeployableRules::SquadFalloff);
			if (SquadDamage > 0.f)
			{
				Health->ApplyDirectHealthLoss(SquadDamage, Source.ToString());
				Member->StopOperative();
				PostLine(Member->DisplayName, FText::Format(SquadLine, FMath::FloorToInt(SquadDamage)));
			}
		}
	}
	ReceiveExploded(Radius);
}

void AInteractableActor::DetonateTrap(bool bByShot, const FText& InstigatorName)
{
	if (!bTrapped && !bByShot)
	{
		return;
	}
	bTrapped = false;
	PostLine(bByShot ? (InstigatorName.IsEmpty() ? LOCTEXT("Sniper", "Снайпер") : InstigatorName) : LOCTEXT("Blast", "ВЗРЫВ"),
		bByShot ? LOCTEXT("ObjectShotBoom", "💥 Взрыв растяжки на объекте от выстрела!") : LOCTEXT("ObjectTrapBoom", "💥 Растяжка на объекте сдетонировала!"));
	ApplyBlast(TrapDamage, TrapDamage * DeployableRules::SquadDamageScale, TrapRadius, 0.45f, EDamageType::Explosive,
		LOCTEXT("ObjectTrapSource", "Ловушка объекта"),
		LOCTEXT("ObjectTrapHit", "💥 Задело взрывом растяжки объекта (-{0} HP)!"));
}

bool AInteractableActor::TrapWithGrenade(AOperativeCharacter* Operative)
{
	if (!Operative || Operative->GrenadesCount <= 0)
	{
		PostLine(LOCTEXT("SquadSpeaker", "Отряд"), LOCTEXT("NeedGrenade", "Для минирования нужна граната в личном инвентаре выбранного бойца."));
		return false;
	}
	if (!CanReceiveTrap())
	{
		return false;
	}
	// Godot grenade.gd: damage 85, effect_radius 4 m.
	bTrapped = true;
	bDefused = false;
	FailedDefusalAttempts = 0;
	TrapDamage = 85.f;
	TrapRadius = 400.f;
	--Operative->GrenadesCount;
	PostLine(Operative->DisplayName, FText::Format(LOCTEXT("Trapped", "🧨 Объект заминирован гранатой. Осталось гранат: {0}."),
		Operative->GrenadesCount));
	return true;
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
		if (bGeneratorBroken || (GeneratorHealth < GeneratorMaxHealth && Quests->IsGeneratorRunning()))
		{
			const bool bEngineer = Leader && Leader->SquadRole == EOperativeRole::Engineer;
			return FActionMenuRequest::MakeMenu(LOCTEXT("GeneratorBrokenTitle", "⚡ Резервный дизель-генератор [АВАРИЯ]"),
				FText::Format(LOCTEXT("GeneratorBrokenDesc", "⚠️ Дизель-генератор повреждён врагами ({0}/{1} HP)!\nПитание турелей отключено.\nИсполнитель: {2} ({3}, ремонт: {4}с)."),
					FMath::FloorToInt(GeneratorHealth), FMath::FloorToInt(GeneratorMaxHealth), Leader ? Leader->DisplayName : LOCTEXT("Soldier", "Боец"),
					bEngineer ? LOCTEXT("EngineerFast", "🛠️ Инженер (в 2 раза быстрее)") : LOCTEXT("RegularSoldier", "Обычный боец"),
					FText::AsNumber(bEngineer ? 2.5f : 5.f, &FNumberFormattingOptions().SetMinimumFractionalDigits(1).SetMaximumFractionalDigits(1))),
				LOCTEXT("GeneratorRepair", "🔧 Починить генератор"), Cancel, false);
		}
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
	// Godot activate_visuals (generator): every turret gets power.
	ATurretActor::SetAllPowered(GetWorld(), true);
}

bool AInteractableActor::IsGeneratorWorking() const
{
	const UQuestSubsystem* Quests = GetWorld()->GetSubsystem<UQuestSubsystem>();
	return Quests && Quests->IsGeneratorRunning() && !bGeneratorBroken;
}

void AInteractableActor::TakeGeneratorDamage(float Amount)
{
	if (ObjectType != EInteractableType::Generator || bGeneratorBroken || Amount <= 0.f)
	{
		return;
	}
	GeneratorHealth = FMath::Max(0.f, GeneratorHealth - Amount);
	if (GeneratorHealth <= 0.f)
	{
		BreakdownGenerator();
	}
}

void AInteractableActor::BreakdownGenerator()
{
	if (bGeneratorBroken)
	{
		return;
	}
	bGeneratorBroken = true;
	GeneratorHealth = 0.f;
	HeatSource->SetHeatActive(false);
	ATurretActor::SetAllPowered(GetWorld(), false);
	PostLine(LOCTEXT("Attention", "ВНИМАНИЕ"), LOCTEXT("GeneratorDown", "⚠️ Дизель-генератор повреждён врагами и заглох! Турели обесточены!"));
}

void AInteractableActor::RepairGenerator()
{
	GeneratorHealth = GeneratorMaxHealth;
	bGeneratorBroken = false;
	HeatSource->SetHeatActive(true);
	ATurretActor::SetAllPowered(GetWorld(), true);
	PostLine(LOCTEXT("EngineerSpeaker", "Инженер"), LOCTEXT("GeneratorBack", "⚡ Генератор восстановлен! Питание подано на все турели!"));
}

void AInteractableActor::FinishGeneratorRepair(TWeakObjectPtr<AOperativeCharacter> WeakUser)
{
	RepairGenerator();
	PostLine(WeakUser.IsValid() ? WeakUser->DisplayName : LOCTEXT("Soldier", "Боец"),
		LOCTEXT("GeneratorRepaired", "✅ Дизель-генератор снова запущен! Электросеть восстановлена!"));
}

#undef LOCTEXT_NAMESPACE
