#include "Interactables/BarrelActor.h"
#include "Characters/OperativeCharacter.h"
#include "Components/BoxComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "Interactables/HeatSourceComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UI/GameMessageSubsystem.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "BarrelActor"

ABarrelActor::ABarrelActor()
{
	PrimaryActorTick.bCanEverTick = true;

	DisplayName = LOCTEXT("BarrelName", "Горючая бочка");
	bCanBeRelocated = true;

	// Godot cylinder: radius 0.6 m, height 1.4 m.
	Box->SetBoxExtent(FVector(60.f, 60.f, 70.f));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BaseMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (CylinderMesh.Succeeded())
	{
		Mesh->SetStaticMesh(CylinderMesh.Object);
	}
	if (BaseMaterial.Succeeded())
	{
		Mesh->SetMaterial(0, BaseMaterial.Object);
	}

	// Godot WarmZone3D heat_radius 5.5 m, active only while burning.
	HeatSource->Radius = 550.f;
	HeatSource->bHeatActive = false;

	// Godot OmniLight3D: 1.2 m above the barrel centre, orange, range 10 m.
	FireLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("FireLight"));
	FireLight->SetupAttachment(Box);
	FireLight->SetRelativeLocation(FVector(0.f, 0.f, 120.f));
	FireLight->SetLightColor(FLinearColor(1.f, 0.5f, 0.1f));
	FireLight->SetAttenuationRadius(1000.f);
	FireLight->SetIntensityUnits(ELightUnits::Candelas);
	FireLight->SetIntensity(FireLightIntensity);
	FireLight->SetVisibility(false);
	FireLight->SetCastShadows(false);
}

void ABarrelActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyVisuals();
}

bool ABarrelActor::CanPushNow() const
{
	return bCanBeRelocated;
}

FActionMenuRequest ABarrelActor::BuildActionMenu(const AOperativeCharacter* Leader) const
{
	const FText Squad = LOCTEXT("SquadSpeaker", "Отряд");
	const FText Cancel = LOCTEXT("Cancel", "Отмена");
	const FText Push = LOCTEXT("Push", "Вытолкать");
	const bool bCanPush = CanPushNow();

	if (Burn.bBurning)
	{
		if (!bCanPush)
		{
			return FActionMenuRequest::MakeMessage(Squad, LOCTEXT("BurningMsg", "Бочка уже горит и согревает воздух вокруг."));
		}
		return FActionMenuRequest::MakeMenu(LOCTEXT("BurningTitle", "🔥 Горящая бочка"),
			LOCTEXT("BurningDesc", "Бочка уже горит и согревает воздух вокруг.\nВы можете вытолкать её на новую позицию."),
			LOCTEXT("BurningBtn", "Уже горит"), Cancel, true, true, Push);
	}
	if (Burn.bBurnt)
	{
		if (!bCanPush)
		{
			return FActionMenuRequest::MakeMessage(Squad, LOCTEXT("BurntMsg", "Горючее в этой бочке уже полностью выгорело."));
		}
		return FActionMenuRequest::MakeMenu(LOCTEXT("BurntTitle", "🪵 Сгоревшая бочка"),
			LOCTEXT("BurntDesc", "Горючее в этой бочке уже полностью выгорело.\nВы можете вытолкать её в другое место."),
			LOCTEXT("BurntBtn", "Пусто"), Cancel, true, true, Push);
	}

	const int32 Matches = Leader ? Leader->MatchesCount : 0;
	const FText LeaderName = Leader ? Leader->DisplayName : LOCTEXT("CommanderGenitive", "Командира");
	FText Description = FText::Format(LOCTEXT("IgniteDesc", "Разжечь огонь для обогрева отряда.\nСтоимость: 🪵 1 спичка (У {0}: {1} шт.)"),
		LeaderName, Matches);
	if (bCanPush)
	{
		Description = FText::Format(LOCTEXT("IgniteDescPush", "{0}\nИли вытолкать бочку на новую позицию."), Description);
	}
	return FActionMenuRequest::MakeMenu(LOCTEXT("FreshTitle", "🔥 Горючая бочка"), Description,
		Matches > 0 ? LOCTEXT("IgniteBtn", "Разжечь (1 спичка)") : LOCTEXT("NoMatchesBtn", "Нет спичек"),
		Cancel, Matches <= 0, bCanPush, Push);
}

void ABarrelActor::PerformAction(AOperativeCharacter* User)
{
	if (User)
	{
		// Godot _safe_look_at: face the barrel before striking the match.
		FRotator Facing = (GetActorLocation() - User->GetActorLocation()).Rotation();
		User->SetActorRotation(FRotator(0.f, Facing.Yaw, 0.f));
	}
	Ignite(User);
}

