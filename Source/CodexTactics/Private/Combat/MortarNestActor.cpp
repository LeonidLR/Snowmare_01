#include "Combat/MortarNestActor.h"

#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/MortarSubsystem.h"
#include "Combat/ProjectileSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "UObject/ConstructorHelpers.h"

namespace MortarNestLocal
{
	/** A crewman this close to his place has arrived, cm. */
	constexpr float ArriveCm = 120.f;

	bool IsAlive(const AOperativeCharacter* Man)
	{
		return IsValid(Man) && Man->HealthComponent && Man->HealthComponent->IsAlive();
	}
}

AMortarNestActor::AMortarNestActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.1f;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	// PLACEHOLDER visuals (the designer picks the models later): a sandbag disc + the tube tilted forward.
	BaseMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BaseMesh"));
	BaseMesh->SetupAttachment(Root);
	BaseMesh->SetRelativeLocation(FVector(0.f, 0.f, 12.f));
	BaseMesh->SetRelativeScale3D(FVector(1.3f, 1.3f, 0.25f));
	BaseMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TubeMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TubeMesh"));
	TubeMesh->SetupAttachment(Root);
	TubeMesh->SetRelativeLocation(FVector(15.f, 0.f, 70.f));
	TubeMesh->SetRelativeRotation(FRotator(-30.f, 0.f, 0.f)); // the tube's axis (Z) leans 30 degrees towards the nest's forward (+X)
	TubeMesh->SetRelativeScale3D(FVector(0.12f, 0.12f, 1.2f));
	TubeMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (Cylinder.Succeeded())
	{
		BaseMesh->SetStaticMesh(Cylinder.Object);
		TubeMesh->SetStaticMesh(Cylinder.Object);
	}
	Stream.Initialize(1109);
}

void AMortarNestActor::BeginPlay()
{
	Super::BeginPlay();
	const FMortarNestConfig& Config = MortarRules::Get();
	Places.SetNum(FMath::Max(1, Config.CrewNeeded));
	if (Ammo < 0)
	{
		Ammo = Config.Ammo;
	}
	if (NestId.IsEmpty())
	{
		NestId = GetName();
	}
	if (UMortarSubsystem* Mortars = UMortarSubsystem::Get(GetWorld()))
	{
		Mortars->RegisterNest(this);
	}
}

void AMortarNestActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ReleaseAllCrew();
	if (UMortarSubsystem* Mortars = UMortarSubsystem::Get(GetWorld()))
	{
		Mortars->UnregisterNest(this);
	}
	Super::EndPlay(EndPlayReason);
}

FVector AMortarNestActor::GetCrewPlace(int32 Index) const
{
	return MortarRules::CrewPlace(GetActorLocation(), GetActorRotation().Yaw, Index, MortarRules::Get());
}

FVector AMortarNestActor::GetMuzzle() const
{
	return TubeMesh ? TubeMesh->GetComponentTransform().TransformPosition(FVector(0.f, 0.f, 50.f)) : GetActorLocation() + FVector(0.f, 0.f, 120.f);
}

int32 AMortarNestActor::AssignCrew(AOperativeCharacter* Man, bool bAtOnce)
{
	if (!MortarNestLocal::IsAlive(Man))
	{
		return -1;
	}
	if (UMortarSubsystem* Mortars = UMortarSubsystem::Get(GetWorld()))
	{
		if (AMortarNestActor* Other = Mortars->FindNestOf(Man); Other && Other != this)
		{
			Other->ReleaseCrew(Man);
		}
	}
	int32 Free = INDEX_NONE;
	for (int32 Index = 0; Index < Places.Num(); ++Index)
	{
		if (Places[Index].Man.Get() == Man)
		{
			return Index;
		}
		if (Free == INDEX_NONE && !MortarNestLocal::IsAlive(Places[Index].Man.Get()))
		{
			Free = Index;
		}
	}
	if (Free == INDEX_NONE)
	{
		return -1;
	}
	if (Man->TeamId != TeamId)
	{
		if (GetCrewAssigned() > 0)
		{
			return -1; // still crewed by the other side
		}
		UE_LOG(LogCodexTactics, Display, TEXT("[Mortar] %s taken by team %d (was %d)"), *NestId, Man->TeamId, TeamId);
		TeamId = Man->TeamId;
		CeaseFire();
	}
	ReleaseCrew(Places[Free].Man.Get());
	Places[Free].Man = Man;
	Places[Free].bAtPlace = false;
	const FVector Place = GetCrewPlace(Free);
	if (bAtOnce)
	{
		Man->TeleportTo(FVector(Place.X, Place.Y, Place.Z + Man->GetSimpleCollisionHalfHeight() + 5.f), (GetActorLocation() - Place).Rotation(), false, true);
	}
	else
	{
		Man->OrderMoveTo(Place, false);
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[Mortar] %s: %s takes place %d (%s)"), *NestId, *Man->DisplayName.ToString(), Free, bAtOnce ? TEXT("at once") : TEXT("walks there"));
	UpdateCrew();
	return Free;
}

void AMortarNestActor::ReleaseCrew(AOperativeCharacter* Man)
{
	if (!Man)
	{
		return;
	}
	for (FCrewPlace& Place : Places)
	{
		if (Place.Man.Get() == Man)
		{
			Place.Man.Reset();
			Place.bAtPlace = false;
		}
	}
	Man->SetTrafficHold(this, false);
}

void AMortarNestActor::ReleaseAllCrew()
{
	for (FCrewPlace& Place : Places)
	{
		if (AOperativeCharacter* Man = Place.Man.Get())
		{
			Man->SetTrafficHold(this, false);
		}
		Place.Man.Reset();
		Place.bAtPlace = false;
	}
}

void AMortarNestActor::UpdateCrew()
{
	const FMortarNestConfig& Config = MortarRules::Get();
	for (int32 Index = 0; Index < Places.Num(); ++Index)
	{
		FCrewPlace& Place = Places[Index];
		AOperativeCharacter* Man = Place.Man.Get();
		if (!Man)
		{
			continue;
		}
		const FVector Spot = GetCrewPlace(Index);
		const float Distance = static_cast<float>(FVector::Dist2D(Man->GetActorLocation(), Spot));
		if (!MortarNestLocal::IsAlive(Man))
		{
			UE_LOG(LogCodexTactics, Display, TEXT("[Mortar] %s: %s is down - the crew is short"), *NestId, *Man->DisplayName.ToString());
			ReleaseCrew(Man);
			continue;
		}
		if (!Place.bAtPlace)
		{
			if (Distance <= MortarNestLocal::ArriveCm)
			{
				Place.bAtPlace = true;
				Man->StopAndFace(GetActorLocation() + GetActorForwardVector() * 1000.f);
				Man->SetTrafficHold(this, true); // the formation no longer pulls him
			}
			else if (!Man->HasOwnMoveOrder() && Distance > Config.CrewLeaveCm)
			{
				Man->OrderMoveTo(Spot, false); // still on his way (an order lost to a stance change etc.)
			}
			continue;
		}
		// At his place: a walk away (the player's / his commander's move) or being pushed off it = he left the crew.
		if (Man->HasOwnMoveOrder() || Distance > Config.CrewLeaveCm)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("[Mortar] %s: %s left the nest (%.0f cm off)"), *NestId, *Man->DisplayName.ToString(), Distance);
			ReleaseCrew(Man);
		}
	}
}

int32 AMortarNestActor::GetCrewAtNest() const
{
	int32 Count = 0;
	for (const FCrewPlace& Place : Places)
	{
		Count += Place.bAtPlace && MortarNestLocal::IsAlive(Place.Man.Get()) ? 1 : 0;
	}
	return Count;
}

int32 AMortarNestActor::GetCrewAssigned() const
{
	int32 Count = 0;
	for (const FCrewPlace& Place : Places)
	{
		Count += MortarNestLocal::IsAlive(Place.Man.Get()) ? 1 : 0;
	}
	return Count;
}

bool AMortarNestActor::IsCrewing(const AOperativeCharacter* Man) const
{
	return Man && Places.ContainsByPredicate([Man](const FCrewPlace& Place) { return Place.Man.Get() == Man; });
}

TArray<AOperativeCharacter*> AMortarNestActor::GetCrew() const
{
	TArray<AOperativeCharacter*> Out;
	for (const FCrewPlace& Place : Places)
	{
		if (AOperativeCharacter* Man = Place.Man.Get())
		{
			Out.Add(Man);
		}
	}
	return Out;
}

EMortarRefusal AMortarNestActor::CheckFireAt(const FVector& Point, AOperativeCharacter** OutSpotter) const
{
	const FMortarNestConfig& Config = MortarRules::Get();
	const UMortarSubsystem* Mortars = UMortarSubsystem::Get(GetWorld());
	AOperativeCharacter* Spotter = Mortars ? Mortars->FindSpotter(TeamId, Point) : nullptr;
	if (OutSpotter)
	{
		*OutSpotter = Spotter;
	}
	return MortarRules::Check(GetCrewAtNest(), Ammo, static_cast<float>(FVector::Dist2D(GetActorLocation(), Point)), Spotter != nullptr, Config);
}

