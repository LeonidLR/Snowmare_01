// The PMC arena's policy session: the order policy's brain + the PPO trainer (Learning Agents probe P0, 2026-11-23; UE-only, no Godot
// reference; docs/systems/pmc-policy-probe.md). Used by CodexTactics.PmcArena Policy=train|infer (Debug/PmcArenaCommand.cpp).

#include "Debug/PmcPolicyTraining.h"

#include "AI/PmcPolicyBrain.h"
#include "AI/PmcPolicyInteractor.h"
#include "AI/PmcSquadSubsystem.h"
#include "Characters/OperativeCharacter.h"
#include "CodexTactics.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "LearningAgentsCommunicator.h"
#include "LearningAgentsCritic.h"
#include "LearningAgentsManager.h"
#include "LearningAgentsNeuralNetwork.h"
#include "LearningAgentsPolicy.h"
#include "LearningAgentsPPOTrainer.h"
#include "LearningCompletion.h"
#include "LearningExperience.h"
#include "LearningExternalTrainer.h"
#include "LearningNeuralNetwork.h"
#include "LearningTrainer.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Scalability.h"

// --- UPmcPolicyTrainingEnvironment -----------------------------------------------------------------------------------------

void UPmcPolicyTrainingEnvironment::GatherAgentReward_Implementation(float& OutReward, const int32 AgentId)
{
	const float* Reward = StepRewards.Find(AgentId);
	OutReward = Reward ? *Reward : 0.f;
}

void UPmcPolicyTrainingEnvironment::GatherAgentCompletion_Implementation(ELearningAgentsCompletion& OutCompletion, const int32 AgentId)
{
	// A death and the round's end are real ends of the episode (no value to bootstrap from): terminations, not truncations.
	OutCompletion = StepCompleted.Contains(AgentId) ? ELearningAgentsCompletion::Termination : ELearningAgentsCompletion::Running;
}

void UPmcPolicyTrainingEnvironment::ResetAgentEpisode_Implementation(const int32 AgentId)
{
	// Nothing to reset in the world: the fallen stay down, and the arena starts the next round itself.
}

#if !UE_BUILD_SHIPPING

namespace PmcPolicyTrainingLocal
{
	/** The Python trainer may take this long to answer (a cold torch import, a PPO iteration without a ping), s. */
	constexpr float TrainerTimeoutSeconds = 180.f;
	/** Episode steps kept beyond TimeLimit / DecisionInterval (the round's last partial interval, the breather). */
	constexpr int32 EpisodeStepMargin = 16;

	int32 PolicyUpdateNumber(const FPmcPolicyBrain& Brain)
	{
		ULearningAgentsPolicy* Policy = Brain.GetPolicy();
		ULearningAgentsNeuralNetwork* Network = Policy ? Policy->GetPolicyNetworkAsset() : nullptr;
		return Network && Network->NeuralNetworkData ? Network->NeuralNetworkData->GetUpdateNumber() : 0;
	}
}

// --- FPmcArenaPolicy -------------------------------------------------------------------------------------------------------

FPmcArenaPolicy::~FPmcArenaPolicy()
{
	Finish();
}

const TCHAR* FPmcArenaPolicy::ModeName(EPmcArenaPolicyMode Mode)
{
	switch (Mode)
	{
	case EPmcArenaPolicyMode::Train: return TEXT("train");
	case EPmcArenaPolicyMode::Infer: return TEXT("infer");
	default: return TEXT("off");
	}
}

bool FPmcArenaPolicy::ParseMode(const FString& Text, EPmcArenaPolicyMode& OutMode)
{
	const FString Lower = Text.ToLower();
	if (Lower == TEXT("train"))
	{
		OutMode = EPmcArenaPolicyMode::Train;
		return true;
	}
	if (Lower == TEXT("infer"))
	{
		OutMode = EPmcArenaPolicyMode::Infer;
		return true;
	}
	if (Lower == TEXT("off") || Lower == TEXT("0") || Lower == TEXT("none"))
	{
		OutMode = EPmcArenaPolicyMode::Off;
		return true;
	}
	return false;
}

