// Dev-only check of cover fire through a window (G016). On the dressed kit map the leader takes an existing window from
// both faces, crouches, and (when the active set is riffle) uses the crouch-over clips. A parked hound on the far side:
// the first cartridge breaks intact glass and does not hit him; the next one may. A slot shifted along the wall is not
// that window. A follower there does not take the pose, and a leader swap does not move it. Codex.Cover.RiffleOutdoor
// stays at its default here; a second run may set it to 1. An empty outdoor set is the side fallback and does not fail.
//   UnrealEditor.exe <uproject> /Game/Maps/KitPilot/L_Gen_StoryShowcase_Kit -game -nullrhi -nosound
//     -LevelJson=L_Gen_StoryShowcase_Kit.json -ExecCmds="CodexTactics.WindowCoverFireSmoke"
// Godot reference: none (UE-only).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AIController.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeAnimInstance.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "CollisionQueryParams.h"
#include "Combat/HealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "Core/CodexTacticsGameMode.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Interactables/WindowBreakage.h"
#include "Misc/CoreDelegates.h"
#include "Tactics/CoverTraceRules.h"
#include "Tactics/WindowCoverRules.h"

namespace WindowCoverFireSmoke
{
	constexpr int32 WarmupFrames = 150;
	constexpr int32 PoseFrames = 60 * 10;
	constexpr int32 MaxFrames = 60 * 90;

	enum class EStep : uint8
	{
		Warmup, Seek, Pose, Formation, Aim, Shot, Solid, Done
	};

	struct FSide
	{
		FCoverSlot Slot;
		bool bSynthetic = false;
		bool bConfirmed = false;
	};

	struct FRunner
	{
		EStep Step = EStep::Warmup;
		int32 Frame = 0;
		int32 StepFrame = 0;
		int32 Failures = 0;
		int32 SideIndex = 0;
		TWeakObjectPtr<AActor> Sill;
		FSide Sides[2];
		FCoverSlot Solid;
		bool bHaveSolid = false;
		TWeakObjectPtr<AEnemyCharacter> Hound;
		int32 GlassActors = 0;
		bool bBrokenBeforeShot = false;
		int32 ClipBefore = 0;
		float HealthBefore = 0.f;
		int32 RoundsBefore = 0;
		bool bFirstWasGlass = false;
		FDelegateHandle EndFrameHandle;
	};

	TSharedPtr<FRunner> Runner;

	void Check(bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		Runner->Failures += bOk ? 0 : 1;
	}

