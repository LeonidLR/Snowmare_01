#include "Vehicles/VehiclePawn.h"
#include "AI/Navigation/NavigationTypes.h"
#include "AI/WorldAIPauseSubsystem.h"
#include "AI/PatrolRouteRules.h"
#include "Animation/AnimInstance.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Interactables/HeatSourceComponent.h"
#include "NavigationData.h"
#include "NavigationSystem.h"
#include "NavFilters/NavigationQueryFilter.h"
#include "Vehicles/VehicleNavigation.h"
#include "Vehicles/VehicleSubsystem.h"

AVehiclePawn::AVehiclePawn()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	AutoPossessAI = EAutoPossessAI::Disabled;
	AIControllerClass = nullptr;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	SetRootComponent(Root);

	Hull = CreateDefaultSubobject<UBoxComponent>(TEXT("Hull"));
	Hull->SetupAttachment(Root);
	Hull->SetRelativeLocation(HullCentre);
	Hull->SetBoxExtent(HullHalfExtent);
	Hull->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Hull->SetCollisionObjectType(ECC_WorldDynamic);
	Hull->SetCollisionResponseToAllChannels(ECR_Block);
	Hull->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	Hull->SetGenerateOverlapEvents(true);
	Hull->SetCanEverAffectNavigation(false); // the infantry footprint comes from NavObstacle (vehicle navmesh stays whole)
	Hull->ShapeColor = FColor(120, 200, 255);

	Mesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Root);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCanEverAffectNavigation(false);
	Mesh->SetGenerateOverlapEvents(false);

	CabinHeat = CreateDefaultSubobject<UHeatSourceComponent>(TEXT("CabinHeat"));
	CabinHeat->SetupAttachment(Root);
	CabinHeat->SetRelativeLocation(HullCentre);
	CabinHeat->Radius = EngineConfig.CabinHeatRadiusCm;
	CabinHeat->bHeatActive = false;

	NavObstacle = CreateDefaultSubobject<UVehicleNavObstacleComponent>(TEXT("NavObstacle"));

	DisplayName = NSLOCTEXT("CodexVehicle", "ApcName", "APC");
	DefaultMesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(TEXT("/Game/VigilanteContent/Vehicles/East_APC_BTR82/SK_East_APC_BTR82.SK_East_APC_BTR82")));
	DefaultAnimClass = TSoftClassPtr<UAnimInstance>(FSoftObjectPath(TEXT("/Game/VigilanteContent/Vehicles/East_APC_BTR82/ABP_East_APC_BTR82.ABP_East_APC_BTR82_C")));
	// Side doors between axles 2 and 3 (x -20 cm), the rear hatch; on the ground, 70 cm off the hull.
	EntryPoints = { FVector(-20.f, -(153.f + 70.f), 0.f), FVector(-20.f, 153.f + 70.f, 0.f), FVector(58.f - 389.f - 70.f, 0.f, 0.f) };
	// Wheels: x +270 / -190 (front / back axle), y +-130 (track).
	GroundProbes = { FVector2D(270.f, 0.f), FVector2D(-190.f, 0.f), FVector2D(40.f, -130.f), FVector2D(40.f, 130.f) };
}

void AVehiclePawn::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	Hull->SetRelativeLocation(HullCentre);
	Hull->SetBoxExtent(HullHalfExtent);
	CabinHeat->SetRelativeLocation(HullCentre);
	CabinHeat->Radius = EngineConfig.CabinHeatRadiusCm;
	ApplyNavObstacleBox();
	// A C++-only spawn (no Blueprint set the model): the vendor mesh + ABP by soft path.
	if (Mesh && !Mesh->GetSkeletalMeshAsset())
	{
		if (USkeletalMesh* Model = DefaultMesh.LoadSynchronous())
		{
			Mesh->SetSkeletalMeshAsset(Model);
			if (UClass* AnimClass = DefaultAnimClass.LoadSynchronous())
			{
				Mesh->SetAnimInstanceClass(AnimClass);
			}
		}
	}
}

void AVehiclePawn::ApplyNavObstacleBox()
{
	// From 1 m below the ground to the hull top: the modifier must contain the navmesh surface under the hull.
	const float Top = HullCentre.Z + HullHalfExtent.Z;
	NavObstacle->SetLocalBox(FVector(HullCentre.X, HullCentre.Y, (Top - 100.f) * 0.5f), FVector(HullHalfExtent.X, HullHalfExtent.Y, (Top + 100.f) * 0.5f));
}