bool FPmcArenaPolicy::Start(UWorld& World, const FPmcArenaPolicyOptions& InOptions, int32 Squad, float TimeLimitSeconds, float DecisionIntervalSeconds, int32 Seed,
	const TSharedPtr<UE::Learning::IExternalTrainer>& TrainerOverride)
{
	Finish();
	Options = InOptions;
	bFailed = false;
	Iterations = 0;
	EpisodesTotal = 0;
	FinalSteps = 0;
	StepsSinceUpdate = 0;
	FinishedEpisodeRewards.Reset();
	LastMeanEpisodeReward = 0.f;
	StartedSeconds = FPlatformTime::Seconds();
	if (Options.Mode == EPmcArenaPolicyMode::Off)
	{
		return true;
	}
	if (Options.File.IsEmpty())
	{
		Options.File = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Arena") / TEXT("Policy") / TEXT("pmc_policy"));
	}
	const bool bTrain = Options.Mode == EPmcArenaPolicyMode::Train;

	FPmcPolicyBrainSettings BrainSettings;
	BrainSettings.MaxAgents = FMath::Max(1, Squad);
	BrainSettings.Seed = Seed;
	// Training samples the policy's distribution (exploration); inference takes its most likely order (one seed, one fight).
	BrainSettings.ActionNoiseScale = bTrain ? 1.f : 0.f;
	Brain = MakeShared<FPmcPolicyBrain>();
	if (!Brain->Setup(World, BrainSettings))
	{
		bFailed = true;
		Brain.Reset();
		return false;
	}

	const bool bHasFiles = FPmcPolicyBrain::HasPolicyFiles(Options.File);
	if (!bTrain || bHasFiles)
	{
		// Policy=infer needs the saved policy; Policy=train goes on from it when it is there (a new PolicyFile starts from scratch).
		if (!Brain->LoadPolicy(Options.File))
		{
			UE_LOG(LogCodexTactics, Error, TEXT("[PmcPolicy] Policy=%s: the policy at %s could not be loaded"), ModeName(Options.Mode), *Options.File);
			bFailed = true; // before Finish: a failed start never saves over the file
			Finish();
			return false;
		}
		UE_LOG(LogCodexTactics, Display, TEXT("[PmcPolicy] policy loaded from %s (.encoder / .policy / .decoder .bin)%s"), *Options.File,
			bTrain ? TEXT(" - the training goes on from it") : TEXT(""));
	}
	else
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[PmcPolicy] no policy at %s yet: the training starts from freshly initialised networks"), *Options.File);
	}

	if (bTrain)
	{
		const int32 MaxEpisodeSteps = FMath::CeilToInt32(FMath::Max(TimeLimitSeconds, 1.f) / FMath::Max(DecisionIntervalSeconds, 0.1f))
			+ PmcPolicyTrainingLocal::EpisodeStepMargin;
		if (!StartTrainer(FMath::Max(1, Squad), MaxEpisodeSteps, Seed, TrainerOverride))
		{
			bFailed = true; // before Finish: a failed start never saves over the file
			Finish();
			return false;
		}
		Brain->OnStepEnded = [this](bool bRoundOver) { HandleStepEnded(bRoundOver); };
	}
	LastUpdateNumber = PmcPolicyTrainingLocal::PolicyUpdateNumber(*Brain);
	UE_LOG(LogCodexTactics, Display, TEXT("[PmcPolicy] Policy=%s ready: the policy picks the defending PMC team's orders; file %s"), ModeName(Options.Mode), *Options.File);
	return true;
}

bool FPmcArenaPolicy::StartTrainer(int32 Squad, int32 MaxEpisodeSteps, int32 Seed, const TSharedPtr<UE::Learning::IExternalTrainer>& TrainerOverride)
{
	ULearningAgentsManager* Manager = Brain->GetManager();
	ULearningAgentsInteractor* Interactor = Brain->GetInteractor();
	ULearningAgentsPolicy* Policy = Brain->GetPolicy();
	if (!Manager || !Interactor || !Policy)
	{
		return false;
	}

	// The critic: the same shape as the policy; <PolicyFile>.critic.bin goes on with the policy when it is there.
	FLearningAgentsCriticSettings CriticSettings;
	CriticSettings.HiddenLayerNum = Brain->GetSettings().HiddenLayerNum;
	CriticSettings.HiddenLayerSize = Brain->GetSettings().HiddenLayerSize;
	ULearningAgentsCritic* NewCritic = ULearningAgentsCritic::MakeCritic(Manager, Interactor, Policy, ULearningAgentsCritic::StaticClass(), TEXT("PmcPolicyCritic"),
		nullptr, true, CriticSettings, Seed);
	if (!NewCritic)
	{
		UE_LOG(LogCodexTactics, Error, TEXT("[PmcPolicy] the critic's setup failed (see LogLearning)"));
		return false;
	}
	Critic = NewCritic;
	if (const FString CriticFile = FPmcPolicyBrain::NetworkFile(Options.File, TEXT("critic")); FPaths::FileExists(CriticFile))
	{
		const bool bCritic = FPmcPolicyBrain::LoadNetwork(NewCritic->GetCriticNetworkAsset(), CriticFile);
		UE_LOG(LogCodexTactics, Display, TEXT("[PmcPolicy] critic %s %s"), bCritic ? TEXT("loaded from") : TEXT("NOT loaded from (a fresh one instead)"), *CriticFile);
	}

	ULearningAgentsTrainingEnvironment* NewEnvironment = ULearningAgentsTrainingEnvironment::MakeTrainingEnvironment(Manager,
		UPmcPolicyTrainingEnvironment::StaticClass(), TEXT("PmcPolicyEnvironment"));
	if (!NewEnvironment)
	{
		UE_LOG(LogCodexTactics, Error, TEXT("[PmcPolicy] the training environment's setup failed (see LogLearning)"));
		return false;
	}
	Environment = CastChecked<UPmcPolicyTrainingEnvironment>(NewEnvironment);

	FLearningAgentsCommunicator Communicator;
	if (TrainerOverride.IsValid())
	{
		PythonPath = TEXT("(the caller's trainer)");
		Communicator.Trainer = TrainerOverride;
	}
	else
	{
		Communicator.Trainer = ConnectPythonTrainer();
	}
	if (!Communicator.Trainer.IsValid() || !Communicator.Trainer->IsValid())
	{
		UE_LOG(LogCodexTactics, Error, TEXT("[PmcPolicy] no connection to the trainer process (see LogLearning)"));
		return false;
	}

	// One PPO iteration per RoundsPerIteration rounds: the replay buffer holds exactly their episodes (one per defender - he dies or
	// the round ends), so it fills by the episode count and no episode is cut at the buffer's end (a cut loses the round's reward).
	const int32 EpisodesPerIteration = Squad * FMath::Max(1, Options.RoundsPerIteration);
	FLearningAgentsPPOTrainerSettings TrainerSettings;
	TrainerSettings.MaxEpisodeStepNum = MaxEpisodeSteps;
	TrainerSettings.MaximumRecordedEpisodesPerIteration = EpisodesPerIteration;
	TrainerSettings.MaximumRecordedStepsPerIteration = EpisodesPerIteration * MaxEpisodeSteps;
	ULearningAgentsPPOTrainer* NewTrainer = ULearningAgentsPPOTrainer::MakePPOTrainer(Manager, Interactor, NewEnvironment, Policy, NewCritic, Communicator,
		ULearningAgentsPPOTrainer::StaticClass(), TEXT("PmcPolicyTrainer"), TrainerSettings);
	if (!NewTrainer)
	{
		UE_LOG(LogCodexTactics, Error, TEXT("[PmcPolicy] the PPO trainer's setup failed (see LogLearning)"));
		return false;
	}
	Trainer = NewTrainer;

	FLearningAgentsPPOTrainingSettings Training;
	Training.LearningRatePolicy = 0.0003f;
	Training.LearningRateCritic = 0.001f;
	Training.PolicyBatchSize = 256;
	Training.CriticBatchSize = 512;
	Training.PolicyWindowSize = 1; // no memory cell: every step trains alone
	Training.IterationsPerGather = 32;
	Training.ActionEntropyWeight = 0.01f; // keeps the nine orders explored
	Training.DiscountFactor = 0.99f; // per decision (0.5 s): a horizon of about 50 s
	Training.RandomSeed = FMath::Max(0, Seed);
	Training.Device = Options.bGpu ? ELearningAgentsTrainingDevice::GPU : ELearningAgentsTrainingDevice::CPU;
	Training.bUseTensorboard = false;
	Training.bSaveSnapshots = false;
	// The game's clock stays the launcher's (-UseFixedTimeStep -FPS=60 for the arena): nothing of the trainer's game settings applies.
	FLearningAgentsTrainingGameSettings Game;
	Game.bUseFixedTimeStep = FApp::UseFixedTimeStep();
	Game.FixedTimeStepFrequency = FApp::GetFixedDeltaTime() > UE_SMALL_NUMBER ? static_cast<float>(1.0 / FApp::GetFixedDeltaTime()) : 60.f;
	Game.bSetMaxPhysicsStepToFixedTimeStep = false;
	Game.bDisableMaxFPS = false;
	Game.bDisableVSync = false;
	Game.bUseUnlitViewportRendering = false;
#if WITH_EDITORONLY_DATA
	Game.bDisableUseLessCPUInTheBackground = false;
	Game.bDisableEditorVSync = false;
#endif
	NewTrainer->BeginTraining(Training, Game, false);
	if (!NewTrainer->IsTraining() || NewTrainer->HasTrainingFailed())
	{
		UE_LOG(LogCodexTactics, Error, TEXT("[PmcPolicy] the PPO training did not start (see LogLearning: the trainer process, its Python packages)"));
		return false;
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[PmcPolicy] PPO training started: one iteration per %d round(s) (%d episodes), episodes up to %d steps, device %s, lr %.4f / %.4f, entropy %.3f, discount %.2f"),
		FMath::Max(1, Options.RoundsPerIteration), EpisodesPerIteration, MaxEpisodeSteps, Options.bGpu ? TEXT("gpu") : TEXT("cpu"), Training.LearningRatePolicy,
		Training.LearningRateCritic, Training.ActionEntropyWeight, Training.DiscountFactor);
	return true;
}

