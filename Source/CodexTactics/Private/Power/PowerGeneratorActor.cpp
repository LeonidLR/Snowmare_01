#include "Power/PowerGeneratorActor.h"

#include "AI/StealthLureSubsystem.h"
#include "Characters/OperativeAnimInstance.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Components/BoxComponent.h"
#include "Components/PointLightComponent.h"
#include "Core/PlayerCommands.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "Interactables/HeatSourceComponent.h"
#include "Interactables/TurretActor.h"
#include "TimerManager.h"
#include "UI/FloatingTextSubsystem.h"
#include "UI/GameMessageSubsystem.h"
#include "UI/OverheadLabel.h"

#define LOCTEXT_NAMESPACE "PowerGeneratorActor"

namespace PowerGeneratorInternal
{
	/** The timed actions advance in game time steps of this length (a timer: frozen in the tactical pause). */
	constexpr float ActionStepSeconds = 0.1f;
	/** The squad-contact check of a pre-powered generator, seconds. */
	constexpr float ContactCheckSeconds = 0.5f;
	/** A worker ordered to walk (moving faster than this, cm/s) abandons the action. */
	constexpr float WorkerMovedSpeed = 80.f;
	/** A blizzard share above this lengthens a start / seize (turret_power.json start_s_blizzard_mult). */
	constexpr float BlizzardThreshold = 0.5f;

	const FName GeneratorStartedEvent(TEXT("GeneratorStarted"));
	const FName GeneratorDryEvent(TEXT("GeneratorDry"));
	const FName PowerStartedEvent(TEXT("Power.GeneratorStarted"));
	const FName PowerStoppedEvent(TEXT("Power.GeneratorStopped"));
	const FName PowerSeizedEvent(TEXT("Power.NetworkSeized"));
	const FName PowerDryEvent(TEXT("Power.GeneratorDry"));
	const FName PowerRefuelledEvent(TEXT("Power.GeneratorRefuelled"));

	FText ActionLabel(EGeneratorAction Action)
	{
		switch (Action)
		{
		case EGeneratorAction::Refuel: return LOCTEXT("ActRefuel", "⛽ Refuel (+1 jerrycan)");
		case EGeneratorAction::Start: return LOCTEXT("ActStart", "⚡ Start");
		case EGeneratorAction::Stop: return LOCTEXT("ActStop", "⏹ Stop");
		case EGeneratorAction::Seize: return LOCTEXT("ActSeize", "⚡ Seize (stop + restart for us)");
		case EGeneratorAction::Repair: return LOCTEXT("ActRepair", "🔧 Repair");
		default: return LOCTEXT("ActNone", "-");
		}
	}

	FText ActionProgress(EGeneratorAction Action)
	{
		switch (Action)
		{
		case EGeneratorAction::Refuel: return LOCTEXT("DoRefuel", "pouring diesel into the tank");
		case EGeneratorAction::Start: return LOCTEXT("DoStart", "cranking the diesel");
		case EGeneratorAction::Stop: return LOCTEXT("DoStop", "cutting the fuel line");
		case EGeneratorAction::Seize: return LOCTEXT("DoSeize", "killing the engine and restarting it on our side");
		case EGeneratorAction::Repair: return LOCTEXT("DoRepair", "patching the generator");
		default: return FText::GetEmpty();
		}
	}

	FText RefusalText(EGeneratorRefusal Refusal)
	{
		switch (Refusal)
		{
		case EGeneratorRefusal::Broken: return LOCTEXT("RefBroken", "The generator is broken - repair it first.");
		case EGeneratorRefusal::NotBroken: return LOCTEXT("RefNotBroken", "Nothing to repair.");
		case EGeneratorRefusal::NoFuel: return LOCTEXT("RefNoFuel", "The tank is dry.");
		case EGeneratorRefusal::NoJerrycan: return LOCTEXT("RefNoCan", "The tank is dry - this operative carries no diesel jerrycan.");
		case EGeneratorRefusal::TankFull: return LOCTEXT("RefFull", "The tank is full.");
		case EGeneratorRefusal::AlreadyRunning: return LOCTEXT("RefRunning", "It is already running.");
		case EGeneratorRefusal::NotRunning: return LOCTEXT("RefNotRunning", "It is not running.");
		case EGeneratorRefusal::AlreadyOurs: return LOCTEXT("RefOurs", "This network is already ours.");
		case EGeneratorRefusal::TurnBased: return LOCTEXT("RefTurnBased", "No time for the generator in this fight.");
		default: return FText::GetEmpty();
		}
	}

