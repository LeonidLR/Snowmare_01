// Dev-only headless checks of the user's PMC combat bugs on Depot (2026-11-07, docs/systems/pmc-squad-ai.md), each on L_MovementTest
// in a runtime arena (SmokeUtils::FindClearArena):
//   Scripts/smoke_parallel.ps1 -Commands PmcDeathPoseSmoke,FireArcSmoke,PmcUnderFireSmoke
// PmcDeathPoseSmoke (#44 "a killed PMC keeps STANDING in the aim pose"): a hostile PMC fighter aiming at the squad is killed - he plays a
//   death clip / the knockdown fall on the FullBody slot, his pelvis drops to the ground, the aim is cleared, the corpse stays shown;
//   a squad member killed the same way is the control.
//   Bug 2026-11-08 (the orange silhouette on a dead fighter): the bodies are seen only through roof slabs (the silhouette on), the target
//   highlight flashes at the kill, the fighters die concealed and are shown 0.5 s later - no overlay on any of their meshes 1 s / 4 s after.
// FireArcSmoke (#33 "a PRONE PMC faces one way but fires in another"): a prone fighter with the target behind his back turns first
//   (at the prone turn rate) and fires only with his body / rifle on the target; a prone operative of the squad the same.
// PmcUnderFireSmoke (#43 "the column in a passage does not react to a PMC firing from a corner"): see its own comment below.
// PmcFireLineSmoke (designer PIE 2026-11-08: a prone PMC fighter in the open kept firing into the wall the squad stood behind): see below.
// CornerExitSmoke (RecoilProbe 2026-11-08: a 50-60 cm sideways jump of the upper body leaving the corner fire pose): see below.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AI/PmcSquadSubsystem.h"
#include "Animation/AnimMontage.h"
#include "Characters/OperativeAnimInstance.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/OperativeMovementRules.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/CombatFeedbackSubsystem.h"
#include "Combat/EnemyRegistrySubsystem.h"
#include "Combat/HealthComponent.h"
#include "Combat/TacticalSightSubsystem.h"
#include "Combat/SuppressionRules.h"
#include "Combat/UnderFireRules.h"
#include "Characters/EnemyCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Data/WeaponDataAsset.h"
#include "Debug/SmokeUtils.h"
#include "Misc/CoreDelegates.h"
#include "Tactics/CoverTraceRules.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"

namespace PmcCombatBugsSmoke
{
	constexpr float StepSeconds = 0.05f;

	struct FShotSample
	{
		float Time = 0.f;
		float BodyErrorDeg = 0.f;
		float BarrelErrorDeg = -1.f;
	};

	struct FState
	{
		FString Name;
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		float Limit = 90.f;
		FVector O = FVector::ZeroVector;
		FVector F = FVector::ForwardVector;
		FVector R = FVector::RightVector;
		TArray<TWeakObjectPtr<AOperativeCharacter>> Members;
		TArray<TWeakObjectPtr<AOperativeCharacter>> Hostiles;
		TArray<TWeakObjectPtr<AStaticMeshActor>> Blocks;
		// PmcDeathPoseSmoke
		float PelvisBefore[2] = { 0.f, 0.f };
		/** The bug 2026-11-08 check: the fighters' components hidden at the kill (their concealment), shown again 0.5 s later. */
		TArray<TWeakObjectPtr<USceneComponent>> ConcealedAtKill;
		bool bOverlaysChecked = false;
		// FireArcSmoke
		TWeakObjectPtr<AOperativeCharacter> Shooter;
		TWeakObjectPtr<AActor> ShotTarget;
		FDelegateHandle ShotHandle;
		TArray<FShotSample> Shots;
		float InitialErrorDeg = 0.f;
		double WatchStart = 0.0;
		float NextLog = 0.f;
		float NextForcedShot = 0.f;
		// PmcUnderFireSmoke
		FVector Goal = FVector::ZeroVector;
		TArray<double> ShotTimeBefore;
		TArray<float> WorstFacing;
		int32 ReturnShots = 0;
		bool bTurned[3] = { false, false, false };
		float FirstTurn[3] = { -1.f, -1.f, -1.f };
		bool bLeaderMovedUnderFire = false;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State, bool bComplete)
	{
		if (AOperativeCharacter* Shooter = State.Shooter.Get(); Shooter && State.ShotHandle.IsValid())
		{
			Shooter->OnWeaponFiredNative.Remove(State.ShotHandle);
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, *State.Name);
		return false;
	}

	void Next(FState& State)
	{
		++State.Stage;
		State.StageTime = 0.f;
	}

