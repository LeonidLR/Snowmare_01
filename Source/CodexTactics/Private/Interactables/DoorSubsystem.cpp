#include "Interactables/DoorSubsystem.h"

#include "AIController.h"
#include "Animation/AnimMontage.h"
#include "Characters/OperativeCharacter.h"
#include "Combat/Explosion.h"
#include "Combat/HealthComponent.h"
#include "CodexTactics.h"
#include "Components/CapsuleComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Data/WeaponDataAsset.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "GameFramework/Character.h"
#include "Interactables/DoorActor.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationData.h"
#include "UI/GameMessageSubsystem.h"

#define LOCTEXT_NAMESPACE "CodexDoors"

namespace DoorSubsystemLocal
{
	/** Kit leaf instances: within this of the doorway along the wall (past the jambs), across it and above its floor. */
	constexpr float KitAlongSlackCm = 15.f;
	constexpr float KitAcrossCm = 25.f;
	constexpr float KitBelowCm = 15.f;
	constexpr float KitAboveCm = 60.f;
	/** A door order is dropped when the operative's move goal is this far from its approach point (another order). */
	constexpr float OrderGoalSlackCm = 120.f;

	bool IsKitLeafPart(const UActorComponent& Component)
	{
		for (const FName& Tag : Component.ComponentTags)
		{
			const FString Text = Tag.ToString();
			if (Text == TEXT("kit_part=door_leaf_closed") || Text == TEXT("kit_part=door_leaf_open") || Text == TEXT("kit_part=flat_door")
				|| Text == TEXT("kit_part=entrance_door"))
			{
				return true;
			}
		}
		return false;
	}

	struct FPawnSample
	{
		ACharacter* Pawn = nullptr;
		FVector Feet = FVector::ZeroVector;
		FVector Velocity = FVector::ZeroVector;
		TArray<FVector> Path;
	};

	void SamplePawns(UWorld& World, TArray<FPawnSample>& Out)
	{
		for (TActorIterator<ACharacter> It(&World); It; ++It)
		{
			ACharacter* Pawn = *It;
			const UCapsuleComponent* Capsule = Pawn->GetCapsuleComponent();
			if (!Capsule || !Capsule->IsCollisionEnabled())
			{
				continue; // dead / ragdolled / dormant (an enemy the squad does not see is hidden, but still opens doors)
			}
			FPawnSample& Sample = Out.AddDefaulted_GetRef();
			Sample.Pawn = Pawn;
			Sample.Feet = Pawn->GetActorLocation() - FVector(0.f, 0.f, Capsule->GetScaledCapsuleHalfHeight());
			Sample.Velocity = Pawn->GetVelocity();
			const AAIController* AI = Cast<AAIController>(Pawn->GetController());
			const UPathFollowingComponent* Follow = AI ? AI->GetPathFollowingComponent() : nullptr;
			if (Follow && Follow->GetStatus() == EPathFollowingStatus::Moving && Follow->GetPath().IsValid())
			{
				const TArray<FNavPathPoint>& Points = Follow->GetPath()->GetPathPoints();
				Sample.Path.Add(Pawn->GetActorLocation());
				for (int32 Index = FMath::Max(1, static_cast<int32>(Follow->GetNextPathIndex())); Index < Points.Num(); ++Index)
				{
					Sample.Path.Add(Points[Index].Location);
				}
			}
		}
	}
}

bool UDoorSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UDoorSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDoorSubsystem, STATGROUP_Tickables);
}

void UDoorSubsystem::Deinitialize()
{
	Explosions::OnDetonated().Remove(ExplosionHandle);
	ExplosionHandle.Reset();
	Doors.Reset();
	Proxies.Reset();
	Orders.Reset();
	Super::Deinitialize();
}

void UDoorSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UWorld* World = GetWorld();
	if (!World || !World->HasBegunPlay())
	{
		return;
	}
	const bool bOn = DoorTuning::IsOpenableOn();
	if (bOn && !bAdopted)
	{
		AdoptLeaves();
	}
	else if (!bOn && bAdopted)
	{
		ReleaseDoors();
	}
	if (!bAdopted)
	{
		return;
	}
	TickAutoOpen(DeltaTime);
	TickOrders(DeltaTime);
}

