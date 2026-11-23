#pragma once

#include "CoreMinimal.h"
#include "AI/PmcCommanderRules.h"
#include "AI/PmcPolicyRules.h"
#include "LearningAgentsTrainingEnvironment.h"
#include "PmcPolicyTraining.generated.h"

class AOperativeCharacter;
class FPmcPolicyBrain;
class ULearningAgentsCritic;
class ULearningAgentsPPOTrainer;
class UPmcSquadSubsystem;
class UWorld;

namespace UE::Learning
{
	struct IExternalTrainer;
}

/**
 * The Learning Agents training environment of the PMC order policy (UE-only, no Godot reference; Learning Agents probe P0, 2026-11-23).
 * It knows nothing of the arena: FPmcArenaPolicy writes each agent's reward (PmcPolicyRules::StepReward) and completion of the step
 * that just ended, then calls GatherCompletions / GatherRewards. Nothing is reset on a completion: a fallen fighter stays down, the
 * round's end removes every agent (the arena respawns the squads itself).
 */
UCLASS(Transient)
class UPmcPolicyTrainingEnvironment : public ULearningAgentsTrainingEnvironment
{
	GENERATED_BODY()

public:
	/** The step's reward / "his episode is over" per agent id (an agent missing from them: 0 / running). */
	TMap<int32, float> StepRewards;
	TSet<int32> StepCompleted;

	//~ Begin ULearningAgentsTrainingEnvironment Interface
	virtual void GatherAgentReward_Implementation(float& OutReward, const int32 AgentId) override;
	virtual void GatherAgentCompletion_Implementation(ELearningAgentsCompletion& OutCompletion, const int32 AgentId) override;
	virtual void ResetAgentEpisode_Implementation(const int32 AgentId) override;
	//~ End ULearningAgentsTrainingEnvironment Interface
};

/** Policy=off|train|infer of CodexTactics.PmcArena. */
enum class EPmcArenaPolicyMode : uint8
{
	/** The utility scorer picks every order (today's arena). */
	Off,
	/** The policy picks the defenders' orders and a PPO trainer learns from the rounds. */
	Train,
	/** A saved policy picks the defenders' orders (no trainer, no Python). */
	Infer
};

/** The arena's Policy* arguments. */
struct FPmcArenaPolicyOptions
{
	EPmcArenaPolicyMode Mode = EPmcArenaPolicyMode::Off;
	/** PolicyFile=<base path> (absolute, or relative to the project): <base>.encoder.bin / .policy.bin / .decoder.bin (+ .critic.bin). */
	FString File;
	/** PolicyPython=<python.exe> of the trainer; empty = the CODEX_LA_PYTHON environment variable, else the plugin's Intermediate/PipInstall one. */
	FString Python;
	/** PolicyMinutes=<wall minutes>: training stops after the round in which this much time has passed (0 = only Rounds= ends it). */
	float Minutes = 0.f;
	/** PolicyRounds=<n>: one PPO iteration per this many rounds (the replay buffer holds exactly their episodes). */
	int32 RoundsPerIteration = 2;
	/** PolicyDevice=cpu|gpu: where the Python trainer runs the networks. */
	bool bGpu = false;
};

/**
 * The PMC arena's policy session (UE-only, no Godot reference; Learning Agents probe P0, 2026-11-23; docs/systems/pmc-policy-probe.md):
 * owns the policy brain (CodexTactics AI/PmcPolicyBrain) and, with Policy=train, the critic, the training environment, the PPO trainer
 * and its Python process. The arena drives it: Start once, BeginRound / Sample / EndRound per round, Finish at the end.
 *
 * Every round the policy plays the DEFENDING PMC team. Rewards (PmcPolicyRules::StepReward): +1 to every living defender per attacker
 * killed, -1 for his own death (his episode ends there), +3 the round won, -3 the point captured, -0.02 per second an attacker stands
 * on the point; the survivors' episodes end with the round.
 */
class FPmcArenaPolicy
{
public:
	FPmcArenaPolicy() = default;
	FPmcArenaPolicy(const FPmcArenaPolicy&) = delete;
	FPmcArenaPolicy& operator=(const FPmcArenaPolicy&) = delete;
	~FPmcArenaPolicy();

	static const TCHAR* ModeName(EPmcArenaPolicyMode Mode);
	/** "train" / "infer" / "off" (anything else: false). */
	static bool ParseMode(const FString& Text, EPmcArenaPolicyMode& OutMode);

