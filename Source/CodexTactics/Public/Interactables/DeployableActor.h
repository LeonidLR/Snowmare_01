#pragma once

#include "CoreMinimal.h"
#include "Interactables/InteractableActor.h"
#include "DeployableActor.generated.h"

/**
 * Engineering object of the squad (turret / barricade / mine): can be picked up into an operative's supply,
 * moved, trapped and defused. Confirming the menu dismantles it after a short crouched action; a trapped / armed
 * object is defused first and only picked up on success. Items go to the role specialist first
 * (barricades -> engineer, mines -> medic-sapper, turrets -> commander), then the leader, then anyone with room.
 * Not available in live combat (only in the tactical pause / preparation / exploration).
 * Godot reference: main.gd `_trigger_menu_for_object` (deployables), `_try_dismantle_deployable`,
 * `_execute_tactical_dismantle`.
 */
UCLASS(Abstract, Blueprintable)
class CODEXTACTICS_API ADeployableActor : public AInteractableActor
{
	GENERATED_BODY()

public:
	ADeployableActor();

	virtual FActionMenuRequest BuildActionMenu(const AOperativeCharacter* Leader) const override;
	virtual void ExecuteAction(AOperativeCharacter* User) override;

	/** Godot deployable: can be picked up into the supply. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Deployable")
	bool bDeployable = true;

	/** Crouched work before the pick-up / defusal resolves, s (Godot 1.1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Deployable", meta = (ClampMin = "0"))
	float DismantleSeconds = 1.1f;

	/** Operative walking up to defuse it; a mine ignores them (Godot approaching_defuser). */
	TWeakObjectPtr<AOperativeCharacter> ApproachingDefuser;

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Deployable")
	EDeployableType GetDeployableType() const { return DeployableType; }

	/** Trapped (or armed, for mines): must be defused before pick-up. */
	virtual bool NeedsDefusal() const { return bTrapped; }

protected:
	/** Title, description and confirm label of the menu (subclass texts). */
	virtual void DescribeForMenu(const AOperativeCharacter* Leader, FText& OutTitle, FText& OutDescription, FText& OutConfirm,
		bool& bOutDisabled) const PURE_VIRTUAL(ADeployableActor::DescribeForMenu, );

	/** Operative that receives the item, or nullptr when every supply is full. */
	AOperativeCharacter* FindRecipient(AOperativeCharacter* Leader) const;

	/** Leader's count / max for this type (Godot shows the leader's numbers in the menu). */
	void GetLeaderSupply(const AOperativeCharacter* Leader, int32& OutCount, int32& OutMax) const;

	EDeployableType DeployableType = EDeployableType::Barricade;

private:
	void FinishDismantle(TWeakObjectPtr<AOperativeCharacter> WeakUser, bool bWasStanding);
};
