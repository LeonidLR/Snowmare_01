// Dev-only rendered screenshots of the cover clip sides (left / right edge, idle, fire-ready, shot, shimmies) for the
// _L / _R mapping review (UE-only, no Godot reference). Needs rendering (not -nullrhi); nothing is saved to the map:
//   UnrealEditor.exe CodexTactics.uproject /Game/Maps/L_MovementTest -game -windowed -ResX=1600 -ResY=900
//     -ExecCmds="CodexTactics.CoverLRShot"   ("CodexTactics.CoverLRShot <folder> runin": only the run-into-cover frame sequence)
// A 3 m high, 6 m wide wall 4 m ahead of the leader is spawned at runtime (6 m so the middle has no corner within the
// 2 m auto-snap). Labels in the world: the two wall ends ("END -R = HIS RIGHT", "END +R = HIS LEFT"), the enemy. Each
// shot logs and labels the clip that is really playing (the FullBody slot montage), the edge, the facing and where the
// enemy is. Images: Saved/Screenshots/<folder>/NN_<scene>.png (CodexTactics.CoverLRShot [folder], default CoverLR).
// User decisions 2026-10-06: pack _L plays at his own RIGHT corner; the corner shot only at an enemy behind the wall
// plane / round the corner (the corner scenes put it there), an enemy out in front gets the open shot off the wall.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Camera/PlayerCameraManager.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeAnimInstance.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/TacticalSightSubsystem.h"
#include "Combat/WaveSubsystem.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TextRenderActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "Tactics/CoverTraceRules.h"
#include "UnrealClient.h"

namespace CoverLRShot
{
	struct FCtx
	{
		float StepTime = 0.f;
		int32 StepIndex = 0;
		int32 ShotIndex = 0;
		FVector P = FVector::ZeroVector;
		FVector F = FVector::ForwardVector;
		FVector R = FVector::RightVector;
		float GroundZ = 0.f;
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<AOperativeCharacter> Op;
		TWeakObjectPtr<AEnemyCharacter> Hound;
		TWeakObjectPtr<ATextRenderActor> HoundLabel;
		TWeakObjectPtr<AStaticMeshActor> Wall;
		FString Scene;
		FString EnemyWhere;
		bool bAllowFire = false;
		bool bFireShotTaken = false;
		bool bShimmyShotTaken = false;
		FVector ShimmyStart = FVector::ZeroVector;
		FString OutDir = TEXT("CoverLR");
		int32 OpenShotsBefore = 0;
		bool bOpenShotTaken = false;
		int32 SequenceShots = 0;
		float SequenceLastShot = -1.f;
		FVector SequenceSlot = FVector::ZeroVector;
		bool bRunInOnly = false;
		int32 LeanShotsBefore = 0;
	};

	using FStep = TFunction<bool(FCtx&)>; // true = step done, go on

	UOperativeAnimInstance* AnimOf(const AOperativeCharacter* Op)
	{
		return Op && Op->GetMesh() ? Cast<UOperativeAnimInstance>(Op->GetMesh()->GetAnimInstance()) : nullptr;
	}

