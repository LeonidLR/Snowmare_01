// Dev-only headless check of the game designer's play-test bug 2026-10-26 ("the fight is over, one of my fighters is down, but I can't
// walk up and take his equipment"). On L_MovementTest:
//   Scripts/smoke_parallel.ps1 -Commands FallenGearSmoke
// Godot reference: Scenes/movements/player.gd _die / get_items_list / loot_single_item (corpse_loot), main.gd
// _handle_expendable_member_death; UE: AOperativeCharacter::SpawnRemainsLoot, ALootCrateActor, UInteractionSubsystem.
//  1. a real-time wave fight (one wave): the Medic-Sapper, carrying known gear, is killed in action;
//  2. a turn-based fight: the Engineer is knocked down and stays down (the get-up waits for his turn);
//  3. the fight ends (every enemy dead) -> PostCombat: the knocked-down Engineer gets up by himself (the revive after the fight) and
//     takes orders;
//  4. her remains: a searchable crate fitted to the lying body - every bone of the body inside its click box, the cursor ray from the
//     camera at her pelvis / chest / head hits the remains (evidence log of what each ray hits);
//  5. the Engineer is selected and the player clicks the body (the controller's click dispatcher, HandleWorldHit): he walks up, the
//     search (0.6 s) opens the loot window with her gear - medkits, food, the M16 rounds of her clip and reserve, her grenades, her
//     weapon; Take All moves everything into his inventory and the remains read "searched".

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/DeathCinematicSubsystem.h"
#include "Combat/HealthComponent.h"
#include "Combat/KnockdownComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Data/WeaponDataAsset.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/InteractionSubsystem.h"
#include "Interactables/LootCrateActor.h"
#include "Kismet/GameplayStatics.h"
#include "Tactics/TurnBasedCombatSubsystem.h"
#include "UI/OverheadLabel.h"