	/**
	 * Builds the brain (and the trainer). Squad = fighters per side, TimeLimitSeconds / DecisionIntervalSeconds size an episode, Seed
	 * seeds the networks and the sampling. False = the arena must not run (the reason is logged). TrainerOverride (Policy=train): the
	 * caller's own external trainer in place of the Python process (the automation test's stand-in).
	 */
	bool Start(UWorld& World, const FPmcArenaPolicyOptions& InOptions, int32 Squad, float TimeLimitSeconds, float DecisionIntervalSeconds, int32 Seed,
		const TSharedPtr<UE::Learning::IExternalTrainer>& TrainerOverride = nullptr);
	/** A round starts: Defenders (team DefenderTeamId) become the policy's agents and the team's order provider is set. */
	void BeginRound(UPmcSquadSubsystem& Pmc, int32 DefenderTeamId, const TArray<AOperativeCharacter*>& Defenders, float AreaFactor, int32 AttackersAlive);
	/** Every arena step of the fight: the attackers still alive, whether one stands on the point, the step's seconds. */
	void Sample(int32 AttackersAlive, bool bEnemyOnPoint, float DeltaSeconds);
	/** The round is over (before the teams are destroyed): the last step's rewards, every episode closed, the provider cleared. */
	void EndRound(UPmcSquadSubsystem& Pmc, const FPmcRoundOutcome& Outcome, bool bDefenderIsA);
	/** Ends the training (the Python process stops), saves the policy, destroys the brain. */
	void Finish();

	EPmcArenaPolicyMode GetMode() const { return Options.Mode; }
	bool IsActive() const { return Options.Mode != EPmcArenaPolicyMode::Off; }
	bool IsRoundActive() const { return bRoundActive; }
	/** The trainer lost its Python process / the policy could not be built: the arena fails. */
	bool HasFailed() const { return bFailed; }
	/** PolicyMinutes of wall time are over (training only). */
	bool IsTimeUp() const;
	const FPmcArenaPolicyOptions& GetOptions() const { return Options; }
	const FString& GetPythonPath() const { return PythonPath; }
	int32 GetIterations() const { return Iterations; }
	int32 GetEpisodes() const { return EpisodesTotal; }
	int32 GetSteps() const;
	/** The mean episode reward of the last PPO iteration (0 before the first). */
	float GetLastMeanEpisodeReward() const { return LastMeanEpisodeReward; }
	/** The policy brain (the team's order provider); null with Policy=off and after Finish. */
	TSharedPtr<FPmcPolicyBrain> GetBrain() const { return Brain; }

private:
	bool StartTrainer(int32 Squad, int32 MaxEpisodeSteps, int32 Seed, const TSharedPtr<UE::Learning::IExternalTrainer>& TrainerOverride);
	/** Spawns the Python trainer process (PolicyPython= / CODEX_LA_PYTHON / the plugin's venv) and connects to it; null (logged) on failure. */
	TSharedPtr<UE::Learning::IExternalTrainer> ConnectPythonTrainer();
	/** The brain's hook: the agents' step is over - rewards, completions, the experience; a policy update is logged and saved. */
	void HandleStepEnded(bool bRoundOver);
	void SaveNetworks() const;
	void LogPicks(const TCHAR* When) const;

	FPmcArenaPolicyOptions Options;
	TSharedPtr<FPmcPolicyBrain> Brain;
	TWeakObjectPtr<UPmcPolicyTrainingEnvironment> Environment;
	TWeakObjectPtr<ULearningAgentsCritic> Critic;
	TWeakObjectPtr<ULearningAgentsPPOTrainer> Trainer;
	FPmcPolicyRewards Rewards;
	FString PythonPath;
	bool bFailed = false;
	bool bRoundActive = false;
	int32 RoundTeamId = INDEX_NONE;
	double StartedSeconds = 0.0;
	/** The team events since the agents' last step. */
	int32 PendingKills = 0;
	float PendingOnPointSeconds = 0.f;
	int32 LastAttackersAlive = 0;
	/** The round's end as the last step sees it. */
	bool bRoundWon = false;
	bool bPointCaptured = false;
	/** Each running episode's reward so far, the finished ones since the last policy update. */
	TMap<int32, float> EpisodeRewards;
	TArray<float> FinishedEpisodeRewards;
	int32 StepsSinceUpdate = 0;
	int32 EpisodesTotal = 0;
	int32 Iterations = 0;
	/** The brain's inference steps, kept past Finish (the arena's JSON reads them after it). */
	int32 FinalSteps = 0;
	int32 LastUpdateNumber = 0;
	float LastMeanEpisodeReward = 0.f;
};
