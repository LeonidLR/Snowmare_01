#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AI/PmcSquadSubsystem.h"
#include "AIController.h"
#include "Characters/OperativeAnimInstance.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/FireGateRules.h"
#include "Combat/GrenadeActor.h"
#include "Combat/GrenadeRules.h"
#include "Combat/GrenadeSubsystem.h"
#include "Combat/HealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "Containers/Ticker.h"
#include "Debug/SmokeUtils.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "NavigationSystem.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Survival/FirstAidRules.h"
#include "Survival/FirstAidSubsystem.h"
#include "Tactics/CoverTraceRules.h"
#include "Tactics/CoverTypes.h"

/**
 * Dev check (G023): verifies all three Fight 2 fixes on L_Gen_Depot_1_X6:
 *  1. An operative on an X6 walkable roof in LowCover behind a 95 cm parapet fires and hits a PMC 20-40 m below,
 *     while a full wall still blocks.
 *  2. Operative and PMC (G014) grenade throws turn before the throw montage and release with yaw <= 20 deg.
 *  3. First aid facing keeps helper yaw <= 30 deg to patient's chest across the entire montage when patient is
 *     down behind (180 deg) and beside (90 deg) the helper.
 */
namespace RoofFightSmoke
{
	constexpr float TimeoutSeconds = 120.f;
	constexpr float PmcStartHp = 500.f;

	enum class EStage : uint8
	{
		Setup,
		RoofShoot,
		GrenadeOp,
		GrenadePmc,
		AidBehindGoDown,
		AidBehindRun,
		AidBesideGoDown,
		AidBesideRun,
		Done
	};

	struct FRoofSetup
	{
		bool bFound = false;
		FCoverSlot ParapetSlot;
		FVector RoofSpot = FVector::ZeroVector;
		FVector GroundPmcSpot = FVector::ZeroVector;
		FVector FullWallBlockedSpot = FVector::ZeroVector;
		FString BuildingId;
	};

	struct FState
	{
		TWeakObjectPtr<UWorld> World;
		SmokeUtils::FPreloadGate Gate;
		EStage Stage = EStage::Setup;
		float TotalTime = 0.f;
		float StageTime = 0.f;
		int32 Failures = 0;
		bool bDone = false;

		TWeakObjectPtr<AOperativeCharacter> Shooter;
		TWeakObjectPtr<AOperativeCharacter> Helper;
		TWeakObjectPtr<AOperativeCharacter> Patient;
		TWeakObjectPtr<AOperativeCharacter> GroundPmc;
		TWeakObjectPtr<AOperativeCharacter> PmcThrower;
		TWeakObjectPtr<AGrenadeActor> ActiveGrenade;

		FVector ClearArenaOrigin = FVector::ZeroVector;
		FVector ThrowTarget = FVector::ZeroVector;
		float ThrowDesiredYaw = 0.f;
		bool bSawThrowClipStart = false;