int32 UDoorSubsystem::AdoptLeaves()
{
	UWorld* World = GetWorld();
	if (!World || bAdopted)
	{
		return 0;
	}
	bAdopted = true;
	AutoOpenCount = 0;
	const FDoorTuning& Tuning = DoorTuning::Get();
	TArray<AStaticMeshActor*> Leaves;
	for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
	{
		if (It->Tags.Contains(FName(TEXT("door_leaf"))))
		{
			Leaves.Add(*It);
		}
	}
	int32 Made = 0;
	int32 KitInstancesLifted = 0;
	int32 KitDoors = 0;
	int32 HiddenProxyDoors = 0;
	for (AStaticMeshActor* Leaf : Leaves)
	{
		FString StateText;
		EDoorLeafState State;
		if (!DoorRules::FindTagValue(Leaf->Tags, TEXT("door_state"), StateText) || !DoorRules::ParseState(StateText, State)
			|| !DoorRules::IsAdoptable(State))
		{
			continue; // barricaded / sealed: the builder's blocking pieces stay
		}
		UStaticMeshComponent* Mesh = Leaf->GetStaticMeshComponent();
		if (!Mesh || !Mesh->GetStaticMesh())
		{
			continue;
		}
		const FBoxSphereBounds MeshBounds = Mesh->GetStaticMesh()->GetBounds();
		const FTransform& Xf = Mesh->GetComponentTransform();
		const FVector Extent = MeshBounds.BoxExtent * Xf.GetScale3D().GetAbs();
		const FVector Center = Xf.TransformPosition(MeshBounds.Origin);
		FDoorSetup Setup;
		Setup.WidthCm = static_cast<float>(Extent.X * 2.0);
		Setup.ThicknessCm = static_cast<float>(Extent.Y * 2.0);
		Setup.HeightCm = static_cast<float>(Extent.Z * 2.0);
		Setup.Frame = FTransform(FRotator(0.f, Xf.Rotator().Yaw, 0.f), Center - FVector(0.f, 0.f, Extent.Z));
		Setup.State = State;
		DoorRules::FindTagValue(Leaf->Tags, TEXT("building"), Setup.BuildingId);
		DoorRules::FindTagValue(Leaf->Tags, TEXT("opening"), Setup.OpeningId);
		FString FloorText;
		if (DoorRules::FindTagValue(Leaf->Tags, TEXT("floor"), FloorText))
		{
			LexFromString(Setup.Floor, *FloorText);
		}
		Setup.LeafMesh = Mesh->GetStaticMesh();
		for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); ++Slot)
		{
			Setup.LeafMaterials.Add(Mesh->GetMaterial(Slot));
		}
		Setup.bVisualHidden = Leaf->IsHidden() || Leaf->Tags.Contains(FName(TEXT("KitDressProxy")));
		Setup.bHeavyTag = Leaf->Tags.Contains(FName(TEXT("door_heavy")));
		DoorRules::FindTagValue(Leaf->Tags, TEXT("door_key"), Setup.KeyId);
		if (Setup.WidthCm < 30.f || Setup.HeightCm < 100.f)
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("Doors: leaf %s is %.0f x %.0f cm - not a door, skipped"), *Leaf->GetName(), Setup.WidthCm, Setup.HeightCm);
			continue;
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ADoorActor* Door = World->SpawnActor<ADoorActor>(ADoorActor::StaticClass(), Setup.Frame, Params);
		if (!Door)
		{
			continue;
		}
		Door->Setup(Setup);
		// The leaf's building= / floor= / opening= tags go along: the storey cutaway (URoofCutawaySubsystem) hides the door with its storey.
		for (const FName& Tag : Leaf->Tags)
		{
			if (Tag != FName(TEXT("door_leaf")) && Tag != FName(TEXT("KitDressProxy")))
			{
				Door->Tags.AddUnique(Tag);
			}
		}
		Door->Tags.AddUnique(FName(TEXT("door_actor")));
		Proxies.Add({ Leaf, Leaf->IsHidden(), Leaf->GetActorEnableCollision() });
		Leaf->SetActorHiddenInGame(true);
		Leaf->SetActorEnableCollision(false);
		if (Setup.bVisualHidden)
		{
			const int32 Kit = LiftKitLeaves(*Door);
			KitInstancesLifted += Kit;
			KitDoors += Kit > 0 ? 1 : 0;
			HiddenProxyDoors += 1;
		}
		Doors.Add(Door);
		++Made;
	}
	Held.Init(0.f, Doors.Num());
	Keys.Reset();
	for (const FString& Key : Tuning.StartKeys)
	{
		Keys.Add(Key);
	}
	KickAttempts = KickInjuries = 0;
	ExplosionHandle = Explosions::OnDetonated().AddUObject(this, &UDoorSubsystem::HandleExplosion);
	UE_LOG(LogCodexTactics, Display, TEXT("Doors: R1 on - %d door(s) from %d leaf actor(s) (auto-open %.2f s, kick %s, %d start key(s)); kit-dressed: %d / %d hidden-proxy door(s) got %d kit leaf instance(s)"),
		Made, Leaves.Num(), Tuning.AutoOpenDelaySeconds, Tuning.bKickEnabled ? TEXT("on") : TEXT("off"), Keys.Num(), KitDoors, HiddenProxyDoors, KitInstancesLifted);
	return Made;
}