namespace FallenGearSmoke
{
	constexpr float StepSeconds = 0.1f;

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		TWeakObjectPtr<AOperativeCharacter> Commander;
		TWeakObjectPtr<AOperativeCharacter> Engineer;
		TWeakObjectPtr<AOperativeCharacter> Medic;
		TWeakObjectPtr<ALootCrateActor> Remains;
		/** What the Medic carried when she fell. */
		int32 MedicMedkits = 0;
		int32 MedicChocolate = 0;
		int32 MedicGrenades = 0;
		int32 MedicRifleRounds = 0;
		FString MedicWeaponId;
		/** The Engineer's stock before the search. */
		int32 EngineerMedkits = 0;
		int32 EngineerChocolate = 0;
		int32 EngineerGrenades = 0;
		int32 EngineerRifleReserve = 0;
		int32 KnockdownRecoveries = 0;
		FHitResult BodyClick;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("FallenGearSmoke"));
		return false;
	}

	void KillActor(UHealthComponent* Health)
	{
		if (Health && Health->IsAlive())
		{
			FDamageSpec Spec;
			Spec.Amount = 100000.f;
			Spec.AttackerSource = TEXT("smoke");
			Health->TakeDamage(Spec);
		}
	}

	/** The cursor's ray (APlayerController::GetHitResultAtScreenPosition: ECC_Visibility, simple collision) from the camera at Target. */
	FHitResult CursorRayAt(UWorld* World, APlayerController* PC, const FVector& Target, FVector& OutEye)
	{
		FRotator Rotation;
		PC->GetPlayerViewPoint(OutEye, Rotation);
		const FVector Direction = (Target - OutEye).GetSafeNormal();
		FHitResult Hit;
		World->LineTraceSingleByChannel(Hit, OutEye, OutEye + Direction * (FVector::Dist(OutEye, Target) + 1000.f), ECC_Visibility,
			FCollisionQueryParams(SCENE_QUERY_STAT(ClickableTrace), false));
		return Hit;
	}

	FString DescribeHit(const FHitResult& Hit)
	{
		return Hit.bBlockingHit ? FString::Printf(TEXT("%s.%s"), Hit.GetActor() ? *Hit.GetActor()->GetName() : TEXT("none"),
			Hit.GetComponent() ? *Hit.GetComponent()->GetName() : TEXT("none")) : FString(TEXT("nothing"));
	}

	/** The world points of the body's main bones that exist on this skeleton. */
	TArray<TPair<FName, FVector>> BodyBones(const AOperativeCharacter* Body)
	{
		TArray<TPair<FName, FVector>> Bones;
		const USkeletalMeshComponent* Mesh = Body ? Body->GetMesh() : nullptr;
		for (const TCHAR* Name : { TEXT("pelvis"), TEXT("spine_03"), TEXT("neck_01"), TEXT("head"), TEXT("hand_l"), TEXT("hand_r"),
			TEXT("calf_l"), TEXT("calf_r"), TEXT("foot_l"), TEXT("foot_r") })
		{
			if (Mesh && Mesh->GetBoneIndex(Name) != INDEX_NONE)
			{
				Bones.Emplace(FName(Name), Mesh->GetBoneLocation(Name));
			}
		}
		return Bones;
	}

	/** Is Point inside the crate's (oriented) click box, with a tolerance? */
	bool IsInBox(const ALootCrateActor* Crate, const FVector& Point, float ToleranceCm)
	{
		const FVector Local = Crate->Box->GetComponentTransform().InverseTransformPosition(Point);
		const FVector Extent = Crate->Box->GetUnscaledBoxExtent() + FVector(ToleranceCm);
		return FMath::Abs(Local.X) <= Extent.X && FMath::Abs(Local.Y) <= Extent.Y && FMath::Abs(Local.Z) <= Extent.Z;
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time > 170.f)
		{
			Check(State, false, FString::Printf(TEXT("timeout (stage %d)"), State.Stage));
			return World ? Finish(State, false) : false;
		}
		if (State.Time < 3.f)
		{
			return true;
		}
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UTurnBasedCombatSubsystem* TurnBased = World->GetSubsystem<UTurnBasedCombatSubsystem>();
		UInteractionSubsystem* Interactions = World->GetSubsystem<UInteractionSubsystem>();
		const UDeathCinematicSubsystem* DeathCam = World->GetSubsystem<UDeathCinematicSubsystem>();
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(UGameplayStatics::GetPlayerController(World, 0));
		if (!Flow || !Squad || !TurnBased || !Interactions || !PC)
		{
			Check(State, false, TEXT("subsystems / player controller"));
			return Finish(State, false);
		}
		AOperativeCharacter* Commander = State.Commander.Get();
		AOperativeCharacter* Engineer = State.Engineer.Get();
		AOperativeCharacter* Medic = State.Medic.Get();
		ALootCrateActor* Remains = State.Remains.Get();
		auto Next = [&State]() { ++State.Stage; State.StageTime = 0.f; };
		switch (State.Stage)
		{
		case 0:
		{
			// One wave only: its end is the final fight's end (PostCombat), the play-test's "the fight is over".
			FGameFlowConfig Config = Flow->GetConfig();
			Config.TotalWaves = 1;
			Flow->SetConfig(Config);
			SmokeUtils::StartWaveFight(Flow);
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				SmokeUtils::ParkEnemyFarAway(*It);
				It->SetActorTickEnabled(false); // the wave stays alive until the smoke ends it
			}
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				(Member->SquadRole == EOperativeRole::Commander ? State.Commander : Member->SquadRole == EOperativeRole::Engineer ? State.Engineer
					: State.Medic) = Member;
			}
			Commander = State.Commander.Get();
			Engineer = State.Engineer.Get();
			Medic = State.Medic.Get();
			Check(State, Commander && Engineer && Medic && Flow->GetPhase() == ECodexGamePhase::WaveCombat,
				TEXT("[fight] a real-time wave fight; commander / engineer / medic"));
			if (!Commander || !Engineer || !Medic)
			{
				return Finish(State, false);
			}
			Commander->HealthComponent->SetMaxHealth(5000.f, true);
			Engineer->HealthComponent->SetMaxHealth(5000.f, true);
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Member->bTacticalCeaseFire = true;
				// Headless (-nullrhi) nothing is drawn: the bones follow the death clip only when the pose always refreshes (as on screen).
				Member->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			}
			// Her gear, known: 2 medkits, 3 chocolate, 2 grenades, the M16 in her hands with 17 + 45 rounds.
			Medic->MedkitsCount = 2;
			Medic->ChocolateCount = 3;
			Medic->GrenadesCount = 2;
			if (Medic->CurrentWeapon && Medic->CurrentWeapon->WeaponId == TEXT("m16"))
			{
				Medic->CurrentClip = 17;
				Medic->ReserveAmmo = 45;
			}
			const FWeaponAmmoState Rifle = Medic->GetAmmoState(TEXT("m16"));
			State.MedicMedkits = Medic->MedkitsCount;
			State.MedicChocolate = Medic->ChocolateCount;
			State.MedicGrenades = Medic->GrenadesCount;
			State.MedicRifleRounds = Rifle.Clip + FMath::Max(0, Rifle.Reserve);
			State.MedicWeaponId = Medic->CurrentWeapon ? Medic->CurrentWeapon->WeaponId : FString();
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: the Medic carries %d medkits, %d chocolate, %d grenades, weapon '%s' with %d M16 rounds (clip %d + reserve %d; AmmoInventory[m16] %s)"),
				State.MedicMedkits, State.MedicChocolate, State.MedicGrenades, *State.MedicWeaponId, State.MedicRifleRounds, Rifle.Clip, Rifle.Reserve,
				Medic->AmmoInventory.Contains(TEXT("m16")) ? *FString::Printf(TEXT("%d + %d"), Medic->AmmoInventory[TEXT("m16")].Clip, Medic->AmmoInventory[TEXT("m16")].Reserve) : TEXT("none"));
			Squad->SetLeader(Commander);
			Next();
			return true;
		}
		case 1:
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			// 1. Killed in action in the fight.
			Medic->StopOperative();
			Medic->TakeHit(100000.f, TEXT("Smoke"), false, true);
			Check(State, Medic->IsKilledInAction() && !Squad->GetMembers().Contains(Medic), TEXT("[KIA] the Medic-Sapper is killed in action and leaves the squad"));
			for (TActorIterator<ALootCrateActor> It(World); It; ++It)
			{
				if (It->ActorHasTag(TEXT("CorpseLoot")) && It->CrateName.ToString().Contains(Medic->DisplayName.ToString()))
				{
					State.Remains = *It;
				}
			}
			Remains = State.Remains.Get();
			Check(State, Remains != nullptr, FString::Printf(TEXT("[KIA] her remains are spawned (%s)"), Remains ? *Remains->CrateName.ToString() : TEXT("none")));
			if (!Remains)
			{
				return Finish(State, false);
			}
			Next();
			return true;
		case 2:
			if (DeathCam && DeathCam->IsActive() && State.StageTime < 15.f)
			{
				return true; // her death cinematic
			}
			if (State.StageTime < 1.f)
			{
				return true;
			}
			// 2. A turn-based fight: a Frostbitten 9 m from the Commander makes the grid; the Engineer is knocked down on it.
			if (AEnemyCharacter* Frost = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::Frostbitten,
				Commander->GetActorLocation() + Commander->GetActorForwardVector() * 900.f + FVector(0.f, 0.f, 30.f)))
			{
				Frost->GetHealthComponent()->SetMaxHealth(5000.f, true);
			}
			Next();
			return true;
		case 3:
			if (State.StageTime < 0.3f)
			{
				return true;
			}
			Check(State, Flow->RequestEnterTurnBased(true) == EGameFlowResult::Ok && TurnBased->IsActive(), TEXT("[turn-based] the grid fight starts"));
			Next();
			return true;
		case 4:
			if (State.StageTime < 0.6f || (TurnBased->IsBusy() && State.StageTime < 8.f))
			{
				return true;
			}
			State.KnockdownRecoveries = Engineer->KnockdownComponent->GetRecoveryCount();
			Check(State, Engineer->KnockdownComponent->ForceKnockDown(EKnockdownDirection::Back, EKnockdownCause::HeavyHit),
				TEXT("[knockdown] the Engineer is knocked down on the grid"));
			Next();
			return true;
		case 5:
			if (State.StageTime < 3.f)
			{
				return true;
			}
			Check(State, Engineer->IsKnockedDown() && Engineer->KnockdownComponent->GetPhase() == EKnockdownPhase::Downed,
				FString::Printf(TEXT("[knockdown] turn-based: he stays down (phase %d) - the get-up waits for his turn"), static_cast<int32>(Engineer->KnockdownComponent->GetPhase())));
			// 3. The fight ends while he lies: every enemy dies.
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				It->SetActorTickEnabled(true);
				It->CustomTimeDilation = 1.f;
				KillActor(It->GetHealthComponent());
			}
			Next();
			return true;
		case 6:
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				KillActor(It->GetHealthComponent()); // late spawns
			}
			if (Flow->GetCombatMode() == ECodexCombatMode::TurnBased && State.StageTime > 3.f && !TurnBased->IsBusy())
			{
				Flow->ExitTurnBasedToRealTime(); // the grid did not close by itself
			}
			if (Flow->GetPhase() == ECodexGamePhase::WaveCleared)
			{
				Flow->AdvanceAfterWave();
			}
			if (Flow->GetPhase() != ECodexGamePhase::PostCombat)
			{
				return State.StageTime < 20.f ? true : (Check(State, false, FString::Printf(TEXT("[after the fight] PostCombat never came (phase %d, mode %d)"),
					static_cast<int32>(Flow->GetPhase()), static_cast<int32>(Flow->GetCombatMode()))), Finish(State, false));
			}
			Check(State, !TurnBased->IsActive(), TEXT("[after the fight] PostCombat, the grid closed"));
			Next();
			return true;
		case 7:
			// The revive after the fight: he gets up by himself and takes orders.
			if (Engineer->IsKnockedDown() && State.StageTime < 10.f)
			{
				return true;
			}
			Check(State, !Engineer->IsKnockedDown() && Engineer->KnockdownComponent->GetRecoveryCount() == State.KnockdownRecoveries + 1
				&& Engineer->GetCharacterMovement()->MovementMode == MOVE_Walking,
				FString::Printf(TEXT("[knockdown] after the fight the Engineer gets up by himself (%.1f s, recoveries %d -> %d)"), State.StageTime,
					State.KnockdownRecoveries, Engineer->KnockdownComponent->GetRecoveryCount()));
			Next();
			return true;
		case 8:
		{
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			// 4. The remains: the evidence of what the cursor meets on her body.
			Squad->SetLeader(Engineer);
			const FVector BoxExtent = Remains->Box->GetUnscaledBoxExtent();
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: remains %s at %s yaw %.0f, box extent %s, hidden %d, relocatable %d, body capsule at %s"),
				*Remains->GetName(), *Remains->GetActorLocation().ToCompactString(), Remains->GetActorRotation().Yaw, *BoxExtent.ToCompactString(),
				Remains->IsHidden() ? 1 : 0, Remains->bCanBeRelocated ? 1 : 0, *Medic->GetActorLocation().ToCompactString());
			FString Items;
			for (const FLootEntry& Entry : Remains->GetItems())
			{
				Items += FString::Printf(TEXT("%s x%d; "), *Entry.Name.ToString(), Entry.Count);
			}
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: remains items: %s"), *Items);
			int32 BonesIn = 0;
			int32 BonesInOldBox = 0;
			int32 RaysOnRemains = 0;
			// The old remains (until 2026-10-26): a 120 x 80 x 80 cm box 40 cm above the capsule's feet, turned like the capsule.
			const FTransform OldBox(Medic->GetActorRotation(), Medic->GetActorLocation() - FVector(0.f, 0.f, Medic->GetSimpleCollisionHalfHeight() - 40.f));
			const TArray<TPair<FName, FVector>> Bones = BodyBones(Medic);
			for (const TPair<FName, FVector>& Bone : Bones)
			{
				FVector Eye;
				const FHitResult Hit = CursorRayAt(World, PC, Bone.Value, Eye);
				const bool bIn = IsInBox(Remains, Bone.Value, 5.f);
				const FVector OldLocal = OldBox.InverseTransformPosition(Bone.Value);
				BonesInOldBox += FMath::Abs(OldLocal.X) <= 60.f && FMath::Abs(OldLocal.Y) <= 40.f && FMath::Abs(OldLocal.Z) <= 40.f ? 1 : 0;
				BonesIn += bIn ? 1 : 0;
				RaysOnRemains += Hit.GetActor() == Remains ? 1 : 0;
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: bone %-8s at %s (%.0f cm from the capsule, %.0f cm up) in box %d; the cursor ray from %s hits %s"),
					*Bone.Key.ToString(), *Bone.Value.ToCompactString(), FVector::Dist2D(Bone.Value, Medic->GetActorLocation()),
					Bone.Value.Z - (Medic->GetActorLocation().Z - Medic->GetSimpleCollisionHalfHeight()), bIn ? 1 : 0, *Eye.ToCompactString(), *DescribeHit(Hit));
				if (Bone.Key == FName(TEXT("spine_03")) || (!State.BodyClick.bBlockingHit && Bone.Key == FName(TEXT("pelvis"))))
				{
					State.BodyClick = Hit;
				}
			}
			Check(State, Bones.Num() >= 4, FString::Printf(TEXT("[remains] her body's bones found (%d)"), Bones.Num()));
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: the old remains box (120 x 80 x 80 cm at her feet) would hold %d / %d of her bones"),
				BonesInOldBox, Bones.Num());
			Check(State, BonesIn == Bones.Num(), FString::Printf(TEXT("[remains] the click box covers the lying body (%d / %d bones inside)"), BonesIn, Bones.Num()));
			Check(State, RaysOnRemains == Bones.Num(), FString::Printf(TEXT("[remains] the cursor on her body hits the remains (%d / %d rays)"), RaysOnRemains, Bones.Num()));
			Check(State, !Remains->bCanBeRelocated, TEXT("[remains] a body is not an object to relocate"));
			const ECollisionResponse PawnResponse = Remains->Box->GetCollisionResponseToChannel(ECC_Pawn);
			Check(State, PawnResponse == ECR_Ignore && !Remains->Box->CanEverAffectNavigation(),
				TEXT("[remains] the click box blocks neither pawns nor the navmesh (the squad steps over the body)"));
			FOverheadLabel Label;
			const bool bLabel = Remains->GetOverheadLabel(Label);
			Check(State, !Remains->IsHidden() && bLabel && Label.Text.Contains(TEXT("Search the body")),
				FString::Printf(TEXT("[remains] the label over the body (Godot CorpseLootPrompt): '%s'"), *Label.Text));
			Check(State, Remains->Contents.GetCount(ELootItem::Grenade) == State.MedicGrenades
				&& Remains->Contents.BonusWeaponId == State.MedicWeaponId && Remains->GetStoredCount(ETransferItem::RifleAmmo) == State.MedicRifleRounds,
				FString::Printf(TEXT("[remains] her gear inside: %d grenades, weapon '%s', %d M16 rounds (Godot get_items_list)"),
					Remains->Contents.GetCount(ELootItem::Grenade), *Remains->Contents.BonusWeaponId, Remains->GetStoredCount(ETransferItem::RifleAmmo)));
			Next();
			return true;
		}
		case 9:
		{
			// 5. The click on the body (the chest): through the controller's click dispatcher.
			State.EngineerMedkits = Engineer->MedkitsCount;
			State.EngineerChocolate = Engineer->ChocolateCount;
			State.EngineerGrenades = Engineer->GrenadesCount;
			State.EngineerRifleReserve = Engineer->GetReserve(TEXT("m16"));
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: the click on her chest hits %s; the Engineer is %.0f cm from the body"),
				*DescribeHit(State.BodyClick), FVector::Dist2D(Engineer->GetActorLocation(), Medic->GetActorLocation()));
			PC->HandleWorldHit(State.BodyClick);
			Check(State, Interactions->GetPendingInteraction() == Remains || Interactions->GetLootCrate() == Remains || Remains->GetSessionState() != ELootSessionState::Closed,
				FString::Printf(TEXT("[click] the click on the body is an interaction with her remains (pending %s)"),
					Interactions->GetPendingInteraction() ? *Interactions->GetPendingInteraction()->GetName() : TEXT("none")));
			Next();
			return true;
		}
		case 10:
			if (Interactions->GetLootCrate() != Remains)
			{
				return State.StageTime < 20.f ? true : (Check(State, false, FString::Printf(TEXT("[search] the loot window never opened (Engineer %.0f cm from the body, moving %d, session %d)"),
					FVector::Dist2D(Engineer->GetActorLocation(), Medic->GetActorLocation()), Engineer->IsMoving() ? 1 : 0, static_cast<int32>(Remains->GetSessionState()))),
					Finish(State, false));
			}
			Check(State, Remains->GetLastOpenDelaySeconds() <= 1.5f, FString::Printf(TEXT("[search] he walked up and searched her: the loot window opened after %.1f s (search %.2f s)"),
				State.StageTime, Remains->GetLastOpenDelaySeconds()));
			Check(State, FVector::Dist2D(Engineer->GetActorLocation(), Medic->GetActorLocation()) < 300.f,
				FString::Printf(TEXT("[search] he stands at the body (%.0f cm)"), FVector::Dist2D(Engineer->GetActorLocation(), Medic->GetActorLocation())));
			Interactions->LootAll();
			Next();
			return true;
		case 11:
		{
			if (!Remains->IsLooted() && State.StageTime < 10.f)
			{
				return true;
			}
			const int32 RifleGain = Engineer->GetReserve(TEXT("m16")) - State.EngineerRifleReserve;
			Check(State, Remains->IsLooted() && Interactions->GetLootCrate() == nullptr, TEXT("[take all] the remains are emptied, the window closed"));
			Check(State, Engineer->MedkitsCount == State.EngineerMedkits + State.MedicMedkits && Engineer->ChocolateCount == State.EngineerChocolate + State.MedicChocolate,
				FString::Printf(TEXT("[take all] her medkits and food are his (medkits %d -> %d, chocolate %d -> %d)"),
					State.EngineerMedkits, Engineer->MedkitsCount, State.EngineerChocolate, Engineer->ChocolateCount));
			Check(State, RifleGain == State.MedicRifleRounds, FString::Printf(TEXT("[take all] her M16 rounds (clip + reserve) are his: +%d of %d"), RifleGain, State.MedicRifleRounds));
			Check(State, Engineer->GrenadesCount == State.EngineerGrenades + State.MedicGrenades,
				FString::Printf(TEXT("[take all] her grenades are his (%d -> %d)"), State.EngineerGrenades, Engineer->GrenadesCount));
			FOverheadLabel Label;
			const bool bLabel = Remains->GetOverheadLabel(Label);
			Check(State, bLabel && Label.Text.Contains(TEXT("[Searched]")),
				FString::Printf(TEXT("[take all] the label reads searched: '%s'"), *Label.Text));
			return Finish(State, true);
		}
		default:
			return Finish(State, false);
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
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
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.FallenGearSmoke"),
		TEXT("Dev check (bug 2026-10-26): a fallen operative's gear after the fight - the remains fitted to the body, the click walks up, searches and "
			"takes it; a knocked-down operative gets up after the fight; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
