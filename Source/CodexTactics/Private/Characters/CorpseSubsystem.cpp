#include "Characters/CorpseSubsystem.h"

#include "Characters/CorpseBody.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/DeathRagdollRules.h"
#include "Combat/HealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Core/CodexTacticsGameMode.h"
#include "Data/WaveConfigTypes.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"

namespace CorpseTuning
{
	static TAutoConsoleVariable<int32> CVarPersist(TEXT("Codex.Corpse.Persist"), 1,
		TEXT("Enemy corpses (monsters and hostile PMC fighters) stay on the ground (1, game designer 2026-11-08 / 11-09); 0 = the old death decay life span / unmanaged PMC bodies"));
	static TAutoConsoleVariable<int32> CVarCap(TEXT("Codex.Corpse.Cap"), -1,
		TEXT(">= 1 = enemy bodies (monsters + hostile PMC, one cap) kept on the level (-1: the level JSON corpse_cap, default 150)"));
	static TAutoConsoleVariable<float> CVarFadeSeconds(TEXT("Codex.Corpse.FadeSeconds"), 1.f,
		TEXT("Seconds a body over the cap takes to fade out (scaled down)"));
	static TAutoConsoleVariable<int32> CVarFreeze(TEXT("Codex.Corpse.Freeze"), 1,
		TEXT("Kept bodies are frozen once settled (1); 0 = they keep ticking (cost A/B measurements only)"));
	static TAutoConsoleVariable<int32> CVarOldestPool(TEXT("Codex.Corpse.OldestPool"), 10,
		TEXT("The fading body is the farthest from the squad among this many oldest bodies"));
	static TAutoConsoleVariable<int32> CVarRagdoll(TEXT("Codex.DeathRagdoll.Enabled"), 1,
		TEXT("Enable death ragdoll simulation on fatal damage (1). 0 = fallback to animation montage / LayCorpseDown."));
	static TAutoConsoleVariable<int32> CVarRagdollMax(TEXT("Codex.DeathRagdoll.MaxSimultaneous"), -1,
		TEXT("Override max simultaneous ragdolls (-1 = from JSON config)"));

	/** Seconds between two cap checks. */
	constexpr float CapCheckSeconds = 0.5f;

	/** Every simulating body of the ragdoll faster than MaxSpeed (cm/s) slowed to it (3D: also the animation's downward bone speed). */
	static void ClampRagdollBodySpeeds(USkeletalMeshComponent* Mesh, float MaxSpeed)
	{
		for (FBodyInstance* BodyInst : Mesh->Bodies)
		{
			if (BodyInst && BodyInst->IsValidBodyInstance() && BodyInst->bSimulatePhysics)
			{
				const FVector BodyVel = BodyInst->GetUnrealWorldVelocity();
				if (BodyVel.SizeSquared() > FMath::Square(MaxSpeed))
				{
					BodyInst->SetLinearVelocity(FDeathRagdollRules::ClampLinearVelocity(BodyVel, MaxSpeed), false);
				}
			}
		}
	}
	/** A body rendered within this many seconds is in the camera view. */
	constexpr float InViewSeconds = 0.2f;
}

bool UCorpseSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UCorpseSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	LoadRagdollConfig();
}

void UCorpseSubsystem::LoadRagdollConfig()
{
	bRagdollConfigAttempted = true;
	const FString ConfigPath = FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Data/Combat/death_ragdoll.json"));
	FString JsonContent;
	if (FFileHelper::LoadFileToString(JsonContent, *ConfigPath))
	{
		if (FDeathRagdollRules::LoadConfigFromJson(JsonContent, RagdollConfig))
		{
			bRagdollConfigLoaded = true;
			UE_LOG(LogCodexTactics, Display, TEXT("[CorpseSubsystem] Loaded death_ragdoll.json (max=%d, timeout=%.1fs, upward=%.2f)"),
				RagdollConfig.MaxSimultaneousRagdolls, RagdollConfig.MaxSettleTimeoutSeconds, RagdollConfig.UpwardImpulseBias);
			return;
		}
	}

	bRagdollConfigLoaded = false;
	RagdollConfig.bEnabled = false;
	UE_LOG(LogCodexTactics, Error, TEXT("[CorpseSubsystem] Failed to load death_ragdoll.json from '%s' - death ragdoll disabled, using LayCorpseDown fallback"), *ConfigPath);
}