	void Finish()
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), Runner->Failures == 0 ? TEXT("PASS") : TEXT("FAIL"));
		FCoreDelegates::OnEndFrame.Remove(Runner->EndFrameHandle);
		Runner->Step = EStep::Done;
		FPlatformMisc::RequestExit(false, TEXT("WindowCoverFireSmoke"));
	}

	void Go(EStep Step)
	{
		Runner->Step = Step;
		Runner->StepFrame = 0;
	}

	UWorld* GameWorld()
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.World() && (Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE))
			{
				return Context.World();
			}
		}
		return nullptr;
	}

	void Put(AOperativeCharacter& Member, const FVector& Feet, float Yaw)
	{
		const float Half = Member.GetCapsuleComponent() ? Member.GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 88.f;
		Member.TeleportTo(FVector(Feet.X, Feet.Y, Feet.Z + Half + 2.f), FRotator(0.f, Yaw, 0.f), false, true);
		Member.StopOperative();
		Member.ColdLevel = 0.f;
		Member.bTacticalCeaseFire = true;
	}

	bool Enter(AOperativeCharacter& Member, const FCoverSlot& Slot)
	{
		Put(Member, Slot.WorldLocation, static_cast<float>(Slot.WallNormal.Rotation().Yaw));
		Member.EnterCover(Slot);
		return Member.bInCover;
	}

	float GroundZ(UWorld& World, const FVector& At, float FromZ)
	{
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(WindowCoverFireSmoke), false);
		const FVector From(At.X, At.Y, FromZ + 80.f);
		if (World.LineTraceSingleByChannel(Hit, From, From - FVector(0.f, 0.f, 600.f), ECC_Visibility, Params))
		{
			return Hit.ImpactPoint.Z;
		}
		return FromZ;
	}

	FVector ThinNormal(const FBox& Box)
	{
		const FVector Extent = Box.GetExtent();
		return Extent.X <= Extent.Y ? FVector(1.f, 0.f, 0.f) : FVector(0.f, 1.f, 0.f);
	}

	FCoverSlot MakeSynthetic(AActor& Sill, const FBox& Box, const FVector& Normal, float FeetZ)
	{
		FCoverSlot Slot;
		const FVector Center = Box.GetCenter();
		Slot.WallNormal = Normal;
		Slot.WallPoint = Center + Normal * 15.f;
		Slot.WallPoint.Z = Box.Max.Z + 40.f;
		Slot.WorldLocation = FVector(Slot.WallPoint.X, Slot.WallPoint.Y, FeetZ) + Normal * 45.f;
		Slot.Height = ECoverHeight::HighCover;
		Slot.WallTopCm = 240.f;
		Slot.WallActor = &Sill;
		return Slot;
	}

	bool TrySide(UWorld& World, AOperativeCharacter& Op, AActor& Sill, const FBox& Box, const FVector& Normal, FSide& Out)
	{
		const FVector Extent = Box.GetExtent();
		const float Half = FMath::Abs(FVector::DotProduct(Extent, Normal));
		FVector Cursor = Box.GetCenter() + Normal * (Half + 80.f);
		Cursor.Z = Box.Max.Z + 20.f;
		FCoverSlot Traced;
		if (CoverTraceRules::FindCoverSlotAt(&World, Cursor, -Normal, Traced) && Enter(Op, Traced) && Op.IsWindowCoverConfirmed()
			&& !Op.bAtCoverCorner && !Op.bShimmying)
		{
			Out.Slot = Op.GetCoverSlot();
			Out.bSynthetic = false;
			Out.bConfirmed = true;
			return true;
		}
		Op.LeaveCover(TEXT("WindowCoverFireSmoke: traced slot is not this window"));
		const float FeetZ = GroundZ(World, Box.GetCenter() + Normal * 60.f, Box.Min.Z);
		const FCoverSlot Synthetic = MakeSynthetic(Sill, Box, Normal, FeetZ);
		if (Enter(Op, Synthetic) && Op.IsWindowCoverConfirmed() && !Op.bAtCoverCorner)
		{
			Out.Slot = Op.GetCoverSlot();
			Out.bSynthetic = true;
			Out.bConfirmed = true;
			return true;
		}
		UE_LOG(LogCodexTactics, Display, TEXT("WindowCoverFireSmoke: %s side %s rejected (%s)"), *Sill.GetName(),
			*Normal.ToCompactString(), WindowCoverRules::RejectName(Op.GetWindowCover().Reject));
		Op.LeaveCover(TEXT("WindowCoverFireSmoke: synthetic slot rejected"));
		return false;
	}

	AEnemyCharacter* SpawnParkedHound(UWorld& World, const FVector& At)
	{
		const ACodexTacticsGameMode* GameMode = World.GetAuthGameMode<ACodexTacticsGameMode>();
		const TSoftClassPtr<AEnemyCharacter>* Soft = GameMode ? GameMode->EnemyClasses.Find(EEnemyArchetype::FrostHound) : nullptr;
		UClass* Class = Soft ? Soft->LoadSynchronous() : nullptr;
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		AEnemyCharacter* Hound = nullptr;
		{
			const AEnemyCharacter::FScopedSpawnArchetype Scope(EEnemyArchetype::FrostHound);
			Hound = World.SpawnActor<AEnemyCharacter>(Class ? Class : AEnemyCharacter::StaticClass(), At, FRotator::ZeroRotator, Params);
		}
		if (Hound)
		{
			Hound->InitializeArchetype(EEnemyArchetype::FrostHound);
			Hound->SetActorTickEnabled(false);
			Hound->CustomTimeDilation = 0.f;
		}
		return Hound;
	}

	int32 CountGlass(UWorld& World, const AActor& Sill)
	{
		const FBox Box = Sill.GetComponentsBoundingBox(true);
		int32 Count = 0;
		for (TActorIterator<AActor> It(&World); It; ++It)
		{
			if (It->ActorHasTag(FName(TEXT("window_glass"))) && WindowBreakage::IsOverSill(Box, It->GetComponentsBoundingBox(true).GetCenter()))
			{
				++Count;
			}
		}
		return Count;
	}

	int32 RiffleOutdoor()
	{
		const IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Cover.RiffleOutdoor"));
		return Var ? Var->GetInt() : 0;
	}

	bool JudgePose(AOperativeCharacter& Op)
	{
		UOperativeAnimInstance* Anim = Op.GetMesh() ? Cast<UOperativeAnimInstance>(Op.GetMesh()->GetAnimInstance()) : nullptr;
		const bool bCrouched = Op.GetStance() == EOperativeStance::Crouching;
		const bool bFlag = Anim && Anim->bCoverWindow;
		if (Runner->StepFrame < 20 && (!bCrouched || !bFlag))
		{
			return false;
		}
		Check(Op.IsWindowCoverConfirmed(), TEXT("stopped in his own window"));
		Check(!Op.IsWindowPathAllowed(), TEXT("a cover shot does not open a climb"));
		Check(bCrouched, TEXT("crouched at the window"));
		Check(bFlag, TEXT("bCoverWindow"));
		if (!Anim)
		{
			Check(false, TEXT("no anim instance"));
			return true;
		}
		const FString Set = Anim->GetActiveCoverAnimSet();
		const bool bWaitClip = Set == TEXT("riffle") && !Anim->IsCoverOverTopFireSet() && Runner->StepFrame < PoseFrames;
		if (bWaitClip)
		{
			return false;
		}
		if (Set.IsEmpty())
		{
			Check(true, FString::Printf(TEXT("outdoor set empty: side fallback (Codex.Cover.RiffleOutdoor %d)"), RiffleOutdoor()));
		}
		else if (Set == TEXT("riffle"))
		{
			Check(Anim->IsCoverOverTopFireSet(), TEXT("riffle: crouch over / Aim2"));
		}
		else
		{
			Check(true, FString::Printf(TEXT("set '%s': Aim2 not required"), *Set));
		}
		UE_LOG(LogCodexTactics, Display, TEXT("WindowCoverFireSmoke: pose set '%s' over %d window %d corner %d synthetic %d"),
			*Set, Anim->IsCoverOverTopFireSet() ? 1 : 0, Anim->bCoverWindow ? 1 : 0, Op.bAtCoverCorner ? 1 : 0,
			Runner->Sides[Runner->SideIndex].bSynthetic ? 1 : 0);
		return true;
	}

	bool Tick()
	{
		UWorld* World = GameWorld();
		if (!World || Runner->Frame++ < WarmupFrames)
		{
			return true;
		}
		if (Runner->Frame > WarmupFrames + MaxFrames)
		{
			Check(false, TEXT("timed out"));
			Finish();
			return false;
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Op = Squad ? Squad->GetLeader() : nullptr;
		if (!Op)
		{
			Check(false, TEXT("no squad leader"));
			Finish();
			return false;
		}
		++Runner->StepFrame;
		switch (Runner->Step)
		{
		case EStep::Warmup:
			Go(EStep::Seek);
			return true;
		case EStep::Seek:
		{
			TArray<AActor*> Sills;
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				if (It->ActorHasTag(FName(TEXT("window_sill"))))
				{
					Sills.Add(*It);
				}
			}
			Check(Sills.Num() > 0, FString::Printf(TEXT("window sills on the map: %d"), Sills.Num()));
			if (Sills.Num() == 0)
			{
				Finish();
				return false;
			}
			bool bFound = false;
			for (AActor* Sill : Sills)
			{
				const FBox Box = Sill->GetComponentsBoundingBox(true);
				if (!Box.IsValid)
				{
					continue;
				}
				const FVector Normal = ThinNormal(Box);
				FSide Sides[2];
				const bool bPlus = TrySide(*World, *Op, *Sill, Box, Normal, Sides[0]);
				Op->LeaveCover(TEXT("WindowCoverFireSmoke: other face"));
				const bool bMinus = TrySide(*World, *Op, *Sill, Box, -Normal, Sides[1]);
				Op->LeaveCover(TEXT("WindowCoverFireSmoke: both faces tried"));
				if (bPlus || bMinus)
				{
					Runner->Sill = Sill;
					Runner->Sides[0] = Sides[0];
					Runner->Sides[1] = Sides[1];
					bFound = true;
					if (bPlus && bMinus)
					{
						break;
					}
				}
			}
			Check(Runner->Sides[0].bConfirmed, TEXT("outside face confirms"));
			Check(Runner->Sides[1].bConfirmed, TEXT("inside face confirms"));
			if (!bFound)
			{
				Finish();
				return false;
			}
			Runner->SideIndex = Runner->Sides[0].bConfirmed ? 0 : 1;
			Enter(*Op, Runner->Sides[Runner->SideIndex].Slot);
			Go(EStep::Pose);
			return true;
		}
		case EStep::Pose:
			if (!JudgePose(*Op))
			{
				return true;
			}
			if (Runner->SideIndex == 0 && Runner->Sides[1].bConfirmed)
			{
				Op->LeaveCover(TEXT("WindowCoverFireSmoke: second face"));
				Runner->SideIndex = 1;
				Enter(*Op, Runner->Sides[1].Slot);
				Runner->StepFrame = 0;
				return true;
			}
			Go(EStep::Formation);
			return true;
		case EStep::Formation:
		{
			AOperativeCharacter* Follower = nullptr;
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				if (Member && Member != Op && Member->HealthComponent && Member->HealthComponent->IsAlive())
				{
					Follower = Member;
					break;
				}
			}
			Check(Follower != nullptr, TEXT("a follower is on the squad"));
			if (!Follower)
			{
				Go(EStep::Aim);
				return true;
			}
			const FCoverSlot Home = Op->GetCoverSlot();
			const float Step = WindowBreakage::ThroughLandingCm * 2.f;
			for (const float Sign : { 1.f, -1.f })
			{
				if (Runner->bHaveSolid)
				{
					break;
				}
				for (int32 Index = 1; Index <= 4; ++Index)
				{
					FCoverSlot Far = Home;
					const FVector Shift = Home.RightTangent() * (Sign * Step * static_cast<float>(Index));
					Far.WorldLocation += Shift;
					Far.WallPoint += Shift;
					Far.bLeftEdgeExposed = false;
					Far.bRightEdgeExposed = false;
					if (!Enter(*Follower, Far))
					{
						continue;
					}
					if (!Follower->IsWindowCoverConfirmed())
					{
						Runner->Solid = Follower->GetCoverSlot();
						Runner->bHaveSolid = true;
						break;
					}
					Follower->LeaveCover(TEXT("WindowCoverFireSmoke: that shift is another window"));
				}
			}
			Check(Runner->bHaveSolid, TEXT("a neighbouring slot is not this window"));
			Check(Op->IsWindowCoverConfirmed(), TEXT("the leader keeps his own window"));
			Check(!Follower->IsWindowCoverConfirmed(), TEXT("the follower does not take the leader's window"));
			const bool bLeaderWindow = Op->IsWindowCoverConfirmed();
			const bool bFollowerWindow = Follower->IsWindowCoverConfirmed();
			Squad->SetLeader(Follower, false);
			Check(Op->IsWindowCoverConfirmed() == bLeaderWindow && Follower->IsWindowCoverConfirmed() == bFollowerWindow,
				TEXT("a leader swap does not move the window pose"));
			Squad->SetLeader(Op, false);
			Go(EStep::Aim);
			return true;
		}
		case EStep::Aim:
		{
			AActor* Sill = Runner->Sill.Get();
			if (!Sill || !Op->IsWindowCoverConfirmed())
			{
				Check(false, TEXT("still at the window before the shot"));
				Finish();
				return false;
			}
			if (Runner->StepFrame == 1)
			{
				const FCoverSlot Slot = Op->GetCoverSlot();
				const FVector Far = Slot.WallPoint - Slot.WallNormal * 220.f;
				Runner->Hound = SpawnParkedHound(*World, FVector(Far.X, Far.Y, Op->GetActorLocation().Z));
				Check(Runner->Hound.IsValid(), TEXT("a parked hound stands on the far side"));
				Runner->GlassActors = CountGlass(*World, *Sill);
				Runner->bBrokenBeforeShot = Sill->ActorHasTag(FName(TEXT("window_broken")));
				Op->bHasCoverThreat = true;
				Op->CoverThreatLocation = Far;
				UE_LOG(LogCodexTactics, Display, TEXT("WindowCoverFireSmoke: glass actors %d, already broken %d"),
					Runner->GlassActors, Runner->bBrokenBeforeShot ? 1 : 0);
			}
			if (Runner->StepFrame < 30)
			{
				return true;
			}
			Check(!Runner->bBrokenBeforeShot && !Runner->Sill->ActorHasTag(FName(TEXT("window_broken"))),
				TEXT("aiming through the opening leaves the glass intact"));
			Go(EStep::Shot);
			return true;
		}
		case EStep::Shot:
		{
			AEnemyCharacter* Hound = Runner->Hound.Get();
			AActor* Sill = Runner->Sill.Get();
			UHealthComponent* Health = Hound ? Hound->GetHealthComponent() : nullptr;
			if (!Hound || !Sill || !Health)
			{
				Check(false, TEXT("shot fixtures missing"));
				Finish();
				return false;
			}
			Runner->ClipBefore = Op->CurrentClip;
			Runner->HealthBefore = Health->GetCurrentHealth();
			Runner->RoundsBefore = Op->GetWindowCoverGlassRounds();
			Op->bForceHitForTesting = true;
			Op->ColdLevel = 0.f;
			const bool bFirst = Op->ShootAtTarget(Hound, 1.f);
			Op->bForceHitForTesting = false;
			const bool bBroken = Sill->ActorHasTag(FName(TEXT("window_broken")));
			const float HealthNow = Health->GetCurrentHealth();
			const int32 Rounds = Op->GetWindowCoverGlassRounds();
			Runner->bFirstWasGlass = bBroken && !Runner->bBrokenBeforeShot && HealthNow >= Runner->HealthBefore - 0.1f;
			if (Runner->bFirstWasGlass)
			{
				Check(Rounds == Runner->RoundsBefore + 1, TEXT("the first cartridge is spent on the glass"));
				Check(Op->CurrentClip == Runner->ClipBefore - 1, TEXT("the glass cartridge leaves the clip"));
				Check(!bFirst, TEXT("the glass round does not hit the hound"));
			}
			else if (!bBroken && HealthNow < Runner->HealthBefore)
			{
				Check(Runner->GlassActors == 0, TEXT("no pane was there, so the first round hits the hound"));
				Check(Rounds == Runner->RoundsBefore, TEXT("a missing pane is not broken again"));
			}
			else
			{
				Check(false, FString::Printf(TEXT("first shot: broken %d, health %.0f -> %.0f, clip %d -> %d, rounds %d, hit %d"),
					bBroken ? 1 : 0, Runner->HealthBefore, HealthNow, Runner->ClipBefore, Op->CurrentClip, Rounds, bFirst ? 1 : 0));
			}
			const float MidHealth = Health->GetCurrentHealth();
			const int32 MidRounds = Op->GetWindowCoverGlassRounds();
			Op->bForceHitForTesting = true;
			const bool bSecond = Op->ShootAtTarget(Hound, 1.f);
			Op->bForceHitForTesting = false;
			Check(Op->GetWindowCoverGlassRounds() == MidRounds, TEXT("the second round does not break the glass again"));
			if (Runner->bFirstWasGlass)
			{
				Check(bSecond && Health->GetCurrentHealth() < MidHealth, TEXT("the second round hits the hound"));
			}
			else
			{
				UE_LOG(LogCodexTactics, Display, TEXT("WindowCoverFireSmoke: no intact pane, second shot hit %d health %.0f"),
					bSecond ? 1 : 0, Health->GetCurrentHealth());
			}
			Go(EStep::Solid);
			return true;
		}
		case EStep::Solid:
			Op->LeaveCover(TEXT("WindowCoverFireSmoke: leave the sill"));
			Check(!Op->IsWindowCoverConfirmed(), TEXT("leaving the sill clears the window pose"));
			if (Runner->bHaveSolid)
			{
				Enter(*Op, Runner->Solid);
				Check(Op->bInCover && !Op->IsWindowCoverConfirmed(), TEXT("the solid wall is ordinary cover"));
				Op->LeaveCover(TEXT("WindowCoverFireSmoke: done"));
			}
			else
			{
				UE_LOG(LogCodexTactics, Display, TEXT("WindowCoverFireSmoke: solid-wall exit was not found"));
			}
			Finish();
			return false;
		default:
			return false;
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		(void)Args;
		Runner = MakeShared<FRunner>();
		if (IConsoleVariable* Pressure = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Pressure.Enabled")))
		{
			Pressure->Set(0, ECVF_SetByConsole);
		}
		SmokeUtils::StopLevelPmc(World);
		Runner->EndFrameHandle = FCoreDelegates::OnEndFrame.AddStatic(&Tick);
		UE_LOG(LogCodexTactics, Display, TEXT("WindowCoverFireSmoke: Codex.Cover.RiffleOutdoor %d"), RiffleOutdoor());
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.WindowCoverFireSmoke"),
		TEXT("Dev check (G016): cover fire through a kit window from both faces, crouch / Aim2, the first glass cartridge, a neighbour and a follower. PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif
