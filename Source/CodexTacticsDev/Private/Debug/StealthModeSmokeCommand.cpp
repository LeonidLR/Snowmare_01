// Designer task 003: end-to-end stealth mode on the committed movement test map. No assets are saved.
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "AI/PatrolRouteActor.h"
#include "Combat/KnockdownComponent.h"
#include "Components/SplineComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Camera/TacticalCameraPawn.h"
#include "Camera/PlayerCameraManager.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeAnimInstance.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "Characters/StealthModeRules.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/TacticalSightSubsystem.h"
#include "Combat/WaveSubsystem.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Debug/SmokeUtils.h"
#include "Data/WeaponDataAsset.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Environment/EnvironmentSubsystem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Paths.h"
#include "Tactics/CoverTraceRules.h"
#include "UnrealClient.h"

namespace StealthModeSmoke
{
	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Leg = 0;
		int32 Samples = 0;
		int32 WallSamples = 0;
		int32 WallLeg = 0;
		int32 WallReverseFrames = 0;
		int32 WallWrongPhase = 0;
		int32 WallWrongYaw = 0;
		float LastWallPhase = 0.f;
		bool bWallPhaseValid = false;
		int32 AmmoBefore = 0;
		int32 Failures = 0;
		bool bShots = false;
		bool bFemale = false;
		int32 WrongSkeletonSamples = 0;
		int32 WrongFemalePhase = 0;
		TMap<FName, float> FemaleOffsets;
		FVector RelativeRoot = FVector::ZeroVector;
		float MaxRootDrift = 0.f;
		bool bRelativeRootValid = false;
		bool bMoving = false;
		FVector Origin = FVector::ZeroVector;
		FVector Forward = FVector::ForwardVector;
		FVector Right = FVector::RightVector;
		FVector WallOrigin = FVector::ZeroVector;
		float GroundZ = 0.f;
		TWeakObjectPtr<AOperativeCharacter> Op;
		TWeakObjectPtr<AEnemyCharacter> Enemy;
		TWeakObjectPtr<AStaticMeshActor> Wall;
		TWeakObjectPtr<UMaterialInterface> OriginalMaterial;
		TMap<FString, int32> Votes;
		FDelegateHandle FrameHandle;
	};
	static const TCHAR* const Directions[8] = { TEXT("F"), TEXT("FR"), TEXT("R"), TEXT("BR"), TEXT("B"), TEXT("BL"), TEXT("L"), TEXT("FL") };

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}
	bool Finish(FState& State, bool bComplete)
	{
		FCoreDelegates::OnEndFrame.Remove(State.FrameHandle);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), bComplete && State.Failures == 0 ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("StealthModeSmoke"));
		return false;
	}
	void Next(FState& State, int32 Stage)
	{
		State.Stage = Stage;
		State.StageTime = 0.f;
	}
	UOperativeAnimInstance* Anim(AOperativeCharacter* Op)
	{
		return Op && Op->GetMesh() ? Cast<UOperativeAnimInstance>(Op->GetMesh()->GetAnimInstance()) : nullptr;
	}
	void CheckVisuals(FState& State, AOperativeCharacter* Op)
	{
		UMaterialInstanceDynamic* Material = Cast<UMaterialInstanceDynamic>(Op->GetMesh()->GetMaterial(0));
		Check(State, Material && Op->GetMesh()->GetOverlayMaterial() == Material, TEXT("body and overlay use the stealth MID"));
		if (Material)
		{
			const FStealthModeConfig& Config = StealthModeRules::GetConfig();
			const FLinearColor Color = Material->K2_GetVectorParameterValue(TEXT("Color"));
			Check(State, FMath::IsNearlyEqual(Color.R, Config.VisualGray) && FMath::IsNearlyEqual(Color.G, Config.VisualGray) && FMath::IsNearlyEqual(Color.B, Config.VisualGray)
				&& FMath::IsNearlyEqual(Material->K2_GetScalarParameterValue(TEXT("OpacityFill")), Config.VisualOpacity), TEXT("material grey and opacity match stealth data"));
		}
	}
	void CheckWeaponSwaps(FState& State, AOperativeCharacter* Op)
	{
		const FString Held = Op->CurrentWeapon ? Op->CurrentWeapon->WeaponId : FString();
		Check(State, Op->SwitchToWeaponById(TEXT("pistol")), TEXT("late-parts: real switch to pistol accepted"));
		UMaterialInterface* Stealth = Op->GetMesh()->GetMaterial(0);
		int32 Parts = 0;
		for (UStaticMeshComponent* Part : Op->WeaponMeshParts)
		{
			if (Part && Part->GetStaticMesh())
			{
				++Parts;
				Check(State, Part->GetMaterial(0) == Stealth && Part->GetOverlayMaterial() == Stealth, TEXT("late-parts: new magazine receives stealth slots and overlay immediately"));
			}
		}
		Check(State, Parts > 0 && Op->IsInStealthMode(), TEXT("late-parts: magazine actually exists and swap keeps stealth"));
		Op->SetStealthMode(false);
		for (UStaticMeshComponent* Part : Op->WeaponMeshParts)
		{
			if (Part && Part->GetStaticMesh())
			{
				Check(State, Part->GetMaterial(0) == Part->GetStaticMesh()->GetMaterial(0) && !Part->GetOverlayMaterial(), TEXT("late-parts: exit restores the magazine's own default material"));
			}
		}
		Op->SetStealthMode(true);
		Check(State, Op->SwitchToWeaponById(TEXT("rpg7")), TEXT("late-parts: real switch to launcher accepted"));
		TInlineComponentArray<UStaticMeshComponent*> Components(Op);
		UStaticMeshComponent* Round = nullptr;
		for (UStaticMeshComponent* Part : Components)
		{
			if (Part->GetName() == TEXT("WeaponRoundMesh")) { Round = Part; }
		}
		Stealth = Op->GetMesh()->GetMaterial(0);
		Check(State, Round && Round->GetStaticMesh() && Round->GetMaterial(0) == Stealth && Round->GetOverlayMaterial() == Stealth,
			TEXT("late-parts: dynamically attached launcher round receives stealth material"));
		Op->SetStealthMode(false);
		Check(State, Round && Round->GetStaticMesh() && Round->GetMaterial(0) == Round->GetStaticMesh()->GetMaterial(0) && !Round->GetOverlayMaterial(),
			TEXT("late-parts: round's own material restored on exit"));
		Op->SetStealthMode(true);
		Check(State, Op->SwitchToWeaponById(Held) && Op->IsInStealthMode(), TEXT("late-parts: return to initial weapon retains stealth"));
		CheckVisuals(State, Op);
	}
	void CheckFastBlend(FState& State, AOperativeCharacter* Op)
	{
		UOperativeAnimInstance* Instance = Anim(Op);
		UCharacterMovementComponent* Movement = Op->GetCharacterMovement();
		// Controlled native-animation fixture: feed the real movement component velocity, without inventing sprint or a larger nav arena.
		// Real movement / sprint commands are checked separately by the eight-direction / break scenarios.
		Op->SetActorRotation(State.Forward.Rotation());
		for (const float Speed : { 120.f, 300.f, 480.f })
		{
			Movement->Velocity = State.Forward * Speed;
			for (int32 Frame = 0; Frame < 80; ++Frame) { Instance->NativeUpdateAnimation(0.05f); }
			float Slow = 0.f, Fast = 0.f, Total = 0.f;
			for (const FOperativeClipState& PoseClip : Instance->GetStealthClips())
			{
				if (!PoseClip.Sequence) { continue; }
				Total += PoseClip.Weight;
				if (PoseClip.Sequence->GetFName() == TEXT("Stealth_Slow_F_IP")) { Slow += PoseClip.Weight; }
				if (PoseClip.Sequence->GetFName() == TEXT("Stealth_Fast_F_IP")) { Fast += PoseClip.Weight; }
			}
			Check(State, Op->IsInStealthMode() && !Op->IsSprinting() && Instance->GetStealthWeight() > 0.9f,
				FString::Printf(TEXT("non-sprint speed %.0f keeps native stealth"), Speed));
			const bool bBlend = Speed < 150.f ? Slow > 0.9f && Fast < 0.1f : Speed < 350.f ? Slow > 0.15f && Fast > 0.15f : Fast > 0.7f;
			Check(State, bBlend && FMath::IsNearlyEqual(Total, 1.f, 0.01f), FString::Printf(TEXT("speed %.0f: Slow %.3f Fast %.3f total %.3f"), Speed, Slow, Fast, Total));
		}
		Movement->StopMovementImmediately();
		Op->SetSprinting(true);
		Check(State, !Op->IsInStealthMode(), TEXT("sprint still breaks stealth after fast non-sprint blend"));
		Op->SetSprinting(false);
		Op->SetStealthMode(true);
	}
	void CheckHearing(FState& State, AOperativeCharacter* Op)
	{
		UWorld* World = Op->GetWorld();
		AEnemyCharacter* Enemy = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::Frostbitten,
			Op->GetActorLocation() + State.Forward * 200.f, (-State.Forward).Rotation());
		APatrolRouteActor* Route = World->SpawnActor<APatrolRouteActor>();
		Check(State, Enemy && Route, TEXT("acoustic fixture has a real enemy and patrol route"));
		if (!Enemy || !Route) { return; }
		Enemy->SetActorTickEnabled(false);
		Enemy->bOverridePerception = true;
		FEnemyPerceptionParams& Params = Enemy->PerceptionOverride;
		Params.SightRangeCm = Params.ProximityCm = Params.SmellRadiusCm = 0.f;
		Params.HearWalkCm = Params.HearRunCm = Params.HearCrawlCm = 0.f;
		Params.HearCrouchWalkCm = 250.f;
		Params.HearingOcclusionPerWall = 0.4f;
		Op->GetCharacterMovement()->StopMovementImmediately();
		Enemy->StartPatrol(Route, nullptr);
		for (int32 Warm = 0; Warm < 20; ++Warm) { Enemy->Tick(0.25f); } // finish any spawn-performance hold through its public tick
		auto Heard = [&](float Distance, bool bStealth)
		{
			Op->SetStealthMode(bStealth);
			Op->SetStance(EOperativeStance::Crouching);
			Enemy->SetActorLocation(Op->GetActorLocation() + State.Forward * Distance);
			Enemy->SetActorRotation((-State.Forward).Rotation());
			USplineComponent* Spline = Route->GetRouteSpline();
			Spline->ClearSplinePoints(false);
			Spline->AddSplinePoint(Enemy->GetActorLocation(), ESplineCoordinateSpace::World, false);
			Spline->AddSplinePoint(Enemy->GetActorLocation() + State.Right * 100.f, ESplineCoordinateSpace::World, true);
			Enemy->StartPatrol(Route, nullptr);
			if (UAnimInstance* EnemyAnim = Enemy->GetMesh()->GetAnimInstance()) { EnemyAnim->Montage_Stop(0.f); } // reset prior alert performance between independent brackets
			Check(State, Enemy->IsOnPatrol(), TEXT("hearing control genuinely starts patrol"));
			Op->GetCharacterMovement()->Velocity = State.Forward * 100.f;
			Enemy->Tick(0.25f); // production Tick -> patrol -> protected perception; never call a fabricated input overload
			Enemy->GetCharacterMovement()->StopMovementImmediately();
			return !Enemy->IsOnPatrol() && Enemy->GetLastPatrolBreakCause() == EPatrolAlertCause::Hearing;
		};
		Check(State, Heard(200.f, false), TEXT("ordinary crouch footsteps heard at 200 of 250 cm"));
		Check(State, !Heard(200.f, true), TEXT("stealth footsteps not heard at 200 cm (virtual distance 400)"));
		Check(State, Heard(100.f, true), TEXT("stealth footsteps heard inside the halved radius"));
		AStaticMeshActor* Wall = World->SpawnActor<AStaticMeshActor>();
		Wall->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
		Wall->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
		Wall->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
		Wall->SetActorTransform(FTransform(State.Forward.Rotation(), Op->GetActorLocation() + State.Forward * 50.f + FVector(0.f, 0.f, 30.f), FVector(0.05f, 2.f, 3.f)));
		Check(State, !Heard(100.f, true), TEXT("one wall muffles otherwise audible stealth footsteps"));
		Op->GetCharacterMovement()->StopMovementImmediately();
		Wall->Destroy(); Enemy->Destroy(); Route->Destroy();
	}
	void Shot(const FState& State, const TCHAR* Name)
	{
		if (State.bShots)
		{
			const APlayerController* PC = State.Op.IsValid() ? State.Op->GetWorld()->GetFirstPlayerController() : nullptr;
			const APlayerCameraManager* Camera = PC ? PC->PlayerCameraManager.Get() : nullptr;
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: frame %s, operative %s, game-camera %s pitch %.1f"), Name,
				State.Op.IsValid() ? *State.Op->GetActorLocation().ToString() : TEXT("missing"), Camera ? *Camera->GetCameraLocation().ToString() : TEXT("missing"), Camera ? Camera->GetCameraRotation().Pitch : 0.f);
			FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Screenshots/Stealth") / FString(Name) + TEXT(".png"), true, false);
		}
	}
	FVector At(const FState& State, float Forward, float Right, float Z = 0.f)
	{
		return State.WallOrigin + State.Forward * Forward + State.Right * Right + FVector(0.f, 0.f, Z);
	}
	void Sample(FState& State)
	{
		AOperativeCharacter* Op = State.Op.Get();
		if (!Op)
		{
			return;
		}
		Op->ColdLevel = 0.f;
		if (State.bShots)
		{
			if (APlayerController* PC = Op->GetWorld()->GetFirstPlayerController())
			{
				if (ATacticalCameraPawn* Camera = Cast<ATacticalCameraPawn>(PC->GetPawn()))
				{
					// The real game camera, re-centred like portrait selection; hidden-window edge pan must not drift away.
					Camera->SetFollowTarget(Op);
					Camera->SnapToFollowTarget();
					PC->SetViewTarget(Camera);
				}
			}
		}
		if (State.Stage == 40 && Op->bShimmying && Op->GetVelocity().Size2D() > 3.f)
		{
			const UOperativeAnimInstance* Instance = Anim(Op);
			const UAnimSequenceBase* Clip = Instance ? Instance->GetStealthDominantClip() : nullptr;
			if (Clip && Clip->GetName().Contains(TEXT("Wall_Slow")) && Instance->GetStealthWeight() > 0.9f)
			{
				const bool bLeft = State.WallLeg == 1 || State.WallLeg == 2;
				if (Clip->GetName().StartsWith(bLeft ? TEXT("Stealth_LeftWall_") : TEXT("Stealth_RightWall_"))) { ++State.WallSamples; }
				const bool bReverse = !Op->IsShimmyForward();
				State.WallReverseFrames += bReverse ? 1 : 0;
				State.WallWrongYaw += FVector::DotProduct(Op->GetActorForwardVector(), Op->GetCoverSlot().WallNormal) < 0.8f ? 1 : 0;
				const float Phase = Instance->GetStealthPhase();
				if (State.bWallPhaseValid)
				{
					const float Delta = FMath::Fmod(Phase - State.LastWallPhase + 1.5f, 1.f) - 0.5f;
					State.WallWrongPhase += (bReverse && Delta > 0.0001f) || (!bReverse && Delta < -0.0001f) ? 1 : 0;
				}
				State.LastWallPhase = Phase;
				State.bWallPhaseValid = true;
			}
			else { State.bWallPhaseValid = false; }
		}

		if (State.Stage == 2)
		{
			Op->SetFormationLookYaw(static_cast<float>(State.Forward.Rotation().Yaw));
			const UOperativeAnimInstance* Instance = Anim(Op);
			if (State.StageTime > 3.f && Instance && Instance->GetStealthWeight() > 0.9f && Op->GetVelocity().Size2D() > 20.f)
			{
				if (const UAnimSequenceBase* Clip = Instance->GetStealthDominantClip())
				{
					if (State.bFemale && (!Clip->GetPathName().Contains(TEXT("/Stealth/Female/")) || Clip->GetSkeleton() != Op->GetMesh()->GetSkeletalMeshAsset()->GetSkeleton())) { ++State.WrongSkeletonSamples; }
					if (State.bFemale)
					{
						const FVector Root = Op->GetMesh()->GetComponentTransform().InverseTransformPosition(Op->GetMesh()->GetBoneLocation(TEXT("root")));
						if (State.bRelativeRootValid) { State.MaxRootDrift = FMath::Max(State.MaxRootDrift, static_cast<float>(FVector::Dist(Root, State.RelativeRoot))); }
						else { State.RelativeRoot = Root; State.bRelativeRootValid = true; }
						for (const FOperativeClipState& PoseClip : Instance->GetStealthClips())
						{
							const float* Expected = PoseClip.Sequence ? State.FemaleOffsets.Find(PoseClip.Sequence->GetFName()) : nullptr;
							if (Expected && PoseClip.Weight > 0.05f && PoseClip.Sequence->GetPlayLength() > 0.f)
							{
								const float Error = FMath::Fmod(PoseClip.Time / PoseClip.Sequence->GetPlayLength() - Instance->GetStealthPhase() - *Expected + 2.5f, 1.f) - 0.5f;
								State.WrongFemalePhase += FMath::Abs(Error) > 0.01f ? 1 : 0;
							}
						}
					}
					State.Votes.FindOrAdd(Clip->GetName())++;
					++State.Samples;
				}
			}
		}
	}
	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += 0.05f;
		State.StageTime += 0.05f;
		UWorld* World = WeakWorld.Get();
		if (!World)
		{
			return false;
		}
		if (State.Time > 240.f)
		{
			Check(State, false, TEXT("stealth smoke timed out"));
			return Finish(State, false);
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(World->GetFirstPlayerController());
		UTacticalSightSubsystem* Sight = World->GetSubsystem<UTacticalSightSubsystem>();
		if (!Squad || !PC || !Sight || !Squad->GetLeader() || State.Time < 6.f)
		{
			return true;
		}
		AOperativeCharacter* Op = State.Op.Get();
		if (State.Stage > 0 && (!Op || !Anim(Op)))
		{
			Check(State, false, TEXT("operative / animation instance lost"));
			return Finish(State, false);
		}
		switch (State.Stage)
		{
		case 0:
		{
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It) { It->Destroy(); }
			Op = Squad->GetLeader();
			if (State.bFemale)
			{
				Op = nullptr;
				for (AOperativeCharacter* Member : Squad->GetMembers())
				{
					const USkeletalMesh* Body = Member->GetMesh()->GetSkeletalMeshAsset();
					UE_LOG(LogCodexTactics, Display, TEXT("Smoke: female candidate %s body %s skeleton %s"), *Member->GetName(),
						Body ? *Body->GetPathName() : TEXT("none"), Body && Body->GetSkeleton() ? *Body->GetSkeleton()->GetPathName() : TEXT("none"));
					if (Body && Body->GetSkeleton() && Body->GetSkeleton()->GetName().Equals(TEXT("Female_Soldier_Skeleton"), ESearchCase::IgnoreCase)) { Op = Member; break; }
				}
				if (!Op)
				{
					// The committed test-map roster may override the GameMode default bodies. Use its real female fallback in memory only.
					Op = Squad->GetLeader();
					for (AOperativeCharacter* Member : Squad->GetMembers()) { if (Member->SquadRole == EOperativeRole::MedicSapper) { Op = Member; break; } }
					USkeletalMesh* FemaleBody = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Characters/Operatives/Optimized/Female_Soldier/SK_Female_Soldier_1slot.SK_Female_Soldier_1slot"));
					Check(State, FemaleBody != nullptr, TEXT("female fixture loads the existing optimized female body read-only"));
					if (!FemaleBody) { return Finish(State, false); }
					Op->ApplyBodyMeshOverride(FemaleBody);
					Op->GetMesh()->InitAnim(true);
				}
				Check(State, Anim(Op) != nullptr, TEXT("female fixture has the operative native animation instance"));
				if (!Anim(Op)) { return Finish(State, false); }
			}
			State.Op = Op;
			if (UEnvironmentSubsystem* Environment = World->GetSubsystem<UEnvironmentSubsystem>())
			{
				Environment->SetClockScale(0.f);
				Environment->ForceWeather(EWeatherState::Clear, true);
				Environment->SetHour(12.f);
			}
			TArray<const AActor*> Ignore;
			for (AOperativeCharacter* Member : Squad->GetMembers()) { Ignore.Add(Member); }
			const SmokeUtils::FClearArena Arena = SmokeUtils::FindClearArena(World, Op->GetActorLocation(), Op->GetActorForwardVector().GetSafeNormal2D(), FBox2D(FVector2D(-450.f, -450.f), FVector2D(450.f, 450.f)), Ignore);
			Check(State, Arena.bFound, TEXT("clear arena for eight-direction movement"));
			if (!Arena.bFound || !Anim(Op)) { return Finish(State, false); }
			State.Origin = SmokeUtils::FreeSpot(World, Arena.Origin, Op);
			State.Forward = Arena.Forward;
			State.Right = FVector::CrossProduct(FVector::UpVector, State.Forward);
			int32 Park = 0;
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Member->StopOperative();
				Member->ColdLevel = 0.f;
				Member->bTacticalCeaseFire = true;
				if (Member != Op)
				{
					Member->SetActorLocation(State.Origin - State.Forward * (2500.f + 200.f * Park++));
					Member->GetCharacterMovement()->DisableMovement(); // parked fixtures must not keep falling outside the floor
					Member->SetActorTickEnabled(false);
				}
			}
			Op->HealthComponent->RestoreHealth(100.f, 100.f);
			Op->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			Op->SetActorLocation(State.Origin, false, nullptr, ETeleportType::TeleportPhysics);
			Op->SetActorRotation(State.Forward.Rotation());
			State.OriginalMaterial = Op->GetMesh()->GetMaterial(0);
			Check(State, Anim(Op)->HasStealthSet(), TEXT("retargeted stealth set loaded"));
			if (State.bFemale)
			{
				const UAnimSequenceBase* Entry = Anim(Op)->GetStealthCrouchClip();
				Check(State, Entry && Entry->GetPathName().Contains(TEXT("/Stealth/Female/")), TEXT("female crouch entry resolves to the female copy"));
				FString Text;
				TSharedPtr<FJsonObject> Root;
				const bool bRead = FFileHelper::LoadFileToString(Text, *(FPaths::ProjectContentDir() / TEXT("Data/Weapons/weapon_anim_sets.json")))
					&& FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root);
				const TSharedPtr<FJsonObject>* Block = nullptr;
				const TSharedPtr<FJsonObject>* Bodies = nullptr;
				const TSharedPtr<FJsonObject>* Female = nullptr;
				const TSharedPtr<FJsonObject>* Speeds = nullptr;
				double Expected = 0.0;
				const bool bMeasured = bRead && Root.IsValid() && Root->TryGetObjectField(TEXT("stealth"), Block) && (*Block)->TryGetObjectField(TEXT("body_measurements"), Bodies)
					&& (*Bodies)->TryGetObjectField(TEXT("Female_Soldier_Skeleton"), Female) && (*Female)->TryGetObjectField(TEXT("clip_speeds"), Speeds)
					&& (*Speeds)->TryGetNumberField(TEXT("Stealth_Slow_F_IP"), Expected);
				Check(State, bMeasured && FMath::IsNearlyEqual(Anim(Op)->GetStealthNominalSpeed(TEXT("Stealth_Slow_F_IP")), static_cast<float>(Expected), 0.1f),
					TEXT("female native layer uses its own measured speed table"));
				const TSharedPtr<FJsonObject>* Offsets = nullptr;
				if (Female && (*Female)->TryGetObjectField(TEXT("phase_offsets"), Offsets))
				{
					for (const TPair<FString, TSharedPtr<FJsonValue>>& OffsetPair : (*Offsets)->Values)
					{
						State.FemaleOffsets.Add(FName(*OffsetPair.Key), static_cast<float>(OffsetPair.Value->AsNumber()));
					}
				}
				Check(State, State.FemaleOffsets.Num() >= 16, TEXT("female measured phase offsets exist for both eight-direction gaits"));
			}
			Squad->SetSelectedGroup({ Op });
			PC->ToggleStealthModeKey();
			Next(State, 1);
			break;
		}
		case 1:
			if (State.StageTime < 2.f) { return true; }
			Check(State, Op->IsInStealthMode() && Op->GetStance() == EOperativeStance::Crouching, TEXT("HUD command toggles selected operative into crouched stealth"));
			CheckVisuals(State, Op);
			Check(State, Op->GetFirePosture() == ESquadFirePosture::Passive && !Op->MayAutoFireNow(), TEXT("stealth entry selects passive and forbids automatic fire"));
			Check(State, Anim(Op)->GetStealthWeight() > 0.9f, TEXT("native stealth idle owns the pose"));
			if (!State.bFemale) { CheckWeaponSwaps(State, Op); }
			// Mixed selection enables all eligible members; a fully enabled selection disables all.
			for (AOperativeCharacter* Mate : Squad->GetMembers())
			{
				if (Mate != Op && Mate->CanEnterStealthMode())
				{
					Squad->SetSelectedGroup({ Op, Mate }, false);
					PC->ToggleStealthModeKey();
					Check(State, Op->IsInStealthMode() && Mate->IsInStealthMode(), TEXT("mixed selected group enables stealth for both"));
					PC->ToggleStealthModeKey();
					Check(State, !Op->IsInStealthMode() && !Mate->IsInStealthMode(), TEXT("fully enabled group toggles off"));
					Squad->SetSelectedGroup({ Op }, false);
					PC->ToggleStealthModeKey();
					break;
				}
			}
			Op->SetStance(EOperativeStance::Prone);
			Op->OrderMoveTo(State.Origin + State.Forward * 150.f, false);
			Next(State, 12);
			break;
		case 12:
			if (State.StageTime < 3.f) { return true; }
			Check(State, Op->GetStance() == EOperativeStance::Prone && Op->IsInStealthMode(), TEXT("healthy prone crawl preserves stealth"));
			Check(State, FVector::Dist2D(State.Origin, Op->GetActorLocation()) > 20.f, TEXT("stealthed prone operative actually crawls"));
			CheckVisuals(State, Op);
			Op->StopOperative();
			Op->SetStealthMode(false);
			Op->SetStealthMode(true);
			Check(State, Op->GetStance() == EOperativeStance::Prone, TEXT("entering stealth while prone does not stand up"));
			Op->SetStance(EOperativeStance::Crouching);
			Next(State, 10);
			break;
		case 10:
			if (State.StageTime < 1.f || (Anim(Op)->GetStealthWeight() < 0.9f && State.StageTime < 4.f)) { return true; }
			Check(State, Anim(Op)->GetStealthWeight() > 0.9f, TEXT("stealth idle settled after weapon-swap checks"));
			Shot(State, TEXT("stealth"));
			Next(State, 11);
			break;
		case 11:
			if (State.StageTime < 0.3f) { return true; }
			Next(State, 2);
			break;
		case 2:
		{
			if (!State.bMoving)
			{
				Op->StopOperative();
				Op->SetActorLocation(State.Origin, false, nullptr, ETeleportType::TeleportPhysics);
				Op->GetCharacterMovement()->StopMovementImmediately();
				Op->SetActorRotation(State.Forward.Rotation());
				const FVector Direction = State.Forward.RotateAngleAxis(State.Leg * 45.f, FVector::UpVector);
				Op->OrderMoveTo(State.Origin + Direction * 400.f, false);
				State.bMoving = true;
				State.Votes.Reset(); State.Samples = 0; State.StageTime = 0.f; State.bRelativeRootValid = false;
			}
			if (Op->HasOwnMoveOrder() && State.StageTime < 13.f) { return true; }
			const FString Expected = FString::Printf(TEXT("Stealth_Slow_%s_IP"), Directions[State.Leg]);
			Check(State, State.Samples > 5 && State.Votes.FindRef(Expected) > State.Samples / 2, FString::Printf(TEXT("direction %s: loop %s (%d/%d steady frames)"), Directions[State.Leg], *Expected, State.Votes.FindRef(Expected), State.Samples));
			State.bMoving = false;
			if (++State.Leg < 8) { return true; }
			if (State.bFemale)
			{
				Check(State, State.WrongSkeletonSamples == 0 && State.WrongFemalePhase == 0, TEXT("all female samples use female paths/skeleton and measured phase offsets"));
				Check(State, State.MaxRootDrift <= 0.5f, FString::Printf(TEXT("female in-place root drift relative to mesh %.3f cm <= 0.5"), State.MaxRootDrift));
			}
			Op->StopOperative();
			Op->SetActorLocation(State.Origin, false, nullptr, ETeleportType::TeleportPhysics);
			Op->SetStealthMode(false);
			Op->SetStance(EOperativeStance::Crouching);
			State.Enemy = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::Frostbitten, State.Origin + State.Forward * 100.f, (-State.Forward).Rotation());
			AEnemyCharacter* Enemy = State.Enemy.Get();
			if (!Enemy) { Check(State, false, TEXT("perception test enemy spawned")); return Finish(State, false); }
			Enemy->SetActorTickEnabled(false);
			Enemy->CustomTimeDilation = 0.f;
			Enemy->bOverridePerception = true;
			Enemy->PerceptionOverride.SightRangeCm = 500.f;
			Enemy->PerceptionOverride.ProximityCm = 0.f;
			Enemy->PerceptionOverride.SmellRadiusCm = 0.f;
			Next(State, 3);
			break;
		}
		case 3:
		{
			if (State.StageTime < 1.f) { return true; }
			AEnemyCharacter* Enemy = State.Enemy.Get();
			float Distance = 0.f, Range = 0.f;
			Check(State, Enemy->FindVisibleOperativeNow(Distance, Range) == Op && Range > 0.f, TEXT("baseline crouched operative visible; measured perception range"));
			const float NormalRange = Range;
			Enemy->SetActorLocation(Op->GetActorLocation() + State.Forward * NormalRange * 0.8f);
			Check(State, Enemy->FindVisibleOperativeNow(Distance, Range) == Op, TEXT("normal operative detected at 80 percent range"));
			Op->SetStealthMode(true);
			Check(State, Enemy->FindVisibleOperativeNow(Distance, Range) != Op, TEXT("stealth operative not detected at the same distance"));
			Enemy->SetActorLocation(Op->GetActorLocation() + State.Forward * NormalRange * 0.35f);
			Check(State, Enemy->FindVisibleOperativeNow(Distance, Range) == Op, TEXT("stealth operative detected inside x0.4 range"));
			Enemy->PerceptionOverride.SightRangeCm = 0.f;
			// Reuse the tested PeekSightSmoke arena / click geometry, with a crouched stealth operative.
			State.GroundZ = Op->GetActorLocation().Z - Op->GetSimpleCollisionHalfHeight();
			State.WallOrigin = FVector(State.Origin.X, State.Origin.Y, State.GroundZ);
			AStaticMeshActor* Wall = World->SpawnActor<AStaticMeshActor>();
			State.Wall = Wall;
			Wall->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
			Wall->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
			Wall->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
			Wall->SetActorTransform(FTransform(State.Forward.Rotation(), At(State, 600.f, 0.f, 125.f), FVector(0.3f, 4.f, 2.5f)));
			Op->SetActorLocation(At(State, 540.f, 150.f, Op->GetSimpleCollisionHalfHeight() + 2.f));
			FCoverSlot Slot;
			const bool bSlot = CoverTraceRules::FindCoverSlotAt(World, At(State, 570.f, 150.f, 90.f), State.Forward, Slot);
			Check(State, bSlot, TEXT("runtime wall has a cover slot"));
			Op->bIgnoreCoverThreatForTesting = true;
			Op->bCornerHoldAtEdge = false; // motion fixture: a visible reverse-shimmy threat must not auto-peek at the endpoint
			if (bSlot) { Op->EnterCover(Slot); }
			Enemy->SetActorLocation(At(State, 1015.f, 350.f, Enemy->GetSimpleCollisionHalfHeight() + 2.f));
			Next(State, 4);
			break;
		}
		case 4:
		{
			if (State.StageTime < 3.f) { return true; }
			const UAnimSequenceBase* Clip = Anim(Op)->GetStealthDominantClip();
			Check(State, Op->bInCover && Clip && Clip->GetName().Contains(TEXT("Wall_Idle")), TEXT("wall press uses stealth wall idle"));
			Sight->Refresh();
			Check(State, !Sight->IsVisibleToSquad(State.Enemy.Get()), TEXT("wall occludes enemy before peek"));
			Shot(State, TEXT("wall"));
			Next(State, 41);
			break;
		}
		case 41:
		{
			if (State.StageTime < 0.3f) { return true; }
			FCoverSlot Shimmy;
			const bool bReverse = State.WallLeg >= 2;
			Op->bIgnoreCoverThreatForTesting = !bReverse;
			if (bReverse)
			{
				const bool bLeft = State.WallLeg == 2;
				const FVector Threat = Op->GetActorLocation() + Op->GetCoverSlot().RightTangent() * (bLeft ? -600.f : 600.f) + Op->GetCoverSlot().WallNormal * 450.f;
				State.Enemy->SetActorLocation(Threat);
				Sight->Refresh();
				Op->CoverFacing = bLeft ? ECoverFacing::Left : ECoverFacing::Right;
				Op->bHasCoverThreat = true;
				Op->CoverThreatLocation = Threat;
			}
			else { Op->bHasCoverThreat = false; }
			const float Right = State.WallLeg % 2 == 0 ? -150.f : 150.f;
			const bool bSlot = CoverTraceRules::FindCoverSlotAt(World, At(State, 570.f, Right, 90.f), State.Forward, Shimmy);
			State.WallSamples = State.WallReverseFrames = State.WallWrongPhase = State.WallWrongYaw = 0;
			State.bWallPhaseValid = false;
			Check(State, bSlot && Op->OrderShimmyTo(Shimmy) == EOperativeOrderResult::Accepted, FString::Printf(TEXT("wall leg %d: real along-wall order accepted"), State.WallLeg));
			Next(State, 40);
			break;
		}
		case 40:
		{
			if (State.StageTime < 1.f || (Op->bShimmying && State.StageTime < 10.f)) { return true; }
			Check(State, State.WallSamples > 5 && Op->IsInStealthMode() && State.WallWrongYaw == 0 && State.WallWrongPhase <= 1,
				FString::Printf(TEXT("wall leg %d: side clip %d frames, wrong yaw %d phase %d"), State.WallLeg, State.WallSamples, State.WallWrongYaw, State.WallWrongPhase));
			if (State.WallLeg >= 2) { Check(State, State.WallReverseFrames > 5, TEXT("wall reverse phase sampled with a held real threat")); }
			Next(State, 42);
			break;
		}
		case 42:
		{
			if (State.StageTime < 1.3f) { return true; }
			const UAnimSequenceBase* Clip = Anim(Op)->GetStealthDominantClip();
			Check(State, Clip && Clip->GetName().Contains(TEXT("Wall_Idle")) && Anim(Op)->GetStealthWeight() > 0.9f, TEXT("shimmy stop settles back into stealth wall idle"));
			if (++State.WallLeg < 4) { Next(State, 41); break; }
			Op->bIgnoreCoverThreatForTesting = true;
			Op->bHasCoverThreat = false;
			State.Enemy->SetActorLocation(At(State, 1015.f, 350.f, State.Enemy->GetSimpleCollisionHalfHeight() + 2.f));
			Sight->Refresh();
			const FHitResult Hit(State.Wall.Get(), State.Wall->GetStaticMeshComponent(), At(State, 585.f, 180.f, 90.f), -State.Forward);
			Check(State, PC->TryHandleCoverClick(Hit, nullptr) && Op->IsManualCornerPeek(), TEXT("real corner click orders manual peek"));
			Next(State, 5);
			break;
		}
		case 5:
		{
			if (State.StageTime < 4.f || (Op->bShimmying && State.StageTime < 15.f)) { return true; }
			const UAnimSequenceBase* Clip = Anim(Op)->GetStealthDominantClip();
			Check(State, Op->IsPeekingRoundCorner() && Clip && Clip->GetName().Contains(TEXT("Wall_Lean")), TEXT("corner peek uses stealth lean clip"));
			Sight->Refresh();
			Check(State, Sight->IsVisibleToSquad(State.Enemy.Get()) && !State.Enemy->IsHidden(), TEXT("peek reveals occluded enemy to squad / shared minimap sight"));
			TArray<FSquadKnownEnemy> Known;
			Check(State, Sight->GetSquadKnownEnemies(Known) && Known.ContainsByPredicate([&State](const FSquadKnownEnemy& Item)
			{
				return Item.bSeenNow && FVector::Dist2D(Item.Location, State.Enemy->GetActorLocation()) < 10.f;
			}), TEXT("shared minimap feed contains the revealed enemy as seen now"));
			Shot(State, TEXT("peek_revealed_enemy"));
			Next(State, 50);
			break;
		}
		case 50:
		{
			if (State.StageTime < 0.3f) { return true; }
			State.AmmoBefore = Op->CurrentClip;
			Op->bTacticalCeaseFire = false;
			Check(State, Op->OrderQuickFire(State.Enemy->GetActorLocation(), State.Enemy.Get()), TEXT("real quick-fire order accepted"));
			Next(State, 6);
			break;
		}
		case 6:
			if (Op->IsInStealthMode() && State.StageTime < 8.f) { return true; }
			Check(State, !Op->IsInStealthMode() && Op->CurrentClip < State.AmmoBefore, TEXT("actual fired round breaks the shooter's stealth"));
			Check(State, Op->GetMesh()->GetMaterial(0) == State.OriginalMaterial.Get(), TEXT("original body material restored"));
			Op->SetStealthMode(true);
			Op->SetSprinting(true);
			Check(State, !Op->IsInStealthMode(), TEXT("sprint explicitly breaks stealth"));
			Op->bTacticalCeaseFire = true;
			if (State.Enemy.IsValid()) { State.Enemy->Destroy(); }
			Op->SetSprinting(false);
			Op->LeaveCover(TEXT("stealth smoke: lifecycle checks"));
			Op->StopOperative();
			Op->SetActorLocation(State.Origin, false, nullptr, ETeleportType::TeleportPhysics);
			Op->SetActorRotation(State.Forward.Rotation());
			Op->SetStealthMode(true);
			Next(State, 60);
			break;
		case 60:
			if (State.StageTime < 4.f) { return true; }
			CheckFastBlend(State, Op);
			CheckHearing(State, Op);
			Op->HealthComponent->Heal(10000.f);
			Next(State, 601);
			break;
		case 601:
			if (State.StageTime < 1.f) { return true; } // allow any legitimate hearing-control attack flash to finish
			Check(State, Op->KnockdownComponent && Op->KnockdownComponent->ForceKnockDown(EKnockdownDirection::Back, EKnockdownCause::HeavyHit),
				TEXT("real knockdown starts from stealth"));
			Next(State, 61);
			break;
		case 61:
			if (State.StageTime < 0.1f) { return true; }
			Check(State, !Op->IsInStealthMode() && Anim(Op)->GetStealthWeight() <= KINDA_SMALL_NUMBER && !Op->CanEnterStealthMode(),
				TEXT("knockdown clears stealth state/weight and refuses entry"));
			Check(State, Op->GetMesh()->GetMaterial(0) == State.OriginalMaterial.Get() && !Op->GetMesh()->GetOverlayMaterial(), TEXT("knockdown restores original body slots/overlay"));
			Next(State, 62);
			break;
		case 62:
			if (Op->IsKnockedDown() && State.StageTime < 20.f) { return true; }
			Check(State, !Op->IsKnockedDown() && !Op->IsInStealthMode(), TEXT("real get-up completes without automatic stealth re-entry"));
			Check(State, Op->SetStealthMode(true), TEXT("stealth can be explicitly re-entered after recovery"));
			Next(State, 63);
			break;
		case 63:
			if (State.StageTime < 2.f) { return true; }
			Check(State, Anim(Op)->GetStealthWeight() > 0.9f, TEXT("stealth pose restored before death fixture"));
			Op->HealthComponent->ApplyDirectHealthLoss(10000.f, TEXT("StealthModeSmoke"));
			Next(State, 64);
			break;
		case 64:
			if (State.StageTime < 0.1f) { return true; }
			Check(State, !Op->HealthComponent->IsAlive() && !Op->IsInStealthMode() && Anim(Op)->GetStealthWeight() <= KINDA_SMALL_NUMBER && !Op->CanEnterStealthMode(),
				TEXT("death clears stealth state/weight and refuses entry"));
			Check(State, Op->GetMesh()->GetMaterial(0) == State.OriginalMaterial.Get() && !Op->GetMesh()->GetOverlayMaterial(), TEXT("death restores body materials without a ghost overlay"));
			return Finish(State, true);
		default: break;
		}
		return true;
	}
	void Run(const TArray<FString>& Args, UWorld* World)
	{
		if (!World) { return; }
		TSharedRef<FState> State = MakeShared<FState>();
		State->bShots = Args.Contains(TEXT("shots"));
		State->bFemale = Args.Contains(TEXT("female"));
		State->FrameHandle = FCoreDelegates::OnEndFrame.AddLambda([State]() { Sample(*State); });
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld = TWeakObjectPtr<UWorld>(World), State](float) { return Step(WeakWorld, *State); }), 0.05f);
	}
	static FAutoConsoleCommandWithWorldAndArgs Command(TEXT("CodexTactics.StealthModeSmoke"), TEXT("Selected stealth toggle, grey material, eight-direction clips, actual enemy perception, wall / lean / squad reveal, real fire and sprint break. Optional shots: three game-camera frames."), FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}
#endif