	FText OwnerText(int32 OwnerTeam)
	{
		switch (PowerRules::AllegianceOf(OwnerTeam))
		{
		case EPowerAllegiance::Friendly: return LOCTEXT("OwnerSquad", "OURS (friendly)");
		case EPowerAllegiance::Hostile: return FText::Format(LOCTEXT("OwnerPmc", "PMC team {0} (hostile)"), OwnerTeam);
		default: return LOCTEXT("OwnerNone", "nobody (neutral)");
		}
	}

	bool IsAliveOperative(const AOperativeCharacter* Operative)
	{
		const UHealthComponent* Health = Operative ? Operative->FindComponentByClass<UHealthComponent>() : nullptr;
		return Health && Health->IsAlive();
	}
}

APowerGeneratorActor::APowerGeneratorActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.1f;
	ObjectType = EInteractableType::Generator;
	DisplayName = LOCTEXT("Name", "Diesel generator");
	Box->SetBoxExtent(FVector(70.f, 50.f, 60.f));

	WorkLamp = CreateDefaultSubobject<UPointLightComponent>(TEXT("WorkLamp"));
	WorkLamp->SetupAttachment(Box);
	WorkLamp->SetRelativeLocation(FVector(0.f, 0.f, 110.f));
	WorkLamp->SetIntensity(2500.f);
	WorkLamp->SetAttenuationRadius(500.f);
	WorkLamp->SetCastShadows(false);
	WorkLamp->SetLightColor(FLinearColor(1.f, 0.65f, 0.2f));
	WorkLamp->SetVisibility(false);
}

void APowerGeneratorActor::BeginPlay()
{
	const FPowerTuning& Tuning = PowerRules::GetTuning();
	GeneratorMaxHealth = Tuning.GeneratorHp;
	const bool bWasBroken = bGeneratorBroken;
	Super::BeginPlay(); // GeneratorHealth = max; no quest binding (IsQuestGenerator false)
	HeatSource->Radius = Tuning.HeatRadiusCm;
	if (!bInitialised)
	{
		InitLevelGenerator(StartState);
	}
	else if (bWasBroken)
	{
		GeneratorHealth = 0.f;
	}
	if (bAutoLinkTurrets)
	{
		// Next tick: every level turret has begun play by then.
		GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]() { AutoLinkNearbyTurrets(); }));
	}
	RefreshNetwork();
}

void APowerGeneratorActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(ActionTimer);
	for (const TWeakObjectPtr<ATurretActor>& Turret : Linked)
	{
		if (Turret.IsValid() && Turret->GetPowerSource() == this)
		{
			Turret->SetPowerSource(nullptr);
		}
	}
	Linked.Reset();
	Super::EndPlay(EndPlayReason);
}

EGeneratorState APowerGeneratorActor::GetState() const
{
	return PowerRules::DeriveState(bGeneratorBroken, FuelL, bSwitchOn);
}

int32 APowerGeneratorActor::GetLiveTurretCount() const
{
	int32 Count = 0;
	for (const TWeakObjectPtr<ATurretActor>& Turret : Linked)
	{
		Count += Turret.IsValid() && !Turret->IsBroken() ? 1 : 0;
	}
	return Count;
}

TArray<ATurretActor*> APowerGeneratorActor::GetLinkedTurrets() const
{
	TArray<ATurretActor*> Out;
	for (const TWeakObjectPtr<ATurretActor>& Turret : Linked)
	{
		if (Turret.IsValid())
		{
			Out.Add(Turret.Get());
		}
	}
	return Out;
}

float APowerGeneratorActor::GetSecondsLeft() const
{
	return PowerRules::SecondsLeft(PowerRules::GetTuning(), FuelL, GetLiveTurretCount());
}

bool APowerGeneratorActor::IsAttackableGenerator() const
{
	// TURRET_POWER.md 5: monsters go for a RUNNING generator (its noise and light draw them); a silent one is scenery.
	return Super::IsAttackableGenerator() && IsRunning();
}

bool APowerGeneratorActor::IsTurnBased() const
{
	const UGameFlowSubsystem* Flow = GetWorld() ? GetWorld()->GetSubsystem<UGameFlowSubsystem>() : nullptr;
	return Flow && Flow->GetCombatMode() == ECodexCombatMode::TurnBased;
}