int32 UDoorSubsystem::LiftKitLeaves(ADoorActor& Door) const
{
	using namespace DoorSubsystemLocal;
	const FName KitTag(*FString::Printf(TEXT("kit_building=%s"), *Door.GetBuildingId()));
	const float HalfWidth = Door.GetWidthCm() * 0.5f;
	const float DoorZ = static_cast<float>(Door.GetActorLocation().Z);
	int32 Lifted = 0;
	double NearestCm = 400.0;
	FString Nearest;
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (!It->Tags.Contains(KitTag))
		{
			continue;
		}
		TInlineComponentArray<UInstancedStaticMeshComponent*> Components(*It);
		for (UInstancedStaticMeshComponent* Component : Components)
		{
			if (!IsKitLeafPart(*Component))
			{
				continue;
			}
			for (int32 Index = 0; Index < Component->GetInstanceCount(); ++Index)
			{
				FTransform Instance;
				if (!Component->GetInstanceTransform(Index, Instance, /*bWorldSpace*/ true) || Instance.GetScale3D().GetMin() < 0.01)
				{
					continue;
				}
				const FVector2D Local = Door.ToLocal(Instance.GetLocation());
				const float Dz = static_cast<float>(Instance.GetLocation().Z) - DoorZ;
				if (FMath::Abs(Local.X) <= HalfWidth + KitAlongSlackCm && FMath::Abs(Local.Y) <= KitAcrossCm && Dz >= -KitBelowCm && Dz <= KitAboveCm)
				{
					Door.AddLiftedInstance(Component, Index, Instance);
					++Lifted;
				}
				else if (const double Cm = FVector::Dist(Instance.GetLocation(), Door.GetActorLocation()); Cm < NearestCm)
				{
					NearestCm = Cm;
					Nearest = FString::Printf(TEXT("%s #%d at local (%.0f, %.0f, dz %.0f)"), *Component->GetName(), Index, Local.X, Local.Y, Dz);
				}
			}
		}
	}
	if (Lifted == 0)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Doors: no kit leaf in the doorway of %s %s (%.0f cm wide); nearest kit leaf piece: %s"), *Door.GetBuildingId(),
			*Door.GetOpeningId(), Door.GetWidthCm(), Nearest.IsEmpty() ? TEXT("none within 4 m") : *Nearest);
	}
	return Lifted;
}

