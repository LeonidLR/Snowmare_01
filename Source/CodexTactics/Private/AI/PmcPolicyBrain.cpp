// The PMC order policy as an order provider (Learning Agents probe P0, 2026-11-23). UE-only, no Godot reference.

#include "AI/PmcPolicyBrain.h"

#include "AI/PmcPolicyInteractor.h"
#include "Characters/OperativeCharacter.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "LearningAgentsManager.h"
#include "LearningAgentsNeuralNetwork.h"
#include "LearningAgentsPolicy.h"
#include "LearningNeuralNetwork.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace PmcPolicyBrainLocal
{
	inline bool IsFighterAlive(const AOperativeCharacter* Fighter)
	{
		return IsValid(Fighter) && !Fighter->IsKilledInAction() && Fighter->HealthComponent && Fighter->HealthComponent->IsAlive();
	}

	/** A snapshot's header: magic, version, input size, output size, compatibility hash (int32 each; ULearningNeuralNetworkData::SaveToSnapshot). */
	constexpr int32 SnapshotHeaderInts = 5;
}

FPmcPolicyBrain::~FPmcPolicyBrain()
{
	Shutdown();
}

bool FPmcPolicyBrain::Setup(UWorld& World, const FPmcPolicyBrainSettings& InSettings)
{
	Shutdown();
	Settings = InSettings;
	Settings.MaxAgents = FMath::Max(1, Settings.MaxAgents);
	PickCounts.Init(0, PmcPolicyRules::ActionCount);
	StepsTotal = 0;

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.ObjectFlags |= RF_Transient;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* HostActor = World.SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, SpawnParameters);
	if (!HostActor)
	{
		UE_LOG(LogCodexTactics, Error, TEXT("[PmcPolicy] no host actor for the Learning Agents manager"));
		return false;
	}
	Host = HostActor;

	// The manager is registered first and sized second: OnRegister fills the vacant ids of the default size (1), SetMaxAgentNum
	// only adds the rest (sized before the registration it would hold every id twice).
	ULearningAgentsManager* NewManager = NewObject<ULearningAgentsManager>(HostActor, TEXT("PmcPolicyManager"), RF_Transient);
	NewManager->RegisterComponent();
	NewManager->SetMaxAgentNum(Settings.MaxAgents);

	ULearningAgentsInteractor* NewInteractor = ULearningAgentsInteractor::MakeInteractor(NewManager, UPmcPolicyInteractor::StaticClass(), TEXT("PmcPolicyInteractor"));
	if (!NewInteractor)
	{
		UE_LOG(LogCodexTactics, Error, TEXT("[PmcPolicy] the interactor's setup failed (see LogLearning)"));
		Shutdown();
		return false;
	}

	FLearningAgentsPolicySettings PolicySettings;
	PolicySettings.HiddenLayerNum = FMath::Max(0, Settings.HiddenLayerNum);
	PolicySettings.HiddenLayerSize = FMath::Max(1, Settings.HiddenLayerSize);
	// No memory cell: the observation carries the running order and the clocks, and a feed-forward policy trains on little data.
	PolicySettings.MemoryCell = ELearningAgentsMemoryCell::NoMemoryCell;
	PolicySettings.MemoryStateSize = 0;
	PolicySettings.bUseParallelEvaluation = false; // a squad, not a crowd
	ULearningAgentsPolicy* NewPolicy = ULearningAgentsPolicy::MakePolicy(NewManager, NewInteractor, ULearningAgentsPolicy::StaticClass(), TEXT("PmcPolicy"),
		nullptr, nullptr, nullptr, true, true, true, PolicySettings, Settings.Seed);
	if (!NewPolicy)
	{
		UE_LOG(LogCodexTactics, Error, TEXT("[PmcPolicy] the policy's setup failed (see LogLearning)"));
		Shutdown();
		return false;
	}

	Manager = NewManager;
	Interactor = CastChecked<UPmcPolicyInteractor>(NewInteractor);
	Policy = NewPolicy;
	UE_LOG(LogCodexTactics, Display, TEXT("[PmcPolicy] brain ready: %d observations -> %d orders, %d x %d hidden, up to %d agents, seed %d, action noise %.2f"),
		PmcPolicyRules::ObservationSize, PmcPolicyRules::ActionCount, PolicySettings.HiddenLayerNum, PolicySettings.HiddenLayerSize, Settings.MaxAgents,
		Settings.Seed, Settings.ActionNoiseScale);
	return true;
}

void FPmcPolicyBrain::Shutdown()
{
	OnStepEnded = nullptr;
	if (ULearningAgentsManager* LiveManager = Manager.Get(); LiveManager && LiveManager->GetAgentNum() > 0)
	{
		LiveManager->RemoveAllAgents();
	}
	Agents.Reset();
	StepsThisRound = 0;
	if (AActor* HostActor = Host.Get())
	{
		HostActor->Destroy();
	}
	Host.Reset();
	Manager.Reset();
	Interactor.Reset();
	Policy.Reset();
}

bool FPmcPolicyBrain::IsSetup() const
{
	return Manager.IsValid() && Interactor.IsValid() && Policy.IsValid();
}

void FPmcPolicyBrain::BeginRound(const TArray<AOperativeCharacter*>& Fighters, float InAreaFactor)
{
	if (!IsSetup())
	{
		return;
	}
	if (!Agents.IsEmpty())
	{
		EndRound();
	}
	AreaFactor = InAreaFactor;
	StepsThisRound = 0;
	ULearningAgentsManager* LiveManager = Manager.Get();
	for (AOperativeCharacter* Fighter : Fighters)
	{
		if (!PmcPolicyBrainLocal::IsFighterAlive(Fighter) || Agents.Num() >= Settings.MaxAgents)
		{
			continue;
		}
		const int32 AgentId = LiveManager->AddAgent(Fighter);
		if (AgentId == INDEX_NONE)
		{
			continue;
		}
		FAgent& Agent = Agents.AddDefaulted_GetRef();
		Agent.AgentId = AgentId;
		Agent.Fighter = Fighter;
		Agent.Observation.Init(0.f, PmcPolicyRules::ObservationSize);
	}
}

void FPmcPolicyBrain::EndRound()
{
	ULearningAgentsManager* LiveManager = Manager.Get();
	UPmcPolicyInteractor* LiveInteractor = Interactor.Get();
	if (LiveManager && LiveInteractor && !Agents.IsEmpty())
	{
		// The last step's final observation: what each agent saw last (a fallen one with no health).
		for (FAgent& Agent : Agents)
		{
			Agent.bDead = !PmcPolicyBrainLocal::IsFighterAlive(Agent.Fighter.Get());
			PushIdleFrame(Agent);
		}
		if (StepsThisRound > 0 && OnStepEnded)
		{
			OnStepEnded(true);
		}
		LiveManager->RemoveAllAgents();
		LiveInteractor->ClearAllFrames();
	}
	Agents.Reset();
	StepsThisRound = 0;
}

void FPmcPolicyBrain::PushIdleFrame(FAgent& Agent) const
{
	UPmcPolicyInteractor* LiveInteractor = Interactor.Get();
	if (!LiveInteractor)
	{
		return;
	}
	if (Agent.bDead && Agent.Observation.IsValidIndex(PmcPolicyRules::ObsHealth))
	{
		Agent.Observation[PmcPolicyRules::ObsHealth] = 0.f;
	}
	TArray<int32> MaskedActions;
	PmcPolicyRules::MaskedActions(0u, false, MaskedActions);
	LiveInteractor->SetAgentFrame(Agent.AgentId, Agent.Observation, MaskedActions);
}