void APowerGeneratorActor::InitLevelGenerator(EGeneratorStart Start, TOptional<float> InFuelL)
{
	const FGeneratorSetup Setup = PowerRules::StartSetup(PowerRules::GetTuning(), Start, InFuelL);
	StartState = Start;
	bGeneratorBroken = Setup.bBroken;
	GeneratorHealth = Setup.bBroken ? 0.f : GeneratorMaxHealth;
	FuelL = Setup.FuelL;
	bSwitchOn = Setup.bSwitchOn;
	OwnerTeam = Setup.OwnerTeam;
	bContact = Setup.bContact;
	NextWarning = 0;
	bInitialised = true;
	if (HasActorBegunPlay())
	{
		RefreshNetwork();
	}
}

bool APowerGeneratorActor::LinkTurret(ATurretActor* Turret)
{
	if (!Turret)
	{
		return false;
	}
	Linked.RemoveAll([](const TWeakObjectPtr<ATurretActor>& Each) { return !Each.IsValid(); });
	if (Linked.Contains(Turret))
	{
		return true;
	}
	if (Linked.Num() >= PowerRules::GetTuning().MaxTurrets)
	{
		UE_LOG(LogCodexTactics, Warning, TEXT("[Power] %s: %s not linked - the network already feeds %d turrets"), *GetName(), *Turret->GetName(),
			Linked.Num());
		return false;
	}
	if (APowerGeneratorActor* Previous = Turret->GetPowerSource(); Previous && Previous != this)
	{
		Previous->Linked.Remove(Turret);
	}
	Turret->SetPowerSource(this);
	Linked.Add(Turret);
	if (bOwnerKnown)
	{
		Turret->RevealOwner(false);
	}
	if (HasActorBegunPlay())
	{
		Turret->ApplyNetworkState(IsRunning(), OwnerTeam);
	}
	return true;
}

bool APowerGeneratorActor::UnlinkTurret(ATurretActor* Turret)
{
	if (!Turret || !Linked.Contains(Turret))
	{
		return false;
	}
	Linked.Remove(Turret);
	if (Turret->GetPowerSource() == this)
	{
		Turret->SetPowerSource(nullptr);
	}
	return true;
}

int32 APowerGeneratorActor::AutoLinkNearbyTurrets()
{
	// A hand-placed generator: the nearest unlinked LEVEL turrets (not the squad's own kit turrets - their plug-in rule is phase 2).
	TArray<ATurretActor*> Candidates;
	TArray<FVector> Locations;
	for (TActorIterator<ATurretActor> It(GetWorld()); It; ++It)
	{
		if (!It->IsNetworkLinked() && It->GetFaction() != ETurretFaction::Friendly)
		{
			Candidates.Add(*It);
			Locations.Add(It->GetActorLocation());
		}
	}
	int32 Count = 0;
	for (const int32 Index : PowerRules::PickTurretsInReach(PowerRules::GetTuning(), GetActorLocation(), Locations))
	{
		Count += LinkTurret(Candidates[Index]) ? 1 : 0;
	}
	if (Count > 0)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Power] %s auto-linked %d turret(s)"), *GetName(), Count);
	}
	return Count;
}

void APowerGeneratorActor::RefreshNetwork()
{
	const EGeneratorState State = GetState();
	const bool bRunning = State == EGeneratorState::Running;
	for (const TWeakObjectPtr<ATurretActor>& Turret : Linked)
	{
		if (Turret.IsValid())
		{
			Turret->ApplyNetworkState(bRunning, OwnerTeam);
		}
	}
	HeatSource->SetHeatActive(bRunning);
	WorkLamp->SetVisibility(bRunning);
	if (State != LastState)
	{
		LastState = State;
		OnPowerStateChanged.Broadcast(this, State);
		ReceivePowerStateChanged(State);
	}
}

void APowerGeneratorActor::RevealNetwork(bool bAnnounce)
{
	const bool bWasKnown = bOwnerKnown;
	bOwnerKnown = true;
	for (const TWeakObjectPtr<ATurretActor>& Turret : Linked)
	{
		if (Turret.IsValid())
		{
			Turret->RevealOwner(false);
		}
	}
	if (bAnnounce && !bWasKnown)
	{
		Announce(FText::Format(LOCTEXT("PanelRead", "⚡ Generator panel: {0}, owner {1}, {2} turret(s) on its cable."),
			FText::FromString(PowerRules::StateName(GetState())), PowerGeneratorInternal::OwnerText(GetOwnerTeam()), Linked.Num()));
	}
}