void AVehiclePawn::BeginPlay()
{
	Super::BeginPlay();
	CurrentYaw = GetActorRotation().Yaw;
	ApplyNavObstacleBox();
	ConformToGround(0.f, true);
	bParkedState = false;
	SetParked(true);
	SetEngineRunning(false);
	if (UVehicleSubsystem* Vehicles = GetWorld()->GetSubsystem<UVehicleSubsystem>())
	{
		Vehicles->RegisterVehicle(this);
	}
}

void AVehiclePawn::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		if (UVehicleSubsystem* Vehicles = World->GetSubsystem<UVehicleSubsystem>())
		{
			Vehicles->UnregisterVehicle(this);
		}
	}
	Super::EndPlay(EndPlayReason);
}

// --- Driving -----------------------------------------------------------------------------------------------------

ANavigationData* AVehiclePawn::GetVehicleNavData() const
{
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	return Nav ? Nav->GetNavDataForAgentName(NavAgentName) : nullptr;
}

EVehicleDriveResult AVehiclePawn::DriveTo(const FVector& InGoal, bool bInRush)
{
	if (!HasDriver())
	{
		return EVehicleDriveResult::NoDriver;
	}
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	ANavigationData* NavData = GetVehicleNavData();
	if (!Nav || !NavData)
	{
		return EVehicleDriveResult::NoPath;
	}
	FNavLocation Start;
	if (!NavData->ProjectPoint(GetActorLocation(), Start, FVector(500.f, 500.f, 400.f)))
	{
		return EVehicleDriveResult::NoPath;
	}
	FNavLocation End;
	const bool bGoalOnNav = NavData->ProjectPoint(InGoal, End, FVector(800.f, 800.f, 500.f));
	const FVector EndPoint = bGoalOnNav ? End.Location : InGoal;
	FPathFindingQuery Query(this, *NavData, Start.Location, EndPoint, NavData->GetDefaultQueryFilter());
	Query.SetAllowPartialPaths(true);
	Query.SetRequireNavigableEndLocation(false);
	const FPathFindingResult Result = Nav->FindPathSync(Query);
	if (!Result.IsSuccessful() || !Result.Path.IsValid() || Result.Path->GetPathPoints().Num() == 0)
	{
		return EVehicleDriveResult::NoPath;
	}
	PathPoints.Reset();
	for (const FNavPathPoint& Point : Result.Path->GetPathPoints())
	{
		PathPoints.Add(Point.Location);
	}
	if (PathPoints.Num() == 1)
	{
		PathPoints.Insert(Start.Location, 0);
	}
	Goal = PathPoints.Last();
	bPathPartial = Result.IsPartial() || !bGoalOnNav || FVector::Dist2D(Goal, InGoal) > 300.f;

	// Automatic reverse: a short goal behind, reached in a (nearly) straight line.
	const FVector2D Forward(FMath::Cos(FMath::DegreesToRadians(CurrentYaw)), FMath::Sin(FMath::DegreesToRadians(CurrentYaw)));
	float PathLength = 0.f;
	for (int32 Index = 0; Index + 1 < PathPoints.Num(); ++Index)
	{
		PathLength += FVector::Dist2D(PathPoints[Index], PathPoints[Index + 1]);
	}
	const float Straight = FVector::Dist2D(GetActorLocation(), Goal);
	bReverse = VehicleRules::ShouldReverse(Forward, FVector2D(Goal - GetActorLocation()), DriveConfig) && PathLength <= Straight * 1.25f + 100.f;
	if (bReverse)
	{
		// Straight back: the navmesh path is already straight; drive the polyline from the vehicle itself.
		PathPoints = { GetActorLocation(), Goal };
	}
	bRush = bInRush && !bReverse;
	// Facing well away from the path (> 70 deg): a K-turn first - back up while turning (up to 6 m), then forward. The
	// vehicle cannot pivot, and a forward arc of 7 m radius would swing it wide into whatever is beside it.
	bManeuverReverse = false;
	ManeuverLeftCm = 0.f;
	if (!bReverse && PathPoints.Num() >= 2)
	{
		int32 Segment = 0;
		float Remaining = 0.f;
		const FVector Look = VehicleRules::GetLookaheadPoint(PathPoints, GetActorLocation(), Segment, DriveConfig.LookaheadCm, Remaining);
		const float LookYaw = FMath::RadiansToDegrees(FMath::Atan2(Look.Y - GetActorLocation().Y, Look.X - GetActorLocation().X));
		if (FMath::Abs(FMath::FindDeltaAngleDegrees(CurrentYaw, LookYaw)) > 70.f && Remaining > DriveConfig.MinTurnRadiusCm)
		{
			bManeuverReverse = true;
			ManeuverLeftCm = 600.f;
		}
	}
	bHasGoal = true;
	PathSegment = 0;
	LastRemaining = TNumericLimits<float>::Max();
	NoProgressSeconds = 0.f;
	BlockedSeconds = 0.f;
	SetParked(false);
	UE_LOG(LogCodexTactics, Display, TEXT("[Vehicle] %s drives to %s (%d path points%s%s%s)"), *GetName(), *Goal.ToCompactString(), PathPoints.Num(),
		bPathPartial ? TEXT(", partial") : TEXT(""), bReverse ? TEXT(", reverse") : TEXT(""), bRush ? TEXT(", rush") : TEXT(""));
	return bPathPartial ? EVehicleDriveResult::StartedPartial : EVehicleDriveResult::Started;
}

void AVehiclePawn::StopDriving()
{
	if (bHasGoal)
	{
		bHasGoal = false;
		UE_LOG(LogCodexTactics, Display, TEXT("[Vehicle] %s stops"), *GetName());
	}
}

void AVehiclePawn::RestoreTransform(const FVector& Location, float Yaw)
{
	bHasGoal = false;
	Speed = 0.f;
	PathPoints.Reset();
	CurrentYaw = FRotator::NormalizeAxis(Yaw);
	SetActorLocationAndRotation(Location, FRotator(0.f, CurrentYaw, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
	ConformToGround(0.f, true);
	// Re-cut the infantry footprint at the new place.
	bParkedState = false;
	SetParked(true);
}

bool AVehiclePawn::IsStopped() const
{
	return !bHasGoal && FMath::Abs(Speed) <= CrewConfig.StoppedSpeedCmS;
}

void AVehiclePawn::FinishDrive(bool bArrived)
{
	bHasGoal = false;
	UE_LOG(LogCodexTactics, Display, TEXT("[Vehicle] %s %s at %s"), *GetName(), bArrived ? TEXT("arrived") : TEXT("gave up (blocked)"),
		*GetActorLocation().ToCompactString());
	if (bArrived)
	{
		OnArrived.Broadcast(this);
	}
	else
	{
		OnDriveBlocked.Broadcast(this);
	}
}

void AVehiclePawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// A dialogue / cutscene blocker holds the world AI: the vehicle waits too.
	if (UWorldAIPauseSubsystem::IsPausedIn(GetWorld()) || DeltaSeconds <= 0.f)
	{
		return;
	}
	const float YawBefore = CurrentYaw;
	if (bHasGoal)
	{
		UpdateDrive(DeltaSeconds);
	}
	else if (!FMath::IsNearlyZero(Speed))
	{
		// Braking to a stop on the current heading.
		Speed = VehicleRules::StepSpeed(Speed, 0.f, DriveConfig, DeltaSeconds);
		const FVector Heading(FMath::Cos(FMath::DegreesToRadians(CurrentYaw)), FMath::Sin(FMath::DegreesToRadians(CurrentYaw)), 0.f);
		SetActorLocation(GetActorLocation() + Heading * Speed * DeltaSeconds);
	}
	if (!bHasGoal && FMath::IsNearlyZero(Speed) && !bParkedState)
	{
		SetParked(true);
	}
	if (!bParkedState || bHasGoal)
	{
		ConformToGround(DeltaSeconds, false);
	}
	const float YawRate = FMath::FindDeltaAngleDegrees(YawBefore, CurrentYaw) / DeltaSeconds;
	ApplyVisuals(YawRate, DeltaSeconds);
	UpdateEngineNoise(DeltaSeconds);
}

void AVehiclePawn::UpdateDrive(float DeltaSeconds)
{
	const FVector Location = GetActorLocation();
	float Remaining = 0.f;
	const FVector Look = VehicleRules::GetLookaheadPoint(PathPoints, Location, PathSegment, DriveConfig.LookaheadCm, Remaining);
	const FVector ToLook = Look - Location;
	float DesiredYaw = ToLook.SizeSquared2D() > 1.f ? FMath::RadiansToDegrees(FMath::Atan2(ToLook.Y, ToLook.X)) : CurrentYaw;
	if (bReverse)
	{
		DesiredYaw = FRotator::NormalizeAxis(DesiredYaw + 180.f);
	}
	const float HeadingError = FMath::FindDeltaAngleDegrees(CurrentYaw, DesiredYaw);
	// Arrived; or close but facing away (inside the turning circle: circling would never reach it) — close enough.
	if (Remaining <= DriveConfig.ArriveToleranceCm
		|| (!bManeuverReverse && Remaining < DriveConfig.MinTurnRadiusCm * 0.8f && FMath::Abs(HeadingError) > 80.f && PathSegment >= PathPoints.Num() - 2))
	{
		FinishDrive(true);
		return;
	}
	float Target = 0.f;
	if (IsPawnAhead())
	{
		BlockedSeconds += DeltaSeconds;
		if (BlockedSeconds > 6.f)
		{
			FinishDrive(false);
			return;
		}
	}
	else
	{
		BlockedSeconds = 0.f;
		Target = VehicleRules::GetDesiredSpeed(bRush, bReverse, Remaining, HeadingError, DriveConfig) * (bReverse ? -1.f : 1.f);
	}
	if (bManeuverReverse)
	{
		// K-turn: backing up turns the nose towards the path (same yaw rate limit); forward again once it points there.
		ManeuverLeftCm -= FMath::Abs(Speed) * DeltaSeconds;
		if (FMath::Abs(HeadingError) < 35.f || ManeuverLeftCm <= 0.f)
		{
			bManeuverReverse = false;
		}
		else if (!FMath::IsNearlyZero(Target))
		{
			Target = -DriveConfig.ReverseSpeedCmS;
		}
	}
	Speed = VehicleRules::StepSpeed(Speed, Target, DriveConfig, DeltaSeconds);
	CurrentYaw = VehicleRules::StepYaw(CurrentYaw, DesiredYaw, Speed, DriveConfig.MinTurnRadiusCm, DeltaSeconds);
	const FVector Heading(FMath::Cos(FMath::DegreesToRadians(CurrentYaw)), FMath::Sin(FMath::DegreesToRadians(CurrentYaw)), 0.f);
	SetActorLocation(Location + Heading * Speed * DeltaSeconds);
	// No progress along the path for a while (stuck on a corner): give up.
	if (Remaining < LastRemaining - 50.f)
	{
		LastRemaining = Remaining;
		NoProgressSeconds = 0.f;
	}
	else if (BlockedSeconds <= 0.f)
	{
		NoProgressSeconds += DeltaSeconds;
		if (NoProgressSeconds > 8.f)
		{
			FinishDrive(false);
		}
	}
}

bool AVehiclePawn::IsPawnAhead() const
{
	const float Direction = Speed < -1.f || (bHasGoal && (bReverse || bManeuverReverse)) ? -1.f : 1.f;
	const FTransform Frame(FRotator(0.f, CurrentYaw, 0.f), GetActorLocation());
	const FVector Centre = Frame.TransformPosition(FVector(HullCentre.X + Direction * (HullHalfExtent.X + 110.f), HullCentre.Y, HullCentre.Z));
	FCollisionQueryParams Params(SCENE_QUERY_STAT(VehicleAhead), false, this);
	for (const FVehicleCrewMember& Member : Crew)
	{
		Params.AddIgnoredActor(Member.Operative.Get());
	}
	TArray<FOverlapResult> Overlaps;
	GetWorld()->OverlapMultiByObjectType(Overlaps, Centre, Frame.GetRotation(), FCollisionObjectQueryParams(ECC_Pawn),
		FCollisionShape::MakeBox(FVector(110.f, HullHalfExtent.Y, 90.f)), Params);
	for (const FOverlapResult& Overlap : Overlaps)
	{
		const AActor* Other = Overlap.GetActor();
		if (!Other || Other == this || Other->IsHidden())
		{
			continue;
		}
		const UHealthComponent* Health = Other->FindComponentByClass<UHealthComponent>();
		if ((Other->IsA<AOperativeCharacter>() || Other->IsA<AEnemyCharacter>()) && (!Health || Health->IsAlive()))
		{
			return true; // never runs anyone over: brakes and waits (user decision 2026-10-12)
		}
	}
	return false;
}

void AVehiclePawn::ConformToGround(float DeltaSeconds, bool bSnap)
{
	UWorld* World = GetWorld();
	if (!World || GroundProbes.Num() < 4)
	{
		return;
	}
	const FVector Location = GetActorLocation();
	const FTransform Frame(FRotator(0.f, CurrentYaw, 0.f), Location);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(VehicleGround), false, this);
	for (const FVehicleCrewMember& Member : Crew)
	{
		Params.AddIgnoredActor(Member.Operative.Get());
	}
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	float Heights[4];
	int32 Hits = 0;
	for (int32 Index = 0; Index < 4; ++Index)
	{
		const FVector Probe = Frame.TransformPosition(FVector(GroundProbes[Index].X, GroundProbes[Index].Y, 0.f));
		FHitResult Hit;
		if (World->LineTraceSingleByObjectType(Hit, Probe + FVector(0.f, 0.f, 250.f), Probe - FVector(0.f, 0.f, 600.f), Objects, Params))
		{
			Heights[Index] = Hit.ImpactPoint.Z;
			++Hits;
		}
		else
		{
			Heights[Index] = Location.Z;
		}
	}
	if (Hits == 0)
	{
		SetActorRotation(FRotator(SmoothedPitch, CurrentYaw, SmoothedRoll));
		return;
	}
	const float Length = FMath::Max(1.f, GroundProbes[0].X - GroundProbes[1].X);
	const float Width = FMath::Max(1.f, GroundProbes[3].Y - GroundProbes[2].Y);
	// UE rotators: positive pitch lifts the nose, positive roll lowers the right (+Y) side.
	const float TargetPitch = FMath::RadiansToDegrees(FMath::Atan2(Heights[0] - Heights[1], Length));
	const float TargetRoll = FMath::RadiansToDegrees(FMath::Atan2(Heights[2] - Heights[3], Width));
	const float TargetZ = (Heights[0] + Heights[1] + Heights[2] + Heights[3]) * 0.25f;
	SmoothedPitch = bSnap ? TargetPitch : FMath::FInterpTo(SmoothedPitch, TargetPitch, DeltaSeconds, 6.f);
	SmoothedRoll = bSnap ? TargetRoll : FMath::FInterpTo(SmoothedRoll, TargetRoll, DeltaSeconds, 6.f);
	const float NewZ = bSnap ? TargetZ : FMath::FInterpTo(Location.Z, TargetZ, DeltaSeconds, 10.f);
	SetActorLocationAndRotation(FVector(Location.X, Location.Y, NewZ), FRotator(SmoothedPitch, CurrentYaw, SmoothedRoll));
}