bool ABarrelActor::Ignite(AOperativeCharacter* User)
{
	int32 NoMatches = 0;
	int32& Matches = User ? User->MatchesCount : NoMatches;
	const FText UserName = User ? User->DisplayName : LOCTEXT("Soldier", "Боец");

	switch (Burn.TryIgnite(Matches, BurnDuration))
	{
	case EBarrelIgniteResult::AlreadyBurning:
		PostLine(LOCTEXT("SquadSpeaker", "Отряд"), LOCTEXT("AlreadyBurning", "Бочка уже ярко горит и согревает воздух."));
		return false;
	case EBarrelIgniteResult::BurntOut:
		PostLine(LOCTEXT("SquadSpeaker", "Отряд"),
			LOCTEXT("BurntOut", "Горючее в этой бочке уже полностью выгорело. Повторно разжечь её не выйдет."));
		return false;
	case EBarrelIgniteResult::NoMatches:
		PostLine(UserName, LOCTEXT("NoMatches", "У меня закончились спички! Переключитесь на другого бойца, у которого ещё есть спички."));
		return false;
	default:
		break;
	}

	ApplyVisuals();
	PostLine(UserName, FText::Format(LOCTEXT("Ignited", "Чиркаю спичкой... Пламя занялось! Вокруг становится теплее (осталось спичек: {0})."),
		Matches));
	OnBurningChanged.Broadcast(this, true);
	ReceiveBurningChanged(true);
	return true;
}

void ABarrelActor::Extinguish()
{
	if (!Burn.bBurning)
	{
		return;
	}
	Burn.bBurning = false;
	Burn.TimeLeft = 0.f;
	HandleFireOut();
}

void ABarrelActor::HandleFireOut()
{
	ApplyVisuals();
	OnBurningChanged.Broadcast(this, false);
	ReceiveBurningChanged(false);
}

void ABarrelActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!Burn.bBurning)
	{
		return;
	}
	// Godot: in turn-based combat the fire lasts a number of rounds, the real-time timer is frozen.
	const UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
	if (Flow && Flow->GetCombatMode() == ECodexCombatMode::TurnBased)
	{
		return;
	}
	if (Burn.Tick(DeltaSeconds))
	{
		HandleFireOut();
		return;
	}
	// Flicker and fade during the last FadeSeconds (Godot: energy * t/7 * rand(0.7, 1.3), else rand(0.95, 1.05)).
	const bool bFading = Burn.TimeLeft <= FadeSeconds;
	const float Flicker = bFading ? FMath::FRandRange(0.7f, 1.3f) : FMath::FRandRange(0.95f, 1.05f);
	FireLight->SetIntensity(FireLightIntensity * Burn.GetFireStrength(FadeSeconds) * Flicker);
}

void ABarrelActor::ApplyVisuals()
{
	HeatSource->SetHeatActive(Burn.bBurning);
	FireLight->SetVisibility(Burn.bBurning);
	FireLight->SetIntensity(FireLightIntensity);
	if (!BodyMaterial)
	{
		BodyMaterial = Mesh->CreateAndSetMaterialInstanceDynamic(0);
	}
	if (BodyMaterial)
	{
		BodyMaterial->SetVectorParameterValue(TEXT("Color"),
			Burn.bBurning ? BurningColor : (Burn.bBurnt ? CharredColor : NormalColor));
	}
}

void ABarrelActor::DetonateTrap(bool bByShot, const FText& InstigatorName)
{
	if (!bTrapped && !bByShot)
	{
		return;
	}
	bTrapped = false;
	PostLine(bByShot ? (InstigatorName.IsEmpty() ? LOCTEXT("Sniper", "Снайпер") : InstigatorName) : LOCTEXT("Blast", "ВЗРЫВ"),
		bByShot ? LOCTEXT("ShotBoom", "💥 Взрыв растяжки на объекте от выстрела!") : LOCTEXT("TrapBoom", "💥 Растяжка на объекте сдетонировала!"));
	Explode(InstigatorName.IsEmpty() ? LOCTEXT("TrapSource", "Ловушка") : InstigatorName);
}

bool ABarrelActor::Explode(const FText& InstigatorName)
{
	if (Burn.bBurning)
	{
		return false;
	}
	// Godot shoot_and_explode: instant fire burning out in 10 s.
	Burn.bBurnt = true;
	Burn.bBurning = true;
	Burn.TimeLeft = 10.f;
	ApplyVisuals();
	OnBurningChanged.Broadcast(this, true);
	ReceiveBurningChanged(true);
	ApplyBlast(120.f, 80.f, 550.f, 0.40f, EDamageType::Fire, InstigatorName,
		LOCTEXT("BarrelHit", "💥 Обожгло взрывом бочки (-{0} HP)!"), EStatusEffect::Burning, 4.f, 10.f);
	return true;
}

bool ABarrelActor::IgniteForTurnBased()
{
	if (Burn.bBurning)
	{
		return false;
	}
	Burn.bBurnt = true;
	Burn.bBurning = true;
	Burn.TimeLeft = 10.f;
	ApplyVisuals();
	OnBurningChanged.Broadcast(this, true);
	ReceiveBurningChanged(true);
	return true;
}

void ABarrelActor::ExtinguishNow()
{
	if (!Burn.bBurning)
	{
		return;
	}
	Burn.bBurning = false;
	Burn.TimeLeft = 0.f;
	HandleFireOut();
}

#undef LOCTEXT_NAMESPACE
