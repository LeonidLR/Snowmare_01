#include "Interactables/BarrelActor.h"
#include "Characters/OperativeCharacter.h"
#include "Components/BoxComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "Interactables/HeatSourceComponent.h"
#include "Interactables/RelocationSubsystem.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UI/GameMessageSubsystem.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "BarrelActor"

ABarrelActor::ABarrelActor()
{
	PrimaryActorTick.bCanEverTick = true;

	DisplayName = LOCTEXT("BarrelName", "Fuel Barrel");
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
	// User decision 2026-10-08: the barrel menu also opens in the fight; "Push" only where objects may be moved
	// (not in the live real-time fight - the tactical pause plans it).
	const URelocationSubsystem* Relocation = GetWorld() ? GetWorld()->GetSubsystem<URelocationSubsystem>() : nullptr;
	return bCanBeRelocated && (!Relocation || Relocation->CanRelocateNow());
}

FActionMenuRequest ABarrelActor::BuildActionMenu(const AOperativeCharacter* Leader) const
{
	const FText Squad = LOCTEXT("SquadSpeaker", "SQUAD");
	const FText Cancel = LOCTEXT("Cancel", "Cancel");
	const FText Push = LOCTEXT("Push", "Push");
	const bool bCanPush = CanPushNow();

	if (Burn.bBurning)
	{
		if (!bCanPush)
		{
			return FActionMenuRequest::MakeMessage(Squad, LOCTEXT("BurningMsg", "The barrel is already burning and warming the air around it."));
		}
		return FActionMenuRequest::MakeMenu(LOCTEXT("BurningTitle", "🔥 Burning Barrel"),
			LOCTEXT("BurningDesc", "The barrel is already burning and warming the air around it.\nYou can push it to a new position."),
			LOCTEXT("BurningBtn", "Already burning"), Cancel, true, true, Push);
	}
	if (Burn.bBurnt)
	{
		if (!bCanPush)
		{
			return FActionMenuRequest::MakeMessage(Squad, LOCTEXT("BurntMsg", "The fuel in this barrel has burnt out."));
		}
		return FActionMenuRequest::MakeMenu(LOCTEXT("BurntTitle", "🪵 Burnt-out Barrel"),
			LOCTEXT("BurntDesc", "The fuel in this barrel has burnt out.\nYou can push it somewhere else."),
			LOCTEXT("BurntBtn", "Empty"), Cancel, true, true, Push);
	}

	const int32 Matches = Leader ? Leader->MatchesCount : 0;
	const FText LeaderName = Leader ? Leader->DisplayName : LOCTEXT("CommanderGenitive", "Commander");
	FText Description = FText::Format(LOCTEXT("IgniteDesc", "Light a fire to warm the squad.\nCost: 🪵 1 match ({0}: x{1})"),
		LeaderName, Matches);
	if (bCanPush)
	{
		Description = FText::Format(LOCTEXT("IgniteDescPush", "{0}\nOr push the barrel to a new position."), Description);
	}
	return FActionMenuRequest::MakeMenu(LOCTEXT("FreshTitle", "🔥 Fuel Barrel"), Description,
		Matches > 0 ? LOCTEXT("IgniteBtn", "Ignite (1 match)") : LOCTEXT("NoMatchesBtn", "No matches"),
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
	const FText UserName = User ? User->DisplayName : LOCTEXT("Soldier", "Soldier");

	switch (Burn.TryIgnite(Matches, BurnDuration))
	{
	case EBarrelIgniteResult::AlreadyBurning:
		PostLine(LOCTEXT("SquadSpeaker", "SQUAD"), LOCTEXT("AlreadyBurning", "The barrel is already burning bright and warming the air."));
		return false;
	case EBarrelIgniteResult::BurntOut:
		PostLine(LOCTEXT("SquadSpeaker", "SQUAD"),
			LOCTEXT("BurntOut", "The fuel in this barrel has burnt out. It cannot be relit."));
		return false;
	case EBarrelIgniteResult::NoMatches:
		PostLine(UserName, LOCTEXT("NoMatches", "I'm out of matches! Switch to another operative who still has some."));
		return false;
	default:
		break;
	}

	ApplyVisuals();
	PostLine(UserName, FText::Format(LOCTEXT("Ignited", "Striking a match... It caught! Getting warmer around here (matches left: {0})."),
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
	PostLine(bByShot ? (InstigatorName.IsEmpty() ? LOCTEXT("Sniper", "Marksman") : InstigatorName) : LOCTEXT("Blast", "BLAST"),
		bByShot ? LOCTEXT("ShotBoom", "💥 Object tripwire detonated by a shot!") : LOCTEXT("TrapBoom", "💥 Object tripwire detonated!"));
	Explode(InstigatorName.IsEmpty() ? LOCTEXT("TrapSource", "Trap") : InstigatorName);
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
		LOCTEXT("BarrelHit", "💥 Burned by the barrel blast (-{0} HP)!"), EStatusEffect::Burning, 4.f, 10.f);
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

void ABarrelActor::RestoreBurnState(bool bBurning, bool bBurnt, float TimeLeft)
{
	const bool bWasBurning = Burn.bBurning;
	Burn.bBurning = bBurning;
	Burn.bBurnt = bBurnt || bBurning;
	Burn.TimeLeft = bBurning ? FMath::Max(TimeLeft, 0.1f) : 0.f;
	ApplyVisuals();
	if (bWasBurning != bBurning)
	{
		OnBurningChanged.Broadcast(this, bBurning);
		ReceiveBurningChanged(bBurning);
	}
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