EMortarRefusal AMortarNestActor::OrderFire(const FVector& Point)
{
	UpdateCrew();
	LastRefusal = CheckFireAt(Point);
	if (LastRefusal != EMortarRefusal::None)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Mortar] %s (team %d): fire order refused - %s"), *NestId, TeamId, *MortarRules::RefusalText(LastRefusal));
		return LastRefusal;
	}
	const FMortarNestConfig& Config = MortarRules::Get();
	if (!bHasTarget || !MortarRules::IsSameTarget(FireTarget, Point, Config.Bracketing))
	{
		RoundOnTarget = 0; // a new target: the bracketing walk starts again
	}
	FireTarget = Point;
	bHasTarget = true;
	RoundsLeft = Config.RoundsPerOrder;
	UE_LOG(LogCodexTactics, Display, TEXT("[Mortar] %s (team %d): fire at %s, %d rounds (%.0f m)"), *NestId, TeamId, *Point.ToCompactString(), RoundsLeft,
		FVector::Dist2D(GetActorLocation(), Point) / 100.f);
	return LastRefusal;
}

void AMortarNestActor::CeaseFire()
{
	RoundsLeft = 0;
}

void AMortarNestActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateCrew();
	Cooldown = FMath::Max(0.f, Cooldown - DeltaSeconds);
	if (RoundsLeft <= 0 || Cooldown > 0.f)
	{
		return;
	}
	// Every round re-checks the crew and the spotter: a man down / walked off, or the spot out of sight, stops the fire.
	LastRefusal = CheckFireAt(FireTarget);
	if (LastRefusal != EMortarRefusal::None)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Mortar] %s (team %d): fire stops - %s"), *NestId, TeamId, *MortarRules::RefusalText(LastRefusal));
		RoundsLeft = 0;
		return;
	}
	FireRound();
}

void AMortarNestActor::FireRound()
{
	UWorld* World = GetWorld();
	UProjectileSubsystem* Projectiles = World ? World->GetSubsystem<UProjectileSubsystem>() : nullptr;
	if (!Projectiles)
	{
		RoundsLeft = 0;
		return;
	}
	const FMortarNestConfig& Config = MortarRules::Get();
	AOperativeCharacter* Spotter = nullptr;
	CheckFireAt(FireTarget, &Spotter);
	FMortarShot Shot;
	Shot.Target = FireTarget;
	Shot.RoundOnTarget = RoundOnTarget;
	Shot.ErrorCm = MortarRules::BracketErrorCm(RoundOnTarget, Config.Bracketing);
	Shot.Aim = MortarRules::BracketPoint(FireTarget, Shot.ErrorCm, Stream.FRand());
	Shot.Spotter = Spotter;
	const FWeaponStats& Round = Config.Round;
	FProjectileLaunch Launch;
	Launch.From = GetMuzzle();
	Launch.Projectile = Round.Projectile;
	Launch.Explosion = Round.Explosion;
	Launch.DirectDamage = Round.Explosion.DirectHitDamage;
	Launch.BlastDamage = Round.Damage.BaseDamage;
	Launch.DamageType = Round.Damage.DamageType;
	Launch.ArmorPenetration = Round.Damage.ArmorPenetration;
	Launch.PenetrationClass = Round.Damage.PenetrationClass;
	Launch.Instigator = this;
	Launch.SourceName = FString::Printf(TEXT("Mortar %s"), *NestId);
	Launch.bSquadNoise = TeamId == 0;
	if (!Projectiles->FireAt(Launch, Shot.Aim))
	{
		LastRefusal = EMortarRefusal::TooFar;
		UE_LOG(LogCodexTactics, Display, TEXT("[Mortar] %s: no high-angle solution to %s - fire stops"), *NestId, *Shot.Aim.ToCompactString());
		RoundsLeft = 0;
		return;
	}
	--Ammo;
	--RoundsLeft;
	++RoundsFired;
	++RoundOnTarget;
	Cooldown = Config.IntervalSeconds;
	if (TeamId == 0)
	{
		AEnemyCharacter::NotifySquadNoise(World, GetActorLocation(), ESquadNoise::Gunshot); // the launch is heard
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[Mortar] %s (team %d): round %d at %s (%.1f m off the aim point, spotter %s), %d left in the order, ammo %d"),
		*NestId, TeamId, Shot.RoundOnTarget + 1, *Shot.Aim.ToCompactString(), Shot.ErrorCm / 100.f, Spotter ? *Spotter->DisplayName.ToString() : TEXT("-"),
		RoundsLeft, Ammo);
	OnRoundFired.Broadcast(this, Shot);
}