void AVehiclePawn::SetAnimFloat(FName Variable, float Value) const
{
	UAnimInstance* Anim = Mesh ? Mesh->GetAnimInstance() : nullptr;
	if (!Anim || Variable.IsNone())
	{
		return;
	}
	if (FNumericProperty* Property = CastField<FNumericProperty>(Anim->GetClass()->FindPropertyByName(Variable)); Property && Property->IsFloatingPoint())
	{
		Property->SetFloatingPointPropertyValue(Property->ContainerPtrToValuePtr<void>(Anim), Value);
	}
}

void AVehiclePawn::ApplyVisuals(float YawRateDeg, float DeltaSeconds)
{
	const float Steer = VehicleRules::GetSteerAngleDeg(YawRateDeg, Speed, DriveConfig);
	LastSteer = FMath::FInterpTo(LastSteer, Steer, DeltaSeconds, 8.f);
	const bool bMoving = !FMath::IsNearlyZero(Speed);
	if (bMoving || !FMath::IsNearlyZero(LastSteer, 0.05f))
	{
		SetAnimFloat(AnimWheelSpeedVariable, VehicleRules::GetWheelSpinDegPerSec(Speed, DriveConfig.WheelRadiusCm));
		SetAnimFloat(AnimSteerVariable, LastSteer);
		ReceiveVisualDrive(Speed, LastSteer);
	}
	else
	{
		SetAnimFloat(AnimWheelSpeedVariable, 0.f);
	}
}