TSharedPtr<UE::Learning::IExternalTrainer> FPmcArenaPolicy::ConnectPythonTrainer()
{
	// The Python trainer process (LearningCore's Content/Python/train.py, module learning_core.train_ppo) over shared memory.
	// The plugin's own spawn only knows <project>/Intermediate/PipInstall/Scripts/python.exe (the editor's pip-install venv); the
	// process object takes any interpreter, so the arena's PolicyPython= / CODEX_LA_PYTHON venv is used when given.
	FLearningAgentsTrainerProcessSettings ProcessSettings;
	ProcessSettings.TaskName = TEXT("PmcPolicy");
	ProcessSettings.TrainerFileName = TEXT("learning_core.train_ppo");
	FLearningAgentsSharedMemoryCommunicatorSettings SharedMemorySettings;
	SharedMemorySettings.Timeout = PmcPolicyTrainingLocal::TrainerTimeoutSeconds;
	FLearningAgentsSharedMemoryTrainerProcess Process;
	const bool bExternalProcess = FParse::Param(FCommandLine::Get(), TEXT("LearningAgentsUseExternalTrainingProcess"));
	if (bExternalProcess)
	{
		PythonPath = TEXT("(external: -LearningAgentsUseExternalTrainingProcess)");
	}
	else
	{
		PythonPath = Options.Python;
		if (PythonPath.IsEmpty())
		{
			PythonPath = FPlatformMisc::GetEnvironmentVariable(TEXT("CODEX_LA_PYTHON"));
		}
		if (PythonPath.IsEmpty())
		{
			PythonPath = UE::Learning::Trainer::GetPythonExecutablePath(ProcessSettings.GetIntermediatePath());
		}
		PythonPath = FPaths::ConvertRelativePathToFull(PythonPath);
		const FString PythonContent = UE::Learning::Trainer::GetPythonContentPath(ProcessSettings.GetEditorEnginePath());
		if (!FPaths::FileExists(PythonPath))
		{
			UE_LOG(LogCodexTactics, Error, TEXT("[PmcPolicy] the trainer's Python is missing: %s (PolicyPython=<python.exe>, or the CODEX_LA_PYTHON environment variable)"), *PythonPath);
			return nullptr;
		}
		if (!FPaths::DirectoryExists(PythonContent))
		{
			UE_LOG(LogCodexTactics, Error, TEXT("[PmcPolicy] the LearningCore trainer scripts are missing: %s"), *PythonContent);
			return nullptr;
		}
		// The trainer imports these before it reports anything: a missing one would only show as a timeout.
		int32 ReturnCode = -1;
		FString StdOut;
		FString StdErr;
		const bool bRan = FPlatformProcess::ExecProcess(*PythonPath, TEXT("-c \"import numpy, torch; print(torch.__version__, 'cuda' if torch.cuda.is_available() else 'cpu')\""),
			&ReturnCode, &StdOut, &StdErr);
		if (!bRan || ReturnCode != 0)
		{
			UE_LOG(LogCodexTactics, Error, TEXT("[PmcPolicy] %s cannot import the trainer's packages (numpy, torch): %s"), *PythonPath,
				*StdErr.TrimStartAndEnd().Right(600));
			return nullptr;
		}
		UE_LOG(LogCodexTactics, Display, TEXT("[PmcPolicy] trainer Python %s: torch %s"), *PythonPath, *StdOut.TrimStartAndEnd());
		// learning_core/train_common.py also does `from smart_open import open` (local files only on the shared-memory path). A Python
		// without that package gets the project's local-file stand-in (Scripts/Tools/la_trainer/smart_open.py) on the trainer process's
		// PYTHONPATH - the real package, once installed, is used instead.
		FString PreviousPythonPath;
		FString PreviousNoBytecode;
		bool bStandInPath = false;
		FString SmartOpenOut;
		FString SmartOpenErr;
		ReturnCode = -1;
		if (!FPlatformProcess::ExecProcess(*PythonPath, TEXT("-c \"import smart_open\""), &ReturnCode, &SmartOpenOut, &SmartOpenErr) || ReturnCode != 0)
		{
			const FString StandInDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Scripts") / TEXT("Tools") / TEXT("la_trainer"));
			if (!FPaths::FileExists(StandInDir / TEXT("smart_open.py")))
			{
				UE_LOG(LogCodexTactics, Error, TEXT("[PmcPolicy] %s has no smart_open package and the stand-in %s/smart_open.py is missing"), *PythonPath, *StandInDir);
				return nullptr;
			}
			PreviousPythonPath = FPlatformMisc::GetEnvironmentVariable(TEXT("PYTHONPATH"));
			FPlatformMisc::SetEnvironmentVar(TEXT("PYTHONPATH"),
				*(PreviousPythonPath.IsEmpty() ? StandInDir : StandInDir + FPlatformMisc::GetPathVarDelimiter() + PreviousPythonPath));
			// No __pycache__ next to the stand-in (the repository does not ignore them).
			PreviousNoBytecode = FPlatformMisc::GetEnvironmentVariable(TEXT("PYTHONDONTWRITEBYTECODE"));
			FPlatformMisc::SetEnvironmentVar(TEXT("PYTHONDONTWRITEBYTECODE"), TEXT("1"));
			bStandInPath = true;
			UE_LOG(LogCodexTactics, Display, TEXT("[PmcPolicy] smart_open is not installed in %s: the trainer process gets the local-file stand-in %s (PYTHONPATH)"),
				*PythonPath, *StandInDir);
		}
		// The process starts here and inherits the environment as it is now.
		Process.TrainerProcess = MakeShared<UE::Learning::FSharedMemoryTrainerServerProcess>(ProcessSettings.TaskName, ProcessSettings.GetCustomTrainerModulePath(),
			ProcessSettings.TrainerFileName, PythonPath, PythonContent, UE::Learning::Trainer::GetIntermediatePath(ProcessSettings.GetIntermediatePath()),
			ProcessSettings.GetNneCpuContentPath(), SharedMemorySettings.Timeout);
		if (bStandInPath)
		{
			FPlatformMisc::SetEnvironmentVar(TEXT("PYTHONPATH"), *PreviousPythonPath);
			FPlatformMisc::SetEnvironmentVar(TEXT("PYTHONDONTWRITEBYTECODE"), *PreviousNoBytecode);
		}
	}
	return ULearningAgentsCommunicatorLibrary::MakeSharedMemoryCommunicator(Process, ProcessSettings, SharedMemorySettings).Trainer;
}

