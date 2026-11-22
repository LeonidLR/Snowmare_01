#include "Interactables/DoorSubsystem.h"

#include "AIController.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequenceBase.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Combat/Explosion.h"
#include "Combat/GrenadeSubsystem.h"
#include "Combat/HealthComponent.h"
#include "CodexTactics.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Data/WeaponDataAsset.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "GameFramework/Character.h"
#include "Interactables/DoorActor.h"
#include "Interactables/LootCrateActor.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationData.h"
#include "NavMesh/NavMeshPath.h"
#include "NavMesh/RecastNavMesh.h"
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
	/** A breach's grenade: the time from the throw to the bang (fuse + flight) before the door is judged, s. */
	constexpr float BlastWaitSeconds = 4.f;
	/** A monster bashes a door once its feet are this much past interact_reach_cm of the doorway. */
	constexpr float BashReachSlackCm = 60.f;
	/** A path whose end lies this close to its goal was not cut short. */
	constexpr float PathShortCm = 150.f;
	/** The clip log keeps this many entries (smokes index into it). */
	constexpr int32 ClipLogMax = 256;
	/** A monster's "path stopped by that door" note counts this long, s. */
	constexpr double BlockedNoteSeconds = 6.0;
	/** A breacher walks on this long after the door gave (the doorway's nav box update), s. */
	constexpr float ResumeDelaySeconds = 0.6f;

	bool IsKitLeafPart(const UActorComponent& Component)
	{
		for (const FName& Tag : Component.ComponentTags)
		{
			const FString Text = Tag.ToString();
			if (Text == TEXT("kit_part=door_leaf_closed") || Text == TEXT("kit_part=door_leaf_open") || Text == TEXT("kit_part=flat_door")
				|| Text == TEXT("kit_part=entrance_door") || Text == TEXT("kit_part=entrance_leaf") || Text == TEXT("kit_part=balcony_door_leaf"))
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

	FVector FeetOf(const ACharacter& Pawn)
	{
		return Pawn.GetActorLocation() - FVector(0.f, 0.f, Pawn.GetSimpleCollisionHalfHeight());
	}

	bool IsAlive(const AActor* Actor)
	{
		const UHealthComponent* Health = Actor ? Actor->FindComponentByClass<UHealthComponent>() : nullptr;
		return Actor && (!Health || Health->IsAlive());
	}

	/** The skeleton name of a pawn's body ("" without one). */
	FString SkeletonNameOf(const ACharacter& Pawn)
	{
		const USkeletalMeshComponent* Mesh = Pawn.GetMesh();
		const USkeletalMesh* Asset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
		const USkeleton* Skeleton = Asset ? Asset->GetSkeleton() : nullptr;
		return Skeleton ? Skeleton->GetName() : FString();
	}

	const TCHAR* ActionName(EDoorAction Action)
	{
		static const TCHAR* const Names[] = { TEXT("open"), TEXT("close"), TEXT("kick"), TEXT("auto-open"), TEXT("unlock"), TEXT("shoot the lock of"), TEXT("blow") };
		return Names[FMath::Clamp(static_cast<int32>(Action), 0, 6)];
	}

	const TCHAR* BreachName(EDoorAction Action)
	{
		switch (Action)
		{
		case EDoorAction::Unlock: return TEXT("key");
		case EDoorAction::ShootLock: return TEXT("shoot");
		case EDoorAction::Blast: return TEXT("blast");
		default: return TEXT("kick");
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
	Bashes.Reset();
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
	// Keys outlive the switch: a carrier's body leaves his keys whether the doors are openable or not.
	TickKeyCarriers();
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
	TickBreachDetection(DeltaTime);
	TickBashes(DeltaTime);
	TickResumes(DeltaTime);
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
	BreachStarts = BreachOpens = 0;
	MontagesPlayed = 0;
	ClipLog.Reset();
	GiveUps.Reset();
	BreachCheckIn = 0.f;
	ExplosionHandle = Explosions::OnDetonated().AddUObject(this, &UDoorSubsystem::HandleExplosion);
	UE_LOG(LogCodexTactics, Display, TEXT("Doors: R1 on - %d door(s) from %d leaf actor(s) (auto-open %.2f s, kick %s, %d start key(s), %d found); kit-dressed: %d / %d hidden-proxy door(s) got %d kit leaf instance(s); breach %s, roll seed %d"),
		Made, Leaves.Num(), Tuning.AutoOpenDelaySeconds, Tuning.bKickEnabled ? TEXT("on") : TEXT("off"), Keys.Num(), FoundKeys.Num(), KitDoors, HiddenProxyDoors,
		KitInstancesLifted, Tuning.bBreachEnabled ? TEXT("on") : TEXT("off"), DoorTuning::GetRollSeed());
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
	Bashes.Reset();
	Resumes.Reset();
	BlockedNotes.Reset();
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

// --- Keys ------------------------------------------------------------------------------------------------------------------------------

void UDoorSubsystem::AddKey(const FString& KeyId)
{
	if (!KeyId.IsEmpty())
	{
		FoundKeys.Add(KeyId);
		UE_LOG(LogCodexTactics, Display, TEXT("Doors: the squad has the key %s"), *KeyId);
	}
}

bool UDoorSubsystem::HasKeyFor(const ADoorActor& Door) const
{
	return DoorRules::KeyFits(Keys, Door.GetBuildingId(), Door.GetOpeningId(), Door.GetKeyId())
		|| DoorRules::KeyFits(FoundKeys, Door.GetBuildingId(), Door.GetOpeningId(), Door.GetKeyId());
}

TArray<FString> UDoorSubsystem::GetSquadKeys() const
{
	TSet<FString> All = Keys;
	All.Append(FoundKeys);
	return All.Array();
}

void UDoorSubsystem::GrantSquadKey(UWorld* World, const FString& KeyId)
{
	if (UDoorSubsystem* Subsystem = World ? World->GetSubsystem<UDoorSubsystem>() : nullptr)
	{
		Subsystem->AddKey(KeyId);
	}
}

void UDoorSubsystem::GiveKeyToPawn(APawn* Pawn, const FString& KeyId)
{
	if (!Pawn || KeyId.IsEmpty())
	{
		return;
	}
	const AOperativeCharacter* Operative = Cast<AOperativeCharacter>(Pawn);
	if (Operative && Operative->IsPlayerTeam())
	{
		AddKey(KeyId); // the squad's keys are one key ring
		return;
	}
	CarriedKeys.FindOrAdd(Pawn).AddUnique(KeyId);
	UE_LOG(LogCodexTactics, Display, TEXT("Doors: %s carries the key %s"), *Pawn->GetName(), *KeyId);
}

bool UDoorSubsystem::PawnHasKeyFor(const APawn* Pawn, const ADoorActor& Door) const
{
	const AOperativeCharacter* Operative = Cast<AOperativeCharacter>(Pawn);
	if (Operative && Operative->IsPlayerTeam())
	{
		return HasKeyFor(Door);
	}
	for (const TPair<TWeakObjectPtr<APawn>, TArray<FString>>& Pair : CarriedKeys)
	{
		if (Pair.Key.Get() == Pawn)
		{
			return DoorRules::KeyFits(TSet<FString>(Pair.Value), Door.GetBuildingId(), Door.GetOpeningId(), Door.GetKeyId());
		}
	}
	return false;
}

TArray<FString> UDoorSubsystem::GetPawnKeys(const APawn* Pawn) const
{
	for (const TPair<TWeakObjectPtr<APawn>, TArray<FString>>& Pair : CarriedKeys)
	{
		if (Pair.Key.Get() == Pawn)
		{
			return Pair.Value;
		}
	}
	return TArray<FString>();
}

void UDoorSubsystem::TickKeyCarriers()
{
	for (auto It = CarriedKeys.CreateIterator(); It; ++It)
	{
		APawn* Pawn = It->Key.Get();
		if (!Pawn)
		{
			It.RemoveCurrent();
			continue;
		}
		if (DoorSubsystemLocal::IsAlive(Pawn) || It->Value.IsEmpty())
		{
			continue;
		}
		// His body keeps the keys: searchable remains bound to the lying body (as a fallen operative's, AOperativeCharacter::SpawnRemainsLoot).
		const FVector Feet = Pawn->GetActorLocation() - FVector(0.f, 0.f, Pawn->GetSimpleCollisionHalfHeight());
		const FTransform At(Pawn->GetActorRotation(), Feet + FVector(0.f, 0.f, 40.f));
		ALootCrateActor* Remains = GetWorld()->SpawnActorDeferred<ALootCrateActor>(ALootCrateActor::StaticClass(), At, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Remains)
		{
			continue;
		}
		Remains->Box->SetCanEverAffectNavigation(false);
		FLootContents Contents;
		Contents.DestroyAll(); // only the keys (no crate defaults)
		Contents.KeyIds = It->Value;
		Remains->Contents = Contents;
		Remains->FinishSpawning(At);
		const AOperativeCharacter* Operative = Cast<AOperativeCharacter>(Pawn);
		const ACharacter* Body = Cast<ACharacter>(Pawn);
		if (Body && Body->GetMesh())
		{
			Remains->BindRemainsBody(Body->GetMesh(), Operative ? Operative->DisplayName : FText::FromString(Pawn->GetName()));
		}
		++KeyRemainsSpawned;
		LastKeyRemains = Remains;
		UE_LOG(LogCodexTactics, Display, TEXT("Doors: %s fell carrying %d key(s) (%s) - his body can be searched for them (%s)"), *Pawn->GetName(), It->Value.Num(),
			*FString::Join(It->Value, TEXT(", ")), *Remains->GetName());
		It.RemoveCurrent();
	}
}

// --- Player orders -------------------------------------------------------------------------------------------------------------------

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
	if (!WalkUp(Order, *Operative, *Door, bDoubleClick && Order.Action == EDoorAction::Kick))
	{
		return false;
	}
	const bool bInReach = IsInReach(Order, *Operative, *Door);
	UE_LOG(LogCodexTactics, Display, TEXT("Doors: %s ordered to %s %s (%s)"), *Operative->GetName(), DoorSubsystemLocal::ActionName(Order.Action), *Door->GetName(),
		bInReach ? TEXT("in reach") : TEXT("walking up"));
	Orders.Add(Order);
	if (bInReach && Act(Orders.Last()) && Conclude(Orders.Last()))
	{
		Orders.Pop();
	}
	return true;
}

bool UDoorSubsystem::WalkUp(FPendingOrder& Order, AOperativeCharacter& Operative, ADoorActor& Door, bool bSprint)
{
	const FDoorTuning& Tuning = DoorTuning::Get();
	const FVector Feet = DoorSubsystemLocal::FeetOf(Operative);
	const float Side = Order.BreachSide != 0.f ? Order.BreachSide : Door.ToLocal(Feet).Y >= 0.f ? 1.f : -1.f;
	switch (Order.Action)
	{
	case EDoorAction::ShootLock:
		Order.Approach = Door.GetActorLocation() + Door.GetNormal() * (Side * Tuning.LockShotStandCm);
		break;
	case EDoorAction::Blast:
		Order.Approach = Door.GetActorLocation() + Door.GetNormal() * (Side * Tuning.BlastStandCm);
		break;
	default:
		Order.Approach = Order.BreachSide != 0.f ? Door.GetActorLocation() + Door.GetNormal() * (Side * DoorTuning::Get().ApproachCm) : Door.GetApproachPoint(Feet);
		break;
	}
	Order.WorkLeft = -1.f;
	if (IsInReach(Order, Operative, Door))
	{
		return true;
	}
	const EOperativeOrderResult Result = Operative.OrderMoveTo(FVector(Order.Approach.X, Order.Approach.Y, Feet.Z + Operative.GetSimpleCollisionHalfHeight()), bSprint);
	if (Result != EOperativeOrderResult::Accepted)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Doors: %s cannot walk up to %s (order result %d)"), *Operative.GetName(), *Door.GetName(), static_cast<int32>(Result));
		return false;
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
	if (Order.Action == EDoorAction::Blast)
	{
		// Out of his own grenade's blast, in throwing range of the door, at its level.
		const float Cm = static_cast<float>(FVector::Dist2D(Feet, Door.GetActorLocation()));
		return Cm >= Tuning.BlastStandCm * 0.8f && Cm <= Tuning.BlastStandCm * 1.6f && Door.IsAtDoorLevel(Feet);
	}
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

// --- Clips ---------------------------------------------------------------------------------------------------------------------------

EDoorSide UDoorSubsystem::HandSideFor(const AActor& Pawn, const ADoorActor& Door) const
{
	return DoorRules::ChooseHandSide(Door.ToLocal(Pawn.GetActorLocation()), Door.GetHandleLocalX(), DoorTuning::Get().bMontageSwapLR);
}

float UDoorSubsystem::PlayClip(ACharacter& Pawn, EDoorClip Clip, EDoorSide Side)
{
	const FString Key = DoorRules::ClipKey(Clip, Side);
	ClipLog.Add(Key);
	if (ClipLog.Num() > DoorSubsystemLocal::ClipLogMax)
	{
		ClipLog.RemoveAt(0);
	}
	const FString Skeleton = DoorSubsystemLocal::SkeletonNameOf(Pawn);
	const FString Path = DoorRules::FindClipPath(DoorTuning::Get(), Skeleton, /*bUseDefault*/ Pawn.IsA<AOperativeCharacter>(), Clip, Side);
	if (Path.IsEmpty())
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Doors: %s %s - no clip for skeleton %s (doors.json montages)"), *Pawn.GetName(), *Key, *Skeleton);
		return 0.f;
	}
	UObject* Asset = FSoftObjectPath(Path).TryLoad();
	float Length = 0.f;
	if (UAnimMontage* Montage = Cast<UAnimMontage>(Asset))
	{
		Length = Pawn.PlayAnimMontage(Montage);
	}
	else if (UAnimSequenceBase* Sequence = Cast<UAnimSequenceBase>(Asset))
	{
		// A plain clip: a dynamic montage on the full-body slot (the interaction clips' slot).
		UAnimInstance* Anim = Pawn.GetMesh() ? Pawn.GetMesh()->GetAnimInstance() : nullptr;
		const UAnimMontage* Dynamic = Anim ? Anim->PlaySlotAnimationAsDynamicMontage(Sequence, TEXT("FullBody"), 0.2f, 0.25f) : nullptr;
		Length = Dynamic ? Dynamic->GetPlayLength() : 0.f;
	}
	else
	{
		UE_LOG(LogCodexTactics, Warning, TEXT("Doors: clip %s (%s) not found - the doors.json timings hold"), *Path, *Key);
		return 0.f;
	}
	if (Length > 0.f)
	{
		++MontagesPlayed;
		LastMontageName = Asset->GetName();
	}
	UE_LOG(LogCodexTactics, Display, TEXT("Doors: %s plays %s (%s, skeleton %s, %.2f s)"), *Pawn.GetName(), *Asset->GetName(), *Key, *Skeleton, Length);
	return Length;
}

float UDoorSubsystem::RollFor(float& Forced, const ADoorActor& Door, const AActor& Pawn, int32 Attempt, const TCHAR* What) const
{
	if (Forced >= 0.f)
	{
		const float Value = Forced;
		Forced = -1.f;
		return Value;
	}
	const FString DoorId = Door.GetBuildingId() + TEXT("/") + Door.GetOpeningId() + (Door.GetOpeningId().IsEmpty() ? Door.GetName() : FString());
	return DoorRules::DeterministicRoll(DoorTuning::GetRollSeed(), DoorRules::RollSalt(DoorId, Pawn.GetName(), Attempt, What));
}

void UDoorSubsystem::AddTrace(const AActor* Pawn, const FString& Step)
{
	FString& Trace = BreachTraces.FindOrAdd(Pawn);
	Trace += Trace.IsEmpty() ? Step : TEXT(" ") + Step;
}

FString UDoorSubsystem::GetBreachTrace(const AActor* Pawn) const
{
	for (const TPair<TWeakObjectPtr<const AActor>, FString>& Pair : BreachTraces)
	{
		if (Pair.Key.Get() == Pawn)
		{
			return Pair.Value;
		}
	}
	return FString();
}

void UDoorSubsystem::StartKickAttempt(FPendingOrder& Order, AOperativeCharacter& Operative, ADoorActor& Door)
{
	const EDoorSide Leg = DoorRules::ChooseKickLeg(Order.KickFirst, Order.Attempts);
	Operative.FaceAimAt(Door.GetLockPoint());
	Order.ClipSeconds = PlayClip(Operative, EDoorClip::Kick, Leg);
	Order.WorkLeft = DoorRules::GetContactSeconds(EDoorClip::Kick, Order.ClipSeconds > 0.f, DoorTuning::Get());
	Order.bRecover = false;
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
	switch (Order.Action)
	{
	case EDoorAction::Open:
	{
		if (Door->GetDoorState() != EDoorLeafState::Closed)
		{
			return true; // open already (somebody walked through), or locked meanwhile
		}
		Operative->StopOperative();
		Operative->FaceAimAt(Door->GetLockPoint());
		// R2: the push after the key / a shot lock, the hand open otherwise; the leaf moves at the clip's contact time.
		const EDoorClip Clip = DoorRules::ChooseOpenClip(Order.bPush || Door->IsLockForced());
		Order.ClipSeconds = PlayClip(*Operative, Clip, HandSideFor(*Operative, *Door));
		Order.WorkLeft = DoorRules::GetContactSeconds(Clip, Order.ClipSeconds > 0.f, Tuning);
		break;
	}
	case EDoorAction::Close:
		if (!Door->CloseDoor(Operative) && Door->IsOpen() && !Door->IsBroken())
		{
			PostLine(LOCTEXT("Doorway", "Somebody is in the doorway."));
		}
		return true;
	case EDoorAction::Unlock:
		Operative->StopOperative();
		Operative->FaceAimAt(Door->GetLockPoint());
		if (!Tuning.UnlockMontage.IsEmpty())
		{
			if (UAnimMontage* Montage = Cast<UAnimMontage>(FSoftObjectPath(Tuning.UnlockMontage).TryLoad()))
			{
				Operative->PlayAnimMontage(Montage);
			}
		}
		Order.WorkLeft = Tuning.UnlockSeconds;
		break;
	case EDoorAction::Kick:
		if (Door->GetDoorState() != EDoorLeafState::Closed && Door->GetDoorState() != EDoorLeafState::Locked)
		{
			return true;
		}
		Operative->StopOperative();
		if (Order.Attempts == 0)
		{
			Order.KickFirst = HandSideFor(*Operative, *Door); // the first kick with the leg on the lock's side, then the other
		}
		StartKickAttempt(Order, *Operative, *Door);
		break;
	case EDoorAction::ShootLock:
		Operative->StopOperative();
		if (!Operative->OrderQuickFire(Door->GetLockPoint(), nullptr))
		{
			if (!Order.bBreach)
			{
				PostLine(LOCTEXT("NoShot", "Can't shoot the lock now."));
			}
			return true;
		}
		++Order.Shots;
		Order.WorkLeft = Tuning.LockShotResolveSeconds;
		break;
	case EDoorAction::Blast:
	{
		Operative->StopOperative();
		UGrenadeSubsystem* Grenades = GetWorld()->GetSubsystem<UGrenadeSubsystem>();
		const float Side = Door->ToLocal(Operative->GetActorLocation()).Y >= 0.f ? 1.f : -1.f;
		const FVector Target = Door->GetActorLocation() + Door->GetNormal() * (Side * 60.f);
		if (!Grenades || !Grenades->ThrowAt(Operative, Target))
		{
			return true;
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Doors: %s throws a grenade at %s"), *Operative->GetName(), *Door->GetName());
		Order.WorkLeft = DoorSubsystemLocal::BlastWaitSeconds;
		break;
	}
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
	switch (Order.Action)
	{
	case EDoorAction::Open:
		Door->OpenDoor(Operative);
		if (Order.bBreach)
		{
			AddTrace(Operative, Order.bPush ? TEXT("push") : TEXT("open"));
		}
		return true;
	case EDoorAction::Unlock:
		if (Door->UnlockDoor(Operative))
		{
			// R2: the key turned - he pushes it open (the push clip; the leaf moves at its contact time).
			if (Order.bBreach)
			{
				AddTrace(Operative, TEXT("key"));
			}
			Order.Action = EDoorAction::Open;
			Order.bPush = true;
			return Act(Order);
		}
		return true;
	case EDoorAction::ShootLock:
	{
		const FString WeaponId = Operative->CurrentWeapon ? Operative->CurrentWeapon->WeaponId : FString();
		const float Chance = DoorRules::GetLockShotChance(WeaponId, Tuning);
		const float Value = RollFor(LockShotRollForTesting, *Door, *Operative, Order.Shots, TEXT("lock"));
		UE_LOG(LogCodexTactics, Display, TEXT("Doors: %s shot the lock of %s with %s: chance %.2f, roll %.2f"), *Operative->GetName(), *Door->GetName(),
			WeaponId.IsEmpty() ? TEXT("nothing") : *WeaponId, Chance, Value);
		if (Value < Chance)
		{
			if (Order.bBreach)
			{
				AddTrace(Operative, TEXT("shoot:gives"));
			}
			if (!Tuning.bLockShotPush)
			{
				Door->ShootLockOpen(Operative); // R1: the door bursts open
				return true;
			}
			// R2: the lock is broken, the door still shut - he walks up and pushes it open.
			Door->BreakLock(Operative);
			Order.Action = EDoorAction::Open;
			Order.bPush = true;
			if (!WalkUp(Order, *Operative, *Door, false))
			{
				return true;
			}
			return IsInReach(Order, *Operative, *Door) ? Act(Order) : false;
		}
		if (Order.bBreach)
		{
			AddTrace(Operative, TEXT("shoot:holds"));
			if (Order.Shots < Tuning.BreachLockShots)
			{
				return Act(Order); // another burst
			}
			return true;
		}
		PostLine(LOCTEXT("LockHolds", "The lock holds."));
		return true;
	}
	case EDoorAction::Kick:
	{
		if (Order.bRecover)
		{
			StartKickAttempt(Order, *Operative, *Door); // the held kick's clip is over: the next attempt
			return Order.WorkLeft <= 0.f && FinishWork(Order);
		}
		const UHealthComponent* Health = Operative->FindComponentByClass<UHealthComponent>();
		const FDoorKickOutcome Kick = DoorRules::ResolveKick(Door->GetDoorState(), Door->IsHeavy(), Health ? Health->GetHealthFraction() : 1.f,
			RollFor(KickRollForTesting, *Door, *Operative, Order.Attempts, TEXT("kick")),
			RollFor(InjuryRollForTesting, *Door, *Operative, Order.Attempts, TEXT("injury")), Tuning);
		const EDoorSide Leg = DoorRules::ChooseKickLeg(Order.KickFirst, Order.Attempts);
		++Order.Attempts;
		++KickAttempts;
		UE_LOG(LogCodexTactics, Display, TEXT("Doors: %s kicks %s (attempt %d/%d, %s leg%s): open %.2f -> %s, injury %.2f -> %s"), *Operative->GetName(), *Door->GetName(),
			Order.Attempts, Tuning.KickMaxAttempts, Leg == EDoorSide::Left ? TEXT("left") : TEXT("right"), Door->IsHeavy() ? TEXT(", heavy") : TEXT(""),
			Kick.OpenChance, Kick.bOpens ? TEXT("gives") : TEXT("holds"), Kick.InjuryChance, Kick.bInjures ? TEXT("hurt") : TEXT("fine"));
		if (Order.bBreach)
		{
			AddTrace(Operative, Kick.bOpens ? TEXT("kick:gives") : TEXT("kick:holds"));
		}
		if (Kick.bInjures && Tuning.KickInjuryDamage > 0.f)
		{
			if (UHealthComponent* Body = Operative->FindComponentByClass<UHealthComponent>())
			{
				Body->ApplyDirectHealthLoss(Tuning.KickInjuryDamage, TEXT("door kick")); // a strained leg, no armour
				++KickInjuries;
				if (Operative->IsPlayerTeam())
				{
					PostLine(FText::Format(LOCTEXT("KickHurt", "{0} hurt himself kicking the door."), Operative->DisplayName));
				}
			}
		}
		Door->KickDoor(Operative, Kick.bOpens);
		if (Kick.bOpens || Door->IsOpen())
		{
			return true;
		}
		if (Order.Attempts >= Tuning.KickMaxAttempts)
		{
			if (!Order.bBreach)
			{
				PostLine(LOCTEXT("DoorHolds", "The door holds. Shoot the lock or blow it."));
			}
			return true;
		}
		// The next attempt: after the rest of this kick's clip (none: kick_s from now, as R1).
		Order.bRecover = true;
		Order.WorkLeft = Order.ClipSeconds > 0.f ? FMath::Max(0.f, Order.ClipSeconds - DoorRules::GetContactSeconds(EDoorClip::Kick, true, Tuning)) : 0.f;
		if (Order.ClipSeconds <= 0.f)
		{
			Order.bRecover = false;
			Order.WorkLeft = Tuning.KickSeconds;
		}
		return false;
	}
	case EDoorAction::Blast:
		AddTrace(Operative, Door->IsBroken() ? TEXT("blast:gives") : TEXT("blast:holds"));
		return true;
	default:
		return true;
	}
}

bool UDoorSubsystem::Conclude(FPendingOrder& Order)
{
	if (!Order.bBreach)
	{
		return true;
	}
	ADoorActor* Door = Order.Door.Get();
	AOperativeCharacter* Operative = Order.Operative.Get();
	if (!Door || !Operative || !DoorSubsystemLocal::IsAlive(Operative))
	{
		return true;
	}
	if (Door->IsOpen() || Door->IsBroken())
	{
		++BreachOpens;
		UE_LOG(LogCodexTactics, Display, TEXT("Doors: %s got through %s (%s) - walks on to (%.0f, %.0f)"), *Operative->GetName(), *Door->GetName(),
			*GetBreachTrace(Operative), Order.ResumeGoal.X, Order.ResumeGoal.Y);
		// The doorway's navmesh changes a moment later (the nav box): the walk is asked again shortly (and once more if it still stops short).
		Resumes.Add({ Operative, Order.ResumeGoal, DoorSubsystemLocal::ResumeDelaySeconds, 3 });
		return true;
	}
	if (Door->GetDoorState() == EDoorLeafState::Closed)
	{
		// Unlocked now (a key / a shot) but still shut: he opens it (the push).
		Order.Action = EDoorAction::Open;
		Order.bPush = Door->IsLockForced();
	}
	else
	{
		Order.Tried.Add(Order.Action);
		EDoorAction Next;
		if (!DoorRules::NextBreachAction(DoorRules::GetBreachActions(DoorTuning::Get(), TEXT("pmc")), Order.Tried,
			AbilityOf(*Operative, *Door), DoorTuning::Get(), Next))
		{
			AddTrace(Operative, TEXT("gave-up"));
			GiveUps.FindOrAdd(Operative).Add(Door, GetWorld()->GetTimeSeconds() + DoorTuning::Get().BreachGiveUpSeconds);
			UE_LOG(LogCodexTactics, Display, TEXT("Doors: %s gives up on %s (%s)"), *Operative->GetName(), *Door->GetName(), *GetBreachTrace(Operative));
			return true;
		}
		Order.Action = Next;
	}
	Order.Attempts = 0;
	Order.Shots = 0;
	Order.bRecover = false;
	Order.Age = 0.f; // every action has its own time budget
	UE_LOG(LogCodexTactics, Display, TEXT("Doors: %s goes on to %s %s"), *Operative->GetName(), DoorSubsystemLocal::ActionName(Order.Action), *Door->GetName());
	if (!WalkUp(Order, *Operative, *Door, false))
	{
		return true;
	}
	return IsInReach(Order, *Operative, *Door) && Act(Order) ? Conclude(Order) : false;
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
		const float Budget = Tuning.InteractTimeoutSeconds + Tuning.UnlockSeconds + Tuning.KickSeconds * Tuning.KickMaxAttempts
			+ (Order.bBreach ? Tuning.InteractTimeoutSeconds * 2.f : 0.f);
		if (!Door || !Operative || Order.Age > Budget || !IsAlive(Operative))
		{
			Orders.RemoveAt(Index);
			continue;
		}
		const AAIController* AI = Cast<AAIController>(Operative->GetController());
		const UPathFollowingComponent* Follow = AI ? AI->GetPathFollowingComponent() : nullptr;
		bool bMovingElsewhere = Follow && Follow->GetStatus() == EPathFollowingStatus::Moving
			&& FVector::Dist2D(Follow->GetCurrentTargetLocation(), Order.Approach) > OrderGoalSlackCm;
		if (bMovingElsewhere && Order.bBreach)
		{
			// His commander asked the same walk again (its path stops at this door): he keeps working the door.
			FVector Goal;
			if (FindBlockingDoor(*Operative, Goal) == Door)
			{
				bMovingElsewhere = false;
				if (Order.WorkLeft >= 0.f)
				{
					Operative->StopOperative();
				}
				else if (const EOperativeOrderResult Result = Operative->OrderMoveTo(FVector(Order.Approach.X, Order.Approach.Y, Operative->GetActorLocation().Z), false);
					Result != EOperativeOrderResult::Accepted)
				{
					Orders.RemoveAt(Index);
					continue;
				}
			}
		}
		if (Order.WorkLeft >= 0.f)
		{
			if (bMovingElsewhere)
			{
				UE_LOG(LogCodexTactics, Display, TEXT("Doors: %s's work on %s dropped (another order)"), *Operative->GetName(), *Door->GetName());
				Orders.RemoveAt(Index);
				continue;
			}
			Order.WorkLeft -= DeltaTime;
			if (Order.WorkLeft <= 0.f && FinishWork(Order) && Conclude(Orders[Index]))
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
		if (IsInReach(Order, *Operative, *Door) && Act(Order) && Conclude(Orders[Index]))
		{
			Orders.RemoveAt(Index);
		}
	}
}

// --- Enemies breach locked doors (R2) -----------------------------------------------------------------------------------------------

bool UDoorSubsystem::IsBreaching(const AActor* Pawn) const
{
	return Pawn && (Orders.ContainsByPredicate([Pawn](const FPendingOrder& Order) { return Order.bBreach && Order.Operative.Get() == Pawn; })
		|| Bashes.ContainsByPredicate([Pawn](const FBash& Bash) { return Bash.Pawn.Get() == Pawn; }));
}

bool UDoorSubsystem::IsPawnBreaching(const AActor* Pawn)
{
	const UWorld* World = Pawn ? Pawn->GetWorld() : nullptr;
	const UDoorSubsystem* Subsystem = World ? World->GetSubsystem<UDoorSubsystem>() : nullptr;
	return Subsystem && Subsystem->IsBreaching(Pawn);
}

ADoorActor* UDoorSubsystem::FindBlockingDoor(const ACharacter& Pawn, FVector& OutGoal, float* OutWorkSide) const
{
	const AAIController* AI = Cast<AAIController>(Pawn.GetController());
	const UPathFollowingComponent* Follow = AI ? AI->GetPathFollowingComponent() : nullptr;
	const FNavPathSharedPtr Path = Follow ? Follow->GetPath() : nullptr;
	if (!Path.IsValid() || !Path->IsValid() || Path->GetPathPoints().Num() == 0)
	{
		return nullptr;
	}
	const FVector Goal = Path->GetQueryData().EndLocation;
	FVector End = Path->GetEndLocation();
	// A crowd-following move keeps only the path's two ends (no string pulling): a partial path's real end is the corridor's last polygon -
	// its point nearest the goal.
	const FNavMeshPath* MeshPath = Path->CastPath<FNavMeshPath>();
	const ARecastNavMesh* Recast = Cast<ARecastNavMesh>(Path->GetNavigationDataUsed());
	if (Path->IsPartial() && MeshPath && Recast && MeshPath->PathCorridor.Num() > 0)
	{
		FVector OnPoly;
		if (Recast->GetClosestPointOnPoly(MeshPath->PathCorridor.Last(), Goal, OnPoly))
		{
			End = OnPoly;
		}
	}
	if (!Path->IsPartial())
	{
		return nullptr; // his path gets there (a complete path - also round the door)
	}
	const FDoorTuning& Tuning = DoorTuning::Get();
	const FVector Feet = DoorSubsystemLocal::FeetOf(Pawn);
	ADoorActor* Best = nullptr;
	double BestCm = TNumericLimits<double>::Max();
	for (ADoorActor* Door : Doors)
	{
		if (!Door || Door->GetDoorState() != EDoorLeafState::Locked || Door->IsBroken() || !Door->IsAtDoorLevel(End))
		{
			continue;
		}
		if (!DoorRules::IsPathStoppedByDoor(Door->ToLocal(Feet), Door->ToLocal(End), Door->ToLocal(Goal), Door->GetWidthCm() * 0.5f, Tuning.BreachDetectCm))
		{
			continue;
		}
		const double Cm = FVector::Dist2D(End, Door->GetActorLocation());
		if (Cm < BestCm)
		{
			Best = Door;
			BestCm = Cm;
		}
	}
	float WorkSide = 0.f;
	if (!Best && Path->IsPartial())
	{
		// The path stops short somewhere else (the navmesh point nearest the goal may be a storey below it): a locked door at the goal's
		// level right next to the goal, the goal behind it, is the one in his way.
		for (ADoorActor* Door : Doors)
		{
			if (!Door || Door->GetDoorState() != EDoorLeafState::Locked || Door->IsBroken() || !Door->IsAtDoorLevel(Goal))
			{
				continue;
			}
			const float Side = DoorRules::GoalBehindDoorSide(Door->ToLocal(Goal), Door->GetWidthCm() * 0.5f, Tuning.BreachGoalDoorCm);
			const double Cm = FVector::Dist2D(Goal, Door->GetActorLocation());
			if (Side != 0.f && Cm < BestCm)
			{
				Best = Door;
				BestCm = Cm;
				WorkSide = -Side;
			}
		}
	}
	if (OutWorkSide)
	{
		*OutWorkSide = WorkSide;
	}
	OutGoal = Goal;
	return Best;
}

FDoorBreachAbility UDoorSubsystem::AbilityOf(const AOperativeCharacter& Operative, const ADoorActor& Door) const
{
	FDoorBreachAbility Ability;
	Ability.bHasKey = PawnHasKeyFor(&Operative, Door);
	const FString WeaponId = Operative.CurrentWeapon ? Operative.CurrentWeapon->WeaponId : FString();
	Ability.bCanShoot = DoorRules::GetLockShotChance(WeaponId, DoorTuning::Get()) > 0.f && Operative.CurrentClip > 0;
	Ability.bHasGrenade = Operative.GrenadesCount > 0;
	Ability.bCanKick = true;
	return Ability;
}

void UDoorSubsystem::NotifyMoveRequested(AOperativeCharacter* Operative)
{
	UWorld* World = Operative ? Operative->GetWorld() : nullptr;
	UDoorSubsystem* Subsystem = World ? World->GetSubsystem<UDoorSubsystem>() : nullptr;
	if (Subsystem && Subsystem->bAdopted && !Subsystem->bStartingBreach && !Operative->IsPlayerTeam() && DoorTuning::Get().bBreachEnabled)
	{
		Subsystem->ConsiderBreach(*Operative);
	}
}

void UDoorSubsystem::ConsiderBreach(AOperativeCharacter& Operative)
{
	if (!DoorSubsystemLocal::IsAlive(&Operative) || IsBreaching(&Operative) || DoorRules::GetBreachActions(DoorTuning::Get(), TEXT("pmc")).Num() == 0)
	{
		return;
	}
	FVector Goal;
	float WorkSide = 0.f;
	ADoorActor* Door = FindBlockingDoor(Operative, Goal, &WorkSide);
	if (!Door)
	{
		return;
	}
	for (const TPair<TWeakObjectPtr<const AActor>, TMap<TWeakObjectPtr<const ADoorActor>, double>>& Pair : GiveUps)
	{
		const double* Until = Pair.Key.Get() == &Operative ? Pair.Value.Find(Door) : nullptr;
		if (Until && *Until > GetWorld()->GetTimeSeconds())
		{
			return;
		}
	}
	StartBreach(Operative, *Door, Goal, WorkSide);
}

void UDoorSubsystem::TickResumes(float DeltaTime)
{
	for (int32 Index = Resumes.Num() - 1; Index >= 0; --Index)
	{
		FResume& Resume = Resumes[Index];
		AOperativeCharacter* Operative = Resume.Operative.Get();
		if (!Operative || !DoorSubsystemLocal::IsAlive(Operative) || IsBreaching(Operative))
		{
			Resumes.RemoveAt(Index);
			continue;
		}
		Resume.TimeLeft -= DeltaTime;
		if (Resume.TimeLeft > 0.f)
		{
			continue;
		}
		Operative->OrderMoveTo(Resume.Goal, false);
		const AAIController* AI = Cast<AAIController>(Operative->GetController());
		const UPathFollowingComponent* Follow = AI ? AI->GetPathFollowingComponent() : nullptr;
		const FNavPathSharedPtr Path = Follow ? Follow->GetPath() : nullptr;
		const bool bShort = !Path.IsValid() || Path->IsPartial(); // (a crowd path keeps only its two ends: IsPartial is the truth)
		UE_LOG(LogCodexTactics, Display, TEXT("Doors: %s walks on to (%.0f, %.0f)%s"), *Operative->GetName(), Resume.Goal.X, Resume.Goal.Y,
			bShort ? TEXT(" - the path still stops short, asked again soon") : TEXT(""));
		if (bShort && --Resume.TriesLeft > 0)
		{
			Resume.TimeLeft = DoorSubsystemLocal::ResumeDelaySeconds;
			continue;
		}
		Resumes.RemoveAt(Index);
	}
}

bool UDoorSubsystem::StartBreach(AOperativeCharacter& Operative, ADoorActor& Door, const FVector& Goal, float WorkSide)
{
	const FDoorTuning& Tuning = DoorTuning::Get();
	TGuardValue<bool> Starting(bStartingBreach, true);
	FPendingOrder Order;
	Order.Operative = &Operative;
	Order.Door = &Door;
	Order.bBreach = true;
	Order.ResumeGoal = Goal;
	Order.BreachSide = WorkSide;
	if (!DoorRules::NextBreachAction(DoorRules::GetBreachActions(Tuning, TEXT("pmc")), Order.Tried, AbilityOf(Operative, Door), Tuning, Order.Action))
	{
		GiveUps.FindOrAdd(&Operative).Add(&Door, GetWorld()->GetTimeSeconds() + Tuning.BreachGiveUpSeconds);
		return false;
	}
	if (!WalkUp(Order, Operative, Door, false))
	{
		return false;
	}
	++BreachStarts;
	BreachTraces.FindOrAdd(&Operative).Reset();
	UE_LOG(LogCodexTactics, Display, TEXT("Doors: %s's way to (%.0f, %.0f) ends at the locked %s - he breaches it (%s first)"), *Operative.GetName(), Goal.X, Goal.Y,
		*Door.GetName(), DoorSubsystemLocal::BreachName(Order.Action));
	Orders.Add(Order);
	if (IsInReach(Orders.Last(), Operative, Door) && Act(Orders.Last()) && Conclude(Orders.Last()))
	{
		Orders.Pop();
	}
	return true;
}

void UDoorSubsystem::TickBreachDetection(float DeltaTime)
{
	const FDoorTuning& Tuning = DoorTuning::Get();
	BreachCheckIn -= DeltaTime;
	if (!Tuning.bBreachEnabled || BreachCheckIn > 0.f)
	{
		return;
	}
	BreachCheckIn = Tuning.BreachIntervalSeconds;
	const double Now = GetWorld()->GetTimeSeconds();
	const auto GaveUp = [this, Now](const AActor* Pawn, const ADoorActor* Door)
	{
		for (const TPair<TWeakObjectPtr<const AActor>, TMap<TWeakObjectPtr<const ADoorActor>, double>>& Pair : GiveUps)
		{
			if (Pair.Key.Get() == Pawn)
			{
				const double* Until = Pair.Value.Find(Door);
				return Until && *Until > Now;
			}
		}
		return false;
	};
	bool bAnyLocked = false;
	for (const ADoorActor* Door : Doors)
	{
		bAnyLocked |= Door && Door->GetDoorState() == EDoorLeafState::Locked && !Door->IsBroken();
	}
	if (!bAnyLocked)
	{
		return;
	}
	// Hostile operatives (PMC fighters: the operative body, the operative's door work); the move request hook catches most at once.
	for (TActorIterator<AOperativeCharacter> It(GetWorld()); It; ++It)
	{
		if (!It->IsPlayerTeam())
		{
			ConsiderBreach(**It);
		}
	}
	// Monsters: bash in place (or a carried key) once they stand at the door their path ends at.
	for (TActorIterator<AEnemyCharacter> It(GetWorld()); It; ++It)
	{
		AEnemyCharacter* Enemy = *It;
		if (!DoorSubsystemLocal::IsAlive(Enemy) || IsBreaching(Enemy))
		{
			continue;
		}
		const TArray<EDoorAction>& Actions = DoorRules::GetBreachActions(Tuning, DoorRules::GetArchetypeKey(false, Enemy->GetArchetype()));
		if (Actions.Num() == 0)
		{
			continue;
		}
		// Its path is seen stopped by the door while it walks; it may stand at the path's end (idle) by the next look - the note keeps it.
		FVector Goal;
		ADoorActor* Door = FindBlockingDoor(*Enemy, Goal);
		if (Door)
		{
			BlockedNotes.Add(Enemy, { Door, Now });
		}
		else if (const FBlockedNote* Note = BlockedNotes.Find(Enemy); Note && Now - Note->Time <= DoorSubsystemLocal::BlockedNoteSeconds)
		{
			Door = Note->Door.Get();
		}
		if (!Door || Door->GetDoorState() != EDoorLeafState::Locked || GaveUp(Enemy, Door)
			|| FVector::Dist2D(DoorSubsystemLocal::FeetOf(*Enemy), Door->GetActorLocation()) > Tuning.InteractReachCm + DoorSubsystemLocal::BashReachSlackCm)
		{
			continue;
		}
		BlockedNotes.Remove(Enemy);
		FDoorBreachAbility Ability;
		Ability.bHasKey = PawnHasKeyFor(Enemy, *Door);
		Ability.bCanShoot = false; // monsters (and the marksman's own brain) never shoot locks: R2 scope
		Ability.bHasGrenade = false;
		FBash Bash;
		Bash.Pawn = Enemy;
		Bash.Door = Door;
		if (!DoorRules::NextBreachAction(Actions, TSet<EDoorAction>(), Ability, Tuning, Bash.Action))
		{
			GiveUps.FindOrAdd(Enemy).Add(Door, Now + Tuning.BreachGiveUpSeconds);
			continue;
		}
		Bash.WorkLeft = Bash.Action == EDoorAction::Unlock ? Tuning.UnlockSeconds : Tuning.KickSeconds;
		++BreachStarts;
		BreachTraces.FindOrAdd(Enemy).Reset();
		UE_LOG(LogCodexTactics, Display, TEXT("Doors: %s (%s) stands at the locked %s - %s"), *Enemy->GetName(),
			*DoorRules::GetArchetypeKey(false, Enemy->GetArchetype()), *Door->GetName(), Bash.Action == EDoorAction::Unlock ? TEXT("its key") : TEXT("it bashes it"));
		Bashes.Add(Bash);
	}
}

void UDoorSubsystem::TickBashes(float DeltaTime)
{
	const FDoorTuning& Tuning = DoorTuning::Get();
	for (int32 Index = Bashes.Num() - 1; Index >= 0; --Index)
	{
		FBash& Bash = Bashes[Index];
		ACharacter* Pawn = Bash.Pawn.Get();
		ADoorActor* Door = Bash.Door.Get();
		if (!Pawn || !Door || !DoorSubsystemLocal::IsAlive(Pawn) || Door->GetDoorState() == EDoorLeafState::Open
			|| FVector::Dist2D(DoorSubsystemLocal::FeetOf(*Pawn), Door->GetActorLocation()) > Tuning.InteractReachCm + DoorSubsystemLocal::BashReachSlackCm * 2.f)
		{
			Bashes.RemoveAt(Index);
			continue;
		}
		Bash.WorkLeft -= DeltaTime;
		if (Bash.WorkLeft > 0.f)
		{
			continue;
		}
		bool bOpened = false;
		if (Bash.Action == EDoorAction::Unlock)
		{
			bOpened = Door->UnlockDoor(Pawn) && Door->OpenDoor(Pawn);
			AddTrace(Pawn, TEXT("key"));
		}
		else
		{
			const FDoorKickOutcome Kick = DoorRules::ResolveKick(Door->GetDoorState(), Door->IsHeavy(), 1.f,
				RollFor(KickRollForTesting, *Door, *Pawn, Bash.Attempts, TEXT("kick")), 1.f, Tuning);
			++Bash.Attempts;
			++KickAttempts;
			UE_LOG(LogCodexTactics, Display, TEXT("Doors: %s bashes %s (attempt %d/%d): open %.2f -> %s"), *Pawn->GetName(), *Door->GetName(), Bash.Attempts,
				Tuning.KickMaxAttempts, Kick.OpenChance, Kick.bOpens ? TEXT("gives") : TEXT("holds"));
			AddTrace(Pawn, Kick.bOpens ? TEXT("kick:gives") : TEXT("kick:holds"));
			bOpened = Door->KickDoor(Pawn, Kick.bOpens);
		}
		if (bOpened || Door->IsOpen())
		{
			++BreachOpens;
			if (AAIController* AI = Cast<AAIController>(Pawn->GetController()))
			{
				AI->StopMovement(); // its own AI asks the walk again (the doorway is free now)
			}
			Bashes.RemoveAt(Index);
			continue;
		}
		if (Bash.Action == EDoorAction::Unlock || Bash.Attempts >= Tuning.KickMaxAttempts)
		{
			AddTrace(Pawn, TEXT("gave-up"));
			GiveUps.FindOrAdd(Pawn).Add(Door, GetWorld()->GetTimeSeconds() + Tuning.BreachGiveUpSeconds);
			Bashes.RemoveAt(Index);
			continue;
		}
		Bash.WorkLeft = Tuning.KickSeconds;
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
		TEXT("Testing; in the game keys are loot (containers, searched bodies) and mission goal rewards."),
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
