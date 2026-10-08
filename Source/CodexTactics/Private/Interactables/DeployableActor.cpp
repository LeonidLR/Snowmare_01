#include "Interactables/DeployableActor.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "Engine/World.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "Interactables/RelocationSubsystem.h"
#include "Interactables/RelocationRules.h"
#include "TimerManager.h"

#define LOCTEXT_NAMESPACE "DeployableActor"

namespace
{
	/** Specialist first, then the others (Godot routing order). */
	TArray<EOperativeRole> RoutingOrder(EDeployableType Type)
	{
		switch (Type)
		{
		case EDeployableType::Turret: return { EOperativeRole::Commander, EOperativeRole::Engineer, EOperativeRole::MedicSapper };
		case EDeployableType::Barricade: return { EOperativeRole::Engineer, EOperativeRole::Commander, EOperativeRole::MedicSapper };
		default: return { EOperativeRole::MedicSapper, EOperativeRole::Engineer, EOperativeRole::Commander };
		}
	}

	FText FoundLine(EDeployableType Type)
	{
		switch (Type)
		{
		case EDeployableType::Turret: return LOCTEXT("FoundTurret", "📦 Found a combat auto-turret ➔ added to inventory ({0})!");
		case EDeployableType::Barricade: return LOCTEXT("FoundBarricade", "📦 Found a tactical barricade ➔ added to inventory ({0})!");
		default: return LOCTEXT("FoundMine", "📦 Found a tactical mine ➔ added to inventory ({0})!");
		}
	}

	FText FullLine(EDeployableType Type)
	{
		switch (Type)
		{
		case EDeployableType::Turret: return LOCTEXT("FullTurrets", "⚠️ Inventory full! Squad turret limit reached.");
		case EDeployableType::Barricade: return LOCTEXT("FullBarricades", "⚠️ Inventory full! Squad barricade limit reached.");
		default: return LOCTEXT("FullMines", "⚠️ Inventory full! Squad mine limit reached.");
		}
	}
}

ADeployableActor::ADeployableActor()
{
	bCanBeRelocated = true;
}

void ADeployableActor::GetLeaderSupply(const AOperativeCharacter* Leader, int32& OutCount, int32& OutMax) const
{
	OutCount = Leader ? Leader->GetDeployableCount(DeployableType) : 0;
	OutMax = DeployableRules::GetMaxCarried(DeployableType);
}

FActionMenuRequest ADeployableActor::BuildActionMenu(const AOperativeCharacter* Leader) const
{
	const URelocationSubsystem* Relocation = GetWorld()->GetSubsystem<URelocationSubsystem>();
	if (Relocation && !Relocation->CanRelocateNow())
	{
		return FActionMenuRequest::MakeMessage(LOCTEXT("HQ", "HQ"),
			LOCTEXT("NotInCombat", "⚠️ Objects cannot be relocated during combat! Use tactical pause [SPACE]."));
	}
	FText Title;
	FText Description;
	FText Confirm;
	bool bDisabled = false;
	DescribeForMenu(Leader, Title, Description, Confirm, bDisabled);
	return FActionMenuRequest::MakeMenu(Title, Description, Confirm, LOCTEXT("Cancel", "Cancel"), bDisabled, bCanBeRelocated,
		LOCTEXT("Relocate", "Relocate"));
}

AOperativeCharacter* ADeployableActor::FindRecipient(AOperativeCharacter* Leader) const
{
	const USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>();
	if (!Squad)
	{
		return nullptr;
	}
	const int32 Max = DeployableRules::GetMaxCarried(DeployableType);
	auto HasRoom = [this, Max](const AOperativeCharacter* Operative)
	{
		return Operative && Operative->GetDeployableCount(DeployableType) < Max;
	};
	auto FindRole = [Squad](EOperativeRole WantedRole) -> AOperativeCharacter*
	{
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			if (Member->SquadRole == WantedRole)
			{
				return Member;
			}
		}
		return nullptr;
	};
	const TArray<EOperativeRole> Order = RoutingOrder(DeployableType);
	if (AOperativeCharacter* Specialist = FindRole(Order[0]); HasRoom(Specialist))
	{
		return Specialist;
	}
	if (HasRoom(Leader))
	{
		return Leader;
	}
	for (EOperativeRole OrderRole : Order)
	{
		if (AOperativeCharacter* Member = FindRole(OrderRole); HasRoom(Member))
		{
			return Member;
		}
	}
	return nullptr;
}

void ADeployableActor::ExecuteAction(AOperativeCharacter* User)
{
	if (!User)
	{
		return;
	}
	const URelocationSubsystem* Relocation = GetWorld()->GetSubsystem<URelocationSubsystem>();
	if (Relocation && !Relocation->CanRelocateNow())
	{
		return;
	}
	if (!bDeployable && !NeedsDefusal())
	{
		PostLine(User->DisplayName, LOCTEXT("Stationary", "This object is stationary: deployable is off, so it cannot be stowed in the inventory."));
		return;
	}
	if (bDeployable && !NeedsDefusal() && !FindRecipient(User))
	{
		PostLine(User->DisplayName, FullLine(DeployableType));
		return;
	}

	// Godot _execute_tactical_dismantle: face it, crouch, work for 1.1 s, then resolve.
	const bool bWasStanding = User->GetStance() == EOperativeStance::Standing;
	const FRotator Facing = (GetActorLocation() - User->GetActorLocation()).Rotation();
	User->StopOperative();
	User->SetActorRotation(FRotator(0.f, Facing.Yaw, 0.f));
	User->SetStance(EOperativeStance::Crouching);
	FTimerHandle Handle;
	GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateUObject(this, &ADeployableActor::FinishDismantle,
		TWeakObjectPtr<AOperativeCharacter>(User), bWasStanding), FMath::Max(DismantleSeconds, 0.01f), false);
}

void ADeployableActor::FinishDismantle(TWeakObjectPtr<AOperativeCharacter> WeakUser, bool bWasStanding)
{
	AOperativeCharacter* User = WeakUser.Get();
	if (!User)
	{
		return;
	}
	auto StandUp = [User, bWasStanding]()
	{
		if (bWasStanding)
		{
			User->SetStance(EOperativeStance::Standing);
		}
	};
	if (NeedsDefusal())
	{
		// A detonation may destroy this actor: keep what we need before the attempt.
		const EDefusalResult Result = AttemptDefusal(User);
		if (Result != EDefusalResult::Success || !bDeployable)
		{
			StandUp();
			return;
		}
	}
	AOperativeCharacter* Recipient = FindRecipient(User);
	if (!Recipient)
	{
		PostLine(User->DisplayName, FullLine(DeployableType));
		StandUp();
		return;
	}
	Recipient->AddDeployable(DeployableType, 1);
	PostLine(User->DisplayName, FText::Format(FoundLine(DeployableType), Recipient->DisplayName));
	StandUp();
	Destroy();
}

#undef LOCTEXT_NAMESPACE