void APowerGeneratorActor::Announce(const FText& Text) const
{
	PostLine(LOCTEXT("GeneratorSpeaker", "GENERATOR"), Text);
}

void APowerGeneratorActor::AddFuel(float Litres)
{
	const FPowerTuning& Tuning = PowerRules::GetTuning();
	FuelL = FMath::Clamp(FuelL + FMath::Max(0.f, Litres), 0.f, Tuning.TankL);
	RefreshNetwork();
}

bool APowerGeneratorActor::StartBy(int32 Team, const AActor* Starter)
{
	if (GetState() != EGeneratorState::Fuelled)
	{
		return false;
	}
	const FPowerTuning& Tuning = PowerRules::GetTuning();
	bSwitchOn = true;
	OwnerTeam = Team;
	bContact = true;
	++StartCount;
	// The warnings already behind the time left are skipped for this run.
	NextWarning = 0;
	const float Left = GetSecondsLeft();
	while (NextWarning < Tuning.LowFuelWarnSeconds.Num() && Left <= Tuning.LowFuelWarnSeconds[NextWarning])
	{
		++NextWarning;
	}
	if (Team == PowerRules::PlayerTeam)
	{
		RevealNetwork(false); // we started it: we know whose it is
	}
	RefreshNetwork();
	EmitStartNoise(Starter);
	Announce(FText::Format(LOCTEXT("Started", "⚡ Generator started - {0} turret(s) online ({1} L, about {2} left). The engine can be heard far away."),
		GetLiveTurretCount(), FMath::RoundToInt(FuelL), FText::FromString(PowerRules::FormatMinutes(GetSecondsLeft()))));
	FGameplayEventContext Context;
	Context.Subject = const_cast<AActor*>(Starter);
	Context.Target = this;
	Context.Kind = FName(*PowerRules::AllegianceName(GetAllegiance()));
	Context.Location = GetActorLocation();
	Context.Radius = Tuning.StartNoiseCm;
	GameplayEvents::RaiseWithContext(this, PowerGeneratorInternal::GeneratorStartedEvent, Context);
	GameplayEvents::RaiseWithContext(this, PowerGeneratorInternal::PowerStartedEvent, Context);
	UE_LOG(LogCodexTactics, Display, TEXT("[Power] %s started by team %d (%.1f L, %d live turrets, noise heard by %d)"), *GetName(), Team, FuelL,
		GetLiveTurretCount(), LastNoiseListeners);
	return true;
}

void APowerGeneratorActor::StopEngine(const AActor* Stopper, bool bAnnounce)
{
	if (!bSwitchOn)
	{
		return;
	}
	bSwitchOn = false;
	OwnerTeam = PowerRules::NoOwner;
	RefreshNetwork();
	if (bAnnounce)
	{
		Announce(LOCTEXT("Stopped", "⏹ Generator stopped - its turrets are offline."));
	}
	FGameplayEventContext Context;
	Context.Subject = const_cast<AActor*>(Stopper);
	Context.Target = this;
	GameplayEvents::RaiseWithContext(this, PowerGeneratorInternal::PowerStoppedEvent, Context);
}

void APowerGeneratorActor::RepairNow(AActor* Repairer)
{
	GeneratorHealth = GeneratorMaxHealth;
	bGeneratorBroken = false;
	bSwitchOn = false;
	OwnerTeam = PowerRules::NoOwner;
	RefreshNetwork();
	Announce(LOCTEXT("Repaired", "✅ Generator repaired. It stays off until someone starts it."));
	FGameplayEventContext Context;
	Context.Subject = Repairer;
	Context.Target = this;
	GameplayEvents::RaiseWithContext(this, GameplayEvents::GeneratorRepaired, Context);
}