bool UCorpseSubsystem::IsRagdollEnabled() const
{
	if (CorpseTuning::CVarRagdoll.GetValueOnGameThread() == 0)
	{
		return false;
	}
	return bRagdollConfigLoaded && RagdollConfig.bEnabled;
}

int32 UCorpseSubsystem::GetActiveRagdollCount() const
{
	int32 Count = 0;
	for (const FKeptCorpse& Kept : Corpses)
	{
		if (Kept.bIsRagdoll && !Kept.bFrozen && Kept.Body.IsValid())
		{
			++Count;
		}
	}
	return Count;
}

bool UCorpseSubsystem::RegisterRagdollCorpse(AActor* Body, EDamageType DamageType)
{
	UWorld* World = GetWorld();
	if (!World || !Body)
	{
		return false;
	}
	const AOperativeCharacter* Op = Cast<AOperativeCharacter>(Body);
	const bool bIsPlayer = Op && Op->IsPlayerTeam();
	if (!bIsPlayer && !GetConfig().bPersist)
	{
		return false;
	}
	for (FKeptCorpse& Kept : Corpses)
	{
		if (Kept.Body.Get() == Body)
		{
			Kept.bIsRagdoll = true;
			Kept.bPlayerTeam = bIsPlayer;
			Kept.DamageType = DamageType;
			return true;
		}
	}
	FKeptCorpse& Kept = Corpses.AddDefaulted_GetRef();
	Kept.Body = Body;
	Kept.DeathTime = World->GetTimeSeconds();
	Kept.bIsRagdoll = true;
	Kept.bPlayerTeam = bIsPlayer;
	Kept.DamageType = DamageType;
	return true;
}

