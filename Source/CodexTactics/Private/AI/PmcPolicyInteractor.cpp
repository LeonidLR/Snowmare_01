// The Learning Agents interactor of the PMC order policy (Learning Agents probe P0, 2026-11-23). UE-only, no Godot reference.

#include "AI/PmcPolicyInteractor.h"

#include "AI/PmcPolicyRules.h"
#include "LearningAgentsActions.h"
#include "LearningAgentsObservations.h"

namespace PmcPolicyInteractorLocal
{
	const FName ObservationTag(TEXT("PmcFighterView"));
	const FName ActionTag(TEXT("PmcOrder"));
}

void UPmcPolicyInteractor::SetAgentFrame(int32 AgentId, TConstArrayView<float> Observation, TConstArrayView<int32> MaskedActions)
{
	FAgentFrame& Frame = Frames.FindOrAdd(AgentId);
	Frame.Observation.Init(0.f, PmcPolicyRules::ObservationSize);
	for (int32 Index = 0; Index < FMath::Min(Observation.Num(), PmcPolicyRules::ObservationSize); ++Index)
	{
		Frame.Observation[Index] = Observation[Index];
	}
	Frame.MaskedActions.Reset();
	Frame.MaskedActions.Append(MaskedActions.GetData(), MaskedActions.Num());
}

void UPmcPolicyInteractor::ClearAgentFrame(int32 AgentId)
{
	Frames.Remove(AgentId);
}

void UPmcPolicyInteractor::ClearAllFrames()
{
	Frames.Reset();
}

EPmcOrder UPmcPolicyInteractor::GetAgentOrder(int32 AgentId) const
{
	const FAgentFrame* Frame = Frames.Find(AgentId);
	return Frame ? Frame->Order : EPmcOrder::Hold;
}

void UPmcPolicyInteractor::SpecifyAgentObservation_Implementation(FLearningAgentsObservationSchemaElement& OutObservationSchemaElement,
	ULearningAgentsObservationSchema* InObservationSchema)
{
	// Manual normalisation without scales: BuildObservation already writes 0..1 (-1..1) values.
	OutObservationSchemaElement = ULearningAgentsObservations::SpecifyContinuousObservation(InObservationSchema, PmcPolicyRules::ObservationSize,
		FLearningAgentsObservationNormalizationSettings(), true, PmcPolicyInteractorLocal::ObservationTag);
}

void UPmcPolicyInteractor::GatherAgentObservation_Implementation(FLearningAgentsObservationObjectElement& OutObservationObjectElement,
	ULearningAgentsObservationObject* InObservationObject, const int32 AgentId)
{
	// An agent without a frame yet (never the case once the brain stepped it) observes zeros.
	FAgentFrame& Frame = Frames.FindOrAdd(AgentId);
	if (Frame.Observation.Num() != PmcPolicyRules::ObservationSize)
	{
		Frame.Observation.Init(0.f, PmcPolicyRules::ObservationSize);
	}
	OutObservationObjectElement = ULearningAgentsObservations::MakeContinuousObservationFromArrayView(InObservationObject, Frame.Observation, true,
		PmcPolicyInteractorLocal::ObservationTag);
}

void UPmcPolicyInteractor::SpecifyAgentAction_Implementation(FLearningAgentsActionSchemaElement& OutActionSchemaElement, ULearningAgentsActionSchema* InActionSchema)
{
	OutActionSchemaElement = ULearningAgentsActions::SpecifyExclusiveDiscreteActionFromArrayView(InActionSchema, PmcPolicyRules::ActionCount,
		TArrayView<const float>(), true, PmcPolicyInteractorLocal::ActionTag);
}

void UPmcPolicyInteractor::PerformAgentAction_Implementation(const ULearningAgentsActionObject* InActionObject,
	const FLearningAgentsActionObjectElement& InActionObjectElement, const int32 AgentId)
{
	int32 ActionIndex = PmcPolicyRules::HoldAction;
	if (!ULearningAgentsActions::GetExclusiveDiscreteAction(ActionIndex, InActionObject, InActionObjectElement, true, PmcPolicyInteractorLocal::ActionTag))
	{
		ActionIndex = PmcPolicyRules::HoldAction;
	}
	Frames.FindOrAdd(AgentId).Order = PmcPolicyRules::OrderOfAction(ActionIndex);
}

void UPmcPolicyInteractor::MakeAgentActionModifier_Implementation(FLearningAgentsActionModifierElement& OutActionModifierElement,
	ULearningAgentsActionModifier* InActionModifier, const ULearningAgentsObservationObject* InObservationObject,
	const FLearningAgentsObservationObjectElement& InObservationObjectElement, const int32 AgentId)
{
	const FAgentFrame* Frame = Frames.Find(AgentId);
	OutActionModifierElement = ULearningAgentsActions::MakeExclusiveDiscreteActionModifierFromArrayView(InActionModifier,
		Frame ? TArrayView<const int32>(Frame->MaskedActions) : TArrayView<const int32>(), PmcPolicyInteractorLocal::ActionTag);
}
