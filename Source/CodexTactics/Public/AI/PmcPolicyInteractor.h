#pragma once

#include "CoreMinimal.h"
#include "AI/PmcCommanderRules.h"
#include "LearningAgentsInteractor.h"
#include "PmcPolicyInteractor.generated.h"

/**
 * The Learning Agents interactor of the PMC order policy (UE-only, no Godot reference; Learning Agents probe P0, 2026-11-23).
 * One agent = one PMC fighter. The observation is PmcPolicyRules::BuildObservation's vector (one continuous observation of
 * PmcPolicyRules::ObservationSize values, already normalised), the action one exclusive discrete choice over
 * PmcPolicyRules::ActionOrders, masked by the scorer's safety gates (MakeAgentActionModifier). The interactor never touches the
 * world: FPmcPolicyBrain pushes each agent's frame (observation + masked actions) before a step and reads the sampled order after it;
 * UPmcSquadSubsystem::ExecuteOrder executes it.
 */
UCLASS()
class CODEXTACTICS_API UPmcPolicyInteractor : public ULearningAgentsInteractor
{
	GENERATED_BODY()

public:
	/** The agent's inputs of the next step: Observation = PmcPolicyRules::ObservationSize values, MaskedActions = the action indices it may not sample. */
	void SetAgentFrame(int32 AgentId, TConstArrayView<float> Observation, TConstArrayView<int32> MaskedActions);
	/** Forgets an agent's frame (the agent was removed). */
	void ClearAgentFrame(int32 AgentId);
	void ClearAllFrames();
	/** The order the policy sampled for the agent at the last PerformActions (Hold before the first). */
	EPmcOrder GetAgentOrder(int32 AgentId) const;

	//~ Begin ULearningAgentsInteractor Interface
	virtual void SpecifyAgentObservation_Implementation(FLearningAgentsObservationSchemaElement& OutObservationSchemaElement,
		ULearningAgentsObservationSchema* InObservationSchema) override;
	virtual void GatherAgentObservation_Implementation(FLearningAgentsObservationObjectElement& OutObservationObjectElement,
		ULearningAgentsObservationObject* InObservationObject, const int32 AgentId) override;
	virtual void SpecifyAgentAction_Implementation(FLearningAgentsActionSchemaElement& OutActionSchemaElement, ULearningAgentsActionSchema* InActionSchema) override;
	virtual void PerformAgentAction_Implementation(const ULearningAgentsActionObject* InActionObject,
		const FLearningAgentsActionObjectElement& InActionObjectElement, const int32 AgentId) override;
	virtual void MakeAgentActionModifier_Implementation(FLearningAgentsActionModifierElement& OutActionModifierElement,
		ULearningAgentsActionModifier* InActionModifier, const ULearningAgentsObservationObject* InObservationObject,
		const FLearningAgentsObservationObjectElement& InObservationObjectElement, const int32 AgentId) override;
	//~ End ULearningAgentsInteractor Interface

private:
	struct FAgentFrame
	{
		TArray<float> Observation;
		TArray<int32> MaskedActions;
		EPmcOrder Order = EPmcOrder::Hold;
	};

	TMap<int32, FAgentFrame> Frames;
};
