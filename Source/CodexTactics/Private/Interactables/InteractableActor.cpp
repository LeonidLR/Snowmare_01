#include "Interactables/InteractableActor.h"
#include "Subsystems/CodexEventBus.h"
#include "UI/OverheadLabel.h"
#include "UI/FloatingTextSubsystem.h"
#include "Characters/EnemyCharacter.h"
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
		UFloatingTextSubsystem::SpawnAboveOperative(User, TEXT("⚡ REPAIRING GENERATOR..."), FLinearColor(0.2f, 0.9f, 0.4f));
		PostLine(User->DisplayName, FText::Format(LOCTEXT("GeneratorRepairing", "⚡ {0}: \"Restoring the generator fuel line ({1}s)...\""),
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
		? LOCTEXT("HighRisk", "⚠️ HIGH RISK OF DETONATION!")
		: FText::Format(LOCTEXT("SuccessChance", "Success chance: ~{0}%"), FMath::FloorToInt(Odds.Chance));
	return FText::Format(LOCTEXT("Performer", "Operator: {0} (Stance: {1}) | {2}"),
		Operative ? Operative->DisplayName : LOCTEXT("Soldier", "Operative"),
		DeployableRules::GetDefusalStanceName(Operative ? Operative->GetStance() : EOperativeStance::Standing), Status);
}

EDefusalResult AInteractableActor::AttemptDefusal(AOperativeCharacter* Operative)
{
	const FText Name = Operative ? Operative->DisplayName : LOCTEXT("Soldier", "Operative");
	const ETrapFlavor Flavor = GetTrapFlavor();
	if (!bTrapped)
	{
		PostLine(Name, Flavor == ETrapFlavor::Turret ? LOCTEXT("TurretSafe", "The turret is safe - no tripwires.")
			: Flavor == ETrapFlavor::Crate ? LOCTEXT("CrateSafe", "The crate is safe - no tripwires.")
			: Flavor == ETrapFlavor::Mine ? LOCTEXT("MineSafe", "The mine is already disarmed or safe.")
			: (Flavor == ETrapFlavor::Barricade ? LOCTEXT("BarricadeSafe", "The barricade is safe - no booby traps.")
				: LOCTEXT("ObjectSafe", "The object is safe - no booby traps.")));
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
			PostLine(Name, FText::Format(LOCTEXT("TurretWarn", "⚠️ {0}: \"The turret is rigged with a tripwire! {1}, {2} - we'll blow ourselves up! Need to warm up or crouch!\""),
				Name, bCold ? LOCTEXT("ColdFingers", "my fingers are numb") : LOCTEXT("WireHinge", "the tripwire is armed on the hinge"),
				bStanding ? LOCTEXT("TurretStanding", "I can't reach the charge standing") : LOCTEXT("PoseDanger", "this stance is too risky")));
		}
		else if (Flavor == ETrapFlavor::Crate)
		{
			PostLine(Name, FText::Format(LOCTEXT("CrateWarn", "⚠️ {0}: \"Defusing the crate is extremely risky! {1}, {2} - we'll blow up and burn all the loot! Need to warm up or at least crouch!\""),
				Name, bCold ? LOCTEXT("ColdFingers", "my fingers are numb") : LOCTEXT("MineUnstable", "the mechanism is too unstable"),
				bStanding ? LOCTEXT("CrateStanding", "I can't reach the detonator standing") : LOCTEXT("PoseDanger", "this stance is too risky")));
		}
		else if (Flavor == ETrapFlavor::Mine)
		{
			PostLine(Name, FText::Format(LOCTEXT("MineWarn", "⚠️ {0}: \"Defusing this looks extremely dangerous! {1}, {2} - we'll blow up! Need to warm up or at least go prone!\""),
				Name, bCold ? LOCTEXT("ColdFingers", "my fingers are numb") : LOCTEXT("MineUnstable", "the mechanism is too unstable"),
				bStanding ? LOCTEXT("MineStanding", "I can't reach it standing") : LOCTEXT("PoseDanger", "this stance is too risky")));
		}
		else
		{
			const FText ColdHint = bCold ? LOCTEXT("ColdFingers", "my fingers are numb")
				: (Flavor == ETrapFlavor::Barricade ? LOCTEXT("WireTight", "the tripwire is pulled taut")
					: LOCTEXT("WireArmed", "the tripwire is armed on the casing"));
			const FText Pose = bStanding ? LOCTEXT("ChargeStanding", "the charge can't be reached standing") : LOCTEXT("PoseDanger", "this stance is too risky");
			PostLine(Name, FText::Format(Flavor == ETrapFlavor::Barricade
				? LOCTEXT("BarricadeWarn", "⚠️ {0}: \"The barricade is rigged with a tripwire! {1}, {2} - we'll blow ourselves up! Need to warm up or crouch!\"")
				: LOCTEXT("ObjectWarn", "⚠️ {0}: \"The object is rigged with a tripwire! {1}, {2} - we'll blow ourselves up! Need to warm up or crouch!\""),
				Name, ColdHint, Pose));
		}
		break;
	}
	case EDefusalResult::Success:
		bTrapped = false;
		bDefused = true;
		PostLine(Name, Flavor == ETrapFlavor::Turret
			? FText::Format(LOCTEXT("TurretDefused", "✅ {0} disarmed the tripwire on the combat turret!"), Name)
			: Flavor == ETrapFlavor::Crate
			? FText::Format(LOCTEXT("CrateDefused", "✅ {0} disarmed the tripwire on the supply crate!"), Name)
			: Flavor == ETrapFlavor::Mine
			? FText::Format(LOCTEXT("MineDefused", "✅ {0} disarmed the mine!"), Name)
			: (Flavor == ETrapFlavor::Barricade
				? FText::Format(LOCTEXT("BarricadeDefused", "✅ {0} disarmed the tripwire on the barricade!"), Name)
				: FText::Format(LOCTEXT("ObjectDefused", "✅ {0} disarmed the tripwire on the object ({1})!"), Name, DisplayName)));
		break;
	case EDefusalResult::Detonation:
		PostLine(Name, Flavor == ETrapFlavor::Turret ? LOCTEXT("TurretBoom", "💥 The turret trap pin slipped! Detonation!")
			: Flavor == ETrapFlavor::Crate ? LOCTEXT("CrateBoom", "💥 The tripwire pin slipped! The crate trap went off, all contents destroyed!")
			: Flavor == ETrapFlavor::Mine ? LOCTEXT("MineBoom", "💥 The fuze slipped! The mine went off during the defusal!")
			: (Flavor == ETrapFlavor::Barricade ? LOCTEXT("BarricadeBoom", "💥 The barricade pin slipped! The trap went off!")
				: LOCTEXT("ObjectBoom", "💥 The object trap pin slipped! Detonation!")));
		DetonateTrap(false, Name);
		break;
	default:
		PostLine(Name, FText::Format(Flavor == ETrapFlavor::Turret
			? LOCTEXT("TurretSlip", "⚠️ {0}: \"Click! The clip shifted, but the detonator held! Next mistake and it blows!\"")
			: Flavor == ETrapFlavor::Crate
			? LOCTEXT("CrateSlip", "⚠️ {0}: \"Click! The wire went taut, but the fuze held! Next slip blows the crate!\"")
			: Flavor == ETrapFlavor::Mine
			? LOCTEXT("MineSlip", "⚠️ {0}: \"Click! The detonator jammed, attempt failed! Another slip sets it off!\"")
			: (Flavor == ETrapFlavor::Barricade
				? LOCTEXT("BarricadeSlip", "⚠️ {0}: \"Click! The wire shifted, the fuze held! Careful!\"")
				: LOCTEXT("ObjectSlip", "⚠️ {0}: \"Click! The wire shifted, the detonator held! Next slip sets it off!\"")),
			Name));
		break;
	}
	return Result;
}