void FPmcPolicyBrain::PickOrders(int32 TeamId, const FPmcWeights& Weights, TArrayView<FPmcOrderRequest> Requests)
{
	ULearningAgentsManager* LiveManager = Manager.Get();
	UPmcPolicyInteractor* LiveInteractor = Interactor.Get();
	ULearningAgentsPolicy* LivePolicy = Policy.Get();
	if (!LiveManager || !LiveInteractor || !LivePolicy || Agents.IsEmpty())
	{
		return; // the requests keep the scorer's picks
	}

	// 1. Every agent's frame of this step: the fresh view of a fighter that decides now, the last one of a fighter that does not.
	TArray<int32, TInlineAllocator<8>> RequestOf;
	TArray<uint32, TInlineAllocator<8>> MaskOf;
	RequestOf.Init(INDEX_NONE, Agents.Num());
	MaskOf.Init(0u, Agents.Num());
	for (int32 RequestIndex = 0; RequestIndex < Requests.Num(); ++RequestIndex)
	{
		const AOperativeCharacter* Fighter = Requests[RequestIndex].Fighter.Get();
		const int32 AgentIndex = Agents.IndexOfByPredicate([Fighter](const FAgent& Agent) { return Fighter && Agent.Fighter.Get() == Fighter; });
		if (AgentIndex != INDEX_NONE)
		{
			RequestOf[AgentIndex] = RequestIndex;
		}
	}
	TArray<int32> MaskedActions;
	for (int32 AgentIndex = 0; AgentIndex < Agents.Num(); ++AgentIndex)
	{
		FAgent& Agent = Agents[AgentIndex];
		Agent.bDead = !PmcPolicyBrainLocal::IsFighterAlive(Agent.Fighter.Get());
		if (Agent.bDead || RequestOf[AgentIndex] == INDEX_NONE)
		{
			RequestOf[AgentIndex] = INDEX_NONE;
			PushIdleFrame(Agent);
			continue;
		}
		const FPmcOrderRequest& Request = Requests[RequestOf[AgentIndex]];
		PmcPolicyRules::BuildObservation(Request.View, Request.Squad, Agent.Observation, AreaFactor);
		MaskOf[AgentIndex] = PmcPolicyRules::AllowedOrderMask(Request.Scored, Request.Commitment, Weights);
		PmcPolicyRules::MaskedActions(MaskOf[AgentIndex], true, MaskedActions);
		LiveInteractor->SetAgentFrame(Agent.AgentId, Agent.Observation, MaskedActions);
	}

	// 2. The step that just ended (a trainer: completions, rewards, the experience - and maybe a policy update).
	if (StepsThisRound > 0 && OnStepEnded)
	{
		OnStepEnded(false);
	}

	// 3. The fallen leave the policy.
	for (int32 AgentIndex = Agents.Num() - 1; AgentIndex >= 0; --AgentIndex)
	{
		if (Agents[AgentIndex].bDead)
		{
			LiveManager->RemoveAgent(Agents[AgentIndex].AgentId);
			LiveInteractor->ClearAgentFrame(Agents[AgentIndex].AgentId);
			Agents.RemoveAt(AgentIndex);
			RequestOf.RemoveAt(AgentIndex);
			MaskOf.RemoveAt(AgentIndex);
		}
	}
	if (Agents.IsEmpty())
	{
		return;
	}

	// 4. One inference for the whole team: observations -> action masks -> encoder / policy / decoder -> the sampled orders.
	LivePolicy->RunInference(Settings.ActionNoiseScale);
	++StepsThisRound;
	++StepsTotal;

	// 5. The picks, through the scorer's gates.
	for (int32 AgentIndex = 0; AgentIndex < Agents.Num(); ++AgentIndex)
	{
		if (RequestOf[AgentIndex] == INDEX_NONE)
		{
			continue;
		}
		FPmcOrderRequest& Request = Requests[RequestOf[AgentIndex]];
		Request.Pick = PmcPolicyRules::GatePick(LiveInteractor->GetAgentOrder(Agents[AgentIndex].AgentId), MaskOf[AgentIndex], Request.Commitment);
		if (const int32 Action = PmcPolicyRules::ActionIndexOf(Request.Pick); PickCounts.IsValidIndex(Action))
		{
			++PickCounts[Action];
		}
	}
}

bool FPmcPolicyBrain::IsAgentDead(int32 AgentId) const
{
	const FAgent* Agent = Agents.FindByPredicate([AgentId](const FAgent& Candidate) { return Candidate.AgentId == AgentId; });
	return !Agent || Agent->bDead;
}

int32 FPmcPolicyBrain::FindAgentId(const AOperativeCharacter* Fighter) const
{
	const FAgent* Agent = Fighter ? Agents.FindByPredicate([Fighter](const FAgent& Candidate) { return Candidate.Fighter.Get() == Fighter; }) : nullptr;
	return Agent ? Agent->AgentId : INDEX_NONE;
}

FString FPmcPolicyBrain::NetworkFile(const FString& BasePath, const TCHAR* Part)
{
	return FString::Printf(TEXT("%s.%s.bin"), *BasePath, Part);
}

bool FPmcPolicyBrain::HasPolicyFiles(const FString& BasePath)
{
	return FPaths::FileExists(NetworkFile(BasePath, TEXT("encoder"))) && FPaths::FileExists(NetworkFile(BasePath, TEXT("policy")))
		&& FPaths::FileExists(NetworkFile(BasePath, TEXT("decoder")));
}

bool FPmcPolicyBrain::SaveNetwork(ULearningAgentsNeuralNetwork* Network, const FString& File)
{
	if (!Network || !Network->NeuralNetworkData)
	{
		return false;
	}
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
	TArray<uint8> Bytes;
	Bytes.SetNumUninitialized(Network->NeuralNetworkData->GetSnapshotByteNum());
	Network->NeuralNetworkData->SaveToSnapshot(Bytes);
	if (!FFileHelper::SaveArrayToFile(Bytes, *File))
	{
		UE_LOG(LogCodexTactics, Error, TEXT("[PmcPolicy] could not write %s"), *File);
		return false;
	}
	return true;
}