void APowerGeneratorActor::EmitStartNoise(const AActor* Source)
{
	LastNoiseListeners = 0;
	if (UStealthLureSubsystem* Lure = GetWorld() ? GetWorld()->GetSubsystem<UStealthLureSubsystem>() : nullptr)
	{
		// TURRET_POWER.md 7: a start is heard 30 m away (walls muffle it); patrols / guards come to investigate - never a sighting.
		const FVector Ground = GetActorLocation() - FVector(0.f, 0.f, Box->GetScaledBoxExtent().Z);
		LastNoiseListeners = Lure->EmitNoise(Ground, EStealthLureKind::Throw, Source ? Source : this, PowerRules::GetTuning().StartNoiseCm,
			FName(TEXT("Generator")));
	}
}

void APowerGeneratorActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!IsRunning())
	{
		return;
	}
	const FPowerTuning& Tuning = PowerRules::GetTuning();
	if (!bContact)
	{
		ContactCheckTimer -= DeltaSeconds;
		if (ContactCheckTimer <= 0.f)
		{
			ContactCheckTimer = PowerGeneratorInternal::ContactCheckSeconds;
			if (const USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>())
			{
				for (const AOperativeCharacter* Member : Squad->GetMembers())
				{
					if (PowerGeneratorInternal::IsAliveOperative(Member) && FVector::Dist(Member->GetActorLocation(), GetActorLocation()) <= Tuning.ContactWakeCm)
					{
						bContact = true;
						break;
					}
				}
			}
		}
	}
	if (!PowerRules::BurnsNow(Tuning, true, bContact, IsTurnBased()))
	{
		return;
	}
	const int32 Load = GetLiveTurretCount();
	FuelL = PowerRules::FuelAfterBurn(Tuning, FuelL, Load, DeltaSeconds);
	const float Left = PowerRules::SecondsLeft(Tuning, FuelL, Load);
	while (FuelL > 0.f && NextWarning < Tuning.LowFuelWarnSeconds.Num() && Left <= Tuning.LowFuelWarnSeconds[NextWarning])
	{
		Announce(FText::Format(LOCTEXT("FuelLow", "⛽ Fuel low: about {0} s left."), FMath::CeilToInt(Left)));
		++NextWarning;
	}
	if (FuelL <= 0.f)
	{
		FuelL = 0.f;
		StopEngine(nullptr, false);
		Announce(LOCTEXT("Dry", "⛽ The generator sputtered and died - dry. Turrets offline."));
		FGameplayEventContext Context;
		Context.Target = this;
		GameplayEvents::RaiseWithContext(this, PowerGeneratorInternal::GeneratorDryEvent, Context);
		GameplayEvents::RaiseWithContext(this, PowerGeneratorInternal::PowerDryEvent, Context);
	}
}

void APowerGeneratorActor::TakeGeneratorDamage(float Amount)
{
	if (bGeneratorBroken || Amount <= 0.f)
	{
		return;
	}
	GeneratorHealth = FMath::Max(0.f, GeneratorHealth - Amount);
	FGameplayEventContext Context;
	Context.Target = this;
	GameplayEvents::RaiseWithContext(this, GameplayEvents::GeneratorDamaged, Context);
	if (GeneratorHealth <= 0.f)
	{
		Breakdown();
	}
}

void APowerGeneratorActor::Breakdown()
{
	if (ActionInProgress != EGeneratorAction::None)
	{
		CancelAction(LOCTEXT("CancelBroken", "the generator broke down"));
	}
	bGeneratorBroken = true;
	GeneratorHealth = 0.f;
	FuelL = PowerRules::FuelAfterBreak(PowerRules::GetTuning(), FuelL);
	bSwitchOn = false;
	OwnerTeam = PowerRules::NoOwner;
	RefreshNetwork();
	Announce(LOCTEXT("Down", "⚠️ The generator is wrecked - half of its fuel spilled, its turrets are dark."));
	FGameplayEventContext Context;
	Context.Target = this;
	GameplayEvents::RaiseWithContext(this, GameplayEvents::GeneratorDown, Context);
}

void APowerGeneratorActor::RestoreGeneratorState(float Health, bool bBroken)
{
	GeneratorHealth = FMath::Clamp(Health, 0.f, GeneratorMaxHealth);
	bGeneratorBroken = bBroken;
	if (bBroken)
	{
		bSwitchOn = false;
		OwnerTeam = PowerRules::NoOwner;
	}
	RefreshNetwork();
}

bool APowerGeneratorActor::HandleDirectInteraction(AOperativeCharacter* Leader)
{
	// TURRET_POWER.md 4 step 3b: reading the panel identifies the whole network; the menu opens as usual.
	RevealNetwork(true);
	return false;
}

FText APowerGeneratorActor::DescribeNetwork() const
{
	const FPowerTuning& Tuning = PowerRules::GetTuning();
	FString Turrets;
	int32 Index = 0;
	for (const TWeakObjectPtr<ATurretActor>& Turret : Linked)
	{
		if (!Turret.IsValid())
		{
			continue;
		}
		const UHealthComponent* TurretHealth = Turret->Health;
		Turrets += FString::Printf(TEXT("\n  - turret %d: %s, %d/%d HP, %d m"), ++Index,
			Turret->IsBroken() ? TEXT("BROKEN") : (Turret->IsPowered() ? TEXT("LIVE") : TEXT("DARK")),
			TurretHealth ? FMath::FloorToInt(TurretHealth->GetCurrentHealth()) : 0, TurretHealth ? FMath::FloorToInt(TurretHealth->GetMaxHealth()) : 0,
			FMath::RoundToInt(FVector::Dist(Turret->GetActorLocation(), GetActorLocation()) / 100.f));
	}
	const FText Left = IsRunning()
		? FText::Format(LOCTEXT("LeftRunning", " - about {0} left"), FText::FromString(PowerRules::FormatMinutes(GetSecondsLeft())))
		: FText::GetEmpty();
	return FText::Format(LOCTEXT("Network", "Fuel: {0} / {1} L{2}\nHP: {3}/{4}\nOwner: {5}\nTurrets on the cable: {6}{7}"),
		FMath::RoundToInt(FuelL), FMath::RoundToInt(Tuning.TankL), Left, FMath::FloorToInt(GeneratorHealth), FMath::FloorToInt(GeneratorMaxHealth),
		PowerGeneratorInternal::OwnerText(GetOwnerTeam()), Index, FText::FromString(Turrets));
}

FActionMenuRequest APowerGeneratorActor::BuildActionMenu(const AOperativeCharacter* Leader) const
{
	using namespace PowerGeneratorInternal;
	const FText Title = FText::Format(LOCTEXT("Title", "⚡ Diesel generator [{0}]"), FText::FromString(PowerRules::StateName(GetState())));
	if (ActionInProgress != EGeneratorAction::None)
	{
		return FActionMenuRequest::MakeMessage(LOCTEXT("GeneratorSpeaker", "GENERATOR"),
			FText::Format(LOCTEXT("Busy", "{0} is already working on the generator."), Worker.IsValid() ? Worker->DisplayName : LOCTEXT("Someone", "Someone")));
	}
	const FPowerTuning& Tuning = PowerRules::GetTuning();
	const int32 Team = Leader ? Leader->TeamId : PowerRules::PlayerTeam;
	const EGeneratorAction Action = PowerRules::PrimaryAction(GetState(), GetOwnerTeam(), Team);
	const EGeneratorRefusal Refusal = PowerRules::CheckAction(Tuning, Action, GetState(), FuelL, Leader ? Leader->DieselCansCount : 0, GetOwnerTeam(),
		Team, IsTurnBased());
	const bool bEngineer = Leader && Leader->SquadRole == EOperativeRole::Engineer;
	const FText Seconds = FText::AsNumber(PowerRules::ActionSeconds(Tuning, Action, bEngineer, false),
		&FNumberFormattingOptions().SetMinimumFractionalDigits(1).SetMaximumFractionalDigits(1));
	const FText ActionLine = Refusal == EGeneratorRefusal::None
		? FText::Format(LOCTEXT("ActionLine", "\n\n{0}: {1} s{2}."), ActionLabel(Action), Seconds,
			Action == EGeneratorAction::Start || Action == EGeneratorAction::Seize ? LOCTEXT("LoudHint", " - LOUD: patrols within 30 m will come") : FText::GetEmpty())
		: FText::Format(LOCTEXT("RefusalLine", "\n\n{0}"), RefusalText(Refusal));
	return FActionMenuRequest::MakeMenu(Title, FText::Format(LOCTEXT("Desc", "{0}{1}"), DescribeNetwork(), ActionLine), ActionLabel(Action),
		LOCTEXT("Close", "Close"), Refusal != EGeneratorRefusal::None);
}