void FPmcArenaPolicy::BeginRound(UPmcSquadSubsystem& Pmc, int32 DefenderTeamId, const TArray<AOperativeCharacter*>& Defenders, float AreaFactor, int32 AttackersAlive)
{
	if (!IsActive() || bFailed || !Brain.IsValid() || !Brain->IsSetup())
	{
		return;
	}
	PendingKills = 0;
	PendingOnPointSeconds = 0.f;
	LastAttackersAlive = AttackersAlive;
	bRoundWon = false;
	bPointCaptured = false;
	EpisodeRewards.Reset();
	Brain->BeginRound(Defenders, AreaFactor);
	Pmc.SetTeamOrderProvider(DefenderTeamId, Brain);
	RoundTeamId = DefenderTeamId;
	bRoundActive = true;
	UE_LOG(LogCodexTactics, Display, TEXT("[PmcPolicy] round: team %d defends under the policy (%d agents, %d attackers)"), DefenderTeamId, Brain->GetAgentNum(), AttackersAlive);
}

void FPmcArenaPolicy::Sample(int32 AttackersAlive, bool bEnemyOnPoint, float DeltaSeconds)
{
	if (!bRoundActive)
	{
		return;
	}
	PendingKills += FMath::Max(0, LastAttackersAlive - AttackersAlive);
	LastAttackersAlive = AttackersAlive;
	PendingOnPointSeconds += bEnemyOnPoint ? FMath::Max(DeltaSeconds, 0.f) : 0.f;
}