void UDoorSubsystem::ReleaseDoors()
{
	for (ADoorActor* Door : Doors)
	{
		if (Door)
		{
			Door->RestoreLiftedInstances();
			Door->Destroy();
		}
	}
	for (const FProxy& Proxy : Proxies)
	{
		if (AActor* Actor = Proxy.Actor.Get())
		{
			Actor->SetActorHiddenInGame(Proxy.bWasHidden);
			Actor->SetActorEnableCollision(Proxy.bHadCollision);
		}
	}
	Explosions::OnDetonated().Remove(ExplosionHandle);
	ExplosionHandle.Reset();
	UE_LOG(LogCodexTactics, Display, TEXT("Doors: R1 off - %d door(s) released, leaves decorative again"), Doors.Num());
	Doors.Reset();
	Held.Reset();
	Proxies.Reset();
	Orders.Reset();
	bAdopted = false;
}

void UDoorSubsystem::TickAutoOpen(float DeltaTime)
{
	using namespace DoorSubsystemLocal;
	const FDoorTuning& Tuning = DoorTuning::Get();
	TArray<FPawnSample> Pawns;
	bool bSampled = false;
	Held.SetNumZeroed(Doors.Num());
	for (int32 DoorIndex = 0; DoorIndex < Doors.Num(); ++DoorIndex)
	{
		ADoorActor* Door = Doors[DoorIndex];
		if (!Door || Door->GetDoorState() != EDoorLeafState::Closed || Door->IsSwinging())
		{
			Held[DoorIndex] = 0.f;
			continue;
		}
		if (!bSampled)
		{
			SamplePawns(*GetWorld(), Pawns);
			bSampled = true;
		}
		const float HalfWidth = Door->GetWidthCm() * 0.5f;
		ACharacter* Opener = nullptr;
		for (const FPawnSample& Sample : Pawns)
		{
			const FVector2D Local = Door->ToLocal(Sample.Feet);
			if (!DoorRules::IsInZone(Local, HalfWidth, Tuning) || !Door->IsAtDoorLevel(Sample.Feet))
			{
				continue;
			}
			bool bHeading = false;
			if (Sample.Path.Num() >= 2)
			{
				TArray<FVector2D> LocalPath;
				LocalPath.Reserve(Sample.Path.Num());
				for (const FVector& Point : Sample.Path)
				{
					LocalPath.Add(Door->ToLocal(Point));
				}
				bHeading = DoorRules::PathCrossesDoorway(LocalPath, HalfWidth, Tuning.PathLookAheadCm);
			}
			if (!bHeading)
			{
				const FVector Velocity = Sample.Velocity;
				const FVector2D LocalVelocity(FVector::DotProduct(Velocity, Door->GetTangent()), FVector::DotProduct(Velocity, Door->GetNormal()));
				bHeading = DoorRules::IsHeadingInto(Local, LocalVelocity, Tuning) && FMath::Abs(Local.X) <= HalfWidth;
			}
			if (bHeading)
			{
				Opener = Sample.Pawn;
				break;
			}
		}
		bool bOpen = false;
		Held[DoorIndex] = DoorRules::AdvanceAutoOpen(Door->GetDoorState(), Held[DoorIndex], Opener != nullptr, DeltaTime, Tuning, bOpen);
		if (bOpen && Door->OpenDoor(Opener))
		{
			Held[DoorIndex] = 0.f;
			++AutoOpenCount;
		}
	}
}

ADoorActor* UDoorSubsystem::FindDoor(const FString& BuildingId, const FString& OpeningId) const
{
	for (ADoorActor* Door : Doors)
	{
		if (Door && Door->GetBuildingId() == BuildingId && Door->GetOpeningId() == OpeningId)
		{
			return Door;
		}
	}
	return nullptr;
}

ADoorActor* UDoorSubsystem::FindNearestDoor(const FVector& Point, float RadiusCm) const
{
	ADoorActor* Best = nullptr;
	double BestCm = RadiusCm;
	for (ADoorActor* Door : Doors)
	{
		if (!Door || !Door->IsAtDoorLevel(Point))
		{
			continue;
		}
		const double Cm = FVector::Dist2D(Point, Door->GetActorLocation());
		if (Cm <= BestCm)
		{
			Best = Door;
			BestCm = Cm;
		}
	}
	return Best;
}

