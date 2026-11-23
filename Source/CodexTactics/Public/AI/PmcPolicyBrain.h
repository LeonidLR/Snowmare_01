#pragma once

#include "CoreMinimal.h"
#include "AI/PmcOrderProvider.h"
#include "AI/PmcPolicyRules.h"

class AActor;
class AOperativeCharacter;
class ULearningAgentsManager;
class ULearningAgentsNeuralNetwork;
class ULearningAgentsPolicy;
class UPmcPolicyInteractor;
class UWorld;

/** How a policy brain is built. The network shape is part of a saved policy: a file loads only into the shape it was trained with. */
struct CODEXTACTICS_API FPmcPolicyBrainSettings
{
	/** The most fighters the policy drives at once (one squad). */
	int32 MaxAgents = 8;
	/** The seed of the network weights and of the action sampling. */
	int32 Seed = 1234;
	/** The policy network: HiddenLayerNum layers of HiddenLayerSize neurons (ELU), no memory cell. */
	int32 HiddenLayerNum = 2;
	int32 HiddenLayerSize = 64;
	/** 1 = sample from the policy's distribution (training), 0 = its most likely order (inference: one seed, one fight). */
	float ActionNoiseScale = 0.f;
};

/**
 * The PMC order policy as an order provider (UE-only, no Godot reference; Learning Agents probe P0, 2026-11-23): a Learning Agents
 * manager + UPmcPolicyInteractor + policy network living on a transient host actor of the world, and the step that feeds a team's
 * decision through them (docs/systems/pmc-policy-probe.md).
 *
 * One round: BeginRound makes the team's fighters agents; every UPmcSquadSubsystem decision calls PickOrders - the frames (observation
 * + action mask) of every agent, OnStepEnded for a trainer, the fallen removed, ONE inference, the sampled orders through
 * PmcPolicyRules::GatePick; EndRound closes the last step and removes the agents. An agent with no request this tick (on a ladder,
 * mid-drop) keeps his last observation and can only sample Hold, so all agents step together (the PPO trainer needs that).
 *
 * Inference only needs this class; the PPO trainer (CodexTacticsDev, FPmcPolicyTraining) hooks OnStepEnded.
 */
class CODEXTACTICS_API FPmcPolicyBrain : public IPmcOrderProvider
{
public:
	virtual ~FPmcPolicyBrain() override;

	/** Spawns the host actor and builds the manager, the interactor and a freshly initialised policy. False (and nothing left behind) when the plugin refuses. */
	bool Setup(UWorld& World, const FPmcPolicyBrainSettings& InSettings);
	/** Removes the agents and destroys the host actor (with it every Learning Agents object). */
	void Shutdown();
	bool IsSetup() const;

	/** A round starts: Fighters (at most MaxAgents) become the agents. AreaFactor = the team's area_factor (the observation's zone fallback). */
	void BeginRound(const TArray<AOperativeCharacter*>& Fighters, float AreaFactor = 2.f);
	/** The round is over: OnStepEnded(true) for the last step (when one was taken), then every agent is removed. */
	void EndRound();

	//~ Begin IPmcOrderProvider Interface
	virtual void PickOrders(int32 TeamId, const FPmcWeights& Weights, TArrayView<FPmcOrderRequest> Requests) override;
	//~ End IPmcOrderProvider Interface

	/** Writes <BasePath>.encoder.bin / .policy.bin / .decoder.bin (Learning Agents network snapshots). */
	bool SavePolicy(const FString& BasePath) const;
	/** Loads the three snapshots; false (the networks untouched) when one is missing or does not fit this build's observation / action layout. */
	bool LoadPolicy(const FString& BasePath);
	static bool HasPolicyFiles(const FString& BasePath);
	/** <BasePath>.<Part>.bin. */
	static FString NetworkFile(const FString& BasePath, const TCHAR* Part);
	/** One network snapshot to / from a file; the load checks the snapshot's input / output sizes and compatibility hash against the network first. */
	static bool SaveNetwork(ULearningAgentsNeuralNetwork* Network, const FString& File);
	static bool LoadNetwork(ULearningAgentsNeuralNetwork* Network, const FString& File);
	/** Reads File and checks it against Network (sizes, compatibility hash, byte count) without loading it. */
	static bool ReadSnapshot(const ULearningAgentsNeuralNetwork* Network, const FString& File, TArray<uint8>& OutBytes);

	/**
	 * The trainer's hook: the agents' previous step is over (bRoundOver: it was the round's last). Called with every agent's new frame
	 * already pushed (a finished episode's final observation) and before the fallen are removed and the next inference runs.
	 */
	TFunction<void(bool bRoundOver)> OnStepEnded;

	ULearningAgentsManager* GetManager() const { return Manager.Get(); }
	UPmcPolicyInteractor* GetInteractor() const { return Interactor.Get(); }
	ULearningAgentsPolicy* GetPolicy() const { return Policy.Get(); }
	/** The agent's fighter is dead (valid while OnStepEnded runs: the fallen are removed right after it). */
	bool IsAgentDead(int32 AgentId) const;
	/** The Learning Agents id of Fighter's agent; INDEX_NONE when he is none (any more). */
	int32 FindAgentId(const AOperativeCharacter* Fighter) const;
	int32 GetAgentNum() const { return Agents.Num(); }
	/** Inference steps of the running round / since Setup. */
	int32 GetStepsThisRound() const { return StepsThisRound; }
	int32 GetStepsTotal() const { return StepsTotal; }
	/** How often each action's order was picked since Setup (PmcPolicyRules::ActionOrders order; after the gates). */
	TConstArrayView<int32> GetPickCounts() const { return PickCounts; }
	const FPmcPolicyBrainSettings& GetSettings() const { return Settings; }
	void SetActionNoiseScale(float Scale) { Settings.ActionNoiseScale = Scale; }

private:
	struct FAgent
	{
		int32 AgentId = INDEX_NONE;
		TWeakObjectPtr<AOperativeCharacter> Fighter;
		/** His last observation (kept while he does not decide; the final observation of his episode). */
		TArray<float> Observation;
		bool bDead = false;
	};

	/** Pushes Agent's frame without a decision: the last observation, nothing but Hold to sample. */
	void PushIdleFrame(FAgent& Agent) const;

	FPmcPolicyBrainSettings Settings;
	TWeakObjectPtr<AActor> Host;
	TWeakObjectPtr<ULearningAgentsManager> Manager;
	TWeakObjectPtr<UPmcPolicyInteractor> Interactor;
	TWeakObjectPtr<ULearningAgentsPolicy> Policy;
	TArray<FAgent> Agents;
	float AreaFactor = 2.f;
	int32 StepsThisRound = 0;
	int32 StepsTotal = 0;
	TArray<int32> PickCounts;
};