void AVehiclePawn::UpdateEngineNoise(float DeltaSeconds)
{
	if (!bEngineRunning)
	{
		NoiseTimer = 0.f;
		return;
	}
	NoiseTimer -= DeltaSeconds;
	if (NoiseTimer > 0.f)
	{
		return;
	}
	NoiseTimer = EngineConfig.NoiseIntervalSeconds;
	const float Radius = VehicleRules::GetNoiseRadiusCm(VehicleRules::GetEngineState(true, Speed, bRush), EngineConfig);
	if (Radius <= 0.f)
	{
		return;
	}
	// Patrols hear the engine (exploration effect; the fight already knows where the squad is).
	TArray<AEnemyCharacter*> Listeners;
	for (TActorIterator<AEnemyCharacter> It(GetWorld()); It; ++It)
	{
		if (It->IsOnPatrol() && !It->IsDying() && FVector::Dist(It->GetActorLocation(), GetActorLocation()) <= Radius)
		{
			Listeners.Add(*It);
		}
	}
	for (AEnemyCharacter* Enemy : Listeners)
	{
		if (IsValid(Enemy) && Enemy->IsOnPatrol())
		{
			UE_LOG(LogCodexTactics, Display, TEXT("[Vehicle] %s hears the engine of %s (%.0f m)"), *Enemy->GetName(), *GetName(), Radius / 100.f);
			Enemy->BreakPatrol(EPatrolAlertCause::Hearing, GetActorLocation());
		}
	}
}

