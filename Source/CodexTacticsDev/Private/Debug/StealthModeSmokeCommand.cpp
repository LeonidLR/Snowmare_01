// Designer task 003: end-to-end stealth mode on the committed movement test map. No assets are saved.
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING
#include "Animation/AnimSequence.h"
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
		int32 AmmoBefore = 0;
		int32 Failures = 0;
		bool bShots = false;
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
			if (Clip && Clip->GetName().Contains(TEXT("Wall_Slow")) && Instance->GetStealthWeight() > 0.9f) { ++State.WallSamples; }
		}
		if (State.Stage == 2)
		{
			Op->SetFormationLookYaw(static_cast<float>(State.Forward.Rotation().Yaw));
			const UOperativeAnimInstance* Instance = Anim(Op);
			if (State.StageTime > 3.f && Instance && Instance->GetStealthWeight() > 0.9f && Op->GetVelocity().Size2D() > 20.f)
			{
				if (const UAnimSequenceBase* Clip = Instance->GetStealthDominantClip())
				{
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
		if (State.Time > 180.f)
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
					Member->SetActorTickEnabled(false);
				}
			}
			Op->HealthComponent->RestoreHealth(100.f, 100.f);
			Op->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			Op->SetActorLocation(State.Origin, false, nullptr, ETeleportType::TeleportPhysics);
			Op->SetActorRotation(State.Forward.Rotation());
			State.OriginalMaterial = Op->GetMesh()->GetMaterial(0);
			Check(State, Anim(Op)->HasStealthSet(), TEXT("retargeted stealth set loaded"));
			Squad->SetSelectedGroup({ Op });
			PC->ToggleStealthModeKey();
			Next(State, 1);
			break;
		}
		case 1:
			if (State.StageTime < 2.f) { return true; }
			Check(State, Op->IsInStealthMode() && Op->GetStance() == EOperativeStance::Crouching, TEXT("HUD command toggles selected operative into crouched stealth"));
			CheckVisuals(State, Op);
			Check(State, Anim(Op)->GetStealthWeight() > 0.9f, TEXT("native stealth idle owns the pose"));
			CheckWeaponSwaps(State, Op);
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
				State.Votes.Reset(); State.Samples = 0; State.StageTime = 0.f;
			}
			if (Op->HasOwnMoveOrder() && State.StageTime < 13.f) { return true; }
			const FString Expected = FString::Printf(TEXT("Stealth_Slow_%s_IP"), Directions[State.Leg]);
			Check(State, State.Samples > 5 && State.Votes.FindRef(Expected) > State.Samples / 2, FString::Printf(TEXT("direction %s: loop %s (%d/%d steady frames)"), Directions[State.Leg], *Expected, State.Votes.FindRef(Expected), State.Samples));
			State.bMoving = false;
			if (++State.Leg < 8) { return true; }
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
			const bool bSlot = CoverTraceRules::FindCoverSlotAt(World, At(State, 570.f, -150.f, 90.f), State.Forward, Shimmy);
			Check(State, bSlot && Op->OrderShimmyTo(Shimmy) == EOperativeOrderResult::Accepted, TEXT("real along-wall movement accepted while stealthed"));
			Next(State, 40);
			break;
		}
		case 40:
		{
			if (State.StageTime < 1.f || (Op->bShimmying && State.StageTime < 10.f)) { return true; }
			Check(State, State.WallSamples > 5 && Op->IsInStealthMode(), FString::Printf(TEXT("stealth wall locomotion played while shimmying (%d frames)"), State.WallSamples));
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
		State->FrameHandle = FCoreDelegates::OnEndFrame.AddLambda([State]() { Sample(*State); });
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld = TWeakObjectPtr<UWorld>(World), State](float) { return Step(WeakWorld, *State); }), 0.05f);
	}
	static FAutoConsoleCommandWithWorldAndArgs Command(TEXT("CodexTactics.StealthModeSmoke"), TEXT("Selected stealth toggle, grey material, eight-direction clips, actual enemy perception, wall / lean / squad reveal, real fire and sprint break. Optional shots: three game-camera frames."), FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}
#endif