ADoorActor* UDoorSubsystem::FindClickedDoor(const FHitResult& Hit) const
{
	if (!bAdopted)
	{
		return nullptr;
	}
	if (ADoorActor* Door = Cast<ADoorActor>(Hit.GetActor()))
	{
		return Door;
	}
	for (ADoorActor* Door : Doors)
	{
		if (Door && Door->IsNearOpenLeaf(Hit.ImpactPoint))
		{
			return Door;
		}
	}
	return nullptr;
}

bool UDoorSubsystem::NoteClick(const ADoorActor* Door, double NowSeconds, double WindowSeconds)
{
	const bool bDouble = Door && LastClickDoor.Get() == Door && LastClickSeconds >= 0.0 && NowSeconds - LastClickSeconds <= WindowSeconds;
	LastClickDoor = bDouble ? nullptr : Door;
	LastClickSeconds = bDouble ? -1.0 : NowSeconds;
	return bDouble;
}

void UDoorSubsystem::AddKey(const FString& KeyId)
{
	if (!KeyId.IsEmpty())
	{
		Keys.Add(KeyId);
		UE_LOG(LogCodexTactics, Display, TEXT("Doors: the squad has the key %s"), *KeyId);
	}
}

bool UDoorSubsystem::HasKeyFor(const ADoorActor& Door) const
{
	return DoorRules::KeyFits(Keys, Door.GetBuildingId(), Door.GetOpeningId(), Door.GetKeyId());
}

bool UDoorSubsystem::RequestInteraction(ADoorActor* Door, AOperativeCharacter* Operative, bool bDoubleClick, bool bAttack)
{
	if (!bAdopted || !Door || !Operative)
	{
		return false;
	}
	const FDoorTuning& Tuning = DoorTuning::Get();
	FPendingOrder Order;
	Order.Operative = Operative;
	Order.Door = Door;
	if (!DoorRules::ChooseClickAction(Door->GetDoorState(), bDoubleClick, bAttack, HasKeyFor(*Door), Door->IsBroken(), Order.Action))
	{
		if (Door->GetDoorState() == EDoorLeafState::Locked && !bAttack)
		{
			PostLine(LOCTEXT("Locked", "Locked. Double click: kick it. Ctrl + click: shoot the lock. Or blow it."));
			return true; // the click is the door's (no walk into a locked door)
		}
		return false;
	}
	if (Order.Action == EDoorAction::Kick && !Tuning.bKickEnabled)
	{
		PostLine(LOCTEXT("NoKick", "Kicking doors is off."));
		return true;
	}
	CancelInteraction(Operative);
	const FVector Feet = Operative->GetActorLocation() - FVector(0.f, 0.f, Operative->GetSimpleCollisionHalfHeight());
	const float Side = Door->ToLocal(Feet).Y >= 0.f ? 1.f : -1.f;
	Order.Approach = Order.Action == EDoorAction::ShootLock
		? Door->GetActorLocation() + Door->GetNormal() * (Side * Tuning.LockShotStandCm)
		: Door->GetApproachPoint(Feet);
	const bool bInReach = IsInReach(Order, *Operative, *Door);
	if (!bInReach)
	{
		const EOperativeOrderResult Result = Operative->OrderMoveTo(FVector(Order.Approach.X, Order.Approach.Y, Feet.Z + Operative->GetSimpleCollisionHalfHeight()),
			bDoubleClick && Order.Action == EDoorAction::Kick);
		if (Result != EOperativeOrderResult::Accepted)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Doors: %s cannot walk up to %s (order result %d)"), *Operative->GetName(), *Door->GetName(), static_cast<int32>(Result));
			return false;
		}
	}
	static const TCHAR* const Names[] = { TEXT("open"), TEXT("close"), TEXT("kick"), TEXT("auto-open"), TEXT("unlock"), TEXT("shoot the lock of"), TEXT("blow") };
	UE_LOG(LogCodexTactics, Display, TEXT("Doors: %s ordered to %s %s (%s)"), *Operative->GetName(), Names[static_cast<int32>(Order.Action)], *Door->GetName(),
		bInReach ? TEXT("in reach") : TEXT("walking up"));
	Orders.Add(Order);
	if (bInReach && Act(Orders.Last()))
	{
		Orders.Pop();
	}
	return true;
}