void AVehiclePawn::SetParked(bool bParked)
{
	if (bParkedState == bParked)
	{
		return;
	}
	bParkedState = bParked;
	if (bParked)
	{
		Speed = 0.f;
		ApplyVisuals(0.f, 1.f);
	}
	if (NavObstacle)
	{
		NavObstacle->SetObstacleActive(bParked);
	}
}

// --- Crew --------------------------------------------------------------------------------------------------------

int32 AVehiclePawn::GetFreeSeats() const
{
	return VehicleRules::GetCapacity(CrewConfig) - Crew.Num();
}

EVehicleSeat AVehiclePawn::GetSeatOf(const AOperativeCharacter* Operative) const
{
	const FVehicleCrewMember* Member = Crew.FindByPredicate([Operative](const FVehicleCrewMember& Each) { return Each.Operative.Get() == Operative; });
	return Member ? Member->Seat : EVehicleSeat::None;
}

AOperativeCharacter* AVehiclePawn::GetOccupant(EVehicleSeat Seat) const
{
	const FVehicleCrewMember* Member = Crew.FindByPredicate([Seat](const FVehicleCrewMember& Each) { return Each.Seat == Seat && Each.Operative.IsValid(); });
	return Member ? Member->Operative.Get() : nullptr;
}

void AVehiclePawn::ReseatCrew()
{
	Crew.RemoveAll([](const FVehicleCrewMember& Each) { return !Each.Operative.IsValid(); });
	TArray<EOperativeRole> Roles;
	for (const FVehicleCrewMember& Member : Crew)
	{
		Roles.Add(Member.Operative->SquadRole);
	}
	const TArray<EVehicleSeat> Seats = VehicleRules::AssignSeats(Roles, CrewConfig);
	for (int32 Index = 0; Index < Crew.Num(); ++Index)
	{
		Crew[Index].Seat = Seats[Index];
	}
}

void AVehiclePawn::AddCrew(AOperativeCharacter* Operative)
{
	if (!Operative || GetSeatOf(Operative) != EVehicleSeat::None)
	{
		return;
	}
	FVehicleCrewMember& Member = Crew.AddDefaulted_GetRef();
	Member.Operative = Operative;
	ReseatCrew();
}

void AVehiclePawn::RemoveCrew(AOperativeCharacter* Operative)
{
	Crew.RemoveAll([Operative](const FVehicleCrewMember& Each) { return Each.Operative.Get() == Operative; });
	ReseatCrew();
	if (!HasDriver())
	{
		StopDriving();
	}
}