	AStaticMeshActor* SpawnBlock(UWorld* World, const FVector& Centre, const FRotator& Rotation, const FVector& SizeCm)
	{
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		const FTransform Transform(Rotation, Centre, SizeCm / 100.f);
		AStaticMeshActor* Block = Cube ? World->SpawnActorDeferred<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Transform, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn) : nullptr;
		if (!Block)
		{
			return nullptr;
		}
		Block->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
		Block->GetStaticMeshComponent()->SetStaticMesh(Cube);
		Block->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
		Block->GetStaticMeshComponent()->SetCanEverAffectNavigation(true);
		Block->FinishSpawning(Transform);
		return Block;
	}

	/** Bones refreshed every frame headless (-nullrhi renders nothing): the pelvis / the rifle are where the pose puts them. */
	void KeepPoseLive(AOperativeCharacter* Operative)
	{
		if (USkeletalMeshComponent* Body = Operative ? Operative->GetMesh() : nullptr)
		{
			Body->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		}
	}

	void MakeTough(AOperativeCharacter* Operative)
	{
		if (Operative && Operative->HealthComponent)
		{
			Operative->HealthComponent->SetMaxHealth(100000.f, false);
			Operative->HealthComponent->Heal(100000.f);
		}
	}

	/** Planar angle between a direction and the line from a point to a target, deg. */
	float PlanarErrorDeg(const FVector& From, const FVector& Direction, const FVector& Target)
	{
		const FVector To = (Target - From).GetSafeNormal2D();
		const FVector Dir = Direction.GetSafeNormal2D();
		return To.IsNearlyZero() || Dir.IsNearlyZero() ? 180.f : FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(To, Dir), -1.f, 1.f)));
	}

	/** His rifle's planar error to a point (the barrel as animated), -1 without a level rifle. */
	float BarrelErrorDeg(const AOperativeCharacter& Operative, const FVector& Target)
	{
		const UStaticMeshComponent* Weapon = Operative.WeaponMesh;
		if (!Weapon || !Weapon->GetStaticMesh() || !Weapon->IsVisible())
		{
			return -1.f;
		}
		const FVector Barrel = Weapon->GetComponentTransform().TransformVectorNoScale(Operative.MuzzleOffset.GetSafeNormal());
		return Barrel.Size2D() < 0.3f ? -1.f : PlanarErrorDeg(Operative.GetActorLocation(), Barrel, Target);
	}

	/** The arena, a real-time fight, the level's enemies parked, the squad placed in a column along F at the arena origin, all tough. */
	bool SetUp(UWorld* World, FState& State, const FBox2D& Box)
	{
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		UPmcSquadSubsystem* Pmc = UPmcSquadSubsystem::Get(World);
		if (!Squad || !Flow || !Pmc || Squad->GetMembers().Num() < 3)
		{
			Check(State, false, TEXT("squad of 3, game flow and the PMC subsystem present"));
			return false;
		}
		TArray<AOperativeCharacter*> Roster = Squad->GetMembers();
		Roster.Sort([](const AOperativeCharacter& A, const AOperativeCharacter& B) { return A.SquadIndex < B.SquadIndex; });
		TArray<const AActor*> Ignore;
		for (const AOperativeCharacter* Member : Roster)
		{
			Ignore.Add(Member);
		}
		const FVector Feet = Roster[0]->GetActorLocation() - FVector(0.f, 0.f, Roster[0]->GetSimpleCollisionHalfHeight());
		const SmokeUtils::FClearArena Arena = SmokeUtils::FindClearArena(World, Feet, Roster[0]->GetActorForwardVector(), Box, Ignore);
		Check(State, Arena.bFound, FString::Printf(TEXT("an empty arena found (tried %d)"), Arena.Tried));
		if (!Arena.bFound)
		{
			return false;
		}
		State.O = Arena.Origin;
		State.F = Arena.Forward;
		State.R = Arena.Right;
		SmokeUtils::StartWaveFight(Flow);
		for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
		{
			SmokeUtils::ParkEnemyFarAway(*It);
			It->SetActorTickEnabled(false);
		}
		Squad->SetSquadPosture(ESquadFirePosture::Aggressive);
		Squad->SetFollowersHolding(true);
		for (int32 Index = 0; Index < Roster.Num(); ++Index)
		{
			AOperativeCharacter* Member = Roster[Index];
			Member->StopOperative();
			Member->ColdLevel = 0.f;
			MakeTough(Member);
			KeepPoseLive(Member);
			const FVector Spot = SmokeUtils::FreeSpot(World, State.O - State.F * (Index * 300.f), Member);
			Member->TeleportTo(FVector(Spot.X, Spot.Y, Spot.Z + 4.f), State.F.Rotation(), false, true);
			State.Members.Add(Member);
		}
		return true;
	}

	AOperativeCharacter* SpawnHostile(UWorld* World, FState& State, const FVector& Spot, const FRotator& Facing, int32 Seed)
	{
		UPmcSquadSubsystem* Pmc = UPmcSquadSubsystem::Get(World);
		const TArray<AOperativeCharacter*> Spawned = Pmc ? Pmc->SpawnSquad(1, SmokeUtils::FreeSpot(World, Spot, nullptr), Facing, 1, Seed) : TArray<AOperativeCharacter*>();
		AOperativeCharacter* Hostile = Spawned.IsEmpty() ? nullptr : Spawned[0];
		if (Hostile)
		{
			Pmc->SetTeamCommanded(1, false); // no commander orders: he only auto-fires (the rules under test)
			KeepPoseLive(Hostile);
			State.Hostiles.Add(Hostile);
		}
		return Hostile;
	}

	float BoneHeight(const AOperativeCharacter& Operative, const TCHAR* Bone)
	{
		const USkeletalMeshComponent* Body = Operative.GetMesh();
		const float FeetZ = Operative.GetActorLocation().Z - Operative.GetSimpleCollisionHalfHeight();
		return Body && Body->GetBoneIndex(Bone) != INDEX_NONE ? Body->GetBoneLocation(Bone).Z - FeetZ : -1.f;
	}

	/** His head over his feet, cm (the corpse check: standing ~165, kneeling ~110, lying < 50). */
	float PelvisHeight(const AOperativeCharacter& Operative)
	{
		return BoneHeight(Operative, TEXT("head"));
	}

	FString PoseHeights(const AOperativeCharacter& Operative)
	{
		const USkeletalMeshComponent* Body = Operative.GetMesh();
		const float FeetZ = Operative.GetActorLocation().Z - Operative.GetSimpleCollisionHalfHeight();
		return FString::Printf(TEXT("pelvis %.0f, spine_03 %.0f, head %.0f, bounds top %.0f cm, %d bones, mesh %s"), BoneHeight(Operative, TEXT("pelvis")),
			BoneHeight(Operative, TEXT("spine_03")), BoneHeight(Operative, TEXT("head")), Body ? Body->Bounds.GetBox().Max.Z - FeetZ : -1.f,
			Body ? Body->GetNumBones() : 0, Body && Body->GetSkeletalMeshAsset() ? *Body->GetSkeletalMeshAsset()->GetName() : TEXT("-"));
	}

	FString FullBodyState(const AOperativeCharacter& Operative, float& OutWeight)
	{
		OutWeight = 0.f;
		const UOperativeAnimInstance* Anim = Operative.GetMesh() ? Cast<UOperativeAnimInstance>(Operative.GetMesh()->GetAnimInstance()) : nullptr;
		if (!Anim)
		{
			return TEXT("no operative anim instance");
		}
		FString Clips;
		for (const FAnimMontageInstance* Instance : Anim->MontageInstances)
		{
			if (Instance && Instance->Montage && Instance->IsActive())
			{
				const UAnimSequenceBase* Inner = Instance->Montage->SlotAnimTracks.IsEmpty() || Instance->Montage->SlotAnimTracks[0].AnimTrack.AnimSegments.IsEmpty()
					? nullptr : Instance->Montage->SlotAnimTracks[0].AnimTrack.AnimSegments[0].GetAnimReference();
				Clips += FString::Printf(TEXT("%s%s on %s"), Clips.IsEmpty() ? TEXT("") : TEXT(", "), Inner ? *Inner->GetName() : *Instance->Montage->GetName(),
					Instance->Montage->SlotAnimTracks.IsEmpty() ? TEXT("?") : *Instance->Montage->SlotAnimTracks[0].SlotName.ToString());
			}
		}
		OutWeight = Anim->GetSlotMontageGlobalWeight(Anim->FullBodySlot);
		return FString::Printf(TEXT("FullBody weight %.2f, montages [%s], aiming %d, dead %d, anim %s"), OutWeight, *Clips, Anim->bIsAiming ? 1 : 0,
			Anim->bIsDead ? 1 : 0, *Anim->GetName());
	}

	// --- #44 ---------------------------------------------------------------------------------------------------------------------------

	/** Every mesh component of him and his attached actors that carries an overlay material (shown or hidden), as a log line. */
	int32 CountOverlays(const AOperativeCharacter& Operative, FString& OutLine)
	{
		TArray<AActor*> Actors;
		Actors.Add(const_cast<AOperativeCharacter*>(&Operative));
		Operative.GetAttachedActors(Actors, false, true);
		int32 Count = 0;
		for (const AActor* Actor : Actors)
		{
			TArray<UMeshComponent*> Meshes;
			Actor->GetComponents(Meshes);
			for (const UMeshComponent* Mesh : Meshes)
			{
				if (Mesh && Mesh->GetOverlayMaterial())
				{
					++Count;
					OutLine += FString::Printf(TEXT(" %s=%s%s;"), *Mesh->GetName(), *GetNameSafe(Mesh->GetOverlayMaterial()), Mesh->bHiddenInGame ? TEXT(" (hidden)") : TEXT(""));
				}
			}
		}
		return Count;
	}

	/**
	 * Bug 2026-11-08: a roof slab between the camera and his body centre (1 m over his feet), 2 m above it along the camera's line - the
	 * see-through silhouette comes on (the camera's view of him is blocked, as by a wall).
	 */
	void OccludeFromCamera(UWorld* World, FState& State, const AOperativeCharacter& Operative)
	{
		const APlayerController* PC = World->GetFirstPlayerController();
		const FVector Centre = Operative.GetActorLocation() - FVector(0.f, 0.f, Operative.GetSimpleCollisionHalfHeight() - 100.f);
		const FVector Camera = PC && PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraLocation() : Centre + FVector(-800.f, 0.f, 1500.f);
		const float Rise = FMath::Max(static_cast<float>(Camera.Z - Centre.Z), 300.f);
		const FVector Slab = Centre + (Camera - Centre) * (200.f / Rise);
		State.Blocks.Add(SpawnBlock(World, Slab, FRotator::ZeroRotator, FVector(260.f, 260.f, 20.f)));
	}

	bool StepDeathPose(UWorld* World, FState& State)
	{
		// Four bodies: a crouched (as spawned) and a standing PMC fighter, a crouched and a standing operative (the controls).
		auto Bodies = [&State]()
		{
			TArray<AOperativeCharacter*> Out;
			Out.Add(State.Hostiles.IsValidIndex(0) ? State.Hostiles[0].Get() : nullptr);
			Out.Add(State.Hostiles.IsValidIndex(1) ? State.Hostiles[1].Get() : nullptr);
			Out.Add(State.Members.IsValidIndex(1) ? State.Members[1].Get() : nullptr);
			Out.Add(State.Members.IsValidIndex(2) ? State.Members[2].Get() : nullptr);
			return Out;
		};
		static const TCHAR* Who[4] = { TEXT("the crouched PMC fighter"), TEXT("the second PMC fighter"), TEXT("the crouched operative"),
			TEXT("the standing operative") };
		static float HeadBefore[4] = { 0.f, 0.f, 0.f, 0.f };
		switch (State.Stage)
		{
		case 0:
		{
			if (State.StageTime < 2.f)
			{
				return true;
			}
			if (!SetUp(World, State, FBox2D(FVector2D(-900.f, -700.f), FVector2D(1600.f, 700.f))))
			{
				return Finish(State, false);
			}
			{
				// The bodies must die from their stances: no prone drop under the smoke's rounds (the manoeuvre under fire, #43).
				FUnderFireConfig NoManoeuvre = UnderFireRules::GetConfig();
				NoManoeuvre.ManoeuvreEnabled = 0.f;
				UnderFireRules::SetConfig(NoManoeuvre);
			}
			for (int32 Index = 1; Index < 3; ++Index)
			{
				if (AOperativeCharacter* Control = State.Members[Index].Get())
				{
					const FVector Spot = SmokeUtils::FreeSpot(World, State.O + State.R * (Index == 1 ? 300.f : -300.f), Control);
					Control->TeleportTo(FVector(Spot.X, Spot.Y, Spot.Z + 4.f), State.F.Rotation(), false, true);
					Control->SetStance(Index == 1 ? EOperativeStance::Crouching : EOperativeStance::Standing);
				}
			}
			AOperativeCharacter* Crouched = SpawnHostile(World, State, State.O + State.F * 1200.f - State.R * 200.f, (-State.F).Rotation(), 7);
			AOperativeCharacter* Standing = SpawnHostile(World, State, State.O + State.F * 1200.f + State.R * 200.f, (-State.F).Rotation(), 8);
			Check(State, Crouched && Standing, TEXT("two hostile PMC fighters 12 m ahead"));
			if (!Crouched || !Standing)
			{
				return Finish(State, false);
			}
			Crouched->SetStance(EOperativeStance::Crouching);
			Standing->SetStance(EOperativeStance::Standing);
			// Bug 2026-11-08: every body seen by the camera only through a roof slab (the silhouette on at the kill).
			OccludeFromCamera(World, State, *Crouched);
			OccludeFromCamera(World, State, *Standing);
			for (int32 Index = 1; Index < 3; ++Index)
			{
				if (const AOperativeCharacter* Control = State.Members[Index].Get())
				{
					OccludeFromCamera(World, State, *Control);
				}
			}
			Next(State);
			return true;
		}
		case 1:
		{
			const TArray<AOperativeCharacter*> All = Bodies();
			AOperativeCharacter* Leader = State.Members[0].Get();
			if (All.Contains(nullptr) || !Leader)
			{
				return Finish(State, false);
			}
			// Everybody aims: a round at the leader each 0.4 s (the aim pose holds for a while after a shot).
			if (State.StageTime >= State.NextForcedShot)
			{
				State.NextForcedShot = State.StageTime + 0.4f;
				for (AOperativeCharacter* Body : All)
				{
					Body->bTacticalCeaseFire = true;
					if (!Body->IsPlayerTeam())
					{
						Body->ShootAtTarget(Leader, 1.f); // the controls do not fire: their rounds knocked a fighter down
					}
				}
			}
			if (State.StageTime < 2.5f)
			{
				return true;
			}
			for (int32 Index = 0; Index < 4; ++Index)
			{
				float Weight = 0.f;
				const UOperativeAnimInstance* Anim = Cast<UOperativeAnimInstance>(All[Index]->GetMesh()->GetAnimInstance());
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke: %s before the kill: %s; %s"), Who[Index], *FullBodyState(*All[Index], Weight), *PoseHeights(*All[Index]));
				Check(State, Index >= 2 || (Anim && Anim->bIsAiming), FString::Printf(TEXT("%s aims / stands ready (stance %d)"), Who[Index],
					static_cast<int32>(All[Index]->GetStance())));
				HeadBefore[Index] = PelvisHeight(*All[Index]);
			}
			// Bug 2026-11-08 (the game designer's screenshot: a killed PMC fighter lying on the ground kept the orange silhouette): the kill
			// as in the game - each body behind the slab shows the see-through silhouette, the shot's target highlight flashes over it, and
			// the two fighters die concealed (their components hidden, as UTacticalSightSubsystem hides an unseen PMC fighter) - the fallen
			// are shown again 0.5 s later.
			UCombatFeedbackSubsystem* Feedback = World->GetSubsystem<UCombatFeedbackSubsystem>();
			State.ConcealedAtKill.Reset();
			for (int32 Index = 0; Index < 4; ++Index)
			{
				FString Line;
				const int32 Count = CountOverlays(*All[Index], Line);
				// The fighters' silhouette is the case under test (a setup check); the controls' depends on the camera angle (logged).
				const FString What = FString::Printf(TEXT("%s behind the slab shows the see-through silhouette before the kill (%d overlay(s):%s)"), Who[Index], Count, *Line);
				if (Index < 2)
				{
					Check(State, All[Index]->IsSilhouetteVisible() && Count > 0, What);
					// The game designer's decision 2026-11-08: a hostile (PMC) silhouette is RED - on the material the overlay shows, not a role colour.
					const UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(All[Index]->GetMesh()->GetOverlayMaterial());
					FLinearColor Shown = FLinearColor::Transparent;
					const bool bShown = MID && MID->GetVectorParameterValue(TEXT("Color"), Shown);
					const FLinearColor Wanted = All[Index]->GetSilhouetteColor();
					Check(State, !All[Index]->IsPlayerTeam() && bShown && Shown.Equals(Wanted, 0.01f) && Wanted.Equals(AOperativeCharacter::HostileSilhouetteColor, 0.01f)
						&& Shown.R > 0.5f && Shown.G < 0.2f && Shown.B < 0.2f,
						FString::Printf(TEXT("%s (TeamId %d): its live silhouette is RED (%.2f, %.2f, %.2f)"), Who[Index], All[Index]->TeamId, Shown.R, Shown.G, Shown.B));
				}
				else
				{
					UE_LOG(LogCodexTactics, Display, TEXT("Smoke: %s"), *What);
					// The squad's own operatives keep their role colours (the leader cyan, the engineer amber, the medic green).
					const FLinearColor Own = All[Index]->GetSilhouetteColor();
					Check(State, All[Index]->IsPlayerTeam() && !Own.Equals(AOperativeCharacter::HostileSilhouetteColor, 0.01f),
						FString::Printf(TEXT("%s (the player's side) keeps its role colour (%.2f, %.2f, %.2f)"), Who[Index], Own.R, Own.G, Own.B));
				}
				if (Feedback)
				{
					Feedback->HighlightTarget(All[Index]);
				}
				if (Index < 2)
				{
					TArray<USceneComponent*> Components;
					All[Index]->GetComponents<USceneComponent>(Components);
					for (USceneComponent* Component : Components)
					{
						if (Component && !Component->bHiddenInGame)
						{
							Component->SetHiddenInGame(true);
							State.ConcealedAtKill.Add(Component);
						}
					}
				}
			}
			for (AOperativeCharacter* Body : All)
			{
				Body->TakeHit(10000000.f, TEXT("Smoke"), false, true);
			}
			State.bOverlaysChecked = false;
			Next(State);
			return true;
		}
		case 2:
		{
			const TArray<AOperativeCharacter*> All = Bodies();
			if (State.StageTime < 0.5f && State.StageTime + StepSeconds >= 0.5f)
			{
				for (int32 Index = 0; Index < 4; ++Index)
				{
					float Weight = 0.f;
					UE_LOG(LogCodexTactics, Display, TEXT("Smoke: %s 0.5 s after: %s; %s"), Who[Index], All[Index] ? *FullBodyState(*All[Index], Weight) : TEXT("-"),
						All[Index] ? *PoseHeights(*All[Index]) : TEXT("-"));
				}
			}
			if (State.StageTime >= 0.5f && !State.ConcealedAtKill.IsEmpty())
			{
				for (const TWeakObjectPtr<USceneComponent>& Weak : State.ConcealedAtKill)
				{
					if (USceneComponent* Component = Weak.Get())
					{
						Component->SetHiddenInGame(false); // the fallen are shown (UTacticalSightSubsystem: a kill is seen)
					}
				}
				State.ConcealedAtKill.Reset();
			}
			if (State.StageTime >= 1.f && !State.bOverlaysChecked)
			{
				State.bOverlaysChecked = true;
				for (int32 Index = 0; Index < 4; ++Index)
				{
					FString Line;
					const int32 Count = All[Index] ? CountOverlays(*All[Index], Line) : -1;
					Check(State, Count == 0, FString::Printf(TEXT("%s's corpse 1 s after the kill (shown again, still behind the slab): no overlay on any mesh (%d:%s)"),
						Who[Index], Count, *Line));
				}
			}
			if (State.StageTime < 4.f) // the death cinematic slows the game for a moment
			{
				return true;
			}
			for (int32 Index = 0; Index < 4; ++Index)
			{
				FString Line;
				const int32 Count = All[Index] ? CountOverlays(*All[Index], Line) : -1;
				Check(State, Count == 0 && All[Index] && !All[Index]->IsSilhouetteVisible(),
					FString::Printf(TEXT("%s's corpse 4 s after the kill: no silhouette / highlight overlay (%d:%s)"), Who[Index], Count, *Line));
			}
			for (int32 Index = 0; Index < 4; ++Index)
			{
				const AOperativeCharacter* Dead = All[Index];
				if (!Dead)
				{
					Check(State, false, FString::Printf(TEXT("%s's body still exists"), Who[Index]));
					continue;
				}
				float Weight = 0.f;
				const FString Anim = FullBodyState(*Dead, Weight);
				const float Head = PelvisHeight(*Dead);
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke: %s dead: %s; %s"), Who[Index], *Anim, *PoseHeights(*Dead));
				Check(State, !Dead->HealthComponent->IsAlive(), FString::Printf(TEXT("%s is dead"), Who[Index]));
				Check(State, Weight > 0.9f, FString::Printf(TEXT("%s: a death clip / fall holds the FullBody slot (%s)"), Who[Index], *Anim));
				Check(State, Head >= 0.f && Head < 60.f && Head < HeadBefore[Index] - 40.f,
					FString::Printf(TEXT("%s lies on the ground (head %.0f -> %.0f cm over his feet)"), Who[Index], HeadBefore[Index], Head));
				const UOperativeAnimInstance* DeadAnim = Cast<UOperativeAnimInstance>(Dead->GetMesh()->GetAnimInstance());
				Check(State, DeadAnim && !DeadAnim->bIsAiming, FString::Printf(TEXT("%s no longer aims"), Who[Index]));
				Check(State, Dead->GetMesh()->IsVisible() && !Dead->GetMesh()->bHiddenInGame && !Dead->IsHidden(),
					FString::Printf(TEXT("%s's corpse stays shown"), Who[Index]));
			}
			// Part B (the Depot case): a PMC fighter at a wall corner in the corner fire stance, killed there.
			AOperativeCharacter* Leader = State.Members[0].Get();
			const FVector P = State.O + State.F * 1700.f;
			State.Blocks.Add(SpawnBlock(World, P - State.F * 415.f + FVector(0.f, 0.f, 150.f), State.F.Rotation(), FVector(30.f, 600.f, 300.f)));
			AOperativeCharacter* Cornered = SpawnHostile(World, State, P, (-State.F).Rotation(), 9);
			if (!Leader || !Cornered)
			{
				Check(State, false, TEXT("part B: the leader and a third PMC fighter"));
				return Finish(State, false);
			}
			MakeTough(Cornered);
			Cornered->SetStance(EOperativeStance::Standing);
			Leader->bTacticalCeaseFire = true;
			const FVector LeaderSpot = SmokeUtils::FreeSpot(World, P - State.F * 1000.f + State.R * 650.f, Leader);
			Leader->TeleportTo(FVector(LeaderSpot.X, LeaderSpot.Y, LeaderSpot.Z + 4.f), State.F.Rotation(), false, true);
			Next(State);
			return true;
		}
		case 3:
		{
			AOperativeCharacter* Cornered = State.Hostiles.IsValidIndex(2) ? State.Hostiles[2].Get() : nullptr;
			if (!Cornered || State.StageTime < 1.f)
			{
				return Cornered ? true : Finish(State, false);
			}
			FCoverSlot Slot;
			const FVector P = State.O + State.F * 1700.f;
			const bool bFound = CoverTraceRules::FindCoverSlotAt(World, P - State.F * 380.f + State.R * 250.f + FVector(0.f, 0.f, 90.f), -State.F, Slot);
			Check(State, bFound && Cornered->OrderTakeCover(Slot, false) == EOperativeOrderResult::Accepted, TEXT("part B: the fighter takes cover at the wall's corner"));
			State.NextForcedShot = 0.f;
			Next(State);
			return true;
		}
		case 4:
		{
			AOperativeCharacter* Cornered = State.Hostiles[2].Get();
			AOperativeCharacter* Leader = State.Members[0].Get();
			if (!Cornered || !Leader)
			{
				return Finish(State, false);
			}
			// The leader's rounds at him give the leader away round the corner: he leans out into the corner fire stance at him.
			if (State.StageTime >= State.NextForcedShot && Cornered->bInCover)
			{
				State.NextForcedShot = State.StageTime + 0.7f;
				Leader->ShootAtTarget(Cornered, 1.f);
			}
			const bool bFiring = Cornered->bInCover && Cornered->IsCornerAimActive() && World->GetTimeSeconds() - Cornered->GetLastShotTime() < 1.0;
			if (!bFiring && State.StageTime < 15.f)
			{
				return true;
			}
			float Weight = 0.f;
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: the cornered fighter before the kill: %s; %s"), *FullBodyState(*Cornered, Weight), *PoseHeights(*Cornered));
			Check(State, bFiring, FString::Printf(TEXT("part B: he fires from the corner stance (in cover %d, corner aim %d)"), Cornered->bInCover ? 1 : 0,
				Cornered->IsCornerAimActive() ? 1 : 0));
			HeadBefore[0] = PelvisHeight(*Cornered);
			Cornered->TakeHit(10000000.f, TEXT("Smoke"), false, true);
			Next(State);
			return true;
		}
		case 5:
		{
			const AOperativeCharacter* Dead = State.Hostiles[2].Get();
			if (!Dead)
			{
				return Finish(State, false);
			}
			if (State.StageTime < 0.5f && State.StageTime + StepSeconds >= 0.5f)
			{
				float Weight = 0.f;
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke: the cornered fighter 0.5 s after: %s; %s"), *FullBodyState(*Dead, Weight), *PoseHeights(*Dead));
			}
			if (State.StageTime < 4.f)
			{
				return true;
			}
			float Weight = 0.f;
			const FString Anim = FullBodyState(*Dead, Weight);
			const float Head = PelvisHeight(*Dead);
			const UOperativeAnimInstance* DeadAnim = Cast<UOperativeAnimInstance>(Dead->GetMesh()->GetAnimInstance());
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: the cornered fighter dead: %s; %s; corpse pose re-held %d time(s)"), *Anim, *PoseHeights(*Dead),
				DeadAnim ? DeadAnim->DeathPoseRestores : -1);
			Check(State, !Dead->bInCover, TEXT("part B: the dead fighter left the cover"));
			Check(State, Weight > 0.9f && !Anim.Contains(TEXT("cvr_")), FString::Printf(TEXT("part B: a death clip holds the FullBody slot, no cover clip (%s)"), *Anim));
			Check(State, Head >= 0.f && Head < 60.f && Head < HeadBefore[0] - 40.f,
				FString::Printf(TEXT("part B: killed in the corner stance he lies on the ground (head %.0f -> %.0f cm)"), HeadBefore[0], Head));
			return Finish(State, true);
		}
		default:
			return Finish(State, false);
		}
	}

	// --- #33 ---------------------------------------------------------------------------------------------------------------------------

	void WatchShots(FState& State, AOperativeCharacter* Shooter, AActor* Target)
	{
		if (AOperativeCharacter* Old = State.Shooter.Get(); Old && State.ShotHandle.IsValid())
		{
			Old->OnWeaponFiredNative.Remove(State.ShotHandle);
		}
		State.Shots.Reset();
		State.Shooter = Shooter;
		State.ShotTarget = Target;
		State.InitialErrorDeg = PlanarErrorDeg(Shooter->GetActorLocation(), Shooter->GetActorForwardVector(), Target->GetActorLocation());
		State.WatchStart = Shooter->GetWorld()->GetTimeSeconds(); // game time: the fixed-step game runs faster than the ticker
		FState* Raw = &State;
		State.ShotHandle = Shooter->OnWeaponFiredNative.AddLambda([Raw](AOperativeCharacter* Who, AActor* At, bool)
		{
			if (Who && At && At == Raw->ShotTarget.Get()) // a round at another target in his sights is no turn under test
			{
				FShotSample Sample;
				Sample.Time = static_cast<float>(Who->GetWorld()->GetTimeSeconds() - Raw->WatchStart);
				Sample.BodyErrorDeg = PlanarErrorDeg(Who->GetActorLocation(), Who->GetActorForwardVector(), At->GetActorLocation());
				Sample.BarrelErrorDeg = BarrelErrorDeg(*Who, At->GetActorLocation());
				Raw->Shots.Add(Sample);
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke: %s fired at %.2f s: body %.0f deg, rifle %.0f deg off the target (stance %d)"),
					*Who->DisplayName.ToString(), Sample.Time, Sample.BodyErrorDeg, Sample.BarrelErrorDeg, static_cast<int32>(Who->GetStance()));
			}
		});
	}

	void LogArc(FState& State)
	{
		const AOperativeCharacter* Shooter = State.Shooter.Get();
		const AActor* Target = State.ShotTarget.Get();
		if (!Shooter || !Target || State.StageTime < State.NextLog)
		{
			return;
		}
		State.NextLog = State.StageTime + 0.25f;
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke: %.2f s %s: body %.0f deg, rifle %.0f deg off the target, residual %.0f, stance %d"), State.StageTime,
			*Shooter->DisplayName.ToString(), PlanarErrorDeg(Shooter->GetActorLocation(), Shooter->GetActorForwardVector(), Target->GetActorLocation()),
			BarrelErrorDeg(*Shooter, Target->GetActorLocation()), Shooter->GetShotAimResidualDeg(Target->GetActorLocation()), static_cast<int32>(Shooter->GetStance()));
	}

	/** The fire-arc rule over the shots watched: he fired, never with his body / rifle off the target, and only after turning. */
	void CheckArc(FState& State, const TCHAR* Who)
	{
		const AOperativeCharacter* Shooter = State.Shooter.Get();
		Check(State, !State.Shots.IsEmpty(), FString::Printf(TEXT("%s fires at the target behind him (%d shots)"), Who, State.Shots.Num()));
		if (State.Shots.IsEmpty() || !Shooter)
		{
			return;
		}
		float WorstBody = 0.f;
		float WorstBarrel = 0.f;
		for (const FShotSample& Shot : State.Shots)
		{
			WorstBody = FMath::Max(WorstBody, Shot.BodyErrorDeg);
			WorstBarrel = FMath::Max(WorstBarrel, Shot.BarrelErrorDeg);
		}
		const float Cone = Shooter->AimConeDeg;
		Check(State, WorstBody <= Cone + 3.f, FString::Printf(TEXT("%s: every shot with the body on the target (worst %.0f deg, cone %.0f)"), Who, WorstBody, Cone));
		Check(State, WorstBarrel <= Cone + 10.f, FString::Printf(TEXT("%s: every shot with the rifle on the target (worst %.0f deg)"), Who, WorstBarrel));
		// Prone he turns at the prone turn rate: the first round leaves no sooner than the turn into the cone takes (20 % slack).
		const float TurnRate = OperativeMovementRules::GetTurnRate(Shooter->MovementConfig, EOperativeStance::Prone);
		const float MinSeconds = 0.8f * FMath::Max(0.f, State.InitialErrorDeg - Cone) / FMath::Max(1.f, TurnRate);
		Check(State, State.Shots[0].Time >= MinSeconds, FString::Printf(TEXT("%s turned first: first shot at %.2f s (the prone turn of %.0f deg at %.0f deg/s takes >= %.2f s)"),
			Who, State.Shots[0].Time, State.InitialErrorDeg, TurnRate, MinSeconds));
	}

	bool StepFireArc(UWorld* World, FState& State)
	{
		LogArc(State);
		switch (State.Stage)
		{
		case 0:
		{
			if (State.StageTime < 2.f)
			{
				return true;
			}
			if (!SetUp(World, State, FBox2D(FVector2D(-1400.f, -700.f), FVector2D(1600.f, 700.f))))
			{
				return Finish(State, false);
			}
			for (const TWeakObjectPtr<AOperativeCharacter>& Member : State.Members)
			{
				Member->bTacticalCeaseFire = true; // part A: nobody of the squad fires back
			}
			// Part A: a prone PMC fighter 10 m ahead with his back to the squad.
			AOperativeCharacter* Hostile = SpawnHostile(World, State, State.O + State.F * 1000.f, State.F.Rotation(), 11);
			Check(State, Hostile != nullptr, TEXT("a PMC fighter 10 m ahead, his back to the squad"));
			if (!Hostile)
			{
				return Finish(State, false);
			}
			MakeTough(Hostile);
			Hostile->SetStance(EOperativeStance::Prone);
			Next(State);
			return true;
		}
		case 1:
		{
			AOperativeCharacter* Hostile = State.Hostiles[0].Get();
			AOperativeCharacter* Leader = State.Members[0].Get();
			if (State.StageTime < 3.f || !Hostile || !Leader)
			{
				return Hostile && Leader ? true : Finish(State, false);
			}
			Check(State, Hostile->GetStance() == EOperativeStance::Prone, TEXT("he lies prone"));
			WatchShots(State, Hostile, Leader);
			// The leader's shot at his back gives the leader away (muzzle flash): he turns round to fire back.
			Leader->bForceHitForTesting = true;
			Leader->ShootAtTarget(Hostile, 1.f);
			Leader->bForceHitForTesting = false;
			State.NextLog = 0.f;
			Next(State);
			return true;
		}
		case 2:
		{
			AOperativeCharacter* Hostile = State.Hostiles[0].Get();
			AOperativeCharacter* Leader = State.Members[0].Get();
			if (!Hostile || !Leader)
			{
				return Finish(State, false);
			}
			if (State.StageTime >= State.NextForcedShot && State.StageTime < 6.f)
			{
				State.NextForcedShot = State.StageTime + 1.f; // he keeps firing: the leader stays given away
				Leader->ShootAtTarget(Hostile, 1.f);
			}
			if (State.Shots.Num() < 4 && State.StageTime < 8.f)
			{
				return true;
			}
			CheckArc(State, TEXT("the prone PMC fighter"));
			// Part B: the squad's leader prone, a PMC fighter firing at his back.
			Hostile->TakeHit(10000000.f, TEXT("Smoke"), false, true); // out of the way: no target ahead of the leader in part B
			Leader->bTacticalCeaseFire = false;
			Leader->SetStance(EOperativeStance::Prone);
			Leader->SetActorRotation(State.F.Rotation());
			AOperativeCharacter* Behind = SpawnHostile(World, State, State.O - State.F * 1000.f + State.R * 200.f, State.F.Rotation(), 13);
			Check(State, Behind != nullptr, TEXT("a PMC fighter 10 m behind the prone leader"));
			if (!Behind)
			{
				return Finish(State, false);
			}
			MakeTough(Behind);
			Behind->bTacticalCeaseFire = true; // his rounds are the smoke's (below)
			Next(State);
			return true;
		}
		case 3:
		{
			AOperativeCharacter* Leader = State.Members[0].Get();
			AOperativeCharacter* Behind = State.Hostiles.Num() > 1 ? State.Hostiles[1].Get() : nullptr;
			if (!Leader || !Behind)
			{
				return Finish(State, false);
			}
			if (State.StageTime < 3.f)
			{
				Leader->SetActorRotation(State.F.Rotation()); // lying down, facing away from him
				return true;
			}
			Check(State, Leader->GetStance() == EOperativeStance::Prone, TEXT("the leader lies prone, his back to the fighter"));
			WatchShots(State, Leader, Behind);
			State.NextForcedShot = 0.f;
			State.NextLog = 0.f;
			Next(State);
			return true;
		}
		case 4:
		{
			AOperativeCharacter* Leader = State.Members[0].Get();
			AOperativeCharacter* Behind = State.Hostiles[1].Get();
			if (!Leader || !Behind)
			{
				return Finish(State, false);
			}
			if (State.StageTime >= State.NextForcedShot && State.StageTime < 6.f)
			{
				State.NextForcedShot = State.StageTime + 0.8f; // he keeps firing at the leader's back: demasked
				Behind->ShootAtTarget(Leader, 1.f);
			}
			if (State.Shots.Num() < 4 && State.StageTime < 8.f)
			{
				return true;
			}
			CheckArc(State, TEXT("the prone leader"));
			return Finish(State, true);
		}
		default:
			return Finish(State, false);
		}
	}

	// --- #43 ---------------------------------------------------------------------------------------------------------------------------
	// PmcUnderFireSmoke: the squad (AGGRESSIVE) walks in a column up a 4.6 m wide, 22 m long passage between two 3 m walls; behind it
	// a PMC fighter stands behind a 1.2 m wall - his body hidden from the squad (no line to it), his rifle over the wall - and fires at
	// the column's backs (the smoke's rounds, every 0.5 s). Within 2.5 s of the first round: the squad's team knows the shooter, every
	// man turns round to his muzzle flash while the column keeps walking (strafing / backing up the passage), and the men with a line
	// to the flash return fire at it (UnderFireRules); nobody got a body line (the wall really hides him).

	bool StepUnderFire(UWorld* World, FState& State)
	{
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UPmcSquadSubsystem* Pmc = UPmcSquadSubsystem::Get(World);
		switch (State.Stage)
		{
		case 0:
		{
			if (State.StageTime < 2.f)
			{
				return true;
			}
			if (!SetUp(World, State, FBox2D(FVector2D(-2400.f, -800.f), FVector2D(2700.f, 800.f))))
			{
				return Finish(State, false);
			}
			const float Z = State.O.Z;
			State.Blocks.Add(SpawnBlock(World, State.O + State.F * 1300.f + State.R * 245.f + FVector(0.f, 0.f, 150.f), State.F.Rotation(), FVector(2200.f, 30.f, 300.f)));
			State.Blocks.Add(SpawnBlock(World, State.O + State.F * 1300.f - State.R * 245.f + FVector(0.f, 0.f, 150.f), State.F.Rotation(), FVector(2200.f, 30.f, 300.f)));
			// Behind the column (it walks away from him: every man has to turn round to face the fire).
			State.Blocks.Add(SpawnBlock(World, State.O - State.F * 1680.f + FVector(0.f, 0.f, 60.f), State.F.Rotation(), FVector(30.f, 400.f, 120.f)));
			AOperativeCharacter* Hostile = SpawnHostile(World, State, State.O - State.F * 1800.f, State.F.Rotation(), 21);
			Check(State, Hostile && State.Blocks.Num() == 3 && !State.Blocks.Contains(nullptr),
				TEXT("a passage of two 3 m walls, a PMC fighter behind a 1.2 m wall 12 m behind the column's tail"));
			if (!Hostile)
			{
				return Finish(State, false);
			}
			MakeTough(Hostile);
			Hostile->SetStance(EOperativeStance::Standing);
			Hostile->bTacticalCeaseFire = true; // his rounds are the smoke's (below)
			Squad->SetFollowersHolding(false);
			State.Goal = State.O + State.F * 2300.f;
			ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(World->GetFirstPlayerController());
			FHitResult Hit;
			const bool bClick = PC && World->LineTraceSingleByChannel(Hit, State.Goal + FVector(0.f, 0.f, 300.f), State.Goal - FVector(0.f, 0.f, 300.f), ECC_Visibility);
			if (bClick)
			{
				PC->HandleWorldHit(Hit);
			}
			Check(State, bClick, FString::Printf(TEXT("the squad is ordered up the passage (ground %.0f)"), Z));
			Next(State);
			return true;
		}
		case 1:
		{
			AOperativeCharacter* Leader = State.Members[0].Get();
			AOperativeCharacter* Hostile = State.Hostiles[0].Get();
			if (!Leader || !Hostile)
			{
				return Finish(State, false);
			}
			// Into the passage: the leader 4 m in, walking.
			const float Progress = FVector::DotProduct(Leader->GetActorLocation() - State.O, State.F);
			if ((Progress < 400.f || Leader->GetVelocity().Size2D() < 50.f) && State.StageTime < 12.f)
			{
				return true;
			}
			Check(State, Progress >= 400.f && Leader->GetVelocity().Size2D() >= 50.f, FString::Printf(TEXT("the column walks in the passage (leader %.0f cm in)"), Progress));
			for (const TWeakObjectPtr<AOperativeCharacter>& Member : State.Members)
			{
				State.ShotTimeBefore.Add(Member.IsValid() ? Member->GetLastShotTime() : 0.0);
				State.WorstFacing.Add(180.f);
				Check(State, Member.IsValid() && !Member->CanHitEnemy(Hostile), FString::Printf(TEXT("%s has no line to the fighter's body (the 1.2 m wall)"),
					Member.IsValid() ? *Member->DisplayName.ToString() : TEXT("-")));
			}
			State.NextForcedShot = 0.f;
			Next(State);
			return true;
		}
		case 2:
		{
			AOperativeCharacter* Hostile = State.Hostiles[0].Get();
			AOperativeCharacter* Leader = State.Members[0].Get();
			if (!Hostile || !Leader)
			{
				return Finish(State, false);
			}
			// His rifle over the wall (the scenario): he stays standing - the squad's return fire would put him down behind it, where his rounds
			// (the smoke's) would leave from below the wall's top and his flash is hidden (honest sight, 2026-11-08).
			if (Hostile->GetStance() != EOperativeStance::Standing)
			{
				Hostile->SetStance(EOperativeStance::Standing);
			}
			if (State.StageTime >= State.NextForcedShot)
			{
				State.NextForcedShot = State.StageTime + 0.5f;
				AOperativeCharacter* Victim = State.Members[FMath::Min(State.Members.Num() - 1, FMath::FloorToInt(State.StageTime / 0.5f) % 2)].Get();
				Hostile->ShootAtTarget(Victim ? Victim : Leader, 1.f);
			}
			State.bTurned[2] |= Pmc && Pmc->IsVisibleToTeam(0, Hostile); // seen by his flash at some moment of the fire
			const FVector Flash = Hostile->GetWeaponMuzzleLocation();
			for (int32 Index = 0; Index < State.Members.Num() && Index < 3; ++Index)
			{
				const AOperativeCharacter* Member = State.Members[Index].Get();
				if (!Member)
				{
					continue;
				}
				const float Error = PlanarErrorDeg(Member->GetActorLocation(), Member->GetActorForwardVector(), Flash);
				if (Error <= 35.f && State.FirstTurn[Index] < 0.f)
				{
					State.FirstTurn[Index] = State.StageTime;
				}
				State.bLeaderMovedUnderFire |= Index == 0 && Member->GetVelocity().Size2D() > 50.f && State.FirstTurn[0] >= 0.f;
			}
			if (State.StageTime < 2.5f)
			{
				return true;
			}
			{
				// fix/pmc-fire-through-wall (2026-11-08): seen = a clear line from an eye to him or to his flash (honest sight).
				const UTacticalSightSubsystem* Sight = World->GetSubsystem<UTacticalSightSubsystem>();
				FString Lines;
				for (const TWeakObjectPtr<AOperativeCharacter>& Member : State.Members)
				{
					if (Member.IsValid() && Sight)
					{
						const FVector Eye = UTacticalSightSubsystem::EyePoint(*Member);
						Lines += FString::Printf(TEXT(" %s: profile %d flash %d;"), *Member->DisplayName.ToString(),
							Sight->HasClearSight(Eye, UTacticalSightSubsystem::ProfilePoint(*Hostile)) ? 1 : 0, Sight->HasClearSight(Eye, Hostile->GetWeaponMuzzleLocation()) ? 1 : 0);
					}
				}
				Check(State, State.bTurned[2], FString::Printf(TEXT("the squad's team saw the shooter by his flash (now %s; lines now:%s)"),
					Pmc && Pmc->IsVisibleToTeam(0, Hostile) ? TEXT("seen") : TEXT("known"), *Lines));
			}
			int32 Returned = 0;
			for (int32 Index = 0; Index < State.Members.Num() && Index < 3; ++Index)
			{
				const AOperativeCharacter* Member = State.Members[Index].Get();
				Check(State, State.FirstTurn[Index] >= 0.f, FString::Printf(TEXT("%s turned to the flash (at %.2f s, facing %s)"),
					Member ? *Member->DisplayName.ToString() : TEXT("-"), State.FirstTurn[Index], Member && Member->IsFacingUnderFire() ? TEXT("the flash") : TEXT("-")));
				Returned += Member ? Member->ReturnFireShots : 0;
			}
			Check(State, State.bLeaderMovedUnderFire, TEXT("the column kept walking while turned to the fire (8-direction strafe)"));
			Check(State, Returned > 0, FString::Printf(TEXT("the squad returns fire at the flash (%d rounds)"), Returned));
			return Finish(State, true);
		}
		default:
			return Finish(State, false);
		}
	}

	// --- fix/pmc-fire-through-wall ---------------------------------------------------------------------------------------------------------
	// PmcFireLineSmoke (game designer PIE 2026-11-08, L_Gen_Depot_1 zone Z_ST_south: a hostile PMC fighter lying PRONE in the open kept
	// shooting at the squad standing behind a solid wall - the tracers straight into the wall, never a hit). The squad stands behind a
	// 3 m x 8 m wall, the fighter lies prone 20 m beyond it. The squad's shots (demasks) are heard by his team; the squad never fires back.
	//  A (old rules, Codex.Combat.HonestFire 0 - the report reproduced): a heard shot makes the leader SEEN by his team; a suppress order at
	//    the leader puts rounds into the wall.
	//  B (honest rules): the heard leader is known (his spot), not seen; the same suppress order drops at once (no line of fire), no round;
	//    then his commander runs 10 s against the heard squad - no round into the wall (he moves or holds).
	//  C: the leader steps out past the wall's end, the fighter sees him (rounds allowed), the leader steps back behind the wall: from 0.7 s
	//    later on no round into the wall for 6 s (the old suppress order followed him behind the wall).

	struct FFireLineWatch
	{
		FBox WallBox = FBox(ForceInit);
		TWeakObjectPtr<AOperativeCharacter> Hostile;
		int32 Rounds = 0;
		int32 WallRounds = 0;
	};

	FFireLineWatch& FireLineWatch()
	{
		static FFireLineWatch Watch;
		return Watch;
	}

	void SetHonestFire(bool bOn)
	{
		if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Combat.HonestFire")))
		{
			Var->Set(bOn ? 1 : 0, ECVF_SetByConsole);
		}
	}

	void WatchFireLine(UWorld* World, AOperativeCharacter* Hostile, const FBox& WallBox)
	{
		FFireLineWatch& Watch = FireLineWatch();
		Watch.WallBox = WallBox;
		Watch.Hostile = Hostile;
		Watch.Rounds = 0;
		Watch.WallRounds = 0;
		if (UCombatFeedbackSubsystem* Feedback = World->GetSubsystem<UCombatFeedbackSubsystem>())
		{
			static FDelegateHandle Handle;
			Feedback->OnTracerSpawned.Remove(Handle);
			Handle = Feedback->OnTracerSpawned.AddLambda([](const FVector& Start, const FVector& End, bool)
			{
				FFireLineWatch& Watch = FireLineWatch();
				const AOperativeCharacter* Shooter = Watch.Hostile.Get();
				if (!Shooter || FVector::Dist(Start, Shooter->GetActorLocation()) > 250.f)
				{
					return; // somebody else's round
				}
				++Watch.Rounds;
				const bool bWall = Watch.WallBox.ExpandBy(30.f).IsInside(End);
				Watch.WallRounds += bWall ? 1 : 0;
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke: the fighter's round (%.0f, %.0f, %.0f) -> (%.0f, %.0f, %.0f)%s"), Start.X, Start.Y, Start.Z, End.X, End.Y, End.Z,
					bWall ? TEXT(" INTO THE WALL") : TEXT(""));
			});
		}
	}

	void ResetFireLineCount()
	{
		FireLineWatch().Rounds = 0;
		FireLineWatch().WallRounds = 0;
	}

	bool StepFireLine(UWorld* World, FState& State)
	{
		UPmcSquadSubsystem* Pmc = UPmcSquadSubsystem::Get(World);
		UTacticalSightSubsystem* Sight = World->GetSubsystem<UTacticalSightSubsystem>();
		AOperativeCharacter* Leader = State.Members.IsEmpty() ? nullptr : State.Members[0].Get();
		AOperativeCharacter* Hostile = State.Hostiles.IsEmpty() ? nullptr : State.Hostiles[0].Get();
		FFireLineWatch& Watch = FireLineWatch();
		if (State.Stage > 0 && (!Pmc || !Sight || !Leader || !Hostile))
		{
			Check(State, false, TEXT("the leader, the fighter and the subsystems stay valid"));
			SetHonestFire(true);
			return Finish(State, false);
		}
		// The squad's shots at "something" (the demask) every 0.5 s while a part runs - heard by the fighter's team, never seen through the wall.
		auto Demask = [&State, Sight, Leader]()
		{
			if (State.StageTime >= State.NextForcedShot)
			{
				State.NextForcedShot = State.StageTime + 0.5f;
				Sight->NotifyFired(Leader);
			}
		};
		switch (State.Stage)
		{
		case 0:
		{
			if (State.StageTime < 2.f)
			{
				return true;
			}
			if (!SetUp(World, State, FBox2D(FVector2D(-1200.f, -1100.f), FVector2D(2800.f, 1100.f))))
			{
				return Finish(State, false);
			}
			// The wall 4 m in front of the leader (3 m tall, 8 m wide, 40 cm thick); the fighter prone 20 m beyond it, facing the squad.
			const FVector WallCentre = State.O + State.F * 400.f + FVector(0.f, 0.f, 150.f);
			AStaticMeshActor* Wall = SpawnBlock(World, WallCentre, State.F.Rotation(), FVector(40.f, 800.f, 300.f));
			State.Blocks.Add(Wall);
			AOperativeCharacter* Spawned = SpawnHostile(World, State, State.O + State.F * 2400.f, (-State.F).Rotation(), 31);
			Check(State, Wall && Spawned, TEXT("a 3 m wall 4 m in front of the squad, a PMC fighter 20 m beyond it"));
			if (!Wall || !Spawned)
			{
				return Finish(State, false);
			}
			MakeTough(Spawned);
			// The designer's fighter: a rifleman (the pool may hand out a shotgun, whose rounds end short of the wall).
			if (!Spawned->SwitchToWeaponById(TEXT("m16")))
			{
				if (UWeaponDataAsset* Rifle = LoadObject<UWeaponDataAsset>(nullptr, TEXT("/Game/Data/Weapons/DA_Weapon_m16.DA_Weapon_m16")))
				{
					Spawned->AddArsenalWeapon(Rifle);
				}
				Spawned->SwitchToWeaponById(TEXT("m16"));
			}
			Check(State, Spawned->CurrentWeapon && Spawned->CurrentWeapon->WeaponId == TEXT("m16"), TEXT("the fighter carries an M16"));
			Spawned->CurrentClip = Spawned->CurrentWeapon ? FMath::Max(1, Spawned->CurrentWeapon->Stats.Ammo.MagazineSize) : 30;
			Spawned->ReserveAmmo = 600;
			Spawned->SetStance(EOperativeStance::Prone);
			State.Goal = Spawned->GetActorLocation();
			for (const TWeakObjectPtr<AOperativeCharacter>& Member : State.Members)
			{
				Member->bTacticalCeaseFire = true; // the squad never fires back (its "shots" are the demasks)
			}
			FVector Origin;
			FVector Extent;
			Wall->GetActorBounds(false, Origin, Extent);
			WatchFireLine(World, Spawned, FBox(Origin - Extent, Origin + Extent));
			SetHonestFire(false);
			State.NextForcedShot = 0.f;
			Next(State);
			return true;
		}
		case 1:
		{
			if (State.StageTime < 3.f)
			{
				return true;
			}
			if (!State.bTurned[0])
			{
				State.bTurned[0] = true;
				Check(State, Hostile->GetStance() == EOperativeStance::Prone, TEXT("the fighter lies prone in the open"));
				Check(State, !Hostile->CanHitEnemy(Leader), TEXT("the wall: no line from his prone muzzle to the leader"));
			}
			if (State.StageTime < 3.4f)
			{
				Demask();
				return true;
			}
			// A (old rules): the heard shot made the leader SEEN.
			const bool bSeenOld = Pmc->IsVisibleToTeam(1, Leader);
			UE_LOG(LogCodexTactics, Display, TEXT("[PmcFireLine] old rules: a shot heard behind the wall -> the leader %s by the fighter's team"),
				bSeenOld ? TEXT("SEEN") : TEXT("not seen"));
			Check(State, bSeenOld, TEXT("A (old rules, the report): a shot heard behind the wall made the leader a seen target (how he acquired them)"));
			ResetFireLineCount();
			Hostile->OrderSuppress(Leader, Leader->GetActorLocation());
			State.NextForcedShot = 0.f;
			Next(State);
			return true;
		}
		case 2:
		{
			Demask();
			if (State.StageTime < 4.f)
			{
				return true;
			}
			UE_LOG(LogCodexTactics, Display, TEXT("[PmcFireLine] old rules: the suppress order fired %d round(s), %d into the wall"), Watch.Rounds, Watch.WallRounds);
			Check(State, Watch.WallRounds > 0, FString::Printf(TEXT("A (old rules, the report): his suppress order put rounds into the wall (%d of %d) - why he fired"),
				Watch.WallRounds, Watch.Rounds));
			Hostile->CancelSuppressFire(TEXT("smoke"));
			SetHonestFire(true);
			State.NextForcedShot = 0.f;
			Next(State);
			return true;
		}
		case 3:
		{
			if (State.StageTime < 2.6f)
			{
				return true; // the old demask runs out (SightRules::DemaskSeconds 2)
			}
			if (Hostile->GetStance() != EOperativeStance::Prone)
			{
				Hostile->SetStance(EOperativeStance::Prone);
			}
			if (State.StageTime < 3.f)
			{
				Demask();
				return true;
			}
			FVector Belief;
			const bool bKnown = Pmc->GetTeamBelief(1, Leader, Belief);
			const bool bSeen = Pmc->IsVisibleToTeam(1, Leader);
			Check(State, bKnown && !bSeen, FString::Printf(TEXT("B: the heard leader is known (his spot %.0f cm off) but not seen"),
				bKnown ? FVector::Dist2D(Belief, Leader->GetActorLocation()) : -1.f));
			ResetFireLineCount();
			const int32 SkipsBefore = Hostile->GetNoLineOfFireSkips();
			Hostile->OrderSuppress(Leader, Leader->GetActorLocation());
			State.ReturnShots = SkipsBefore;
			State.NextForcedShot = 0.f;
			Next(State);
			return true;
		}
		case 4:
		{
			Demask();
			if (State.StageTime < 2.f)
			{
				return true;
			}
			Check(State, !Hostile->HasSuppressOrder() && Hostile->GetLastSuppressStop() == ESuppressOrderStop::NoLineOfFire && Hostile->GetNoLineOfFireSkips() > State.ReturnShots,
				FString::Printf(TEXT("B: the suppress order at the leader behind the wall dropped (no line of fire; stop %s)"),
					SuppressionRules::StopName(Hostile->GetLastSuppressStop())));
			Check(State, Watch.Rounds == 0, FString::Printf(TEXT("B: not a round left for it (%d)"), Watch.Rounds));
			// His commander now, against the heard squad behind the wall.
			Pmc->SetTeamCommanded(1, true);
			State.ShotTimeBefore = { Hostile->GetActorLocation().X, Hostile->GetActorLocation().Y };
			ResetFireLineCount();
			State.NextForcedShot = 0.f;
			Next(State);
			return true;
		}
		case 5:
		{
			Demask();
			if (State.StageTime < 10.f)
			{
				return true;
			}
			const float Moved = State.ShotTimeBefore.Num() == 2
				? static_cast<float>(FVector2D::Distance(FVector2D(Hostile->GetActorLocation()), FVector2D(State.ShotTimeBefore[0], State.ShotTimeBefore[1]))) : 0.f;
			UE_LOG(LogCodexTactics, Display, TEXT("[PmcFireLine] commanded 10 s against the heard squad: %d round(s), %d into the wall; moved %.0f cm, suppress %s, stance %d"),
				Watch.Rounds, Watch.WallRounds, Moved, Hostile->HasSuppressOrder() ? TEXT("on") : TEXT("off"), static_cast<int32>(Hostile->GetStance()));
			Check(State, Watch.WallRounds == 0, FString::Printf(TEXT("B: commanded, no round into the wall (%d of %d rounds; he moved %.0f cm or held)"),
				Watch.WallRounds, Watch.Rounds, Moved));
			// C: the fighter back on his spot, prone, uncommanded (the order below is the commander's typical answer); the leader steps out past
			// the wall's end, in his sight.
			Pmc->SetTeamCommanded(1, false);
			Hostile->CancelSuppressFire(TEXT("smoke"));
			Hostile->StopOperative();
			Hostile->TeleportTo(FVector(State.Goal.X, State.Goal.Y, State.Goal.Z + 4.f), (-State.F).Rotation(), false, true);
			Hostile->SetStance(EOperativeStance::Prone);
			const FVector Out = SmokeUtils::FreeSpot(World, State.O + State.R * 750.f, Leader);
			Leader->TeleportTo(FVector(Out.X, Out.Y, Out.Z + 4.f), State.F.Rotation(), false, true);
			State.NextLog = 0.f;
			Next(State);
			return true;
		}
		case 6:
		{
			if (State.NextLog <= 0.f)
			{
				if (!Pmc->IsVisibleToTeam(1, Leader))
				{
					if (State.StageTime < 6.f)
					{
						return true;
					}
				}
				else
				{
					State.NextLog = FMath::Max(State.StageTime, 0.01f);
					Check(State, true, FString::Printf(TEXT("C: the fighter's team sees the leader out past the wall's end (after %.1f s)"), State.StageTime));
					Hostile->OrderSuppress(Leader, Leader->GetActorLocation()); // pins the seen man (the commander's suppress)
				}
			}
			if (State.NextLog > 0.f && State.StageTime - State.NextLog < 1.5f)
			{
				return true; // he engages him in the open (rounds allowed)
			}
			Check(State, State.NextLog > 0.f, TEXT("C: the fighter's team saw the leader out past the wall's end"));
			const FVector Back = SmokeUtils::FreeSpot(World, State.O, Leader);
			Leader->TeleportTo(FVector(Back.X, Back.Y, Back.Z + 4.f), State.F.Rotation(), false, true);
			State.NextForcedShot = 0.f;
			Next(State);
			return true;
		}
		case 7:
		{
			if (State.StageTime < 0.7f)
			{
				ResetFireLineCount(); // the rounds in flight / the decision of that moment
				return true;
			}
			Demask();
			if (State.StageTime < 6.7f)
			{
				return true;
			}
			UE_LOG(LogCodexTactics, Display, TEXT("[PmcFireLine] after the leader stepped back behind the wall: %d round(s), %d into the wall; suppress %s"),
				Watch.Rounds, Watch.WallRounds, Hostile->HasSuppressOrder() ? TEXT("on") : TEXT("off"));
			Check(State, Watch.WallRounds == 0, FString::Printf(TEXT("C: seen a moment ago, now behind the wall - no round into the wall (%d of %d)"), Watch.WallRounds, Watch.Rounds));
			Check(State, !Pmc->IsVisibleToTeam(1, Leader), TEXT("C: behind the wall again he is not seen (only known)"));
			Watch.Hostile.Reset();
			return Finish(State, true);
		}
		default:
			return Finish(State, false);
		}
	}

	// --- fix/corner-exit-jerk --------------------------------------------------------------------------------------------------------------
	// CornerExitSmoke (found by the shotgun lane's CodexTactics.RecoilProbe 2026-11-08: the upper body jumps 50-60 cm SIDEWAYS in one frame when
	// an operative - squad or PMC alike - leaves the corner fire pose). A squad man at a high wall's end, a PMC fighter at another and the squad
	// man crouched at a 1.2 m wall's end, each with a seen threat past the corner (the fire-ready pose): corner peek in, held, then out - back
	// along the wall (a shimmy, still in cover) and out of the cover (standing where he is), twice each at the high walls, once each at the low
	// one. Every frame the pelvis / spine_03 in the world: no step > 5 cm on the way out (the shimmy's arrival included).

	struct FBodyTrack
	{
		TWeakObjectPtr<AOperativeCharacter> Who;
		FVector Pelvis = FVector::ZeroVector;
		FVector Spine = FVector::ZeroVector;
		FVector Actor = FVector::ZeroVector;
		bool bHas = false;
		bool bWatch = false;
		float WatchUntil = -1.f;
		float MaxPelvisStep = 0.f;
		float MaxSpineStep = 0.f;
		int32 BigSteps = 0;
		int32 FramesWatched = 0;
		float Yaw = 0.f;
		FString Phase;
	};

	FBodyTrack& BodyTrack()
	{
		static FBodyTrack Track;
		return Track;
	}

	void SampleBody(UWorld* World)
	{
		FBodyTrack& Track = BodyTrack();
		AOperativeCharacter* Who = Track.Who.Get();
		USkeletalMeshComponent* Mesh = Who ? Who->GetMesh() : nullptr;
		if (!Mesh || Who->GetWorld() != World)
		{
			Track.bHas = false;
			return;
		}
		const FVector Pelvis = Mesh->GetBoneLocation(TEXT("pelvis"));
		const FVector Spine = Mesh->GetBoneLocation(TEXT("spine_03"));
		const FVector Actor = Who->GetActorLocation();
		if (Track.bHas && Track.bWatch)
		{
			const float PelvisStep = static_cast<float>(FVector::Dist(Pelvis, Track.Pelvis));
			const float SpineStep = static_cast<float>(FVector::Dist(Spine, Track.Spine));
			Track.MaxPelvisStep = FMath::Max(Track.MaxPelvisStep, PelvisStep);
			Track.MaxSpineStep = FMath::Max(Track.MaxSpineStep, SpineStep);
			++Track.FramesWatched;
			if (PelvisStep > 5.f || SpineStep > 5.f || Track.FramesWatched <= 4)
			{
				Track.BigSteps += PelvisStep > 5.f || SpineStep > 5.f ? 1 : 0;
				const UOperativeAnimInstance* Anim = Cast<UOperativeAnimInstance>(Mesh->GetAnimInstance());
				FString Clip;
				float MontageWeight = 0.f;
				float SlotWeight = 0.f;
				if (Anim)
				{
					Anim->GetCoverPlayback(Clip, MontageWeight, SlotWeight);
				}
				FString Montages;
				if (Anim)
				{
					for (const FAnimMontageInstance* Instance : Anim->MontageInstances)
					{
						if (Instance && Instance->Montage)
						{
							const FAnimTrack& Track0 = Instance->Montage->SlotAnimTracks.IsEmpty() ? FAnimTrack() : Instance->Montage->SlotAnimTracks[0].AnimTrack;
							const UAnimSequenceBase* Seq = Track0.AnimSegments.IsEmpty() ? nullptr : Track0.AnimSegments[0].GetAnimReference().Get();
							Montages += FString::Printf(TEXT(" [%s w %.2f t %.2f%s]"), Seq ? *Seq->GetName() : *Instance->Montage->GetName(), Instance->GetWeight(),
								Instance->GetPosition(), Instance->IsActive() ? TEXT("") : TEXT(" stopped"));
						}
					}
				}
				const int32 RootIndex = Mesh->GetBoneIndex(TEXT("root"));
				const int32 PelvisIndex = Mesh->GetBoneIndex(TEXT("pelvis"));
				const TArray<FTransform>& Cs = Mesh->GetComponentSpaceTransforms();
				const FVector RootCs = Cs.IsValidIndex(RootIndex) ? Cs[RootIndex].GetLocation() : FVector::ZeroVector;
				const FVector PelvisCs = Cs.IsValidIndex(PelvisIndex) ? Cs[PelvisIndex].GetLocation() : FVector::ZeroVector;
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke: %s %s frame %llu: pelvis step %.1f cm, spine_03 %.1f cm (actor %.1f cm, yaw %+.1f deg); in cover %d shimmy %d peek %d fire pose %d facing %d; mesh rel %s yaw %.1f; root cs %s pelvis cs %s; clip %s w %.2f slot %.2f;%s"),
					*Who->DisplayName.ToString(), *Track.Phase, static_cast<unsigned long long>(GFrameCounter), PelvisStep, SpineStep, FVector::Dist(Actor, Track.Actor),
					FRotator::NormalizeAxis(static_cast<float>(Who->GetActorRotation().Yaw) - Track.Yaw),
					Who->bInCover ? 1 : 0, Who->bShimmying ? 1 : 0, Who->IsPeekingRoundCorner() ? 1 : 0, Anim && Anim->IsCoverInFirePose() ? 1 : 0, static_cast<int32>(Who->CoverFacing),
					*Mesh->GetRelativeLocation().ToCompactString(), Mesh->GetRelativeRotation().Yaw, *RootCs.ToCompactString(), *PelvisCs.ToCompactString(), *Clip, MontageWeight, SlotWeight, *Montages);
			}
		}
		Track.Pelvis = Pelvis;
		Track.Spine = Spine;
		Track.Actor = Actor;
		Track.Yaw = static_cast<float>(Who->GetActorRotation().Yaw);
		Track.bHas = true;
	}

	void WatchBody(AOperativeCharacter* Who, const TCHAR* Phase)
	{
		FBodyTrack& Track = BodyTrack();
		Track.Who = Who;
		Track.bHas = false;
		Track.bWatch = true;
		Track.MaxPelvisStep = Track.MaxSpineStep = 0.f;
		Track.BigSteps = 0;
		Track.FramesWatched = 0;
		Track.Phase = Phase;
	}

	struct FCornerExitRun
	{
		int32 Subject = 0;   // 0 the squad man (high wall), 1 the PMC fighter (high wall), 2 the squad man crouched at a low wall
		int32 Exit = 0;      // 0..3: shimmy, leave, shimmy, leave
		int32 Step = 0;
		float StepTime = 0.f;
		FVector WallCentre = FVector::ZeroVector; // high wall, arena frame
		float WorstPelvis[3] = { 0.f, 0.f, 0.f };
		float WorstSpine[3] = { 0.f, 0.f, 0.f };
		int32 Peeks[3] = { 0, 0, 0 };
	};

	FCornerExitRun& CornerExitRun()
	{
		static FCornerExitRun Run;
		return Run;
	}

	/** A point of the arena frame: F along, R right, from State.O. */
	FVector ArenaAt(const FState& State, float Forward, float Right)
	{
		return State.O + State.F * Forward + State.R * Right;
	}

	bool StepCornerExit(UWorld* World, FState& State)
	{
		FCornerExitRun& Run = CornerExitRun();
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(World->GetFirstPlayerController());
		const float WallRight[3] = { 0.f, -1800.f, -900.f }; // wall centres along R (each 4 m wide: ends at +-200); the third a 1.2 m low wall
		const bool bLowWall = Run.Subject == 2;
		auto Subject = [&State, &Run]() -> AOperativeCharacter* { return Run.Subject != 1 ? (State.Members.IsEmpty() ? nullptr : State.Members[0].Get()) : (State.Hostiles.IsEmpty() ? nullptr : State.Hostiles[0].Get()); };
		switch (State.Stage)
		{
		case 0:
		{
			if (State.StageTime < 2.f)
			{
				return true;
			}
			if (!SetUp(World, State, FBox2D(FVector2D(-1000.f, -2400.f), FVector2D(1600.f, 700.f))) || !PC)
			{
				return Finish(State, false);
			}
			for (int32 Wall = 0; Wall < 3; ++Wall)
			{
				const float Height = Wall == 2 ? 120.f : 250.f;
				State.Blocks.Add(SpawnBlock(World, ArenaAt(State, 600.f, WallRight[Wall]) + FVector(0.f, 0.f, Height * 0.5f), State.F.Rotation(), FVector(30.f, 400.f, Height)));
			}
			// The PMC fighter (subject 1) behind the second wall; the threats past each corner: the other side's man, frozen in the open.
			AOperativeCharacter* Fighter = SpawnHostile(World, State, ArenaAt(State, 300.f, WallRight[1] + 150.f), State.F.Rotation(), 51);
			Check(State, Fighter && State.Blocks.Num() == 3 && !State.Blocks.Contains(nullptr), TEXT("two 2.5 m walls and a 1.2 m one, a PMC fighter by the second"));
			if (!Fighter)
			{
				return Finish(State, false);
			}
			MakeTough(Fighter);
			Fighter->bTacticalCeaseFire = true;
			for (const TWeakObjectPtr<AOperativeCharacter>& Member : State.Members)
			{
				Member->bTacticalCeaseFire = true;
			}
			FCoreDelegates::OnEndFrame.AddLambda([WeakWorld = TWeakObjectPtr<UWorld>(World)]()
			{
				if (UWorld* Live = WeakWorld.Get())
				{
					SampleBody(Live);
				}
			});
			Run = FCornerExitRun();
			Next(State);
			return true;
		}
		case 1:
		{
			// One peek: into cover at the wall's +R end, the threat past the corner, the peek, held, then out.
			AOperativeCharacter* Op = Subject();
			AOperativeCharacter* Threat = Run.Subject != 1 ? (State.Hostiles.IsEmpty() ? nullptr : State.Hostiles[0].Get())
				: (State.Members.Num() > 1 ? State.Members[1].Get() : nullptr);
			if (!Op || !Threat)
			{
				Check(State, false, TEXT("the subject and his threat"));
				return Finish(State, false);
			}
			const float WallR = WallRight[Run.Subject];
			Run.StepTime += StepSeconds;
			// The threat: frozen in the open past the corner (12 m out, 6 m to the side), never firing.
			const FVector ThreatSpot = ArenaAt(State, 1800.f, WallR + 900.f);
			if (FVector::Dist2D(Threat->GetActorLocation(), ThreatSpot) > 80.f)
			{
				Threat->StopOperative();
				const FVector Spot = SmokeUtils::FreeSpot(World, ThreatSpot, Threat);
				Threat->TeleportTo(FVector(Spot.X, Spot.Y, Spot.Z + 4.f), (-State.F).Rotation(), false, true);
			}
			switch (Run.Step)
			{
			case 0:
			{
				// Into cover near the +R end (the slot at the click; the auto snap takes him to the corner).
				FCoverSlot Slot;
				const bool bFound = CoverTraceRules::FindCoverSlotAt(World, ArenaAt(State, 570.f, WallR + 150.f) + FVector(0.f, 0.f, bLowWall ? 60.f : 90.f), State.F, Slot);
				const bool bTaken = bFound && Op->OrderTakeCover(Slot, false) == EOperativeOrderResult::Accepted;
				Check(State, bTaken, FString::Printf(TEXT("%s takes cover at the wall's end (exit %d)"), *Op->DisplayName.ToString(), Run.Exit));
				if (!bTaken)
				{
					return Finish(State, false);
				}
				Run.Step = 1;
				Run.StepTime = 0.f;
				return true;
			}
			case 1:
			{
				if ((!Op->bInCover || Op->bShimmying || Run.StepTime < 2.f) && Run.StepTime < 15.f)
				{
					return true;
				}
				const FVector EdgePoint = ArenaAt(State, 585.f, WallR + 180.f) + FVector(0.f, 0.f, bLowWall ? 90.f : 150.f);
				AStaticMeshActor* Wall = State.Blocks[Run.Subject].Get();
				const FHitResult EdgeHit(Wall, Wall ? Wall->GetStaticMeshComponent() : nullptr, EdgePoint, -State.F);
				ECoverFacing Side = ECoverFacing::Left;
				float Edge = 0.f;
				const bool bPeek = Op->bInCover && PC->FindCornerPeekForHit(Op, EdgeHit, Side, Edge) && Op->OrderCornerPeek(Side, Edge) == EOperativeOrderResult::Accepted;
				Check(State, bPeek, FString::Printf(TEXT("%s in cover, ordered to peek round the corner (side %d, edge %.0f)"), *Op->DisplayName.ToString(), static_cast<int32>(Side), Edge));
				if (!bPeek)
				{
					return Finish(State, false);
				}
				Run.Step = 2;
				Run.StepTime = 0.f;
				return true;
			}
			case 2:
			{
				const UOperativeAnimInstance* Anim = Op->GetMesh() ? Cast<UOperativeAnimInstance>(Op->GetMesh()->GetAnimInstance()) : nullptr;
				const bool bFirePose = Op->IsPeekingRoundCorner() && Anim && Anim->IsCoverInFirePose();
				if ((!bFirePose || Run.StepTime < 2.5f) && Run.StepTime < 12.f)
				{
					return true;
				}
				Run.Peeks[Run.Subject] += bFirePose ? 1 : 0;
				FString PoseClip;
				float PoseWeight = 0.f;
				float PoseSlot = 0.f;
				if (Anim)
				{
					Anim->GetCoverPlayback(PoseClip, PoseWeight, PoseSlot);
				}
				Check(State, bFirePose, FString::Printf(TEXT("%s holds the corner fire pose (peeking %d, fire pose %d, %s, facing %d)"), *Op->DisplayName.ToString(),
					Op->IsPeekingRoundCorner() ? 1 : 0, Anim && Anim->IsCoverInFirePose() ? 1 : 0, *PoseClip, static_cast<int32>(Op->CoverFacing)));
				// Out: back along the wall (a shimmy 1.5 m from the corner, still in cover) or out of the cover where he stands.
				const bool bShimmy = Run.Exit % 2 == 0;
				WatchBody(Op, bShimmy ? TEXT("shimmy back") : TEXT("leave cover"));
				if (bShimmy)
				{
					FCoverSlot Back;
					const bool bFound = CoverTraceRules::FindCoverSlotAt(World, ArenaAt(State, 570.f, WallR + 20.f) + FVector(0.f, 0.f, bLowWall ? 60.f : 90.f), State.F, Back);
					Check(State, bFound && Op->OrderShimmyTo(Back) == EOperativeOrderResult::Accepted, FString::Printf(TEXT("%s shimmies back from the corner"), *Op->DisplayName.ToString()));
				}
				else
				{
					Op->LeaveCover(TEXT("CornerExitSmoke"));
				}
				Run.Step = 3;
				Run.StepTime = 0.f;
				return true;
			}
			case 3:
			{
				if (Run.StepTime < 2.f)
				{
					return true;
				}
				FBodyTrack& Track = BodyTrack();
				Track.bWatch = false;
				UE_LOG(LogCodexTactics, Display, TEXT("[CornerExit] %s, %s: largest step per frame - pelvis %.1f cm, spine_03 %.1f cm (%d over 5 cm)"),
					*Op->DisplayName.ToString(), *Track.Phase, Track.MaxPelvisStep, Track.MaxSpineStep, Track.BigSteps);
				Run.WorstPelvis[Run.Subject] = FMath::Max(Run.WorstPelvis[Run.Subject], Track.MaxPelvisStep);
				Run.WorstSpine[Run.Subject] = FMath::Max(Run.WorstSpine[Run.Subject], Track.MaxSpineStep);
				Check(State, Track.MaxPelvisStep <= 5.f && Track.MaxSpineStep <= 5.f, FString::Printf(TEXT("%s, %s: the body leaves the corner fire pose continuously (pelvis %.1f, spine_03 %.1f cm per frame, <= 5)"),
					*Op->DisplayName.ToString(), *Track.Phase, Track.MaxPelvisStep, Track.MaxSpineStep));
				if (Op->bInCover)
				{
					Op->LeaveCover(TEXT("CornerExitSmoke: next round"));
				}
				Run.Step = 0;
				Run.StepTime = 0.f;
				if (++Run.Exit >= (Run.Subject == 2 ? 2 : 4))
				{
					Run.Exit = 0;
					if (++Run.Subject >= 3)
					{
						Next(State);
					}
				}
				return true;
			}
			default:
				return Finish(State, false);
			}
		}
		case 2:
			UE_LOG(LogCodexTactics, Display, TEXT("[CornerExit] worst per-frame step leaving the corner fire pose: squad man pelvis %.1f / spine %.1f cm (%d peeks), PMC fighter pelvis %.1f / spine %.1f cm (%d peeks), squad man at the low wall pelvis %.1f / spine %.1f cm (%d peeks)"),
				Run.WorstPelvis[0], Run.WorstSpine[0], Run.Peeks[0], Run.WorstPelvis[1], Run.WorstSpine[1], Run.Peeks[1], Run.WorstPelvis[2], Run.WorstSpine[2], Run.Peeks[2]);
			return Finish(State, true);
		default:
			return Finish(State, false);
		}
	}

	// --- runner ------------------------------------------------------------------------------------------------------------------------

	using FStepFn = bool (*)(UWorld*, FState&);

	void Start(UWorld* World, const TCHAR* Name, FStepFn StepFn, float Limit)
	{
		if (!World)
		{
			return;
		}
		{
			FTimerHandle PlaceHandle;
			TWeakObjectPtr<UWorld> PlaceWorld(World);
			World->GetTimerManager().SetTimer(PlaceHandle, FTimerDelegate::CreateLambda([PlaceWorld]()
			{
				SmokeUtils::PlaceSquadAtTestStart(PlaceWorld.Get());
			}), 0.5f, false);
		}
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		State->Name = Name;
		State->Limit = Limit;
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State, StepFn](float)
		{
			State->Time += StepSeconds;
			State->StageTime += StepSeconds;
			UWorld* Current = WeakWorld.Get();
			if (!Current || State->Time > State->Limit)
			{
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke stopped in stage %d"), State->Stage);
				return Current ? Finish(*State, false) : false;
			}
			return StepFn(Current, *State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs DeathPoseCommand(
		TEXT("CodexTactics.PmcDeathPoseSmoke"),
		TEXT("Dev check (bug #44): a killed PMC fighter falls (FullBody death clip / knockdown fall, pelvis on the ground, aim cleared) like a squad member; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World) { Start(World, TEXT("PmcDeathPoseSmoke"), &StepDeathPose, 60.f); }));

	static FAutoConsoleCommandWithWorldAndArgs FireArcCommand(
		TEXT("CodexTactics.FireArcSmoke"),
		TEXT("Dev check (bug #33): a prone PMC fighter / a prone operative with the target behind turns first (prone turn rate) and fires only with the body and the rifle on it; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World) { Start(World, TEXT("FireArcSmoke"), &StepFireArc, 60.f); }));

	static FAutoConsoleCommandWithWorldAndArgs FireLineCommand(
		TEXT("CodexTactics.PmcFireLineSmoke"),
		TEXT("Dev check (designer 2026-11-08, fix/pmc-fire-through-wall): a prone PMC fighter in the open never fires into the wall the squad stands behind - heard is not seen, no suppress without a line of fire (A = the old rules reproduce the report); logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World) { Start(World, TEXT("PmcFireLineSmoke"), &StepFireLine, 90.f); }));

	static FAutoConsoleCommandWithWorldAndArgs CornerExitCommand(
		TEXT("CodexTactics.CornerExitSmoke"),
		TEXT("Dev check (fix/corner-exit-jerk, RecoilProbe 2026-11-08): a squad man and a PMC fighter leave the corner fire pose (a shimmy back, out of the cover) with no pelvis / spine_03 step over 5 cm per frame; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World) { Start(World, TEXT("CornerExitSmoke"), &StepCornerExit, 150.f); }));

	static FAutoConsoleCommandWithWorldAndArgs UnderFireCommand(
		TEXT("CodexTactics.PmcUnderFireSmoke"),
		TEXT("Dev check (bug #43): a column in a passage fired at by a PMC fighter whose body it cannot hit knows him, turns to his flash while walking and returns fire at it; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World) { Start(World, TEXT("PmcUnderFireSmoke"), &StepUnderFire, 60.f); }));
}

#endif // !UE_BUILD_SHIPPING