bool UCorpseSubsystem::TryActivateDeathRagdoll(
	ACharacter* Character,
	const FVector& HitDirection,
	float DamageAmount,
	EDamageType DamageType,
	FName ArchetypeId)
{
	if (!Character)
	{
		return false;
	}

	if (!bRagdollConfigAttempted)
	{
		LoadRagdollConfig();
	}

	if (!IsRagdollEnabled())
	{
		return false;
	}

	// 1. Fallback archetype check (e.g. Spitter, CRYO_DRONE)
	if (FDeathRagdollRules::ShouldFallbackToPose(ArchetypeId, RagdollConfig))
	{
		UE_LOG(LogCodexTactics, Verbose, TEXT("[Corpse] %s is fallback archetype '%s' - bypassing ragdoll"),
			*Character->GetName(), *ArchetypeId.ToString());
		return false;
	}

	// 2. Concurrency budget cap check
	const int32 CVarMax = CorpseTuning::CVarRagdollMax.GetValueOnGameThread();
	FDeathRagdollConfig EffectiveConfig = RagdollConfig;
	if (CVarMax >= 0)
	{
		EffectiveConfig.MaxSimultaneousRagdolls = CVarMax;
	}
	if (!FDeathRagdollRules::CanSimulateRagdoll(GetActiveRagdollCount(), EffectiveConfig))
	{
		UE_LOG(LogCodexTactics, Verbose, TEXT("[Corpse] %s budget cap reached (%d active) - bypassing ragdoll"),
			*Character->GetName(), GetActiveRagdollCount());
		return false;
	}

	// 3. Mesh & Physics Asset validation
	USkeletalMeshComponent* Mesh = Character->GetMesh();
	if (!Mesh || !Mesh->GetSkeletalMeshAsset())
	{
		return false;
	}

	// Prevent duplicate impulse application if ragdoll is already simulating or character is already kept as ragdoll
	if (Mesh->IsSimulatingPhysics() && Mesh->GetCollisionProfileName() == FName(TEXT("Ragdoll")))
	{
		UE_LOG(LogCodexTactics, Verbose, TEXT("[Corpse] %s mesh is already simulating physics - duplicate activation ignored"), *Character->GetName());
		return true;
	}
	for (const FKeptCorpse& Kept : Corpses)
	{
		if (Kept.Body.Get() == Character && Kept.bIsRagdoll)
		{
			UE_LOG(LogCodexTactics, Verbose, TEXT("[Corpse] %s is already an active death ragdoll - duplicate activation ignored"), *Character->GetName());
			return true;
		}
	}

	FString ErrorMsg;
	if (!FDeathRagdollRules::ValidatePhysicsAssetSetup(Mesh->GetSkeletalMeshAsset(), ErrorMsg))
	{
		UE_LOG(LogCodexTactics, Verbose, TEXT("[Corpse] %s physics asset validation failed: %s"),
			*Character->GetName(), *ErrorMsg);
		return false;
	}

	// 4. Set capsule collision to QueryOnly with ECR_Ignore so traces work but pawns don't block
	if (UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
	{
		Capsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Capsule->SetCollisionResponseToAllChannels(ECR_Ignore);
	}

	// 5. Stop and disable movement
	if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
		Movement->DisableMovement();
	}

	// 6. Configure Skeletal Mesh for ragdoll simulation
	Mesh->SetCollisionProfileName(TEXT("Ragdoll"));
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Mesh->SetAllBodiesSimulatePhysics(true);
	Mesh->SetAllUseCCD(true);
	Mesh->WakeAllRigidBodies();
	Mesh->bBlendPhysics = true;
	Mesh->SetLinearDamping(RagdollConfig.LinearDamping);
	Mesh->SetAngularDamping(RagdollConfig.AngularDamping);

	// 7. Calculate and apply kinetic impulse to pelvis scaled by body mass
	const float ReferenceMass = RagdollConfig.ReferenceBodyMassKg;
	const float BodyMass = Mesh->GetMass();
	const float EffectiveMass = BodyMass > 1.f ? BodyMass : ReferenceMass;
	const float MassScale = EffectiveMass / ReferenceMass;
	const FVector Impulse = FDeathRagdollRules::CalculateImpulse(HitDirection, DamageAmount, DamageType, RagdollConfig, MassScale);
	const float MaxLaunchSpeed = FDeathRagdollRules::GetMaxLaunchSpeed(DamageType, RagdollConfig);

	// The impulse becomes ONE velocity change for the pelvis and every body below it, clamped so the launch stays under
	// RootLaunchSpeedFraction x MaxLaunchSpeed (headroom for joint constraints / limb whip). 'pelvis' or the root-most body
	// (a hound has no 'pelvis': before 067 its impulse went to a missing bone and was silently dropped).
	const float RootSpeedLimit = MaxLaunchSpeed * RagdollConfig.RootLaunchSpeedFraction;
	const FName PelvisBone = ResolvePelvisBone(Mesh);
	// The bodies start with the animation's bone velocities (a hound's running forearm: 702 cm/s in the frame of death): clamp first.
	CorpseTuning::ClampRagdollBodySpeeds(Mesh, MaxLaunchSpeed);
	const FVector InitialVel = Mesh->GetPhysicsLinearVelocity(PelvisBone);
	FVector DeltaV = FDeathRagdollRules::ClampLaunchVelocityChange(InitialVel, Impulse / EffectiveMass, RootSpeedLimit);
	// Designer 2026-11-25: lying on the ground (prone / knocked down) -> no backward flight.
	if (const AOperativeCharacter* Op = Cast<AOperativeCharacter>(Character);
		Op && (Op->GetStance() == EOperativeStance::Prone || Op->IsKnockedDown()))
	{
		DeltaV *= RagdollConfig.ProneImpulseScale;
	}

	// Cap angular velocity to prevent high-speed rotational limb whip
	for (FBodyInstance* BodyInst : Mesh->Bodies)
	{
		if (BodyInst && BodyInst->IsValidBodyInstance() && BodyInst->bSimulatePhysics)
		{
			BodyInst->SetMaxAngularVelocityInRadians(RagdollConfig.MaxAngularSpeedRadS, false);
		}
	}

	Mesh->AddImpulseToAllBodiesBelow(DeltaV, PelvisBone, /*bVelChange*/ true, /*bIncludeSelf*/ true);

	// 8. Register as active ragdoll corpse in subsystem
	RegisterRagdollCorpse(Character, DamageType);

	UE_LOG(LogCodexTactics, Display, TEXT("[Corpse] %s activated death ragdoll (impulse magnitude: %.1f, delta-V %.1f cm/s on '%s', max launch speed: %.1f)"),
		*Character->GetName(), Impulse.Size(), DeltaV.Size(), *PelvisBone.ToString(), MaxLaunchSpeed);
	return true;
}