void UDoorSubsystem::CancelInteraction(const AOperativeCharacter* Operative)
{
	Orders.RemoveAll([Operative](const FPendingOrder& Order) { return !Order.Operative.IsValid() || Order.Operative.Get() == Operative; });
}

bool UDoorSubsystem::HasPendingInteraction(const AOperativeCharacter* Operative) const
{
	return Orders.ContainsByPredicate([Operative](const FPendingOrder& Order) { return Order.Operative.Get() == Operative; });
}

bool UDoorSubsystem::IsInReach(const FPendingOrder& Order, const AOperativeCharacter& Operative, const ADoorActor& Door) const
{
	const FDoorTuning& Tuning = DoorTuning::Get();
	const FVector Feet = Operative.GetActorLocation() - FVector(0.f, 0.f, Operative.GetSimpleCollisionHalfHeight());
	if (Order.Action != EDoorAction::ShootLock)
	{
		return FVector::Dist2D(Feet, Door.GetActorLocation()) <= Tuning.InteractReachCm && Door.IsAtDoorLevel(Feet);
	}
	// The lock shot: in range with a clear line from his chest to the lock (the leaf itself is the only thing it may hit).
	const FVector Eye = Operative.GetActorLocation() + FVector(0.f, 0.f, 50.f);
	if (FVector::Dist(Eye, Door.GetLockPoint()) > Tuning.LockShotRangeCm || !Door.IsAtDoorLevel(Feet))
	{
		return false;
	}
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DoorLockShotLine), false, &Operative);
	for (TActorIterator<APawn> It(GetWorld()); It; ++It)
	{
		Params.AddIgnoredActor(*It);
	}
	return !GetWorld()->LineTraceSingleByChannel(Hit, Eye, Door.GetLockPoint(), ECC_Visibility, Params) || Hit.GetActor() == &Door;
}

bool UDoorSubsystem::Act(FPendingOrder& Order)
{
	ADoorActor* Door = Order.Door.Get();
	AOperativeCharacter* Operative = Order.Operative.Get();
	if (!Door || !Operative)
	{
		return true;
	}
	const FDoorTuning& Tuning = DoorTuning::Get();
	const auto PlayHook = [Operative](const FString& Path)
	{
		if (Path.IsEmpty())
		{
			return; // no clip yet: the timing in doors.json still holds
		}
		if (UAnimMontage* Montage = Cast<UAnimMontage>(FSoftObjectPath(Path).TryLoad()))
		{
			Operative->PlayAnimMontage(Montage);
		}
		else
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("Doors: montage %s not found (doors.json)"), *Path);
		}
	};
	switch (Order.Action)
	{
	case EDoorAction::Open:
		if (Door->GetDoorState() != EDoorLeafState::Closed)
		{
			return true; // open already (somebody walked through), or locked meanwhile
		}
		Operative->StopOperative();
		PlayHook(Tuning.HandOpenMontage);
		Order.WorkLeft = Tuning.HandOpenSeconds;
		break;
	case EDoorAction::Close:
		if (!Door->CloseDoor(Operative) && Door->IsOpen() && !Door->IsBroken())
		{
			PostLine(LOCTEXT("Doorway", "Somebody is in the doorway."));
		}
		return true;
	case EDoorAction::Unlock:
		Operative->StopOperative();
		PlayHook(Tuning.UnlockMontage);
		Order.WorkLeft = Tuning.UnlockSeconds;
		break;
	case EDoorAction::Kick:
		if (Door->GetDoorState() != EDoorLeafState::Closed && Door->GetDoorState() != EDoorLeafState::Locked)
		{
			return true;
		}
		Operative->StopOperative();
		PlayHook(Tuning.KickMontage);
		Order.WorkLeft = Tuning.KickSeconds;
		break;
	case EDoorAction::ShootLock:
		Operative->StopOperative();
		if (!Operative->OrderQuickFire(Door->GetLockPoint(), nullptr))
		{
			PostLine(LOCTEXT("NoShot", "Can't shoot the lock now."));
			return true;
		}
		Order.WorkLeft = Tuning.LockShotResolveSeconds;
		break;
	default:
		return true;
	}
	return Order.WorkLeft <= 0.f && FinishWork(Order);
}

