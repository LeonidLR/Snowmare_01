// Dev-only headless check of FIRST AID BY WOUND TIER (the game designer 2026-11-16, Survival/FirstAidRules, UFirstAidSubsystem) on L_MovementTest:
//   powershell -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -NoRetry -ExtraArgs -Multiprocess -Commands FirstAidSmoke
// Two operatives in Solo (the others parked, the enemies removed, autonomy off, cease fire), through the REAL input path:
//  1. LIGHT (40 % HP): the drawer's medkit line (UInventoryDrawerWidget::Activate = a click) -> a STANDING self-treatment: a self_standing clip
//     on the FullBody slot, the weapon hidden, the HP lands at apply_at, one medkit used;
//  2. MEDIUM (28 %): the same click -> a KNEELING clip (self_kneeling);
//  3. HEAVY (15 %, he goes down by himself): H / the drawer click on HIMSELF is refused ("cannot treat himself"), nothing used;
//  4. HEAVY: the other operative's drawer medkit line dragged (CreateDragOperation) onto the wounded's PORTRAIT (UActionBarWidget::
//     HandleTransferDropOnSlot) from 6 m: he walks up, the helper plays the medkit's FAST helper clip, the wounded the lying patient clip, the
//     heal lands, the tier improves, one medkit used;
//  5. HEAVY: the medical bag dragged onto the wounded's 3D MODEL (ACodexTacticsHUD::HandleTransferDropOnActor, the world drop): the bag's longer
//     helper clip, a longer treatment, more HP than the medkit, the bag used;
//  6. CANCEL: a medkit drop again, a move order for the helper mid-treatment -> interrupted (a new order), no heal, the medkit KEPT
//     (consume_on_cancel false), the clips stopped.
// Before the copies exist (Scripts/Editor/setup_first_aid_animation.py) the clip checks fail with "no clip"; the rules still run.
// Nothing is saved. Godot reference: none (UE-only; Godot's medkit was instant, player.gd heal_with_item).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeAnimInstance.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Survival/FirstAidRules.h"
#include "Survival/FirstAidSubsystem.h"
#include "Characters/SquadTransferSubsystem.h"
#include "UI/ActionBarWidget.h"
#include "UI/CodexTacticsHUD.h"
#include "UI/InventoryDragDropOperation.h"
#include "UI/InventoryDrawerWidget.h"

namespace FirstAidSmoke
{
	using namespace FirstAidRules;

	constexpr float Timeout = 300.f;
	constexpr float NavWarmupSeconds = 3.f;
	constexpr float LightPct = 0.40f;
	constexpr float MediumPct = 0.28f;
	constexpr float HeavyPct = 0.15f;
	constexpr float HelperDistanceCm = 600.f;
	/** A stage waits this long for its treatment to end (s; the longest is the bag's 11 s + the walk). */
	constexpr float StageLimit = 40.f;

	enum class EStage : uint8
	{
		Setup,
		LightStart,
		LightRun,
		MediumStart,
		MediumRun,
		HeavySelf,
		HeavyGoDown,
		MedkitAidStart,
		MedkitAidRun,
		BagAidStart,
		BagAidRun,
		CancelStart,
		CancelRun,
		CancelCheck,
		Done
	};