TStatId UCorpseSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UCorpseSubsystem, STATGROUP_Tickables);
}

FCorpseConfig UCorpseSubsystem::GetConfig() const
{
	FCorpseConfig Config;
	const UWorld* World = GetWorld();
	const ACodexTacticsGameMode* GameMode = World ? World->GetAuthGameMode<ACodexTacticsGameMode>() : nullptr;
	const ULevelConfigAsset* Level = GameMode ? GameMode->GetActiveLevelConfig() : nullptr;
	Config.bPersist = CorpseTuning::CVarPersist.GetValueOnGameThread() != 0;
	Config.Cap = CorpseRules::ResolveCap(Level ? Level->Config.CorpseCap : Config.Cap, CorpseTuning::CVarCap.GetValueOnGameThread());
	Config.FadeSeconds = FMath::Max(0.f, CorpseTuning::CVarFadeSeconds.GetValueOnGameThread());
	Config.OldestPool = FMath::Max(1, CorpseTuning::CVarOldestPool.GetValueOnGameThread());
	return Config;
}

ICorpseBody* UCorpseSubsystem::AsCorpse(AActor* Body)
{
	return IsValid(Body) ? Cast<ICorpseBody>(Body) : nullptr;
}

const ICorpseBody* UCorpseSubsystem::AsCorpse(const AActor* Body)
{
	return AsCorpse(const_cast<AActor*>(Body));
}

bool UCorpseSubsystem::RegisterCorpse(AActor* Body)
{
	UWorld* World = GetWorld();
	if (!World || !AsCorpse(Body) || !GetConfig().bPersist)
	{
		return false;
	}
	if (const AOperativeCharacter* Op = Cast<AOperativeCharacter>(Body); Op && Op->IsPlayerTeam())
	{
		return false; // Player's own KIA never kept under corpse cap
	}
	if (!IsKept(Body))
	{
		FKeptCorpse& Kept = Corpses.AddDefaulted_GetRef();
		Kept.Body = Body;
		Kept.DeathTime = World->GetTimeSeconds();
	}
	return true;
}

int32 UCorpseSubsystem::GetFrozenCount() const
{
	int32 Count = 0;
	for (const FKeptCorpse& Kept : Corpses)
	{
		Count += Kept.bFrozen && Kept.FadeStart < 0.0 ? 1 : 0;
	}
	return Count;
}

int32 UCorpseSubsystem::GetFadingCount() const
{
	int32 Count = 0;
	for (const FKeptCorpse& Kept : Corpses)
	{
		Count += Kept.FadeStart >= 0.0 ? 1 : 0;
	}
	return Count;
}

TArray<AActor*> UCorpseSubsystem::GetCorpses() const
{
	TArray<AActor*> Out;
	for (const FKeptCorpse& Kept : Corpses)
	{
		if (AActor* Body = Kept.Body.Get())
		{
			Out.Add(Body);
		}
	}
	return Out;
}

bool UCorpseSubsystem::IsKept(const AActor* Body) const
{
	return Body && Corpses.ContainsByPredicate([Body](const FKeptCorpse& Kept) { return Kept.Body.Get() == Body; });
}