		float PatientYaw0 = 0.f;
		int32 AidClipFrames = 0;
		float MaxAidChestErrDeg = -1.f;
		int32 RecordsBefore = 0;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State, bool bComplete)
	{
		FirstAidRules::ResetConfig();
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), (State.Failures == 0 && bComplete) ? TEXT("PASS") : TEXT("FAIL"));
		State.bDone = true;
		FPlatformMisc::RequestExit(false, TEXT("RoofFightSmoke"));
		return false;
	}

	void Next(FState& State, EStage NewStage)
	{
		State.Stage = NewStage;
		State.StageTime = 0.f;
	}

	void PlaceOnSpot(AOperativeCharacter* Op, const FVector& FeetSpot, float Yaw)
	{
		if (!Op)
		{
			return;
		}
		if (Op->bInCover)
		{
			Op->LeaveCover(TEXT("RoofFightSmoke"));
		}
		const float HalfH = Op->GetCapsuleComponent() ? Op->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 88.f;
		Op->TeleportTo(FeetSpot + FVector(0.f, 0.f, HalfH + 2.f), FRotator(0.f, Yaw, 0.f), false, true);
		Op->StopOperative();
		if (AAIController* AI = Cast<AAIController>(Op->GetController()))
		{
			AI->StopMovement();
		}
		if (USkeletalMeshComponent* Mesh = Op->GetMesh())
		{
			Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		}
	}

	FRoofSetup FindX6RoofAndTargets(UWorld* World, const TArray<const AActor*>& IgnoredActors)
	{
		FRoofSetup Out;
		FString LevelText;
		TSharedPtr<FJsonObject> Level;
		const FString LevelFile = FPaths::ProjectContentDir() / TEXT("Data/LevelJson/L_Gen_Depot_1_X6.json");
		if (!FFileHelper::LoadFileToString(LevelText, *LevelFile)
			|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(LevelText), Level) || !Level.IsValid()
			|| !Level->HasField(TEXT("levelgen_layout")))
		{
			return Out;
		}
		FString LayoutText;
		TSharedPtr<FJsonObject> Layout;
		if (!FFileHelper::LoadFileToString(LayoutText, *(FPaths::ProjectDir() / Level->GetStringField(TEXT("levelgen_layout"))))
			|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(LayoutText), Layout) || !Layout.IsValid())
		{
			return Out;
		}
		const float GroundZ = Layout->HasField(TEXT("ground_z")) ? static_cast<float>(Layout->GetNumberField(TEXT("ground_z"))) : 0.f;
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		if (!Nav)
		{
			return Out;
		}

		for (const TSharedPtr<FJsonValue>& BV : Layout->GetArrayField(TEXT("buildings")))
		{
			const TSharedPtr<FJsonObject>& B = BV->AsObject();
			bool bWalkable = false;
			bool bEnterable = false;
			B->TryGetBoolField(TEXT("roof_walkable"), bWalkable);
			B->TryGetBoolField(TEXT("enterable"), bEnterable);
			if (!bWalkable || !bEnterable)
			{
				continue;
			}
			const int32 Floors = static_cast<int32>(B->GetNumberField(TEXT("floors")));
			const float RoofZ = GroundZ + Floors * 300.f;
			const TArray<TSharedPtr<FJsonValue>>& Footprint = B->GetArrayField(TEXT("footprint"));
			const FVector2D Min(Footprint[0]->AsArray()[0]->AsNumber(), Footprint[0]->AsArray()[1]->AsNumber());
			const FVector2D Max(Footprint[1]->AsArray()[0]->AsNumber(), Footprint[1]->AsArray()[1]->AsNumber());
			const FVector2D Centre = (Min + Max) * 0.5;

			struct FEdgeSpec
			{
				FVector Inner;
				FVector Outward;
			};
			const TArray<FEdgeSpec> Edges = {
				{ FVector(Max.X - 80.f, Centre.Y, RoofZ), FVector(1.f, 0.f, 0.f) },
				{ FVector(Min.X + 80.f, Centre.Y, RoofZ), FVector(-1.f, 0.f, 0.f) },
				{ FVector(Centre.X, Max.Y - 80.f, RoofZ), FVector(0.f, 1.f, 0.f) },
				{ FVector(Centre.X, Min.Y + 80.f, RoofZ), FVector(0.f, -1.f, 0.f) }
			};

			FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(RoofFightSmokeFind), false);
			for (const AActor* Ig : IgnoredActors)
			{
				TraceParams.AddIgnoredActor(Ig);
			}

			for (const FEdgeSpec& Edge : Edges)
			{
				FHitResult ParapetHit;
				if (!World->LineTraceSingleByChannel(ParapetHit, Edge.Inner + FVector(0.f, 0.f, 50.f),
					Edge.Inner + Edge.Outward * 180.f + FVector(0.f, 0.f, 50.f), ECC_Visibility, TraceParams))
				{
					continue;
				}
				FCoverSlot Slot;
				if (!CoverTraceRules::FindCoverSlotAt(World, ParapetHit.ImpactPoint, Edge.Outward, Slot, FCoverTraceConfig(), IgnoredActors)
					|| Slot.Height != ECoverHeight::LowCover)
				{
					continue;
				}
				const FVector Outward = (-Slot.WallNormal).GetSafeNormal2D();
				for (const float DistCm : { 2400.f, 2800.f, 3200.f, 3600.f })
				{
					FNavLocation GroundNav;
					const FVector QueryGround(Slot.WorldLocation.X + Outward.X * DistCm, Slot.WorldLocation.Y + Outward.Y * DistCm, GroundZ + 30.f);
					if (!Nav->ProjectPointToNavigation(QueryGround, GroundNav, FVector(250.f, 250.f, 120.f)))
					{
						continue;
					}
					const FVector OverEye = FVector(Slot.WorldLocation.X, Slot.WorldLocation.Y, RoofZ) + Outward * 35.f + FVector(0.f, 0.f, 145.f);
					const FVector PmcChest = GroundNav.Location + FVector(0.f, 0.f, 110.f);
					FHitResult LosHit;
					if (World->LineTraceSingleByChannel(LosHit, OverEye, PmcChest, ECC_Visibility, TraceParams))
					{
						continue;
					}
					// Opposite/inside wall target so full-wall block can be verified from the exact same roof cover slot.
					const FVector OppositePoint = FVector(Centre.X, Centre.Y, GroundZ + 110.f) - Outward * 1200.f;
					FHitResult WallHit;
					if (!World->LineTraceSingleByChannel(WallHit, OverEye, OppositePoint, ECC_Visibility, TraceParams))
					{
						continue;
					}
					Out.bFound = true;
					Out.ParapetSlot = Slot;
					Out.RoofSpot = Slot.WorldLocation;
					Out.GroundPmcSpot = GroundNav.Location;
					Out.FullWallBlockedSpot = OppositePoint;
					Out.BuildingId = B->GetStringField(TEXT("id"));
					return Out;
				}
			}
		}
		return Out;
	}

	bool Step(FState& State, float DeltaTime)
	{
		UWorld* World = State.World.Get();
		if (!World)
		{
			return false;
		}
		switch (State.Gate.Poll(World))
		{
		case SmokeUtils::EPreloadGate::Waiting: return true;
		case SmokeUtils::EPreloadGate::Failed:
			Check(State, false, State.Gate.FailReason);
			return Finish(State, false);
		default: break;
		}

		State.TotalTime += DeltaTime;
		State.StageTime += DeltaTime;
		if (State.TotalTime > TimeoutSeconds)
		{
			Check(State, false, FString::Printf(TEXT("timeout in stage %d"), static_cast<int32>(State.Stage)));
			return Finish(State, false);
		}

		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UPmcSquadSubsystem* Pmc = UPmcSquadSubsystem::Get(World);
		UGrenadeSubsystem* Grenades = World->GetSubsystem<UGrenadeSubsystem>();
		UFirstAidSubsystem* FirstAid = UFirstAidSubsystem::Get(World);
		if (!Squad || !Pmc || !Grenades || !FirstAid || Squad->GetMembers().Num() < 3)
		{
			if (State.TotalTime < 15.f)
			{
				return true;
			}
			Check(State, false, TEXT("required subsystems and 3 squad members"));
			return Finish(State, false);
		}

		switch (State.Stage)
		{
		case EStage::Setup:
		{
			if (State.StageTime < 2.f)
			{
				for (AOperativeCharacter* M : Squad->GetMembers())
				{
					M->bTacticalCeaseFire = true;
				}
				return true;
			}
			SmokeUtils::StopLevelPmc(World);
			TArray<AOperativeCharacter*> Members = Squad->GetMembers();
			Members.Sort([](const AOperativeCharacter& L, const AOperativeCharacter& R) { return L.SquadIndex < R.SquadIndex; });
			State.Shooter = Members[0];
			State.Helper = Members[1];
			State.Patient = Members[2];
			if (!Squad->IsSoloMode())
			{
				Squad->EnterSoloMode();
			}
			TArray<const AActor*> Ignored;
			for (AOperativeCharacter* M : Members)
			{
				M->bTacticalCeaseFire = true;
				M->ColdLevel = 0.f;
				Ignored.Add(M);
			}

			const FRoofSetup Roof = FindX6RoofAndTargets(World, Ignored);
			Check(State, Roof.bFound, FString::Printf(TEXT("found X6 walkable roof LowCover parapet + 20-40 m ground target (%s)"), *Roof.BuildingId));
			if (!Roof.bFound)
			{
				return Finish(State, false);
			}

			// Keep helper & patient well away so they neither obstruct Shooter nor share targets during roof shooting.
			PlaceOnSpot(State.Helper.Get(), Roof.FullWallBlockedSpot + FVector(0.f, 0.f, 10.f), 0.f);
			PlaceOnSpot(State.Patient.Get(), Roof.FullWallBlockedSpot + FVector(200.f, 0.f, 10.f), 0.f);
			PlaceOnSpot(State.Shooter.Get(), Roof.RoofSpot, static_cast<float>((-Roof.ParapetSlot.WallNormal).Rotation().Yaw));
			State.Shooter->EnterCover(Roof.ParapetSlot);
			State.Shooter->SwitchToWeaponById(TEXT("m16"));
			State.Shooter->CurrentClip = 30;
			State.Shooter->ReserveAmmo = 90;

			AOperativeCharacter* Target = Pmc->SpawnFighter(1, Roof.GroundPmcSpot, FRotator::ZeroRotator, 77, 0);
			Check(State, Target != nullptr && Target->HealthComponent != nullptr, TEXT("spawned ground PMC target 24-36 m from roof"));
			if (!Target || !Target->HealthComponent)
			{
				return Finish(State, false);
			}
			Target->HealthComponent->RestoreHealth(PmcStartHp, PmcStartHp);
			Target->bTacticalCeaseFire = true;
			Target->CustomTimeDilation = 0.f;
			State.GroundPmc = Target;

			// Pure + world gate verification right from LowCover behind 95 cm roof parapet.
			Check(State, State.Shooter->bInCover && State.Shooter->CurrentCoverHeight == ECoverHeight::LowCover,
				TEXT("roof shooter entered LowCover behind X6 parapet"));
			Check(State, State.Shooter->HasUsefulFireLine(Target->GetActorLocation()),
				TEXT("HasUsefulFireLine over X6 LowCover roof parapet reaches ground PMC"));
			Check(State, !State.Shooter->HasUsefulFireLine(Roof.FullWallBlockedSpot),
				TEXT("HasUsefulFireLine still blocked by full wall"));

			Squad->SetAutonomousSquadCombat(true);
			Squad->SetSquadPosture(ESquadFirePosture::Aggressive);
			if (IConsoleVariable* TraceVar = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Debug.FireTrace")))
			{
				TraceVar->Set(1, ECVF_SetByConsole);
			}
			State.Shooter->bTacticalCeaseFire = false;
			Pmc->RefreshSight();
			Next(State, EStage::RoofShoot);
			return true;
		}
		case EStage::RoofShoot:
		{
			AOperativeCharacter* Shooter = State.Shooter.Get();
			AOperativeCharacter* Target = State.GroundPmc.Get();
			if (!Shooter || !Target || !Target->HealthComponent)
			{
				Check(State, false, TEXT("roof shooter / PMC target valid"));
				return Finish(State, false);
			}
			const float Hp = Target->HealthComponent->GetCurrentHealth();
			if (Hp < PmcStartHp - 1.f)
			{
				Check(State, true, FString::Printf(TEXT("roof shooter fired and hit ground PMC (HP %.0f -> %.0f, clip %d/30)"), PmcStartHp, Hp, Shooter->CurrentClip));
				Shooter->bTacticalCeaseFire = true;
				Pmc->DestroyTeam(1);

				// Setup open arena near ground PMC spot for grenade facing + first aid checks.
				const TArray<const AActor*> Ignored = { State.Shooter.Get(), State.Helper.Get(), State.Patient.Get() };
				const SmokeUtils::FClearArena Arena = SmokeUtils::FindClearArena(World, Target->GetActorLocation(), FVector::ForwardVector,
					FBox2D(FVector2D(-500.f, -500.f), FVector2D(1200.f, 500.f)), Ignored);
				State.ClearArenaOrigin = Arena.bFound ? Arena.Origin : Target->GetActorLocation();

				PlaceOnSpot(Shooter, State.ClearArenaOrigin, 0.f);
				Shooter->SetStance(EOperativeStance::Standing);
				Shooter->GrenadesCount = 3;
				// Throw 153 deg behind shooter (+X facing -> target behind/left).
				State.ThrowTarget = State.ClearArenaOrigin + FVector(-800.f, 450.f, 0.f);
				State.ThrowDesiredYaw = GrenadeRules::ThrowYaw(State.ClearArenaOrigin, State.ThrowTarget, 0.f);
				State.bSawThrowClipStart = false;
				State.ActiveGrenade = Grenades->ThrowAt(Shooter, State.ThrowTarget);
				Check(State, State.ActiveGrenade.IsValid(), TEXT("squad grenade throw started"));
				Next(State, EStage::GrenadeOp);
				return true;
			}
			if (State.StageTime > 10.f)
			{
				FVector PeekEye = FVector::ZeroVector;
				FVector PeekDir = FVector::ZeroVector;
				const bool bPeek = Shooter->GetPeekSight(PeekEye, PeekDir);
				const bool bLineOk = Shooter->HasUsefulFireLine(Target->GetActorLocation());
				const TCHAR* GateReason = TEXT("?");
				EFireGateBlock GateBlock = EFireGateBlock::None;
				const bool bGateOk = Shooter->CanFireAt(Target->GetActorLocation(), GateReason, &GateBlock);
				Check(State, false, FString::Printf(TEXT("roof shooter did not hit ground PMC within 10 s (HP %.0f, clip %d, inCover %d, teamVis %d, canShoot %d, canCovFire %d, peek %d, lineOk %d, gateOk %d [%s], coverDec %s)"),
					Hp, Shooter->CurrentClip, Shooter->bInCover ? 1 : 0, Pmc->IsVisibleToTeam(0, Target) ? 1 : 0,
					Shooter->CanShoot() ? 1 : 0, Shooter->CanFireFromCover() ? 1 : 0, bPeek ? 1 : 0,
					bLineOk ? 1 : 0, bGateOk ? 1 : 0, GateReason,
					CoverFacingRules::CoverFireAtName(Shooter->DecideCoverFireAtPoint(Target->GetActorLocation(), true))));
				return Finish(State, false);
			}
			return true;
		}
		case EStage::GrenadeOp:
		case EStage::GrenadePmc:
		{
			const bool bPmcStage = State.Stage == EStage::GrenadePmc;
			AOperativeCharacter* Thrower = bPmcStage ? State.PmcThrower.Get() : State.Shooter.Get();
			AGrenadeActor* Grenade = State.ActiveGrenade.Get();
			if (!Thrower || !Grenade)
			{
				Check(State, false, TEXT("grenade thrower / grenade valid"));
				return Finish(State, false);
			}
			if (!Grenade->IsPreTurnPending())
			{
				State.bSawThrowClipStart = true;
			}
			if (Grenade->HasReleased())
			{
				const float ErrDeg = GrenadeRules::ThrowErrorDeg(static_cast<float>(Thrower->GetActorRotation().Yaw), State.ThrowDesiredYaw);
				Check(State, State.bSawThrowClipStart && ErrDeg <= 20.f,
					FString::Printf(TEXT("%s grenade throw yaw err at release = %.1f deg (<= 20)"),
						bPmcStage ? TEXT("PMC G014") : TEXT("Operative"), ErrDeg));
				if (!bPmcStage)
				{
					AOperativeCharacter* ThrowerPmc = Pmc->SpawnFighter(1, State.ClearArenaOrigin + FVector(300.f, 0.f, 0.f), FRotator(0.f, 90.f, 0.f), 88, 0);
					Check(State, ThrowerPmc != nullptr, TEXT("spawned PMC grenade thrower (G014)"));
					if (!ThrowerPmc)
					{
						return Finish(State, false);
					}
					ThrowerPmc->bTacticalCeaseFire = true;
					ThrowerPmc->SetStance(EOperativeStance::Standing);
					ThrowerPmc->GrenadesCount = 2;
					State.PmcThrower = ThrowerPmc;
					State.ThrowTarget = ThrowerPmc->GetActorLocation() + FVector(700.f, -300.f, 0.f);
					State.ThrowDesiredYaw = GrenadeRules::ThrowYaw(ThrowerPmc->GetActorLocation(), State.ThrowTarget, 90.f);
					State.bSawThrowClipStart = false;
					State.ActiveGrenade = Grenades->ThrowAt(ThrowerPmc, State.ThrowTarget);
					Check(State, State.ActiveGrenade.IsValid(), TEXT("PMC grenade throw started"));
					Next(State, EStage::GrenadePmc);
					return true;
				}
				Pmc->DestroyTeam(1);
				// Setup First Aid behind test (180 deg away); move Shooter clear of Helper/Patient first.
				PlaceOnSpot(State.Shooter.Get(), State.ClearArenaOrigin - FVector(400.f, 0.f, 0.f), 0.f);
				PlaceOnSpot(State.Patient.Get(), State.ClearArenaOrigin, 0.f);
				State.Patient->HealthComponent->RestoreHealth(State.Patient->HealthComponent->GetMaxHealth() * 0.15f,
					State.Patient->HealthComponent->GetMaxHealth());
				Next(State, EStage::AidBehindGoDown);
				return true;
			}
			if (State.StageTime > 5.f)
			{
				Check(State, false, TEXT("grenade did not release within 5 s"));
				return Finish(State, false);
			}
			return true;
		}
		case EStage::AidBehindGoDown:
		case EStage::AidBesideGoDown:
		{
			const bool bBeside = State.Stage == EStage::AidBesideGoDown;
			AOperativeCharacter* Helper = State.Helper.Get();
			AOperativeCharacter* Patient = State.Patient.Get();
			if (!Helper || !Patient || !Patient->HealthComponent)
			{
				return Finish(State, false);
			}
			Patient->HealthComponent->RestoreHealth(Patient->HealthComponent->GetMaxHealth() * 0.15f,
				Patient->HealthComponent->GetMaxHealth());
			if (Patient->GetStance() != EOperativeStance::Prone || State.StageTime < 1.5f)
			{
				if (State.StageTime > 10.f)
				{
					Check(State, false, TEXT("patient did not enter critical prone state"));
					return Finish(State, false);
				}
				return true;
			}
			State.PatientYaw0 = static_cast<float>(Patient->GetActorRotation().Yaw);
			Helper->MedkitsCount = 3;
			if (!bBeside)
			{
				// Patient directly BEHIND helper (180 deg away).
				PlaceOnSpot(Helper, Patient->GetActorLocation() - FVector(0.f, 0.f, Patient->GetSimpleCollisionHalfHeight()) + FVector(95.f, 0.f, 0.f), 0.f);
			}
			else
			{
				// Patient BESIDE helper (90 deg to his right while helper faces -Y/+X off-axis).
				PlaceOnSpot(Helper, Patient->GetActorLocation() - FVector(0.f, 0.f, Patient->GetSimpleCollisionHalfHeight()) + FVector(0.f, 95.f, 0.f), 180.f);
			}
			State.RecordsBefore = FirstAid->GetRecords().Num();
			State.AidClipFrames = 0;
			State.MaxAidChestErrDeg = -1.f;
			const FirstAidRules::ERefusal Refusal = FirstAid->RequestAid(Helper, Patient, EFirstAidItem::Medkit, true);
			Check(State, Refusal == FirstAidRules::ERefusal::None,
				FString::Printf(TEXT("FirstAid (%s) RequestAid accepted (%s)"), bBeside ? TEXT("beside") : TEXT("behind"), FirstAidRules::RefusalName(Refusal)));
			Next(State, bBeside ? EStage::AidBesideRun : EStage::AidBehindRun);
			return true;
		}
		case EStage::AidBehindRun:
		case EStage::AidBesideRun:
		{
			const bool bBeside = State.Stage == EStage::AidBesideRun;
			AOperativeCharacter* Helper = State.Helper.Get();
			AOperativeCharacter* Patient = State.Patient.Get();
			const UOperativeAnimInstance* HelperAnim = UOperativeAnimInstance::Of(Helper);
			if (Helper && Patient && HelperAnim && HelperAnim->IsFirstAidClipActive())
			{
				++State.AidClipFrames;
				const FFirstAidRecord* Running = FirstAid->FindRunning(Helper);
				const bool bLying = Running ? FirstAidRules::IsLyingPose(Running->PatientPose) : true;
				const float WantYaw = FirstAidRules::HelperFaceYaw(FirstAidRules::GetConfig(), bLying, Patient->GetActorLocation(),
					State.PatientYaw0, Helper->GetActorLocation(), static_cast<float>(Helper->GetActorRotation().Yaw));
				const float ErrDeg = FMath::Abs(FRotator3f::NormalizeAxis(static_cast<float>(Helper->GetActorRotation().Yaw) - WantYaw));
				State.MaxAidChestErrDeg = FMath::Max(State.MaxAidChestErrDeg, ErrDeg);
			}
			if (FirstAid->GetRecords().Num() > State.RecordsBefore)
			{
				Check(State, State.AidClipFrames > 0 && State.MaxAidChestErrDeg >= 0.f && State.MaxAidChestErrDeg <= 30.f,
					FString::Printf(TEXT("FirstAid (%s) max yaw vs chest = %.1f deg (<= 30, frames %d)"),
						bBeside ? TEXT("beside") : TEXT("behind"), State.MaxAidChestErrDeg, State.AidClipFrames));
				if (!bBeside)
				{
					PlaceOnSpot(Patient, State.ClearArenaOrigin, 90.f);
					Patient->HealthComponent->RestoreHealth(Patient->HealthComponent->GetMaxHealth() * 0.15f,
						Patient->HealthComponent->GetMaxHealth());
					Next(State, EStage::AidBesideGoDown);
					return true;
				}
				return Finish(State, true);
			}
			if (State.StageTime > 15.f)
			{
				Check(State, false, FString::Printf(TEXT("FirstAid (%s) treatment did not finish"), bBeside ? TEXT("beside") : TEXT("behind")));
				return Finish(State, false);
			}
			return true;
		}
		default:
			return Finish(State, false);
		}
	}

	void Run(const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		TSharedRef<FState> State = MakeShared<FState>();
		State->World = World;
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float DeltaTime)
		{
			return Step(*State, DeltaTime);
		}));
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.RoofFightSmoke"),
		TEXT("Dev check (G023): roof LowCover shooting + full wall block + grenade facing (op & PMC) + first aid chest facing; PASS/FAIL then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