void AInteractableActor::ApplyBlast(float EnemyDamageBase, float SquadDamageBase, float Radius, float ArmorPenetration, EDamageType DamageType,
	const FText& Source, const FText& SquadLine, EStatusEffect Status, float StatusDuration, float StatusTickDamage)
{
	const FVector Center = GetActorLocation();
	// A placed charge / trap (tripwire, mine, trapped object, barrel): its damage is a trap event — a patrol searches
	// instead of engaging and no ambush fight starts (user amendment 2026-10-06).
	const AEnemyCharacter::FScopedTrapBlast TrapBlast;
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
				Member->TakeHit(SquadDamage, Source.ToString(), false, true); // Godot take_damage(..., bypass_avoidance)
				Member->StopOperative();
				PostLine(Member->DisplayName, FText::Format(SquadLine, FMath::FloorToInt(SquadDamage)));
			}
		}
	}
	// Patrols within the trap alert radius (20 m) go searching around the blast.
	AEnemyCharacter::AlertPatrolsNearTrap(GetWorld(), Center);
	ReceiveExploded(Radius);
}

void AInteractableActor::DetonateTrap(bool bByShot, const FText& InstigatorName)
{
	if (!bTrapped && !bByShot)
	{
		return;
	}
	bTrapped = false;
	PostLine(bByShot ? (InstigatorName.IsEmpty() ? LOCTEXT("Sniper", "Marksman") : InstigatorName) : LOCTEXT("Blast", "BLAST"),
		bByShot ? LOCTEXT("ObjectShotBoom", "💥 A shot set off the tripwire on the object!") : LOCTEXT("ObjectTrapBoom", "💥 The tripwire on the object went off!"));
	ApplyBlast(TrapDamage, TrapDamage * DeployableRules::SquadDamageScale, TrapRadius, 0.45f, EDamageType::Explosive,
		LOCTEXT("ObjectTrapSource", "Object trap"),
		LOCTEXT("ObjectTrapHit", "💥 Caught in the object tripwire blast (-{0} HP)!"));
}

bool AInteractableActor::TrapWithGrenade(AOperativeCharacter* Operative)
{
	if (!Operative || Operative->GrenadesCount <= 0)
	{
		PostLine(LOCTEXT("SquadSpeaker", "SQUAD"), LOCTEXT("NeedGrenade", "Setting a trap needs a grenade in the selected operative's personal inventory."));
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
	PostLine(Operative->DisplayName, FText::Format(LOCTEXT("Trapped", "🧨 Object trapped with a grenade. Grenades left: {0}."),
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
	const FText Squad = LOCTEXT("SquadSpeaker", "SQUAD");
	const FText Cancel = LOCTEXT("Cancel", "Cancel");
	const FText Close = LOCTEXT("Close", "Close");

	switch (ObjectType)
	{
	case EInteractableType::Canister:
		if (Quests->HasEmptyCanister() || Quests->HasFuelCanister())
		{
			return FActionMenuRequest::MakeMessage(Squad, LOCTEXT("CanisterOwned", "We already have the jerrycan."));
		}
		return FActionMenuRequest::MakeMenu(LOCTEXT("CanisterTitle", "🛢️ Empty jerrycan"),
			LOCTEXT("CanisterDesc", "Take the empty fuel jerrycan?"), LOCTEXT("CanisterTake", "Take jerrycan"), Cancel, false);

	case EInteractableType::Vehicle:
	{
		const FText Title = LOCTEXT("VehicleTitle", "🚜 Abandoned BMP-2");
		if (Quests->HasFuelCanister() || Quests->IsGeneratorRunning())
		{
			return FActionMenuRequest::MakeMessage(Squad, LOCTEXT("VehicleDrained", "The BMP fuel tank is already drained into the jerrycan."));
		}
		if (Quests->HasEmptyCanister())
		{
			return FActionMenuRequest::MakeMenu(Title,
				LOCTEXT("VehicleDrainDesc", "Drain diesel from the BMP tank into the jerrycan?\n(The fuel will fill the jerrycan to start the generator)"),
				LOCTEXT("VehicleDrain", "Drain diesel"), Cancel, false);
		}
		return FActionMenuRequest::MakeMenu(Title,
			LOCTEXT("VehicleNoCanDesc", "The BMP fuel tank is full of diesel, but the squad has no container to drain it into."),
			LOCTEXT("VehicleNoCan", "Need a container"), Close, true);
	}

	case EInteractableType::Generator:
	{
		const FText Title = LOCTEXT("GeneratorTitle", "⚡ Backup Diesel Generator");
		if (bGeneratorBroken || (GeneratorHealth < GeneratorMaxHealth && Quests->IsGeneratorRunning()))
		{
			const bool bEngineer = Leader && Leader->SquadRole == EOperativeRole::Engineer;
			return FActionMenuRequest::MakeMenu(LOCTEXT("GeneratorBrokenTitle", "⚡ Backup Diesel Generator [FAULT]"),
				FText::Format(LOCTEXT("GeneratorBrokenDesc", "⚠️ The diesel generator is damaged by the enemy ({0}/{1} HP)!\nTurret power is cut.\nOperator: {2} ({3}, repair: {4}s)."),
					FMath::FloorToInt(GeneratorHealth), FMath::FloorToInt(GeneratorMaxHealth), Leader ? Leader->DisplayName : LOCTEXT("Soldier", "Operative"),
					bEngineer ? LOCTEXT("EngineerFast", "🛠️ Engineer (2x faster)") : LOCTEXT("RegularSoldier", "Regular operative"),
					FText::AsNumber(bEngineer ? 2.5f : 5.f, &FNumberFormattingOptions().SetMinimumFractionalDigits(1).SetMaximumFractionalDigits(1))),
				LOCTEXT("GeneratorRepair", "🔧 Repair generator"), Cancel, false);
		}
		if (Quests->IsGeneratorRunning())
		{
			return FActionMenuRequest::MakeMessage(Squad, LOCTEXT("GeneratorRunning", "The generator is already running at full power and heating the area."));
		}
		if (Quests->HasFuelCanister())
		{
			return FActionMenuRequest::MakeMenu(Title,
				LOCTEXT("GeneratorFuelDesc", "Pour the diesel from the jerrycan into the generator and start it?\n(Creates a wide heat zone and powers the blast-door console)"),
				LOCTEXT("GeneratorFuel", "Refuel"), Cancel, false);
		}
		return FActionMenuRequest::MakeMenu(Title,
			LOCTEXT("GeneratorDryDesc", "The generator is dry and dead. Drain diesel from the BMP into the jerrycan first."),
			LOCTEXT("GeneratorDry", "Needs fuel"), Close, true);
	}

	case EInteractableType::GateTerminal:
	{
		const FText Title = LOCTEXT("TerminalTitle", "🎛️ Gate control console");
		if (Quests->IsGatePowered())
		{
			return FActionMenuRequest::MakeMessage(Squad, LOCTEXT("TerminalPowered", "The gate is already powered. The doors are unlocked."));
		}
		if (Quests->IsGeneratorRunning())
		{
			return FActionMenuRequest::MakeMenu(Title,
				LOCTEXT("TerminalOpenDesc", "Send high voltage to the servos and open the blast doors?"),
				LOCTEXT("TerminalOpen", "Open gate"), Cancel, false);
		}
		return FActionMenuRequest::MakeMenu(Title,
			LOCTEXT("TerminalNoPowerDesc", "The main grid is down. Refuel and start the backup generator first."),
			LOCTEXT("TerminalNoPower", "No power"), Close, true);
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
	BroadcastGeneratorState(true); // Godot EventBus.generator_state_changed
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
	BroadcastGeneratorState(false); // Godot EventBus.generator_state_changed
	PostLine(LOCTEXT("Attention", "WARNING"), LOCTEXT("GeneratorDown", "⚠️ The enemy damaged the diesel generator and it stalled! Turrets are without power!"));
}

void AInteractableActor::RepairGenerator()
{
	GeneratorHealth = GeneratorMaxHealth;
	bGeneratorBroken = false;
	HeatSource->SetHeatActive(true);
	ATurretActor::SetAllPowered(GetWorld(), true);
	BroadcastGeneratorState(true); // Godot EventBus.generator_state_changed
	PostLine(LOCTEXT("EngineerSpeaker", "Engineer"), LOCTEXT("GeneratorBack", "⚡ Generator restored! All turrets are powered!"));
}

void AInteractableActor::FinishGeneratorRepair(TWeakObjectPtr<AOperativeCharacter> WeakUser)
{
	RepairGenerator();
	PostLine(WeakUser.IsValid() ? WeakUser->DisplayName : LOCTEXT("Soldier", "Operative"),
		LOCTEXT("GeneratorRepaired", "✅ The diesel generator is running again! Power grid restored!"));
}

#undef LOCTEXT_NAMESPACE

bool AInteractableActor::GetOverheadLabel(FOverheadLabel& OutLabel) const
{
	if (ObjectType != EInteractableType::Generator)
	{
		return false;
	}
	// Godot interactable.gd _update_generator_overhead_ui (2.4 m).
	OutLabel.HeightCm = 240.f;
	if (bGeneratorBroken)
	{
		OutLabel.Text = FString::Printf(TEXT("⚡ Generator: DISABLED [0/%d HP]\n(Needs repair)"), FMath::FloorToInt(GeneratorMaxHealth));
		OutLabel.Color = FLinearColor(1.f, 0.25f, 0.25f);
	}
	else if (IsGeneratorWorking())
	{
		OutLabel.Text = FString::Printf(TEXT("⚡ Generator: RUNNING [%d/%d HP]\n(Powered)"), FMath::FloorToInt(GeneratorHealth), FMath::FloorToInt(GeneratorMaxHealth));
		OutLabel.Color = FLinearColor(0.2f, 0.9f, 0.4f);
	}
	else
	{
		OutLabel.Text = FString::Printf(TEXT("⚡ Backup generator [%d/%d HP]"), FMath::FloorToInt(GeneratorHealth), FMath::FloorToInt(GeneratorMaxHealth));
		OutLabel.Color = FLinearColor(0.9f, 0.8f, 0.3f);
	}
	return true;
}

void AInteractableActor::BroadcastGeneratorState(bool bPowered) const
{
	if (UCodexEventBus* Bus = UCodexEventBus::Get(this))
	{
		Bus->OnGeneratorStateChanged.Broadcast(bPowered);
	}
}