bool UCorpseSubsystem::IsFading(const AActor* Body) const
{
	return Body && Corpses.ContainsByPredicate([Body](const FKeptCorpse& Kept) { return Kept.Body.Get() == Body && Kept.FadeStart >= 0.0; });
}

FName UCorpseSubsystem::ResolvePelvisBone(const USkeletalMeshComponent* Mesh)
{
	if (!Mesh)
	{
		return NAME_None;
	}
	const FName Pelvis = FDeathRagdollRules::GetDefaultPelvisBoneName();
	if (Mesh->GetBoneIndex(Pelvis) != INDEX_NONE)
	{
		return Pelvis;
	}
	// Bone indices run parents first: the body on the smallest bone index is the root-most one.
	const UPhysicsAsset* Asset = Mesh->GetPhysicsAsset();
	FName Best = NAME_None;
	int32 BestIndex = MAX_int32;
	if (Asset)
	{
		for (const USkeletalBodySetup* Setup : Asset->SkeletalBodySetups)
		{
			const int32 BoneIndex = Setup ? Mesh->GetBoneIndex(Setup->BoneName) : INDEX_NONE;
			if (BoneIndex != INDEX_NONE && BoneIndex < BestIndex)
			{
				BestIndex = BoneIndex;
				Best = Setup->BoneName;
			}
		}
	}
	return Best;
}

bool UCorpseSubsystem::GetPelvisBodyBottomZ(const USkeletalMeshComponent* Mesh, FName PelvisBone, float& OutBottomZ)
{
	const UPhysicsAsset* Asset = Mesh ? Mesh->GetPhysicsAsset() : nullptr;
	const int32 BoneIndex = Mesh ? Mesh->GetBoneIndex(PelvisBone) : INDEX_NONE;
	const int32 BodyIndex = Asset ? Asset->FindBodyIndex(PelvisBone) : INDEX_NONE;
	if (BoneIndex == INDEX_NONE || BodyIndex == INDEX_NONE || !Asset->SkeletalBodySetups.IsValidIndex(BodyIndex) || !Asset->SkeletalBodySetups[BodyIndex])
	{
		return false;
	}
	const FBox Box = Asset->SkeletalBodySetups[BodyIndex]->AggGeom.CalcAABB(Mesh->GetBoneTransform(BoneIndex));
	if (!Box.IsValid)
	{
		return false;
	}
	OutBottomZ = static_cast<float>(Box.Min.Z);
	return true;
}

bool UCorpseSubsystem::TraceCorpseFloorZ(const UWorld* World, const FVector& From, const AActor* Ignore, const FDeathRagdollConfig& Config, float& OutFloorZ)
{
	if (!World)
	{
		return false;
	}
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	const FCollisionQueryParams Params(SCENE_QUERY_STAT(CorpseFloorTrace), false, Ignore);
	TArray<FHitResult> Hits;
	World->LineTraceMultiByObjectType(Hits, From + FVector(0.f, 0.f, Config.FloorTraceUpCm), From - FVector(0.f, 0.f, Config.FloorTraceDownCm), Objects, Params);
	for (const FHitResult& Hit : Hits)
	{
		// Only what a ragdoll can rest on: an object query also returns overlap-only volumes (a loot crate's interaction box
		// 54 cm above the floor lifted a KIA body onto thin air, 067 smoke run 1).
		const UPrimitiveComponent* Surface = Hit.GetComponent();
		if (!Surface || !Surface->IsPhysicsCollisionEnabled() || Surface->GetCollisionResponseToChannel(ECC_PhysicsBody) != ECR_Block)
		{
			continue;
		}
		if (FDeathRagdollRules::IsCorpseFloorHit(static_cast<float>(Hit.ImpactPoint.Z), static_cast<float>(Hit.ImpactNormal.Z), static_cast<float>(From.Z), Config))
		{
			OutFloorZ = static_cast<float>(Hit.ImpactPoint.Z);
			return true;
		}
	}
	return false;
}