void APowerGeneratorActor::PerformAction(AOperativeCharacter* User)
{
	if (!User)
	{
		return;
	}
	if (ActionInProgress != EGeneratorAction::None)
	{
		PostLine(User->DisplayName, LOCTEXT("AlreadyWorking", "Someone is already working on the generator."));
		return;
	}
	const FPowerTuning& Tuning = PowerRules::GetTuning();
	const EGeneratorAction Action = PowerRules::PrimaryAction(GetState(), GetOwnerTeam(), User->TeamId);
	const EGeneratorRefusal Refusal = PowerRules::CheckAction(Tuning, Action, GetState(), FuelL, User->DieselCansCount, GetOwnerTeam(),
		User->TeamId, IsTurnBased());
	if (Refusal != EGeneratorRefusal::None)
	{
		PostLine(User->DisplayName, PowerGeneratorInternal::RefusalText(Refusal));
		return;
	}
	BeginAction(User, Action);
}

void APowerGeneratorActor::BeginAction(AOperativeCharacter* User, EGeneratorAction Action)
{
	const FPowerTuning& Tuning = PowerRules::GetTuning();
	const UOperativeAnimInstance* Anim = UOperativeAnimInstance::Of(User);
	const bool bBlizzard = Anim && Anim->BlizzardAmount >= PowerGeneratorInternal::BlizzardThreshold;
	ActionInProgress = Action;
	Worker = User;
	ActionElapsed = 0.f;
	bSeizeStopped = false;
	ActionDuration = PowerRules::ActionSeconds(Tuning, Action, User->SquadRole == EOperativeRole::Engineer, bBlizzard);
	const FRotator Facing = (GetActorLocation() - User->GetActorLocation()).Rotation();
	User->StopOperative();
	User->SetActorRotation(FRotator(0.f, Facing.Yaw, 0.f));
	User->SetStance(EOperativeStance::Crouching);
	UFloatingTextSubsystem::SpawnAboveOperative(User, TEXT("⚡ GENERATOR..."), FLinearColor(1.f, 0.75f, 0.25f));
	PostLine(User->DisplayName, FText::Format(LOCTEXT("Working", "⚡ {0}: {1} ({2} s)..."), User->DisplayName, PowerGeneratorInternal::ActionProgress(Action),
		FText::AsNumber(ActionDuration, &FNumberFormattingOptions().SetMinimumFractionalDigits(1).SetMaximumFractionalDigits(1))));
	GetWorldTimerManager().SetTimer(ActionTimer, this, &APowerGeneratorActor::TickAction, PowerGeneratorInternal::ActionStepSeconds, true);
	UE_LOG(LogCodexTactics, Display, TEXT("[Power] %s: %s begins %s (%.1f s)"), *GetName(), *User->DisplayName.ToString(),
		*UEnum::GetValueAsString(Action), ActionDuration);
}

void APowerGeneratorActor::TickAction()
{
	const FPowerTuning& Tuning = PowerRules::GetTuning();
	AOperativeCharacter* User = Worker.Get();
	FText Reason;
	if (!PowerGeneratorInternal::IsAliveOperative(User))
	{
		Reason = LOCTEXT("CancelDead", "the worker is down");
	}
	else if (User->IsKnockedDown())
	{
		Reason = LOCTEXT("CancelKnocked", "the worker was knocked down");
	}
	else if (GetDistanceTo(User->GetActorLocation()) > Tuning.InterruptReachCm || User->GetVelocity().Size2D() > PowerGeneratorInternal::WorkerMovedSpeed)
	{
		Reason = LOCTEXT("CancelMoved", "the worker left the panel");
	}
	else if (IsTurnBased())
	{
		Reason = LOCTEXT("CancelTurnBased", "the fight turned turn-based");
	}
	else if (bGeneratorBroken && ActionInProgress != EGeneratorAction::Repair)
	{
		Reason = LOCTEXT("CancelBroken", "the generator broke down");
	}
	if (!Reason.IsEmpty())
	{
		CancelAction(Reason);
		return;
	}
	ActionElapsed += PowerGeneratorInternal::ActionStepSeconds;
	if (ActionInProgress == EGeneratorAction::Seize && !bSeizeStopped && ActionElapsed >= FMath::Min(Tuning.SeizeDarkAfterSeconds, ActionDuration))
	{
		// The network is dark from second 1 of a seize (section 11: nothing fires during it).
		bSeizeStopped = true;
		StopEngine(User, false);
	}
	if (ActionElapsed + KINDA_SMALL_NUMBER >= ActionDuration)
	{
		FinishAction();
	}
}

void APowerGeneratorActor::CancelAction(const FText& Reason)
{
	GetWorldTimerManager().ClearTimer(ActionTimer);
	const EGeneratorAction Action = ActionInProgress;
	ActionInProgress = EGeneratorAction::None;
	const AOperativeCharacter* User = Worker.Get();
	Worker.Reset();
	// All-or-nothing (section 7): no fuel moved, the can stays with him; a seize past second 1 leaves the generator stopped.
	PostLine(User ? User->DisplayName : LOCTEXT("GeneratorSpeaker", "GENERATOR"),
		FText::Format(LOCTEXT("Cancelled", "Generator work interrupted ({0}): {1}."), PowerGeneratorInternal::ActionLabel(Action), Reason));
	UE_LOG(LogCodexTactics, Display, TEXT("[Power] %s: %s cancelled (%s)"), *GetName(), *UEnum::GetValueAsString(Action), *Reason.ToString());
}

void APowerGeneratorActor::FinishAction()
{
	using namespace PowerGeneratorInternal;
	GetWorldTimerManager().ClearTimer(ActionTimer);
	const EGeneratorAction Action = ActionInProgress;
	ActionInProgress = EGeneratorAction::None;
	AOperativeCharacter* User = Worker.Get();
	Worker.Reset();
	if (!User)
	{
		return;
	}
	const FPowerTuning& Tuning = PowerRules::GetTuning();
	switch (Action)
	{
	case EGeneratorAction::Refuel:
		if (User->DieselCansCount > 0)
		{
			--User->DieselCansCount;
			AddFuel(Tuning.JerrycanL);
			PostLine(User->DisplayName, FText::Format(LOCTEXT("Refuelled", "⛽ {0}: one jerrycan in - {1} L in the tank."), User->DisplayName,
				FMath::RoundToInt(FuelL)));
			FGameplayEventContext Context;
			Context.Subject = User;
			Context.Target = this;
			GameplayEvents::RaiseWithContext(this, PowerRefuelledEvent, Context);
		}
		break;
	case EGeneratorAction::Start:
		StartBy(User->TeamId, User);
		break;
	case EGeneratorAction::Stop:
		StopEngine(User);
		break;
	case EGeneratorAction::Seize:
		if (StartBy(User->TeamId, User))
		{
			Announce(FText::Format(LOCTEXT("Seized", "⚡ Network seized - {0} turret(s) now answer to us."), GetLiveTurretCount()));
			FGameplayEventContext Context;
			Context.Subject = User;
			Context.Target = this;
			GameplayEvents::RaiseWithContext(this, PowerSeizedEvent, Context);
		}
		break;
	case EGeneratorAction::Repair:
		RepairNow(User);
		break;
	default:
		break;
	}
}

#undef LOCTEXT_NAMESPACE

bool APowerGeneratorActor::GetOverheadLabel(FOverheadLabel& OutLabel) const
{
	OutLabel.HeightCm = 200.f;
	const EGeneratorState State = GetState();
	switch (State)
	{
	case EGeneratorState::Running:
		OutLabel.Text = bOwnerKnown
			? FString::Printf(TEXT("Generator: RUNNING [%s] %d L ~%s"), *PowerRules::AllegianceName(GetAllegiance()), FMath::RoundToInt(FuelL),
				*PowerRules::FormatMinutes(GetSecondsLeft()))
			: FString(TEXT("Generator: RUNNING ?"));
		OutLabel.Color = FLinearColor(1.f, 0.75f, 0.25f);
		break;
	case EGeneratorState::Fuelled:
		OutLabel.Text = FString::Printf(TEXT("Generator: OFF [%d L]"), FMath::RoundToInt(FuelL));
		OutLabel.Color = FLinearColor(0.9f, 0.85f, 0.4f);
		break;
	case EGeneratorState::Broken:
		OutLabel.Text = FString::Printf(TEXT("Generator: BROKEN [0/%d HP]"), FMath::FloorToInt(GeneratorMaxHealth));
		OutLabel.Color = FLinearColor(1.f, 0.25f, 0.25f);
		break;
	default:
		OutLabel.Text = TEXT("Generator: DRY");
		OutLabel.Color = FLinearColor(0.6f, 0.62f, 0.66f);
		break;
	}
	return true;
}