void AVehiclePawn::SetCrewSeat(AOperativeCharacter* Operative, EVehicleSeat Seat)
{
	FVehicleCrewMember* Member = Crew.FindByPredicate([Operative](const FVehicleCrewMember& Each) { return Each.Operative.Get() == Operative; });
	if (!Member || Seat == EVehicleSeat::None || Member->Seat == Seat)
	{
		return;
	}
	if (Seat != EVehicleSeat::Passenger)
	{
		// The driver / gunner seat holds one: swap with its holder.
		if (FVehicleCrewMember* Holder = Crew.FindByPredicate([Seat](const FVehicleCrewMember& Each) { return Each.Seat == Seat; }))
		{
			Holder->Seat = Member->Seat;
		}
	}
	Member->Seat = Seat;
}

void AVehiclePawn::SetEngineRunning(bool bRunning)
{
	const bool bChanged = bEngineRunning != bRunning;
	bEngineRunning = bRunning;
	if (CabinHeat)
	{
		CabinHeat->Radius = EngineConfig.CabinHeatRadiusCm;
		CabinHeat->SetHeatActive(bRunning);
	}
	if (bChanged)
	{
		NoiseTimer = 0.f;
		ReceiveEngineChanged(bRunning);
	}
}

void AVehiclePawn::SetDoorsOpen(bool bOpen)
{
	if (bDoorsOpen == bOpen)
	{
		return;
	}
	bDoorsOpen = bOpen;
	SetAnimFloat(AnimDoorVariable, bOpen ? DoorOpenAngle : 0.f);
	ReceiveDoorsChanged(bOpen);
}

// --- Geometry ----------------------------------------------------------------------------------------------------

FVector AVehiclePawn::GetEntryPointWorld(int32 Index) const
{
	const FTransform Frame(FRotator(0.f, CurrentYaw, 0.f), GetActorLocation());
	return EntryPoints.IsValidIndex(Index) ? Frame.TransformPosition(EntryPoints[Index]) : GetActorLocation();
}

TArray<FVector> AVehiclePawn::GetExitCandidatesWorld() const
{
	const FTransform Frame(FRotator(0.f, CurrentYaw, 0.f), GetActorLocation());
	const float Side = HullHalfExtent.Y + CrewConfig.ExitOffsetCm;
	const float Rear = HullCentre.X - HullHalfExtent.X - CrewConfig.ExitOffsetCm;
	TArray<FVector> Points;
	for (const float Sign : { -1.f, 1.f })
	{
		for (const float X : { -20.f, 150.f, -200.f })
		{
			Points.Add(Frame.TransformPosition(FVector(HullCentre.X + X, Sign * Side, 0.f)));
		}
	}
	Points.Add(Frame.TransformPosition(FVector(Rear, -70.f, 0.f)));
	Points.Add(Frame.TransformPosition(FVector(Rear, 70.f, 0.f)));
	return Points;
}

FVector AVehiclePawn::GetHullCentreWorld() const
{
	return Hull ? Hull->GetComponentLocation() : GetActorLocation();
}

bool AVehiclePawn::IsInsideFootprint(const FVector& World, float MarginCm) const
{
	const FTransform Frame(FRotator(0.f, CurrentYaw, 0.f), GetActorLocation());
	const FVector Local = Frame.InverseTransformPosition(World);
	return FMath::Abs(Local.X - HullCentre.X) <= HullHalfExtent.X + MarginCm && FMath::Abs(Local.Y - HullCentre.Y) <= HullHalfExtent.Y + MarginCm;
}

bool AVehiclePawn::GetCoverProbe(const FVector& ClickPoint, const FVector& ViewDirection, FVector& OutPoint, FVector& OutNormal) const
{
	const FTransform Frame(FRotator(0.f, CurrentYaw, 0.f), GetActorLocation());
	const FVector LocalClick = Frame.InverseTransformPosition(ClickPoint) - HullCentre;
	const FVector LocalDir = Frame.InverseTransformVectorNoScale(FVector(ViewDirection.X, ViewDirection.Y, 0.f));
	FVector2D Point;
	FVector2D Normal;
	if (!VehicleRules::GetHullSideFacing(FVector2D(LocalClick), FVector2D(LocalDir), FVector2D(HullHalfExtent), Point, Normal))
	{
		return false;
	}
	OutPoint = Frame.TransformPosition(FVector(HullCentre.X + Point.X + Normal.X * 5.f, HullCentre.Y + Point.Y + Normal.Y * 5.f, 120.f));
	OutNormal = Frame.TransformVectorNoScale(FVector(Normal.X, Normal.Y, 0.f)).GetSafeNormal();
	return true;
}