void UCorpseSubsystem::UpdateSettling(double Now, const FCorpseConfig& Config)
{
	for (FKeptCorpse& Kept : Corpses)
	{
		AActor* Actor = Kept.Body.Get();
		ICorpseBody* Body = AsCorpse(Actor);
		if (!Body || Kept.bFrozen || CorpseTuning::CVarFreeze.GetValueOnGameThread() == 0)
		{
			continue;
		}

		bool bSettled = false;
		float RagdollRootSpeed = 0.f;
		if (Kept.bIsRagdoll)
		{
			ACharacter* Char = Cast<ACharacter>(Actor);
			USkeletalMeshComponent* Mesh = Char ? Char->GetMesh() : nullptr;
			if (Mesh && Mesh->IsSimulatingPhysics())
			{
				const float SinceDeath = static_cast<float>(Now - Kept.DeathTime);
				// In the early post-death frames (LaunchClampWindowSeconds), clamp body velocities to prevent runaway launches
				if (SinceDeath <= RagdollConfig.LaunchClampWindowSeconds)
				{
					CorpseTuning::ClampRagdollBodySpeeds(Mesh, FDeathRagdollRules::GetMaxLaunchSpeed(Kept.DamageType, RagdollConfig));
				}

				const FVector LinVel = Mesh->GetPhysicsLinearVelocity();
				RagdollRootSpeed = static_cast<float>(LinVel.Size());
				const FVector AngVel = Mesh->GetPhysicsAngularVelocityInRadians();
				bSettled = FDeathRagdollRules::HasBodySettled(LinVel, AngVel, SinceDeath, RagdollConfig);
			}
			else
			{
				bSettled = true;
			}
		}
		else
		{
			bSettled = Body->IsDeathPoseSettled();
		}

		if (!bSettled)
		{
			Kept.SettledTime = -1.0;
		}
		else if (Kept.SettledTime < 0.0)
		{
			Kept.SettledTime = Now;
		}

		const float SinceDeath = static_cast<float>(Now - Kept.DeathTime);
		const float SinceSettled = Kept.SettledTime < 0.0 ? -1.f : static_cast<float>(Now - Kept.SettledTime);
		const bool bTimeout = Kept.bIsRagdoll && FDeathRagdollRules::IsRagdollTimeoutFreeze(SinceDeath, RagdollRootSpeed, RagdollConfig);

		if (CorpseRules::ShouldFreeze(SinceDeath, SinceSettled, Body->IsCorpseFalling(), Body->IsCorpseOnLadder(), Config) || bTimeout)
		{
			if (Kept.bIsRagdoll)
			{
				ACharacter* Char = Cast<ACharacter>(Actor);
				if (USkeletalMeshComponent* Mesh = Char ? Char->GetMesh() : nullptr)
				{
					// The settled pose stays where physics left it (067: the old capsule-based relative-Z snap pushed the 4.9x Brute
					// 134 cm into the ground). Measured on the pelvis COLLISION BODY bottom, not the bone: the Brute's bone lies 99 cm
					// above its own body bottom.
					const FName PelvisBone = ResolvePelvisBone(Mesh);
					const FVector PelvisBefore = Mesh->GetBoneLocation(PelvisBone);
					float BottomBefore = static_cast<float>(PelvisBefore.Z);
					const bool bHasBody = GetPelvisBodyBottomZ(Mesh, PelvisBone, BottomBefore);
					float FloorZ = BottomBefore;
					const bool bHasFloor = TraceCorpseFloorZ(GetWorld(), PelvisBefore, Char, RagdollConfig, FloorZ);
					UE_LOG(LogCodexTactics, Log, TEXT("[Corpse] %s freeze %.2f s after death (%s): pelvis '%s' bone Z=%.1f, body bottom Z=%.1f, floor Z=%.1f%s (bottom - floor = %.1f), pelvis speed %.1f cm/s"),
						*Char->GetName(), SinceDeath, SinceSettled >= 0.f ? TEXT("settled") : TEXT("settle time out"), *PelvisBone.ToString(), PelvisBefore.Z,
						BottomBefore, FloorZ, bHasFloor ? TEXT("") : TEXT(" (no floor hit)"), BottomBefore - FloorZ, Mesh->GetPhysicsLinearVelocity(PelvisBone).Size());

					Mesh->SetAllUseCCD(false);
					Mesh->bPauseAnims = true;
					Mesh->SetComponentTickEnabled(false);
					Mesh->PutAllRigidBodiesToSleep();
					Mesh->SetAllBodiesSimulatePhysics(false);
					Mesh->SetCollisionProfileName(TEXT("NoCollision"));
					Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

					// Safety net: a pelvis body more than MaxPelvisSinkCm under the floor lifts the whole mesh onto it (warning once,
					// the corpse freezes once).
					float BottomAfter = BottomBefore;
					if (bHasBody && bHasFloor && GetPelvisBodyBottomZ(Mesh, PelvisBone, BottomAfter))
					{
						const float Lift = FDeathRagdollRules::CalculatePelvisFloorCorrection(BottomAfter, FloorZ, RagdollConfig.MaxPelvisSinkCm, RagdollConfig.MaxFloorLiftCm);
						if (Lift > 0.0f)
						{
							Mesh->AddWorldOffset(FVector(0.f, 0.f, Lift), false, nullptr, ETeleportType::TeleportPhysics);
							UE_LOG(LogCodexTactics, Warning, TEXT("[Corpse] %s pelvis body settled %.1f cm below the floor (bottom Z=%.1f, floor Z=%.1f) - lifted the mesh %.1f cm"),
								*Char->GetName(), FloorZ - BottomAfter, BottomAfter, FloorZ, Lift);
						}
					}
				}
			}
			Body->FreezeCorpse();
			Kept.bFrozen = true;
			UE_LOG(LogCodexTactics, Verbose, TEXT("[Corpse] %s frozen %.2f s after death (%s, ragdoll=%d)"),
				*Actor->GetName(), SinceDeath,
				SinceSettled >= 0.f ? TEXT("death pose settled") : TEXT("settle time out"),
				Kept.bIsRagdoll ? 1 : 0);
		}
	}
}

void UCorpseSubsystem::ApplyCap(const FCorpseConfig& Config, double Now)
{
	TArray<FCorpseCandidate> Candidates;
	Candidates.Reserve(Corpses.Num());
	TArray<FVector, TInlineAllocator<8>> Squaddies;
	if (const USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>())
	{
		for (const AOperativeCharacter* Member : Squad->GetMembers())
		{
			if (Member && Member->HealthComponent && Member->HealthComponent->IsAlive())
			{
				Squaddies.Add(Member->GetActorLocation());
			}
		}
	}
	for (const FKeptCorpse& Kept : Corpses)
	{
		if (Kept.bPlayerTeam)
		{
			continue; // Player's own KIA never count towards corpse cap or fade out
		}
		FCorpseCandidate& Candidate = Candidates.AddDefaulted_GetRef();
		const AActor* Body = Kept.Body.Get();
		const ICorpseBody* Corpse = AsCorpse(Body);
		const UPrimitiveComponent* Visual = Corpse ? Corpse->GetCorpseVisual() : nullptr;
		Candidate.DeathTime = Kept.DeathTime;
		Candidate.bFrozen = Kept.bFrozen;
		Candidate.bFading = Kept.FadeStart >= 0.0;
		Candidate.bInView = Visual && Visual->WasRecentlyRendered(CorpseTuning::InViewSeconds);
		Candidate.bUnsearchedLoot = Corpse && Corpse->HoldsUnsearchedLoot();
		Candidate.SquadDistanceCm = Squaddies.IsEmpty() ? 0.f : TNumericLimits<float>::Max();
		for (const FVector& Point : Squaddies)
		{
			if (Body)
			{
				Candidate.SquadDistanceCm = FMath::Min(Candidate.SquadDistanceCm, static_cast<float>(FVector::Dist(Point, Body->GetActorLocation())));
			}
		}
	}
	for (int32 Over = CorpseRules::CountOverCap(Candidates, Config.Cap); Over > 0; --Over)
	{
		const int32 Pick = CorpseRules::PickCorpseToFade(Candidates, Config);
		if (Pick == INDEX_NONE)
		{
			break; // only bodies still settling: wait for them
		}
		Candidates[Pick].bFading = true;
		FKeptCorpse& Kept = Corpses[Pick];
		Kept.FadeStart = Now;
		if (const ICorpseBody* Corpse = AsCorpse(Kept.Body.Get()); Corpse && Corpse->GetCorpseVisual())
		{
			Kept.BaseScale = Corpse->GetCorpseVisual()->GetComponentScale();
		}
		UE_LOG(LogCodexTactics, Display, TEXT("[Corpse] over the cap of %d: %s fades out (%.0f m from the squad%s)"), Config.Cap,
			*GetNameSafe(Kept.Body.Get()), Candidates[Pick].SquadDistanceCm / 100.f, Candidates[Pick].bInView ? TEXT(", in view: no hidden body left") : TEXT(""));
	}
}