void FPmcArenaPolicy::EndRound(UPmcSquadSubsystem& Pmc, const FPmcRoundOutcome& Outcome, bool bDefenderIsA)
{
	if (!bRoundActive)
	{
		return;
	}
	PmcPolicyRules::OutcomeEvents(Outcome, bDefenderIsA, bRoundWon, bPointCaptured);
	if (Brain.IsValid())
	{
		Brain->EndRound(); // -> HandleStepEnded(true) when the trainer listens
	}
	Pmc.SetTeamOrderProvider(RoundTeamId, nullptr);
	bRoundActive = false;
	RoundTeamId = INDEX_NONE;
	EpisodeRewards.Reset();
	LogPicks(TEXT("so far"));
}

void FPmcArenaPolicy::HandleStepEnded(bool bRoundOver)
{
	UPmcPolicyTrainingEnvironment* LiveEnvironment = Environment.Get();
	ULearningAgentsPPOTrainer* LiveTrainer = Trainer.Get();
	ULearningAgentsManager* Manager = Brain.IsValid() ? Brain->GetManager() : nullptr;
	if (bFailed || !LiveEnvironment || !LiveTrainer || !Manager || !LiveTrainer->IsTraining())
	{
		return;
	}
	// The step's rewards: the team's events for every agent that lived through it, his own death for the one that did not.
	FPmcPolicyEvents Shared;
	Shared.AttackersKilled = PendingKills;
	Shared.EnemyOnPointSeconds = PendingOnPointSeconds;
	PendingKills = 0;
	PendingOnPointSeconds = 0.f;
	LiveEnvironment->StepRewards.Reset();
	LiveEnvironment->StepCompleted.Reset();
	for (const int32 AgentId : Manager->GetAllAgentIds())
	{
		FPmcPolicyEvents Events = Shared;
		Events.bDied = Brain->IsAgentDead(AgentId);
		// The round's end is the survivors': a fallen man's episode ended with his death.
		Events.bRoundWon = bRoundOver && !Events.bDied && bRoundWon;
		Events.bPointCaptured = bRoundOver && !Events.bDied && bPointCaptured;
		const float Reward = PmcPolicyRules::StepReward(Events, Rewards);
		LiveEnvironment->StepRewards.Add(AgentId, Reward);
		float& Episode = EpisodeRewards.FindOrAdd(AgentId);
		Episode += Reward;
		if (bRoundOver || Events.bDied)
		{
			LiveEnvironment->StepCompleted.Add(AgentId);
			FinishedEpisodeRewards.Add(Episode);
			EpisodeRewards.Remove(AgentId);
			++EpisodesTotal;
		}
		++StepsSinceUpdate;
	}
	LiveEnvironment->GatherCompletions();
	LiveEnvironment->GatherRewards();
	// The experience goes to the episode buffers; a full replay buffer goes to Python and this call waits for the new networks.
	// The agents are not reset on an update: their round goes on with the new policy.
	LiveTrainer->ProcessExperience(false);
	if (LiveTrainer->HasTrainingFailed() || !LiveTrainer->IsTraining())
	{
		UE_LOG(LogCodexTactics, Error, TEXT("[PmcPolicy] the PPO training stopped (see LogLearning: the trainer process timed out or ended)"));
		bFailed = true;
		return;
	}
	if (const int32 UpdateNumber = PmcPolicyTrainingLocal::PolicyUpdateNumber(*Brain); UpdateNumber != LastUpdateNumber)
	{
		LastUpdateNumber = UpdateNumber;
		++Iterations;
		float Sum = 0.f;
		for (const float Value : FinishedEpisodeRewards)
		{
			Sum += Value;
		}
		LastMeanEpisodeReward = FinishedEpisodeRewards.IsEmpty() ? 0.f : Sum / static_cast<float>(FinishedEpisodeRewards.Num());
		UE_LOG(LogCodexTactics, Display, TEXT("[PmcPolicy] iteration %d: mean episode reward %.3f over %d episodes (%d steps), %d episodes in all, %.1f min"),
			Iterations, LastMeanEpisodeReward, FinishedEpisodeRewards.Num(), StepsSinceUpdate, EpisodesTotal, (FPlatformTime::Seconds() - StartedSeconds) / 60.0);
		FinishedEpisodeRewards.Reset();
		StepsSinceUpdate = 0;
		SaveNetworks(); // every iteration: a run that is cut short keeps its last policy
	}
}

void FPmcArenaPolicy::SaveNetworks() const
{
	if (!Brain.IsValid() || !Brain->IsSetup() || Options.File.IsEmpty())
	{
		return;
	}
	const bool bPolicy = Brain->SavePolicy(Options.File);
	ULearningAgentsCritic* LiveCritic = Critic.Get();
	const bool bCritic = LiveCritic && FPmcPolicyBrain::SaveNetwork(LiveCritic->GetCriticNetworkAsset(), FPmcPolicyBrain::NetworkFile(Options.File, TEXT("critic")));
	if (!bPolicy || !bCritic)
	{
		UE_LOG(LogCodexTactics, Warning, TEXT("[PmcPolicy] the policy was not fully saved to %s (policy %d, critic %d)"), *Options.File, bPolicy ? 1 : 0, bCritic ? 1 : 0);
	}
}