bool UDoorSubsystem::FinishWork(FPendingOrder& Order)
{
	ADoorActor* Door = Order.Door.Get();
	AOperativeCharacter* Operative = Order.Operative.Get();
	if (!Door || !Operative)
	{
		return true;
	}
	const FDoorTuning& Tuning = DoorTuning::Get();
	const auto Roll = [](float& Forced) { const float Value = Forced >= 0.f ? Forced : FMath::FRand(); Forced = -1.f; return Value; };
	switch (Order.Action)
	{
	case EDoorAction::Open:
		Door->OpenDoor(Operative);
		return true;
	case EDoorAction::Unlock:
		if (Door->UnlockDoor(Operative))
		{
			Door->OpenDoor(Operative); // the key turned: he pushes it open
		}
		return true;
	case EDoorAction::ShootLock:
	{
		const FString WeaponId = Operative->CurrentWeapon ? Operative->CurrentWeapon->WeaponId : FString();
		const float Chance = DoorRules::GetLockShotChance(WeaponId, Tuning);
		const float Value = Roll(LockShotRollForTesting);
		UE_LOG(LogCodexTactics, Display, TEXT("Doors: %s shot the lock of %s with %s: chance %.2f, roll %.2f"), *Operative->GetName(), *Door->GetName(),
			WeaponId.IsEmpty() ? TEXT("nothing") : *WeaponId, Chance, Value);
		if (Value < Chance)
		{
			Door->ShootLockOpen(Operative);
		}
		else
		{
			PostLine(LOCTEXT("LockHolds", "The lock holds."));
		}
		return true;
	}
	case EDoorAction::Kick:
	{
		const UHealthComponent* Health = Operative->FindComponentByClass<UHealthComponent>();
		const FDoorKickOutcome Kick = DoorRules::ResolveKick(Door->GetDoorState(), Door->IsHeavy(), Health ? Health->GetHealthFraction() : 1.f,
			Roll(KickRollForTesting), Roll(InjuryRollForTesting), Tuning);
		++Order.Attempts;
		++KickAttempts;
		UE_LOG(LogCodexTactics, Display, TEXT("Doors: %s kicks %s (attempt %d/%d%s): open %.2f -> %s, injury %.2f -> %s"), *Operative->GetName(), *Door->GetName(),
			Order.Attempts, Tuning.KickMaxAttempts, Door->IsHeavy() ? TEXT(", heavy") : TEXT(""), Kick.OpenChance, Kick.bOpens ? TEXT("gives") : TEXT("holds"),
			Kick.InjuryChance, Kick.bInjures ? TEXT("hurt") : TEXT("fine"));
		if (Kick.bInjures && Tuning.KickInjuryDamage > 0.f)
		{
			if (UHealthComponent* Body = Operative->FindComponentByClass<UHealthComponent>())
			{
				Body->ApplyDirectHealthLoss(Tuning.KickInjuryDamage, TEXT("door kick")); // a strained leg, no armour
				++KickInjuries;
				PostLine(FText::Format(LOCTEXT("KickHurt", "{0} hurt himself kicking the door."), Operative->DisplayName));
			}
		}
		Door->KickDoor(Operative, Kick.bOpens);
		if (Kick.bOpens || Door->IsOpen())
		{
			return true;
		}
		if (Order.Attempts >= Tuning.KickMaxAttempts)
		{
			PostLine(LOCTEXT("DoorHolds", "The door holds. Shoot the lock or blow it."));
			return true;
		}
		Order.WorkLeft = Tuning.KickSeconds; // the next attempt
		return false;
	}
	default:
		return true;
	}
}