bool FPmcPolicyBrain::ReadSnapshot(const ULearningAgentsNeuralNetwork* Network, const FString& File, TArray<uint8>& OutBytes)
{
	OutBytes.Reset();
	if (!Network || !Network->NeuralNetworkData)
	{
		return false;
	}
	if (!FFileHelper::LoadFileToArray(OutBytes, *File))
	{
		UE_LOG(LogCodexTactics, Error, TEXT("[PmcPolicy] could not read %s"), *File);
		return false;
	}
	// ULearningNeuralNetworkData::LoadFromSnapshot takes the file's sizes as they are: a snapshot of another observation / action
	// layout or network shape is refused here, before it replaces the live network.
	const ULearningNeuralNetworkData* Data = Network->NeuralNetworkData;
	int32 Header[PmcPolicyBrainLocal::SnapshotHeaderInts] = {};
	if (OutBytes.Num() <= static_cast<int32>(sizeof(Header) + sizeof(int32)))
	{
		UE_LOG(LogCodexTactics, Error, TEXT("[PmcPolicy] %s is no network snapshot (%d bytes)"), *File, OutBytes.Num());
		return false;
	}
	FMemory::Memcpy(Header, OutBytes.GetData(), sizeof(Header));
	if (Header[2] != Data->GetInputSize() || Header[3] != Data->GetOutputSize() || Header[4] != Data->GetCompatibilityHash()
		|| OutBytes.Num() != Data->GetSnapshotByteNum())
	{
		UE_LOG(LogCodexTactics, Error, TEXT("[PmcPolicy] %s does not fit this build's policy (inputs %d / outputs %d / hash %d / %d bytes; the file has %d / %d / %d / %d)"),
			*File, Data->GetInputSize(), Data->GetOutputSize(), Data->GetCompatibilityHash(), Data->GetSnapshotByteNum(), Header[2], Header[3], Header[4], OutBytes.Num());
		return false;
	}
	return true;
}

bool FPmcPolicyBrain::LoadNetwork(ULearningAgentsNeuralNetwork* Network, const FString& File)
{
	TArray<uint8> Bytes;
	return ReadSnapshot(Network, File, Bytes) && Network->NeuralNetworkData->LoadFromSnapshot(Bytes);
}

bool FPmcPolicyBrain::SavePolicy(const FString& BasePath) const
{
	ULearningAgentsPolicy* LivePolicy = Policy.Get();
	if (!LivePolicy || BasePath.IsEmpty())
	{
		return false;
	}
	const bool bEncoder = SaveNetwork(LivePolicy->GetEncoderNetworkAsset(), NetworkFile(BasePath, TEXT("encoder")));
	const bool bPolicy = SaveNetwork(LivePolicy->GetPolicyNetworkAsset(), NetworkFile(BasePath, TEXT("policy")));
	const bool bDecoder = SaveNetwork(LivePolicy->GetDecoderNetworkAsset(), NetworkFile(BasePath, TEXT("decoder")));
	return bEncoder && bPolicy && bDecoder;
}

bool FPmcPolicyBrain::LoadPolicy(const FString& BasePath)
{
	ULearningAgentsPolicy* LivePolicy = Policy.Get();
	if (!LivePolicy || BasePath.IsEmpty())
	{
		return false;
	}
	if (!HasPolicyFiles(BasePath))
	{
		UE_LOG(LogCodexTactics, Error, TEXT("[PmcPolicy] no policy at %s (.encoder.bin / .policy.bin / .decoder.bin)"), *BasePath);
		return false;
	}
	// All three checked before any is loaded: a policy is never left half replaced.
	ULearningAgentsNeuralNetwork* Networks[] = { LivePolicy->GetEncoderNetworkAsset(), LivePolicy->GetPolicyNetworkAsset(), LivePolicy->GetDecoderNetworkAsset() };
	const TCHAR* Parts[] = { TEXT("encoder"), TEXT("policy"), TEXT("decoder") };
	TArray<uint8> Bytes[UE_ARRAY_COUNT(Networks)];
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Networks); ++Index)
	{
		if (!ReadSnapshot(Networks[Index], NetworkFile(BasePath, Parts[Index]), Bytes[Index]))
		{
			return false;
		}
	}
	bool bLoaded = true;
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Networks); ++Index)
	{
		bLoaded &= Networks[Index]->NeuralNetworkData->LoadFromSnapshot(Bytes[Index]);
	}
	return bLoaded;
}