void FPmcArenaPolicy::LogPicks(const TCHAR* When) const
{
	if (!Brain.IsValid())
	{
		return;
	}
	FString Picks;
	const TConstArrayView<int32> Counts = Brain->GetPickCounts();
	for (int32 Index = 0; Index < Counts.Num(); ++Index)
	{
		Picks += FString::Printf(TEXT(" %s %d"), PmcCommanderRules::OrderName(PmcPolicyRules::OrderOfAction(Index)), Counts[Index]);
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[PmcPolicy] picks %s (%d steps):%s"), When, Brain->GetStepsTotal(), *Picks);
}

bool FPmcArenaPolicy::IsTimeUp() const
{
	return Options.Mode == EPmcArenaPolicyMode::Train && Options.Minutes > 0.f
		&& (FPlatformTime::Seconds() - StartedSeconds) >= static_cast<double>(Options.Minutes) * 60.0;
}

int32 FPmcArenaPolicy::GetSteps() const
{
	return Brain.IsValid() ? Brain->GetStepsTotal() : FinalSteps;
}

void FPmcArenaPolicy::Finish()
{
	if (!Brain.IsValid())
	{
		return;
	}
	Brain->OnStepEnded = nullptr;
	if (ULearningAgentsPPOTrainer* LiveTrainer = Trainer.Get())
	{
		if (LiveTrainer->IsTraining())
		{
			LiveTrainer->EndTraining(); // stops the Python process
		}
		if (Options.Mode == EPmcArenaPolicyMode::Train && !bFailed)
		{
			SaveNetworks();
			UE_LOG(LogCodexTactics, Display, TEXT("[PmcPolicy] training over: %d iterations, %d episodes, %d steps, last mean episode reward %.3f; policy saved to %s (.encoder / .policy / .decoder / .critic .bin)"),
				Iterations, EpisodesTotal, Brain->GetStepsTotal(), LastMeanEpisodeReward, *Options.File);
		}
	}
	LogPicks(TEXT("of the run"));
	bRoundActive = false;
	FinalSteps = Brain->GetStepsTotal();
	Brain->Shutdown();
	Brain.Reset();
	Environment.Reset();
	Critic.Reset();
	Trainer.Reset();
}

// --- CodexTactics.AI.PmcPolicy.TrainerLoop -----------------------------------------------------------------------------------

#if WITH_DEV_AUTOMATION_TESTS

// The session's PPO loop against the plugin's real ULearningAgentsPPOTrainer (episode buffers, the replay buffer, completions, the
// iteration bookkeeping) with a stand-in for the Python process: two scripted rounds of three defenders, the rewards and the replay
// buffer checked by hand. It lives in this module because the session does (the tests module does not link CodexTacticsDev).

namespace PmcPolicyTrainingLocal
{
	/** Answers the PPO trainer as the Python process would, trains nothing: every pushed network comes back as it went. */
	struct FStandInTrainer : public UE::Learning::IExternalTrainer
	{
		int32 Networks = 0;
		int32 Configs = 0;
		int32 Stops = 0;
		TArray<int32> BufferEpisodes;
		TArray<int32> BufferSteps;
		TArray<int32> BufferTerminations;
		TArray<float> BufferRewardSums;

		static void Reload(ULearningNeuralNetworkData& Network)
		{
			// A network that "came back": the same bytes loaded again (the update number moves, as after a real iteration).
			TArray<uint8> Bytes;
			Bytes.SetNumUninitialized(Network.GetSnapshotByteNum());
			Network.SaveToSnapshot(Bytes);
			Network.LoadFromSnapshot(Bytes);
		}

		virtual bool IsValid() override { return true; }
		virtual void Terminate() override {}
		virtual UE::Learning::ETrainerResponse SendStop() override
		{
			++Stops;
			return UE::Learning::ETrainerResponse::Success;
		}
		virtual UE::Learning::ETrainerResponse Wait() override { return UE::Learning::ETrainerResponse::Success; }
		virtual bool HasNetworkOrCompleted() override { return false; }
		virtual UE::Learning::ETrainerResponse SendConfigs(const TSharedRef<FJsonObject>& DataConfigObject, const TSharedRef<FJsonObject>& ModelConfigObject,
			const TSharedRef<FJsonObject>& TrainerConfigObject, const UE::Learning::ELogSetting LogSettings) override
		{
			++Configs;
			return UE::Learning::ETrainerResponse::Success;
		}
		virtual int32 AddNetwork(const ULearningNeuralNetworkData& Network) override { return Networks++; }
		virtual UE::Learning::ETrainerResponse ReceiveNetwork(const int32 NetworkId, ULearningNeuralNetworkData& OutNetwork, FRWLock* NetworkLock,
			const UE::Learning::ELogSetting LogSettings) override
		{
			Reload(OutNetwork);
			return UE::Learning::ETrainerResponse::Success;
		}
		virtual TArray<UE::Learning::ETrainerResponse> ReceiveNetworks(const TArray<int32>& NetworkIds, TArray<TObjectPtr<ULearningNeuralNetworkData>> InNetworks,
			TArray<FRWLock*> NetworkLocks, const UE::Learning::ELogSetting LogSettings) override
		{
			TArray<UE::Learning::ETrainerResponse> Responses;
			for (const TObjectPtr<ULearningNeuralNetworkData>& Network : InNetworks)
			{
				Reload(*Network);
				Responses.Add(UE::Learning::ETrainerResponse::Success);
			}
			return Responses;
		}
		virtual UE::Learning::ETrainerResponse SendNetwork(const int32 NetworkId, const ULearningNeuralNetworkData& Network, FRWLock* NetworkLock,
			const UE::Learning::ELogSetting LogSettings) override
		{
			return UE::Learning::ETrainerResponse::Success;
		}
		virtual int32 AddReplayBuffer(const UE::Learning::FReplayBuffer& ReplayBuffer) override { return 0; }
		virtual UE::Learning::ETrainerResponse SendReplayBuffer(const int32 ReplayBufferId, const UE::Learning::FReplayBuffer& ReplayBuffer,
			const UE::Learning::ELogSetting LogSettings) override
		{
			BufferEpisodes.Add(ReplayBuffer.GetEpisodeNum());
			BufferSteps.Add(ReplayBuffer.GetStepNum());
			int32 Terminations = 0;
			for (int32 Episode = 0; Episode < ReplayBuffer.GetEpisodeNum(); ++Episode)
			{
				Terminations += ReplayBuffer.GetEpisodeCompletionModes()[Episode] == UE::Learning::ECompletionMode::Terminated ? 1 : 0;
			}
			BufferTerminations.Add(Terminations);
			float RewardSum = 0.f;
			for (int32 Step = 0; Step < ReplayBuffer.GetStepNum(); ++Step)
			{
				RewardSum += ReplayBuffer.GetRewards(0)[Step][0];
			}
			BufferRewardSums.Add(RewardSum);
			return UE::Learning::ETrainerResponse::Success;
		}
	};

	/** A scratch game world for the session's host actor and the fighter bodies. */
	struct FScopedTrainingTestWorld
	{
		UWorld* World = nullptr;
		FWorldContext* Context = nullptr;

		FScopedTrainingTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("PmcPolicyTrainerLoopWorld"));
			if (World && GEngine)
			{
				Context = &GEngine->CreateNewWorldContext(EWorldType::Game);
				Context->SetCurrentWorld(World);
				World->InitializeActorsForPlay(FURL());
			}
		}

		~FScopedTrainingTestWorld()
		{
			if (World && GEngine)
			{
				if (Context)
				{
					GEngine->DestroyWorldContext(World);
				}
				World->DestroyWorld(false);
			}
		}
	};

	/** Squad fighter bodies without controllers (the brain only asks whether they live). */
	TArray<AOperativeCharacter*> SpawnTrainingTestSquad(UWorld& World, int32 Count)
	{
		TArray<AOperativeCharacter*> Out;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const FTransform Transform(FVector(Index * 300.f, 0.f, 200.f));
			AOperativeCharacter* Fighter = World.SpawnActorDeferred<AOperativeCharacter>(AOperativeCharacter::StaticClass(), Transform, nullptr, nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (Fighter)
			{
				Fighter->AutoPossessAI = EAutoPossessAI::Disabled;
				Fighter->FinishSpawning(Transform);
				Out.Add(Fighter);
			}
		}
		return Out;
	}

	/** One decision of the living fighters through the brain (what UPmcSquadSubsystem::Decide does for a team with a provider). */
	void StepTrainingTestSquad(FPmcPolicyBrain& Brain, const TArray<AOperativeCharacter*>& Fighters)
	{
		const FPmcWeights Weights;
		TArray<FPmcOrderRequest> Requests;
		for (AOperativeCharacter* Fighter : Fighters)
		{
			if (!IsValid(Fighter))
			{
				continue;
			}
			FPmcOrderRequest& Request = Requests.AddDefaulted_GetRef();
			Request.Fighter = Fighter;
			Request.Squad.Alive = Fighters.Num();
			Request.Scored = { { EPmcOrder::TakeCover, 1.f }, { EPmcOrder::Hold, 0.3f } };
			Request.ScorerPick = Request.Pick = EPmcOrder::TakeCover;
		}
		Brain.PickOrders(1, Weights, Requests);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcPolicyTrainerLoopTest, "CodexTactics.AI.PmcPolicy.TrainerLoop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcPolicyTrainerLoopTest::RunTest(const FString&)
{
	using namespace PmcPolicyTrainingLocal;
	FScopedTrainingTestWorld Scope;
	UPmcSquadSubsystem* Pmc = UPmcSquadSubsystem::Get(Scope.World);
	if (!TestNotNull(TEXT("a scratch game world with the PMC squad subsystem"), Pmc))
	{
		return false;
	}
	UWorld& World = *Scope.World;
	// The plugin's EndTraining re-applies the game user settings (its revert of the trainer's game settings): the scalability levels
	// of this editor process are put back afterwards.
	const Scalability::FQualityLevels QualityBefore = Scalability::GetQualityLevels();

	const FString PolicyDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation") / TEXT("PmcPolicyTrainerLoop"));
	IFileManager::Get().DeleteDirectory(*PolicyDir, false, true);
	FPmcArenaPolicyOptions Options;
	Options.Mode = EPmcArenaPolicyMode::Train;
	Options.File = PolicyDir / TEXT("policy");
	Options.RoundsPerIteration = 2;
	const TSharedRef<FStandInTrainer> StandIn = MakeShared<FStandInTrainer>();
	constexpr int32 TeamId = 1;
	{
		FPmcArenaPolicy Policy;
		// Three defenders, 10 s rounds at 0.5 s per decision: 6 episodes of up to 36 steps per iteration.
		if (!TestTrue(TEXT("the session starts with the stand-in trainer"), Policy.Start(World, Options, 3, 10.f, 0.5f, 7, StandIn)))
		{
			Scalability::SetQualityLevels(QualityBefore);
			return false;
		}
		TestEqual(TEXT("the configs went to the trainer once"), StandIn->Configs, 1);
		TestEqual(TEXT("policy, critic, encoder, decoder registered"), StandIn->Networks, 4);
		const TSharedPtr<FPmcPolicyBrain> Brain = Policy.GetBrain();
		if (!TestTrue(TEXT("the brain exists"), Brain.IsValid()))
		{
			Scalability::SetQualityLevels(QualityBefore);
			return false;
		}

		// Round 1: a kill and half a second of an attacker on the point, then one defender falls, then the defenders win.
		TArray<AOperativeCharacter*> Squad = SpawnTrainingTestSquad(World, 3);
		TestEqual(TEXT("three defenders"), Squad.Num(), 3);
		Policy.BeginRound(*Pmc, TeamId, Squad, 2.f, 3);
		TestTrue(TEXT("the defenders' team has the policy as its order provider"), Pmc->HasTeamOrderProvider(TeamId));
		StepTrainingTestSquad(*Brain, Squad); // the first decision: nothing to learn from yet
		Policy.Sample(3, false, 0.5f);
		StepTrainingTestSquad(*Brain, Squad); // step 1: reward 0
		for (int32 Tick = 0; Tick < 5; ++Tick)
		{
			Policy.Sample(2, true, 0.1f);
		}
		StepTrainingTestSquad(*Brain, Squad); // step 2: +1 the kill, -0.01 the point
		Squad[2]->Destroy();
		Policy.Sample(2, false, 0.5f);
		StepTrainingTestSquad(*Brain, Squad); // step 3: the fallen man -1 (his episode: 0 + 0.99 - 1), the others 0
		TestEqual(TEXT("the fallen defender's episode is closed"), Policy.GetEpisodes(), 1);
		FPmcRoundOutcome Won;
		Won.bOver = true;
		Won.Winner = TEXT("A");
		Won.Reason = TEXT("elimination");
		Won.ObjectiveResult = TEXT("eliminated");
		Policy.EndRound(*Pmc, Won, true); // step 4: the two survivors +3 (0 + 0.99 + 0 + 3 each)
		TestFalse(TEXT("the provider is cleared at the round's end"), Pmc->HasTeamOrderProvider(TeamId));
		TestEqual(TEXT("three episodes after round 1"), Policy.GetEpisodes(), 3);
		TestEqual(TEXT("no iteration before the replay buffer is full"), Policy.GetIterations(), 0);
		TestEqual(TEXT("nothing sent yet"), StandIn->BufferEpisodes.Num(), 0);

		// Round 2: an attacker on the point for a second, then the point is captured.
		Squad = SpawnTrainingTestSquad(World, 3);
		Policy.BeginRound(*Pmc, TeamId, Squad, 2.f, 3);
		StepTrainingTestSquad(*Brain, Squad);
		for (int32 Tick = 0; Tick < 10; ++Tick)
		{
			Policy.Sample(3, true, 0.1f);
		}
		StepTrainingTestSquad(*Brain, Squad); // step 1: -0.02
		FPmcRoundOutcome Captured;
		Captured.bOver = true;
		Captured.Winner = TEXT("B");
		Captured.Reason = TEXT("objective");
		Captured.ObjectiveResult = TEXT("captured");
		Policy.EndRound(*Pmc, Captured, true); // step 2: -3 each (-0.02 - 3)

		// The sixth episode filled the replay buffer: one iteration, nothing cut.
		TestEqual(TEXT("six episodes"), Policy.GetEpisodes(), 6);
		TestEqual(TEXT("one PPO iteration"), Policy.GetIterations(), 1);
		if (TestEqual(TEXT("one replay buffer sent"), StandIn->BufferEpisodes.Num(), 1))
		{
			TestEqual(TEXT("... with the six episodes"), StandIn->BufferEpisodes[0], 6);
			TestEqual(TEXT("... all of them terminations"), StandIn->BufferTerminations[0], 6);
			TestEqual(TEXT("... of 3 + 4 + 4 + 2 + 2 + 2 steps"), StandIn->BufferSteps[0], 17);
			TestEqual(TEXT("... and every reward in it"), StandIn->BufferRewardSums[0], -0.01f + 2.f * 3.99f + 3.f * -3.02f, 1e-3f);
		}
		TestEqual(TEXT("the iteration's mean episode reward"), Policy.GetLastMeanEpisodeReward(), (-0.01f + 2.f * 3.99f + 3.f * -3.02f) / 6.f, 1e-3f);
		TestTrue(TEXT("the policy is saved after the iteration"), FPmcPolicyBrain::HasPolicyFiles(Options.File));
		TestTrue(TEXT("... with the critic"), FPaths::FileExists(FPmcPolicyBrain::NetworkFile(Options.File, TEXT("critic"))));
		TestEqual(TEXT("six inferences (four decisions in round 1, two in round 2)"), Policy.GetSteps(), 4 + 2);

		Policy.Finish();
		TestEqual(TEXT("the trainer was told to stop"), StandIn->Stops, 1);
		TestEqual(TEXT("the step total survives Finish"), Policy.GetSteps(), 4 + 2);
		TestFalse(TEXT("no brain after Finish"), Policy.GetBrain().IsValid());
	}
	Scalability::SetQualityLevels(QualityBefore);
	IFileManager::Get().DeleteDirectory(*PolicyDir, false, true);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

#endif // !UE_BUILD_SHIPPING