	struct FRunSamples
	{
		int32 Frames = 0;
		int32 HelperClipFrames = 0;
		int32 PatientClipFrames = 0;
		int32 WeaponHiddenFrames = 0;
		FString HelperClip;
		FString PatientClip;
		float MaxHelperSlot = 0.f;
		float MaxPatientSlot = 0.f;
	};

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		EStage Stage = EStage::Setup;
		int32 Failures = 0;
		bool bDone = false;
		SmokeUtils::FPreloadGate Gate;
		TWeakObjectPtr<AOperativeCharacter> A;
		TWeakObjectPtr<AOperativeCharacter> B;
		FVector Spot = FVector::ZeroVector;
		FVector Forward = FVector::ForwardVector;
		int32 RecordsBefore = 0;
		int32 CountBefore = 0;
		float HealthBefore = 0.f;
		float MedkitGain = 0.f;
		float MedkitSeconds = 0.f;
		FRunSamples Samples;
		TArray<FString> Report;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State, bool bComplete)
	{
		FirstAidRules::ResetConfig();
		for (const FString& Line : State.Report)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: %s"), *Line);
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		State.bDone = true;
		FPlatformMisc::RequestExit(false, TEXT("FirstAidSmoke"));
		return false;
	}

	void Next(FState& State, EStage Stage)
	{
		State.Stage = Stage;
		State.StageTime = 0.f;
	}

	void SetHealthShare(AOperativeCharacter* Op, float Share)
	{
		if (Op && Op->HealthComponent)
		{
			Op->HealthComponent->RestoreHealth(Share * Op->HealthComponent->GetMaxHealth(), Op->HealthComponent->GetMaxHealth());
		}
	}

	float Health(const AOperativeCharacter* Op)
	{
		return Op && Op->HealthComponent ? Op->HealthComponent->GetCurrentHealth() : 0.f;
	}

	ACodexTacticsHUD* GetHud(UWorld* World)
	{
		APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
		return PC ? Cast<ACodexTacticsHUD>(PC->GetHUD()) : nullptr;
	}

	/** The newest record after the stage started (null while the treatment runs). */
	const FFirstAidRecord* NewRecord(const UFirstAidSubsystem* FirstAid, const FState& State)
	{
		return FirstAid && FirstAid->GetRecords().Num() > State.RecordsBefore ? &FirstAid->GetRecords().Last() : nullptr;
	}

	void Sample(FState& State)
	{
		const UOperativeAnimInstance* HelperAnim = UOperativeAnimInstance::Of(State.A.Get());
		const UOperativeAnimInstance* PatientAnim = State.B.IsValid() && State.B != State.A ? UOperativeAnimInstance::Of(State.B.Get()) : nullptr;
		++State.Samples.Frames;
		if (HelperAnim && HelperAnim->IsFirstAidClipActive())
		{
			++State.Samples.HelperClipFrames;
			State.Samples.HelperClip = HelperAnim->GetFirstAidClipName();
			State.Samples.MaxHelperSlot = FMath::Max(State.Samples.MaxHelperSlot, HelperAnim->GetSlotMontageGlobalWeight(HelperAnim->FullBodySlot));
		}
		if (PatientAnim && PatientAnim->IsFirstAidClipActive())
		{
			++State.Samples.PatientClipFrames;
			State.Samples.PatientClip = PatientAnim->GetFirstAidClipName();
			State.Samples.MaxPatientSlot = FMath::Max(State.Samples.MaxPatientSlot, PatientAnim->GetSlotMontageGlobalWeight(PatientAnim->FullBodySlot));
		}
		const AOperativeCharacter* A = State.A.Get();
		State.Samples.WeaponHiddenFrames += A && A->WeaponMesh && !A->WeaponMesh->IsVisible() ? 1 : 0;
	}

	/** Starts a stage's treatment bookkeeping. */
	void Arm(FState& State, const UFirstAidSubsystem* FirstAid, AOperativeCharacter* CountOwner, EFirstAidItem Item, const AOperativeCharacter* Patient)
	{
		State.RecordsBefore = FirstAid ? FirstAid->GetRecords().Num() : 0;
		State.CountBefore = CountOwner ? UFirstAidSubsystem::CountOf(*CountOwner, Item) : 0;
		State.HealthBefore = Health(Patient);
		State.Samples = FRunSamples();
	}

	void Place(UWorld* World, AOperativeCharacter* Who, const FVector& Spot, const FVector& Facing)
	{
		if (Who)
		{
			Who->StopOperative();
			Who->TeleportTo(SmokeUtils::FreeSpot(World, Spot, Who), FRotator(0.f, Facing.Rotation().Yaw, 0.f), false, true);
		}
	}

	/** The slot index of Member on the action bar (INDEX_NONE when not shown). */
	int32 SlotOf(const UActionBarWidget* Bar, const AOperativeCharacter* Member)
	{
		for (int32 Index = 0; Bar && Index < 8; ++Index)
		{
			if (Bar->GetSlotMember(Index) == Member)
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}

	/** Checks a finished self-treatment record. */
	void CheckSelfRecord(FState& State, const FFirstAidRecord& Record, EMode Mode, const TArray<FString>& Set, const AOperativeCharacter* A, const TCHAR* Name)
	{
		Check(State, Record.bCompleted && Record.bApplied && Record.Mode == Mode, FString::Printf(TEXT("[%s] completed in mode '%s' (%s, %.1f / %.1f s, interrupt %s)"),
			Name, ModeName(Record.Mode), Record.bCompleted ? TEXT("done") : TEXT("not done"), Record.Elapsed, Record.Seconds, InterruptName(Record.Interrupt)));
		Check(State, Set.Contains(Record.HelperClip) && State.Samples.HelperClipFrames >= State.Samples.Frames * 8 / 10 && State.Samples.MaxHelperSlot > 0.9f,
			FString::Printf(TEXT("[%s] the clip '%s' (one of the %s set) played on the FullBody slot %d / %d frames (weight %.2f)"), Name, *Record.HelperClip,
				ModeName(Mode), State.Samples.HelperClipFrames, State.Samples.Frames, State.Samples.MaxHelperSlot));
		Check(State, Health(A) > State.HealthBefore + 1.f && Record.HealthAfter > Record.HealthBefore,
			FString::Printf(TEXT("[%s] HP up %.0f -> %.0f, tier %s -> %s"), Name, State.HealthBefore, Health(A), TierKey(Record.TierBefore), TierKey(Record.TierAfter)));
		Check(State, A && A->MedkitsCount == State.CountBefore - 1, FString::Printf(TEXT("[%s] one medkit used (%d -> %d)"), Name, State.CountBefore, A ? A->MedkitsCount : -1));
		Check(State, State.Samples.WeaponHiddenFrames > 0 && A && A->WeaponMesh && A->WeaponMesh->IsVisible(),
			FString::Printf(TEXT("[%s] the weapon hidden while treating (%d frames) and shown again"), Name, State.Samples.WeaponHiddenFrames));
		State.Report.Add(FString::Printf(TEXT("%s: %s, %.1f s, %.0f -> %.0f HP"), Name, *Record.HelperClip, Record.Seconds, Record.HealthBefore, Record.HealthAfter));
	}

	bool Step(UWorld* World, FState& State, float DeltaTime)
	{
		switch (State.Gate.Poll(World))
		{
		case SmokeUtils::EPreloadGate::Waiting: return true;
		case SmokeUtils::EPreloadGate::Failed:
			Check(State, false, State.Gate.FailReason);
			return Finish(State, false);
		default: break;
		}
		State.Time += DeltaTime;
		State.StageTime += DeltaTime;
		if (State.Time > Timeout)
		{
			Check(State, false, FString::Printf(TEXT("timeout in stage %d"), static_cast<int32>(State.Stage)));
			return Finish(State, false);
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UFirstAidSubsystem* FirstAid = UFirstAidSubsystem::Get(World);
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(UGameplayStatics::GetPlayerController(World, 0));
		ACodexTacticsHUD* Hud = GetHud(World);
		UInventoryDrawerWidget* Drawer = Hud ? Hud->GetInventoryDrawer() : nullptr;
		UActionBarWidget* Bar = Hud ? Hud->GetActionBar() : nullptr;
		if (!Squad || !FirstAid || !PC || !Drawer || !Bar || Squad->GetMembers().Num() < 2)
		{
			if (State.Time < 15.f)
			{
				return true;
			}
			Check(State, false, TEXT("squad of 2+, the first-aid subsystem, the player controller, the drawer and the action bar"));
			return Finish(State, false);
		}
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			Member->ColdLevel = 0.f;
			Member->bTacticalCeaseFire = true;
		}
		AOperativeCharacter* A = State.A.Get();
		AOperativeCharacter* B = State.B.Get();
		if (State.Stage != EStage::Setup && (!A || !B))
		{
			Check(State, false, TEXT("the two operatives are still there"));
			return Finish(State, false);
		}
		const FFirstAidConfig& Config = GetConfig();

		switch (State.Stage)
		{
		case EStage::Setup:
		{
			if (State.StageTime < NavWarmupSeconds)
			{
				return true;
			}
			TArray<AOperativeCharacter*> Members = Squad->GetMembers();
			Members.Sort([](const AOperativeCharacter& L, const AOperativeCharacter& R) { return L.SquadIndex < R.SquadIndex; });
			State.A = A = Members[0];
			State.B = B = Members[1];
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				It->Destroy();
			}
			Squad->SetAutonomousSquadCombat(false);
			Squad->SetSquadPosture(ESquadFirePosture::Passive);
			Squad->SetLeader(A);
			if (!Squad->IsSoloMode())
			{
				Squad->EnterSoloMode();
			}
			TArray<const AActor*> Ignore;
			for (AOperativeCharacter* Member : Members)
			{
				Ignore.Add(Member);
				if (Member->GetMesh())
				{
					Member->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
				}
			}
			const SmokeUtils::FClearArena Arena = SmokeUtils::FindClearArena(World, A->GetActorLocation(), A->GetActorForwardVector().GetSafeNormal2D(),
				FBox2D(FVector2D(-400.f, -400.f), FVector2D(HelperDistanceCm + 400.f, 400.f)), Ignore);
			State.Spot = Arena.bFound ? Arena.Origin + FVector(0.f, 0.f, A->GetSimpleCollisionHalfHeight() + 5.f) : A->GetActorLocation();
			State.Forward = Arena.bFound ? Arena.Forward.GetSafeNormal2D() : A->GetActorForwardVector().GetSafeNormal2D();
			Check(State, Arena.bFound, FString::Printf(TEXT("a clear %.0f m patch at %s"), (HelperDistanceCm + 400.f) / 100.f, *State.Spot.ToCompactString()));
			// The others far away (no medic heals the wounded on his own).
			for (int32 Index = 2; Index < Members.Num(); ++Index)
			{
				Place(World, Members[Index], State.Spot - State.Forward * 2500.f + FVector(0.f, 300.f * Index, 0.f), State.Forward);
			}
			Place(World, A, State.Spot, State.Forward);
			Place(World, B, State.Spot + State.Forward * HelperDistanceCm, -State.Forward);
			A->MedkitsCount = 5;
			A->MedicalBagsCount = 1;
			B->MedkitsCount = 2;
			UE_LOG(LogCodexTactics, Display, TEXT("FirstAidSmoke: %s (helper / self) and %s (the wounded), max HP %.0f / %.0f"), *A->DisplayName.ToString(),
				*B->DisplayName.ToString(), A->HealthComponent->GetMaxHealth(), B->HealthComponent->GetMaxHealth());
			Next(State, EStage::LightStart);
			return true;
		}
		case EStage::LightStart:
		{
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			SetHealthShare(A, LightPct);
			Check(State, UFirstAidSubsystem::TierOf(*A) == ETier::Light, FString::Printf(TEXT("[light] %.0f %% HP = the light tier (%s)"), LightPct * 100.f,
				TierKey(UFirstAidSubsystem::TierOf(*A))));
			Arm(State, FirstAid, A, EFirstAidItem::Medkit, A);
			Hud->ToggleInventoryDrawer();
			Drawer->Activate(EInventoryDrawerSlot::Medkit); // the drawer's medkit line clicked
			const FFirstAidRecord* Running = FirstAid->FindRunning(A);
			Check(State, Running && Running->Mode == EMode::SelfStanding, FString::Printf(TEXT("[light] the drawer click starts a STANDING self-treatment (%s)"),
				Running ? ModeName(Running->Mode) : TEXT("none")));
			Check(State, A->MedkitsCount == State.CountBefore && Health(A) == State.HealthBefore, TEXT("[light] nothing applied at the start (the clip first)"));
			Next(State, EStage::LightRun);
			return true;
		}
		case EStage::LightRun:
		case EStage::MediumRun:
		{
			const bool bLight = State.Stage == EStage::LightRun;
			const FFirstAidRecord* Record = NewRecord(FirstAid, State);
			if (!Record)
			{
				Sample(State);
				if (State.StageTime > StageLimit)
				{
					Check(State, false, TEXT("the self-treatment never ended"));
					return Finish(State, false);
				}
				return true;
			}
			CheckSelfRecord(State, *Record, bLight ? EMode::SelfStanding : EMode::SelfKneeling,
				bLight ? Config.Medkit.SelfStandingClips : Config.Medkit.SelfKneelingClips, A, bLight ? TEXT("light") : TEXT("medium"));
			Check(State, FMath::IsNearlyEqual(Record->Seconds, TreatmentSeconds(Config, EFirstAidItem::Medkit, bLight ? ETier::Light : ETier::Medium, true), 0.01f),
				FString::Printf(TEXT("[%s] the treatment's length from first_aid.json (%.1f s)"), bLight ? TEXT("light") : TEXT("medium"), Record->Seconds));
			Next(State, bLight ? EStage::MediumStart : EStage::HeavySelf);
			return true;
		}
		case EStage::MediumStart:
		{
			if (State.StageTime < 1.f)
			{
				return true;
			}
			SetHealthShare(A, MediumPct);
			Check(State, UFirstAidSubsystem::TierOf(*A) == ETier::Medium, FString::Printf(TEXT("[medium] %.0f %% HP = the medium tier (%s)"), MediumPct * 100.f,
				TierKey(UFirstAidSubsystem::TierOf(*A))));
			Arm(State, FirstAid, A, EFirstAidItem::Medkit, A);
			Drawer->Activate(EInventoryDrawerSlot::Medkit);
			const FFirstAidRecord* Running = FirstAid->FindRunning(A);
			Check(State, Running && Running->Mode == EMode::SelfKneeling, FString::Printf(TEXT("[medium] the drawer click starts a KNEELING self-treatment (%s)"),
				Running ? ModeName(Running->Mode) : TEXT("none")));
			Next(State, EStage::MediumRun);
			return true;
		}
		case EStage::HeavySelf:
		{
			if (State.StageTime < 1.f)
			{
				return true;
			}
			// B goes heavy; as the leader he cannot treat himself (the drawer's medkit line / H).
			SetHealthShare(B, HeavyPct);
			Squad->SetLeader(B);
			Arm(State, FirstAid, B, EFirstAidItem::Medkit, B);
			PC->UseSquadItem(EPersonalItem::Medkit); // H
			Check(State, !FirstAid->IsInTreatment(B) && B->MedkitsCount == State.CountBefore && FMath::IsNearlyEqual(Health(B), State.HealthBefore, 0.5f),
				FString::Printf(TEXT("[heavy] H on himself is refused (tier %s): no treatment, nothing used"), TierKey(UFirstAidSubsystem::TierOf(*B))));
			Squad->SetLeader(A);
			Next(State, EStage::HeavyGoDown);
			return true;
		}
		case EStage::HeavyGoDown:
		{
			SetHealthShare(B, HeavyPct);
			if (B->GetStance() != EOperativeStance::Prone || State.StageTime < 2.f)
			{
				if (State.StageTime > 15.f)
				{
					Check(State, false, TEXT("[heavy] the wounded never went down"));
					return Finish(State, false);
				}
				return true;
			}
			Check(State, B->IsCriticallyWounded() && UFirstAidSubsystem::TierOf(*B) == ETier::Heavy, TEXT("[heavy] 15 % HP: down, the heavy tier"));
			Next(State, EStage::MedkitAidStart);
			return true;
		}
		case EStage::MedkitAidStart:
		case EStage::BagAidStart:
		case EStage::CancelStart:
		{
			if (State.StageTime < 1.f)
			{
				return true;
			}
			const bool bBag = State.Stage == EStage::BagAidStart;
			const EFirstAidItem Item = bBag ? EFirstAidItem::MedicalBag : EFirstAidItem::Medkit;
			SetHealthShare(B, HeavyPct);
			Squad->SetLeader(A);
			// The helper back 6 m away (the cancel stage: within reach - the treatment starts at once).
			Place(World, A, State.Stage == EStage::CancelStart ? B->GetActorLocation() - State.Forward * 120.f : State.Spot, State.Forward);
			Arm(State, FirstAid, A, Item, B);
			if (!Drawer->IsOpen())
			{
				Hud->ToggleInventoryDrawer();
			}
			UInventoryDragDropOperation* Drag = Drawer->CreateDragOperation(bBag ? EInventoryDrawerSlot::MedicalBag : EInventoryDrawerSlot::Medkit);
			Check(State, Drag && Drag->Sender.Get() == A, FString::Printf(TEXT("[%s] the drawer's %s line drags (the leader's stock)"), ItemKey(Item), ItemKey(Item)));
			if (!Drag)
			{
				return Finish(State, false);
			}
			ETransferRequestOutcome Outcome = ETransferRequestOutcome::Failed;
			if (bBag)
			{
				Outcome = Hud->HandleTransferDropOnActor(Drag->Sender.Get(), Drag->Item, B, B->GetActorLocation()); // dropped on his 3D model
			}
			else
			{
				Outcome = Bar->HandleTransferDropOnSlot(SlotOf(Bar, B), Drag->Sender.Get(), Drag->Item); // dropped on his portrait
			}
			const FFirstAidRecord* Running = FirstAid->FindRunning(A);
			Check(State, Running && Running->Mode == EMode::Aid && FirstAid->IsInTreatment(B) && Outcome != ETransferRequestOutcome::Failed,
				FString::Printf(TEXT("[%s] the drop on the heavily wounded starts first aid (outcome %d, %s)"), ItemKey(Item), static_cast<int32>(Outcome),
					Running ? (Running->bApproached ? TEXT("walking over") : TEXT("in reach")) : TEXT("no treatment")));
			Check(State, UFirstAidSubsystem::CountOf(*A, Item) == State.CountBefore && UFirstAidSubsystem::CountOf(*B, Item) == (bBag ? 0 : 2),
				FString::Printf(TEXT("[%s] not handed over, not used yet"), ItemKey(Item)));
			Next(State, State.Stage == EStage::MedkitAidStart ? EStage::MedkitAidRun : (bBag ? EStage::BagAidRun : EStage::CancelRun));
			return true;
		}
		case EStage::MedkitAidRun:
		case EStage::BagAidRun:
		{
			const bool bBag = State.Stage == EStage::BagAidRun;
			const EFirstAidItem Item = bBag ? EFirstAidItem::MedicalBag : EFirstAidItem::Medkit;
			const FItemConfig& Data = Config.Item(Item);
			const FFirstAidRecord* Record = NewRecord(FirstAid, State);
			if (!Record)
			{
				const FFirstAidRecord* Running = FirstAid->FindRunning(A);
				if (Running && Running->Frames > 0)
				{
					Sample(State);
				}
				SetHealthShare(A, 1.f);
				if (State.StageTime > StageLimit)
				{
					Check(State, false, FString::Printf(TEXT("[%s] the aid never ended"), ItemKey(Item)));
					return Finish(State, false);
				}
				return true;
			}
			const float Gain = Record->HealthAfter - Record->HealthBefore;
			Check(State, Record->bCompleted && Record->bApplied && Record->bApproached,
				FString::Printf(TEXT("[%s] walked over and completed (%.1f / %.1f s, interrupt %s)"), ItemKey(Item), Record->Elapsed, Record->Seconds,
					InterruptName(Record->Interrupt)));
			Check(State, Data.HelperClips.Contains(Record->HelperClip) && State.Samples.HelperClipFrames >= State.Samples.Frames * 8 / 10 && State.Samples.MaxHelperSlot > 0.9f,
				FString::Printf(TEXT("[%s] the helper's clip '%s' (the %s set) on the FullBody slot %d / %d frames"), ItemKey(Item), *Record->HelperClip, ItemKey(Item),
					State.Samples.HelperClipFrames, State.Samples.Frames));
			Check(State, Data.PatientClips.Contains(Record->PatientClip) && State.Samples.PatientClipFrames >= State.Samples.Frames * 8 / 10,
				FString::Printf(TEXT("[%s] the wounded's lying clip '%s' %d / %d frames"), ItemKey(Item), *Record->PatientClip, State.Samples.PatientClipFrames,
					State.Samples.Frames));
			Check(State, FVector::Dist2D(A->GetActorLocation(), B->GetActorLocation()) <= Config.ReachCm + 30.f,
				FString::Printf(TEXT("[%s] the helper beside the wounded (%.0f cm)"), ItemKey(Item), FVector::Dist2D(A->GetActorLocation(), B->GetActorLocation())));
			Check(State, Gain > 1.f && static_cast<int32>(Record->TierAfter) < static_cast<int32>(Record->TierBefore),
				FString::Printf(TEXT("[%s] +%.0f HP, the tier improves %s -> %s"), ItemKey(Item), Gain, TierKey(Record->TierBefore), TierKey(Record->TierAfter)));
			Check(State, UFirstAidSubsystem::CountOf(*A, Item) == State.CountBefore - 1, FString::Printf(TEXT("[%s] one used (%d -> %d)"), ItemKey(Item),
				State.CountBefore, UFirstAidSubsystem::CountOf(*A, Item)));
			Check(State, FMath::IsNearlyEqual(Record->Seconds, TreatmentSeconds(Config, Item, ETier::Heavy, false), 0.01f),
				FString::Printf(TEXT("[%s] the aid's length from first_aid.json (%.1f s)"), ItemKey(Item), Record->Seconds));
			if (bBag)
			{
				Check(State, Record->Seconds > State.MedkitSeconds && Gain > State.MedkitGain,
					FString::Printf(TEXT("[bag] longer (%.1f vs %.1f s) and more HP (+%.0f vs +%.0f) than the medkit"), Record->Seconds, State.MedkitSeconds, Gain,
						State.MedkitGain));
			}
			else
			{
				State.MedkitGain = Gain;
				State.MedkitSeconds = Record->Seconds;
				Check(State, Record->HelperClip.Contains(TEXT("Fast")), FString::Printf(TEXT("[medkit] the FAST helper variant (%s)"), *Record->HelperClip));
			}
			State.Report.Add(FString::Printf(TEXT("%s aid: helper %s, patient %s, %.1f s, %.0f -> %.0f HP"), ItemKey(Item), *Record->HelperClip, *Record->PatientClip,
				Record->Seconds, Record->HealthBefore, Record->HealthAfter));
			Next(State, bBag ? EStage::CancelStart : EStage::BagAidStart);
			return true;
		}
		case EStage::CancelRun:
		{
			const FFirstAidRecord* Running = FirstAid->FindRunning(A);
			SetHealthShare(A, 1.f);
			if (Running && Running->Frames > 0)
			{
				Sample(State);
			}
			// Mid-treatment (before the apply moment): a move order for the helper.
			const float Before = TreatmentSeconds(Config, EFirstAidItem::Medkit, ETier::Heavy, false) * Config.Medkit.ApplyAt * 0.5f;
			if (!Running || State.StageTime < Before)
			{
				if (!Running && !NewRecord(FirstAid, State))
				{
					Check(State, false, TEXT("[cancel] no treatment running"));
					return Finish(State, false);
				}
				return !Running ? (Next(State, EStage::CancelCheck), true) : true;
			}
			Check(State, State.Samples.HelperClipFrames > 0, TEXT("[cancel] the clip plays before the order"));
			const EOperativeOrderResult Order = A->OrderMoveTo(State.Spot - State.Forward * 300.f, false);
			Check(State, Order == EOperativeOrderResult::Accepted, TEXT("[cancel] the move order accepted"));
			Next(State, EStage::CancelCheck);
			return true;
		}
		case EStage::CancelCheck:
		{
			if (State.StageTime < 0.3f)
			{
				return true;
			}
			const FFirstAidRecord* Record = NewRecord(FirstAid, State);
			Check(State, Record && !Record->bCompleted && Record->Interrupt == EInterrupt::Order && !Record->bApplied,
				FString::Printf(TEXT("[cancel] interrupted by the order before the apply moment (%s, %.1f s)"), Record ? InterruptName(Record->Interrupt) : TEXT("no record"),
					Record ? Record->Elapsed : -1.f));
			Check(State, A->MedkitsCount == State.CountBefore && Record && !Record->bConsumed,
				FString::Printf(TEXT("[cancel] the medkit kept (consume_on_cancel false: %d -> %d)"), State.CountBefore, A->MedkitsCount));
			Check(State, FMath::IsNearlyEqual(Health(B), State.HealthBefore, 0.5f), FString::Printf(TEXT("[cancel] no heal (%.0f HP)"), Health(B)));
			const UOperativeAnimInstance* HelperAnim = UOperativeAnimInstance::Of(A);
			const UOperativeAnimInstance* PatientAnim = UOperativeAnimInstance::Of(B);
			Check(State, HelperAnim && !HelperAnim->IsFirstAidClipActive() && PatientAnim && !PatientAnim->IsFirstAidClipActive() && !FirstAid->IsInTreatment(A),
				TEXT("[cancel] both clips stopped, no treatment left"));
			Next(State, EStage::Done);
			return true;
		}
		default:
			if (Drawer->IsOpen())
			{
				Hud->ToggleInventoryDrawer();
			}
			return Finish(State, true);
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			UWorld* W = WeakWorld.Get();
			return W && !State->bDone && Step(W, *State, W->IsPaused() ? 0.f : W->GetDeltaSeconds());
		}), 0.f);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.FirstAidSmoke"),
		TEXT("Dev check (the game designer 2026-11-16, L_MovementTest): first aid by wound tier through the drawer / drag input - light = standing self clip, ")
		TEXT("medium = kneeling, heavy = refused on himself, a mate treats him (medkit fast via the portrait, the medical bag longer + stronger via the model), ")
		TEXT("a move order cancels before the apply moment (the medkit kept). Logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