void UDoorSubsystem::TickOrders(float DeltaTime)
{
	using namespace DoorSubsystemLocal;
	const FDoorTuning& Tuning = DoorTuning::Get();
	for (int32 Index = Orders.Num() - 1; Index >= 0; --Index)
	{
		FPendingOrder& Order = Orders[Index];
		ADoorActor* Door = Order.Door.Get();
		AOperativeCharacter* Operative = Order.Operative.Get();
		Order.Age += DeltaTime;
		const float Budget = Tuning.InteractTimeoutSeconds + Tuning.UnlockSeconds + Tuning.KickSeconds * Tuning.KickMaxAttempts;
		if (!Door || !Operative || Order.Age > Budget)
		{
			Orders.RemoveAt(Index);
			continue;
		}
		const AAIController* AI = Cast<AAIController>(Operative->GetController());
		const UPathFollowingComponent* Follow = AI ? AI->GetPathFollowingComponent() : nullptr;
		const bool bMovingElsewhere = Follow && Follow->GetStatus() == EPathFollowingStatus::Moving
			&& FVector::Dist2D(Follow->GetCurrentTargetLocation(), Order.Approach) > OrderGoalSlackCm;
		if (Order.WorkLeft >= 0.f)
		{
			if (bMovingElsewhere)
			{
				UE_LOG(LogCodexTactics, Display, TEXT("Doors: %s's work on %s dropped (another order)"), *Operative->GetName(), *Door->GetName());
				Orders.RemoveAt(Index);
				continue;
			}
			Order.WorkLeft -= DeltaTime;
			if (Order.WorkLeft <= 0.f && FinishWork(Order))
			{
				Orders.RemoveAt(Index);
			}
			continue;
		}
		if (bMovingElsewhere)
		{
			Orders.RemoveAt(Index); // another move order replaced the door order
			continue;
		}
		if (IsInReach(Order, *Operative, *Door) && Act(Order))
		{
			Orders.RemoveAt(Index);
		}
	}
}

void UDoorSubsystem::HandleExplosion(UWorld* World, const FExplosionResult& Result)
{
	if (World != GetWorld() || !bAdopted)
	{
		return;
	}
	const float Reach = FMath::Min(Result.RadiusCm, DoorTuning::Get().BlastReachCm);
	for (ADoorActor* Door : Doors)
	{
		if (Door && !Door->IsBroken() && FVector::Dist(Door->GetDoorwayCenter(), Result.Centre) <= Reach)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Doors: the %s blast %.0f cm from %s breaks it"), *Result.Kind.ToString(),
				FVector::Dist(Door->GetDoorwayCenter(), Result.Centre), *Door->GetName());
			Door->BlastDoor(nullptr);
		}
	}
}

namespace DoorSubsystemLocal
{
	void GiveKey(const TArray<FString>& Args, UWorld* World)
	{
		UDoorSubsystem* Doors = World ? World->GetSubsystem<UDoorSubsystem>() : nullptr;
		if (Doors && Args.Num() > 0)
		{
			Doors->AddKey(Args[0]);
		}
	}

	static FAutoConsoleCommandWithWorldAndArgs GiveKeyCommand(TEXT("Codex.Doors.GiveKey"),
		TEXT("Openable doors: the squad gets a key - <building id> (every locked door of it), <opening id>, a door_key tag value, or * (all). ")
		TEXT("Stub until keys are loot items."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&GiveKey));
}

void UDoorSubsystem::PostLine(const FText& Text) const
{
	if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
	{
		Messages->PostMessage(LOCTEXT("Squad", "SQUAD"), Text);
	}
}

#undef LOCTEXT_NAMESPACE