void UCorpseSubsystem::UpdateFades(double Now, const FCorpseConfig& Config)
{
	for (int32 Index = Corpses.Num() - 1; Index >= 0; --Index)
	{
		FKeptCorpse& Kept = Corpses[Index];
		AActor* Body = Kept.Body.Get();
		if (!Body || Body->IsActorBeingDestroyed())
		{
			Corpses.RemoveAt(Index); // gone another way (level cleanup, a load, the arena's next round)
			continue;
		}
		if (Kept.bPlayerTeam || Kept.FadeStart < 0.0)
		{
			continue;
		}
		const float Scale = CorpseRules::FadeScale(static_cast<float>(Now - Kept.FadeStart), Config.FadeSeconds);
		if (Scale <= 0.f)
		{
			Body->Destroy();
			Corpses.RemoveAt(Index);
			++FadedTotal;
			continue;
		}
		if (UPrimitiveComponent* Visual = AsCorpse(Body) ? AsCorpse(Body)->GetCorpseVisual() : nullptr)
		{
			Visual->SetWorldScale3D(Kept.BaseScale * Scale); // shrinks into its root on the ground
		}
	}
}

void UCorpseSubsystem::Tick(float DeltaTime)
{
	UWorld* World = GetWorld();
	if (!World || Corpses.IsEmpty())
	{
		return;
	}
	Corpses.RemoveAll([](const FKeptCorpse& Kept) { return !Kept.Body.IsValid() || Kept.Body->IsActorBeingDestroyed(); });
	const FCorpseConfig Config = GetConfig();
	const double Now = World->GetTimeSeconds();
	UpdateSettling(Now, Config);
	CapTimer -= DeltaTime;
	if (CapTimer <= 0.f || Corpses.Num() > Config.Cap + 8)
	{
		CapTimer = CorpseTuning::CapCheckSeconds;
		ApplyCap(Config, Now);
	}
	UpdateFades(Now, Config);
}

namespace CorpseCommands
{
	static FAutoConsoleCommandWithWorld Status(
		TEXT("CodexTactics.Corpse.Status"),
		TEXT("Logs the enemy bodies (monsters + hostile PMC) kept on this level (frozen / settling / fading) and the cap."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			if (const UCorpseSubsystem* Corpses = World ? World->GetSubsystem<UCorpseSubsystem>() : nullptr)
			{
				const FCorpseConfig Config = Corpses->GetConfig();
				UE_LOG(LogCodexTactics, Display, TEXT("[Corpse] %d bodies kept (%d frozen, %d fading), cap %d, %d faded out so far, persist %s"),
					Corpses->GetCorpseCount(), Corpses->GetFrozenCount(), Corpses->GetFadingCount(), Config.Cap, Corpses->GetFadedTotal(),
					Config.bPersist ? TEXT("on") : TEXT("off"));
			}
		}));
}