	ATextRenderActor* SpawnLabel(UWorld* World, const FVector& Where, const FRotator& Facing, const FString& Text, const FColor& Color, float Size = 36.f)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ATextRenderActor* Label = World->SpawnActor<ATextRenderActor>(Where, Facing, Params);
		if (Label)
		{
			Label->GetTextRender()->SetText(FText::FromString(Text));
			Label->GetTextRender()->SetTextRenderColor(Color);
			Label->GetTextRender()->SetWorldSize(Size);
			Label->GetTextRender()->SetHorizontalAlignment(EHTA_Center);
		}
		return Label;
	}

	AStaticMeshActor* SpawnBlock(UWorld* World, const FVector& Centre, const FRotator& Rotation, const FVector& Scale)
	{
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		AStaticMeshActor* Block = Cube ? World->SpawnActorDeferred<AStaticMeshActor>(AStaticMeshActor::StaticClass(), FTransform(Rotation, Centre, Scale),
			nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn) : nullptr;
		if (!Block)
		{
			return nullptr;
		}
		Block->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
		Block->GetStaticMeshComponent()->SetStaticMesh(Cube);
		Block->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
		Block->GetStaticMeshComponent()->SetCanEverAffectNavigation(true);
		Block->FinishSpawning(FTransform(Rotation, Centre, Scale));
		return Block;
	}

	FString Playing(const AOperativeCharacter* Op)
	{
		FString Clip;
		float MontageWeight = 0.f;
		float SlotWeight = 0.f;
		if (const UOperativeAnimInstance* Anim = AnimOf(Op))
		{
			Anim->GetCoverPlayback(Clip, MontageWeight, SlotWeight);
		}
		return Clip.IsEmpty() ? FString(TEXT("(none)")) : Clip;
	}

	/** Labels the frame on screen, logs the state and requests the screenshot. */
	void Shot(FCtx& Ctx, const FString& Name)
	{
		AOperativeCharacter* Op = Ctx.Op.Get();
		UWorld* World = Ctx.World.Get();
		if (!Op || !World)
		{
			return;
		}
		const FString Clip = Playing(Op);
		const float Along = FVector::DotProduct(Op->GetActorLocation() - Ctx.P, Ctx.R);
		FString Edge = TEXT("middle");
		if (Op->bAtCoverCorner)
		{
			Edge = Op->CoverFacing == ECoverFacing::Right ? TEXT("corner on HIS RIGHT (-R end)") : TEXT("corner on HIS LEFT (+R end)");
		}
		FRotator CamRot = FRotator::ZeroRotator;
		if (const APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0); PC && PC->PlayerCameraManager)
		{
			CamRot = PC->PlayerCameraManager->GetCameraRotation();
		}
		const FString Line1 = FString::Printf(TEXT("%02d %s | PLAYING: %s"), Ctx.ShotIndex + 1, *Name, *Clip);
		const FString Line2 = FString::Printf(TEXT("aim=%d in cover=%d open shot=%d | code facing=%s (Right = his own right = -R) | at corner=%d | shimmy=%d dir=%+.0f fwd=%d | threat known=%d | along R=%+.0f cm"),
			Op->IsCornerAimActive() ? 1 : 0, Op->bInCover ? 1 : 0, Op->IsCoverOpenShotActive() ? 1 : 0, Op->CoverFacing == ECoverFacing::Right ? TEXT("Right") : TEXT("Left"),
			Op->bAtCoverCorner ? 1 : 0, Op->bShimmying ? 1 : 0, Op->ShimmyDirection, Op->IsShimmyForward() ? 1 : 0, Op->bHasCoverThreat ? 1 : 0, Along);
		const FString Line3 = FString::Printf(TEXT("edge: %s | enemy: %s | camera yaw %.0f (F yaw %.0f) pitch %.0f"), *Edge, *Ctx.EnemyWhere,
			CamRot.Yaw, Ctx.F.Rotation().Yaw, CamRot.Pitch);
		if (GEngine)
		{
			GEngine->ClearOnScreenDebugMessages();
			GEngine->AddOnScreenDebugMessage(3, 30.f, FColor::Cyan, Line3, false, FVector2D(1.3f, 1.3f));
			GEngine->AddOnScreenDebugMessage(2, 30.f, FColor::Yellow, Line2, false, FVector2D(1.3f, 1.3f));
			GEngine->AddOnScreenDebugMessage(1, 30.f, FColor::White, Line1, false, FVector2D(1.6f, 1.6f));
		}
		++Ctx.ShotIndex;
		const FString File = FPaths::ProjectSavedDir() / TEXT("Screenshots") / Ctx.OutDir / FString::Printf(TEXT("%02d_%s.png"), Ctx.ShotIndex, *Name);
		FScreenshotRequest::RequestScreenshot(File, /*bShowUI*/ true, /*bAddFilenameSuffix*/ false);
		UE_LOG(LogCodexTactics, Display, TEXT("CoverLR shot %s\n    %s\n    %s\n    %s"), *File, *Line1, *Line2, *Line3);
	}

	void PlaceHound(FCtx& Ctx, const FVector& Where, const FString& Description)
	{
		AEnemyCharacter* Hound = Ctx.Hound.Get();
		if (!Hound)
		{
			return;
		}
		Hound->SetActorLocation(FVector(Where.X, Where.Y, Ctx.GroundZ + Hound->GetSimpleCollisionHalfHeight() + 2.f));
		Ctx.EnemyWhere = Description;
		if (ATextRenderActor* Label = Ctx.HoundLabel.Get())
		{
			Label->SetActorLocation(FVector(Where.X, Where.Y, Ctx.GroundZ + 220.f));
		}
	}

	/** Cover order at the wall point Along (cm along +R from the wall centre); waits until he is in cover. */
	FStep TakeCover(float Along)
	{
		return [Along](FCtx& Ctx)
		{
			AOperativeCharacter* Op = Ctx.Op.Get();
			if (Ctx.StepTime == 0.f)
			{
				if (Op->bInCover)
				{
					Op->LeaveCover(TEXT("CoverLRShot"));
				}
				FCoverSlot Slot;
				if (CoverTraceRules::FindCoverSlotAt(Ctx.World.Get(), Ctx.P + Ctx.F * 380.f + Ctx.R * Along + FVector(0.f, 0.f, 90.f), Ctx.F, Slot))
				{
					Op->OrderTakeCover(Slot, false);
				}
				else
				{
					UE_LOG(LogCodexTactics, Warning, TEXT("CoverLR: no slot at along %.0f"), Along);
				}
				return false;
			}
			return Op->bInCover || Ctx.StepTime > 10.f;
		};
	}

	FStep Wait(float Seconds)
	{
		return [Seconds](FCtx& Ctx) { return Ctx.StepTime >= Seconds; };
	}

	FStep Do(TFunction<void(FCtx&)> Action)
	{
		return [Action](FCtx& Ctx) { Action(Ctx); return true; };
	}

	FStep TakeShot(const FString& Name)
	{
		return [Name](FCtx& Ctx) { Shot(Ctx, Name); return true; };
	}

	/** Shimmy to the wall point Along; the screenshot once he has side-stepped 50 cm (mid-shimmy). */
	FStep ShimmyShot(float Along, const FString& Name)
	{
		return [Along, Name](FCtx& Ctx)
		{
			AOperativeCharacter* Op = Ctx.Op.Get();
			if (Ctx.StepTime == 0.f)
			{
				FCoverSlot Target;
				const FVector Click = Ctx.P + Ctx.F * 380.f + Ctx.R * Along + FVector(0.f, 0.f, 90.f);
				const bool bOk = CoverTraceRules::FindShimmySlot(Ctx.World.Get(), Op->GetCoverSlot(), Click, Target)
					&& Op->OrderShimmyTo(Target) == EOperativeOrderResult::Accepted;
				Ctx.bShimmyShotTaken = false;
				Ctx.ShimmyStart = Op->GetActorLocation();
				UE_LOG(LogCodexTactics, Display, TEXT("CoverLR: shimmy to along %.0f ordered %d"), Along, bOk ? 1 : 0);
				return false;
			}
			if (!Ctx.bShimmyShotTaken && Op->bShimmying && FVector::Dist2D(Op->GetActorLocation(), Ctx.ShimmyStart) >= 50.f)
			{
				Ctx.bShimmyShotTaken = true;
				Shot(Ctx, Name);
			}
			return Ctx.StepTime > 1.f && (!Op->bShimmying || Ctx.StepTime > 8.f);
		};
	}

	/** Ctrl + click on the hound (slowed time); the screenshot while the fire clip plays. */
	FStep FireShot(const FString& Name)
	{
		return [Name](FCtx& Ctx)
		{
			AOperativeCharacter* Op = Ctx.Op.Get();
			UWorld* World = Ctx.World.Get();
			if (Ctx.StepTime == 0.f)
			{
				Ctx.bAllowFire = true;
				Ctx.bFireShotTaken = false;
				Ctx.LeanShotsBefore = Op->GetCoverLeanShots();
				UGameplayStatics::SetGlobalTimeDilation(World, 0.25f);
				if (ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(UGameplayStatics::GetPlayerController(World, 0)))
				{
					PC->IssueTargetedShot(Ctx.Hound.Get());
				}
				else
				{
					Op->SetManualPriorityTarget(Ctx.Hound.Get());
				}
				return false;
			}
			const FString Clip = Playing(Op);
			const bool bFireClip = Clip.Contains(TEXT("_fire_")) && !Clip.Contains(TEXT("fire_idle")) && !Clip.Contains(TEXT("_to_"));
			if (!Ctx.bFireShotTaken && (bFireClip || Ctx.StepTime > 6.f))
			{
				Ctx.bFireShotTaken = true;
				Shot(Ctx, Name);
			}
			if (Ctx.bFireShotTaken && Ctx.StepTime > 0.2f)
			{
				UGameplayStatics::SetGlobalTimeDilation(World, 1.f);
				Ctx.bAllowFire = false;
				Op->AssignPriorityTarget(nullptr);
				UE_LOG(LogCodexTactics, Display, TEXT("CoverLR: fire step done, lean shots %d -> %d, clip log tail: %s"), Ctx.LeanShotsBefore, Op->GetCoverLeanShots(),
					AnimOf(Op) && AnimOf(Op)->GetCoverClipLog().Num() > 0 ? *AnimOf(Op)->GetCoverClipLog().Last() : TEXT("-"));
				return true;
			}
			return false;
		};
	}

	/**
	 * Sustained corner aim (user request 2026-10-07): Ctrl + click on the enemy round the corner, the screenshot after the
	 * second shot of the burst (he holds the fire stance between shots), then fire held until the aim ends.
	 */
	FStep BurstShot(const FString& Name)
	{
		return [Name](FCtx& Ctx)
		{
			AOperativeCharacter* Op = Ctx.Op.Get();
			UWorld* World = Ctx.World.Get();
			if (Ctx.StepTime == 0.f)
			{
				Ctx.bAllowFire = true;
				Ctx.bFireShotTaken = false;
				Ctx.LeanShotsBefore = Op->GetCoverLeanShots();
				if (ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(UGameplayStatics::GetPlayerController(World, 0)))
				{
					PC->IssueTargetedShot(Ctx.Hound.Get());
				}
				else
				{
					Op->SetManualPriorityTarget(Ctx.Hound.Get());
				}
				return false;
			}
			if (!Ctx.bFireShotTaken && ((Op->GetCoverLeanShots() >= Ctx.LeanShotsBefore + 2 && Ctx.StepTime > 0.3f) || Ctx.StepTime > 8.f))
			{
				Ctx.bFireShotTaken = true;
				Shot(Ctx, Name);
				UE_LOG(LogCodexTactics, Display, TEXT("CoverLR: mid-burst after %d shots, corner aim %d"), Op->GetCoverLeanShots() - Ctx.LeanShotsBefore,
					Op->IsCornerAimActive() ? 1 : 0);
				Ctx.bAllowFire = false;
				Op->AssignPriorityTarget(nullptr);
			}
			return Ctx.bFireShotTaken && (!Op->IsCornerAimActive() || Ctx.StepTime > 12.f);
		};
	}

	/** Ctrl + click on an enemy out in front of the wall: the screenshot once he is off the wall and has fired, then back. */
	FStep OpenShotShot(const FString& Name)
	{
		return [Name](FCtx& Ctx)
		{
			AOperativeCharacter* Op = Ctx.Op.Get();
			UWorld* World = Ctx.World.Get();
			if (Ctx.StepTime == 0.f)
			{
				Ctx.bAllowFire = true;
				Ctx.bOpenShotTaken = false;
				Ctx.OpenShotsBefore = Op->GetCoverOpenShots();
				if (ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(UGameplayStatics::GetPlayerController(World, 0)))
				{
					PC->IssueTargetedShot(Ctx.Hound.Get());
				}
				else
				{
					Op->SetManualPriorityTarget(Ctx.Hound.Get());
				}
				return false;
			}
			if (!Ctx.bOpenShotTaken && ((Op->GetCoverOpenShots() > Ctx.OpenShotsBefore && !Op->bInCover) || Ctx.StepTime > 6.f))
			{
				Ctx.bOpenShotTaken = true;
				Shot(Ctx, Name);
				Ctx.bAllowFire = false;
				Op->AssignPriorityTarget(nullptr);
				UE_LOG(LogCodexTactics, Display, TEXT("CoverLR: open shots %d -> %d, in cover %d"), Ctx.OpenShotsBefore, Op->GetCoverOpenShots(), Op->bInCover ? 1 : 0);
			}
			return Ctx.bOpenShotTaken && (Op->bInCover || Ctx.StepTime > 8.f);
		};
	}

	/**
	 * Run into cover (user-found bug 2026-10-07): a sprint order to the wall's middle from 3 m out, a frame sequence from
	 * 3.5 m before the slot until the cover loop (every ~0.07 s, 16 frames).
	 */
	FStep RunInSequence(const FString& Name)
	{
		return [Name](FCtx& Ctx)
		{
			AOperativeCharacter* Op = Ctx.Op.Get();
			if (Ctx.StepTime == 0.f)
			{
				Ctx.SequenceShots = 0;
				Ctx.SequenceLastShot = -1.f;
				FCoverSlot Slot;
				if (CoverTraceRules::FindCoverSlotAt(Ctx.World.Get(), Ctx.P + Ctx.F * 380.f + FVector(0.f, 0.f, 90.f), Ctx.F, Slot))
				{
					Ctx.SequenceSlot = Slot.WorldLocation;
					Op->OrderTakeCover(Slot, true);
				}
				return false;
			}
			const bool bNear = Op->bInCover || FVector::Dist2D(Op->GetActorLocation(), Ctx.SequenceSlot) <= 350.f;
			if (bNear && Ctx.SequenceShots == 0)
			{
				// Slow motion for the sequence (a screenshot costs ~0.3 s of real time): ~0.08 s of game time per frame.
				UGameplayStatics::SetGlobalTimeDilation(Ctx.World.Get(), 0.25f);
			}
			if (bNear && Ctx.SequenceShots < 16 && Ctx.StepTime - Ctx.SequenceLastShot >= 0.07f)
			{
				Ctx.SequenceLastShot = Ctx.StepTime;
				++Ctx.SequenceShots;
				Shot(Ctx, FString::Printf(TEXT("%s_%02d"), *Name, Ctx.SequenceShots));
			}
			const bool bDone = Ctx.SequenceShots >= 16 || Ctx.StepTime > 20.f;
			if (bDone)
			{
				UGameplayStatics::SetGlobalTimeDilation(Ctx.World.Get(), 1.f);
			}
			return bDone;
		};
	}

	TArray<FStep> BuildRunInScript()
	{
		TArray<FStep> S;
		S.Add(Do([](FCtx& C) { C.Op->bIgnoreCoverThreatForTesting = true; PlaceHound(C, C.P - C.F * 3000.f, TEXT("none (threat unknown)")); }));
		S.Add(RunInSequence(TEXT("RunIntoCover")));
		S.Add(Wait(1.5f));
		S.Add(TakeShot(TEXT("RunIntoCover_Settled")));
		return S;
	}

	TArray<FStep> BuildScript()
	{
		TArray<FStep> S;
		// Corner on HIS RIGHT (-R end of the wall: he stands back to the wall facing -F, his right = -R).
		S.Add(Do([](FCtx& C) { C.Op->bIgnoreCoverThreatForTesting = true; PlaceHound(C, C.P - C.F * 3000.f, TEXT("none (threat unknown)")); }));
		S.Add(TakeCover(-250.f));
		S.Add(Wait(4.f));
		S.Add(TakeShot(TEXT("HisRightEdge_NoThreat_CornerIdle")));
		S.Add(Do([](FCtx& C)
		{
			C.Op->bIgnoreCoverThreatForTesting = false;
			PlaceHound(C, C.P + C.F * 700.f - C.R * 800.f, TEXT("BEHIND the wall, round HIS RIGHT corner (-R)"));
		}));
		S.Add(Wait(4.f));
		S.Add(TakeShot(TEXT("HisRightEdge_ThreatHisRight_FireIdle")));
		S.Add(FireShot(TEXT("HisRightEdge_ThreatHisRight_Fire")));
		S.Add(Wait(3.f));
		S.Add(BurstShot(TEXT("HisRightEdge_ThreatHisRight_MidBurst")));
		S.Add(Wait(1.f));
		// Corner on HIS LEFT (+R end).
		S.Add(Do([](FCtx& C) { C.Op->bIgnoreCoverThreatForTesting = true; PlaceHound(C, C.P - C.F * 3000.f, TEXT("none (threat unknown)")); }));
		S.Add(TakeCover(250.f));
		S.Add(Wait(4.f));
		S.Add(TakeShot(TEXT("HisLeftEdge_NoThreat_CornerIdle")));
		S.Add(Do([](FCtx& C)
		{
			C.Op->bIgnoreCoverThreatForTesting = false;
			PlaceHound(C, C.P + C.F * 700.f + C.R * 800.f, TEXT("BEHIND the wall, round HIS LEFT corner (+R)"));
		}));
		S.Add(Wait(4.f));
		S.Add(TakeShot(TEXT("HisLeftEdge_ThreatHisLeft_FireIdle")));
		S.Add(FireShot(TEXT("HisLeftEdge_ThreatHisLeft_Fire")));
		S.Add(Wait(2.f));
		// An enemy out IN FRONT of the wall (his open side): the normal shot off the wall, then back to the slot.
		S.Add(Do([](FCtx& C) { PlaceHound(C, C.Op->GetActorLocation() - C.F * 700.f + C.R * 100.f, TEXT("IN FRONT of the wall, 7 m (open side)")); }));
		S.Add(Wait(2.f));
		S.Add(OpenShotShot(TEXT("HisLeftEdge_EnemyInFront_OpenShot")));
		S.Add(Wait(0.5f));
		S.Add(TakeShot(TEXT("HisLeftEdge_EnemyInFront_BackInCover")));
		// Middle of the wall, threat on his RIGHT: idle, shimmy right (towards it), shimmy left (away).
		S.Add(Do([](FCtx& C) { PlaceHound(C, C.P + C.F * 380.f - C.F * 800.f - C.R * 500.f, TEXT("in front, 5 m toward HIS RIGHT (-R)")); }));
		S.Add(TakeCover(0.f));
		S.Add(Wait(3.f));
		S.Add(TakeShot(TEXT("Middle_ThreatHisRight_Idle")));
		S.Add(ShimmyShot(-120.f, TEXT("Middle_ThreatHisRight_ShimmyHisRight")));
		S.Add(Wait(1.f));
		S.Add(ShimmyShot(120.f, TEXT("Middle_ThreatHisRight_ShimmyHisLeft")));
		S.Add(Wait(1.f));
		// Threat on his LEFT.
		S.Add(Do([](FCtx& C) { PlaceHound(C, C.P + C.F * 380.f - C.F * 800.f + C.R * 500.f, TEXT("in front, 5 m toward HIS LEFT (+R)")); }));
		S.Add(TakeCover(0.f));
		S.Add(Wait(3.f));
		S.Add(TakeShot(TEXT("Middle_ThreatHisLeft_Idle")));
		S.Add(ShimmyShot(120.f, TEXT("Middle_ThreatHisLeft_ShimmyHisLeft")));
		S.Add(Wait(1.f));
		S.Add(ShimmyShot(-120.f, TEXT("Middle_ThreatHisLeft_ShimmyHisRight")));
		S.Add(Wait(1.f));
		// Threat unknown.
		S.Add(Do([](FCtx& C) { C.Op->bIgnoreCoverThreatForTesting = true; PlaceHound(C, C.P - C.F * 3000.f, TEXT("none (threat unknown)")); }));
		S.Add(TakeCover(0.f));
		S.Add(Wait(3.f));
		S.Add(TakeShot(TEXT("Middle_NoThreat_Idle")));
		S.Add(ShimmyShot(-120.f, TEXT("Middle_NoThreat_ShimmyHisRight")));
		S.Add(Wait(1.f));
		S.Add(ShimmyShot(120.f, TEXT("Middle_NoThreat_ShimmyHisLeft")));
		S.Add(Wait(1.f));
		return S;
	}

	bool Setup(FCtx& Ctx)
	{
		UWorld* World = Ctx.World.Get();
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		Flow->TriggerCombatZone();
		Flow->FinishCutscene();
		Flow->FinishPreparation();
		AOperativeCharacter* Op = Squad->GetLeader();
		if (!Op)
		{
			return false;
		}
		Ctx.Op = Op;
		Ctx.F = Op->GetActorForwardVector().GetSafeNormal2D();
		Ctx.R = FVector::CrossProduct(FVector::UpVector, Ctx.F);
		Ctx.P = Op->GetActorLocation();
		Ctx.GroundZ = Ctx.P.Z - Op->GetSimpleCollisionHalfHeight();
		// The parked hound keeps the wave alive (WaveCleared would stop the world); the spawned wave goes.
		AEnemyCharacter* Hound = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::FrostHound,
			FVector(Ctx.P.X, Ctx.P.Y, Ctx.GroundZ + 100.f) - Ctx.F * 6000.f, Ctx.F.Rotation());
		if (Hound)
		{
			Hound->CustomTimeDilation = 0.f;
			Hound->SetActorTickEnabled(false);
			Hound->GetHealthComponent()->SetMaxHealth(100000.f);
		}
		Ctx.Hound = Hound;
		for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
		{
			if (*It != Hound)
			{
				It->Destroy();
			}
		}
		int32 Index = 0;
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			Member->HealthComponent->SetMaxHealth(100000.f);
			if (Member != Op)
			{
				Member->StopOperative();
				Member->TeleportTo(Ctx.P - Ctx.F * 900.f + Ctx.R * (Index++ % 2 == 0 ? 600.f : -600.f), Ctx.F.Rotation(), false, true);
			}
		}
		const FVector WallCentre = Ctx.P + Ctx.F * 400.f;
		Ctx.Wall = SpawnBlock(World, FVector(WallCentre.X, WallCentre.Y, Ctx.GroundZ + 150.f), Ctx.F.Rotation(), FVector(0.4f, 6.f, 3.f));
		// Labels readable from the operative's side (facing -F), on the wall face above each end and on the ground.
		const FRotator FaceBack = (-Ctx.F).Rotation();
		SpawnLabel(World, WallCentre - Ctx.F * 25.f - Ctx.R * 230.f + FVector(0.f, 0.f, Ctx.GroundZ - Ctx.P.Z + 250.f), FaceBack,
			TEXT("END -R = HIS RIGHT"), FColor::Red);
		SpawnLabel(World, WallCentre - Ctx.F * 25.f + Ctx.R * 230.f + FVector(0.f, 0.f, Ctx.GroundZ - Ctx.P.Z + 250.f), FaceBack,
			TEXT("END +R = HIS LEFT"), FColor::Green);
		Ctx.HoundLabel = SpawnLabel(World, Ctx.P, FaceBack, TEXT("ENEMY"), FColor::Orange, 48.f);
		UE_LOG(LogCodexTactics, Display, TEXT("CoverLR setup: P %s F %s R %s; wall 6 m x 3 m at %s"), *Ctx.P.ToString(), *Ctx.F.ToString(), *Ctx.R.ToString(), *WallCentre.ToString());
		return Ctx.Wall.IsValid() && Hound;
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		TSharedRef<FCtx> Ctx = MakeShared<FCtx>();
		Ctx->World = World;
		if (Args.Num() > 0 && !Args[0].IsEmpty())
		{
			Ctx->OutDir = Args[0];
		}
		Ctx->bRunInOnly = Args.Contains(TEXT("runin"));
		TSharedRef<TArray<FStep>> Script = MakeShared<TArray<FStep>>(Ctx->bRunInOnly ? BuildRunInScript() : BuildScript());
		TSharedRef<float> Boot = MakeShared<float>(0.f);
		TSharedRef<bool> bReady = MakeShared<bool>(false);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Ctx, Script, Boot, bReady](float DeltaTime)
		{
			UWorld* W = Ctx->World.Get();
			if (!W)
			{
				return false;
			}
			if (!*bReady)
			{
				*Boot += DeltaTime;
				if (*Boot < 3.f)
				{
					return true;
				}
				if (!Setup(*Ctx))
				{
					UE_LOG(LogCodexTactics, Error, TEXT("CoverLR: setup failed"));
					FPlatformMisc::RequestExit(false, TEXT("CoverLRShot"));
					return false;
				}
				*bReady = true;
				*Boot = 0.f;
				return true;
			}
			if (*Boot < 2.5f)
			{
				*Boot += DeltaTime; // the navmesh rebuilds round the wall
				return true;
			}
			AOperativeCharacter* Op = Ctx->Op.Get();
			if (!Op)
			{
				FPlatformMisc::RequestExit(false, TEXT("CoverLRShot"));
				return false;
			}
			if (USquadSubsystem* Squad = W->GetSubsystem<USquadSubsystem>())
			{
				for (AOperativeCharacter* Member : Squad->GetMembers())
				{
					Member->ColdLevel = 0.f;
					Member->bTacticalCeaseFire = !(Ctx->bAllowFire && Member == Op);
				}
			}
			if (UTacticalSightSubsystem* Sight = W->GetSubsystem<UTacticalSightSubsystem>())
			{
				Sight->Refresh();
			}
			if (!Script->IsValidIndex(Ctx->StepIndex))
			{
				UE_LOG(LogCodexTactics, Display, TEXT("CoverLR: done, %d shots"), Ctx->ShotIndex);
				FPlatformMisc::RequestExit(false, TEXT("CoverLRShot"));
				return false;
			}
			if ((*Script)[Ctx->StepIndex](*Ctx))
			{
				++Ctx->StepIndex;
				Ctx->StepTime = 0.f;
			}
			else
			{
				Ctx->StepTime += FMath::Max(DeltaTime, 0.001f);
			}
			return true;
		}), 0.05f);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.CoverLRShot"),
		TEXT("Dev: rendered screenshots of the cover clip sides (edges, fire-ready, shot, shimmies) -> Saved/Screenshots/CoverLR."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif
