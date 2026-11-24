#pragma once

#include "GameFlow/GameFlowTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "Telemetry/SessionRecordingRules.h"
#include "TimerManager.h"
#include "SessionRecordingSubsystem.generated.h"

class AActor;
class AOperativeCharacter;
struct FCoverSlot;

UCLASS()
class CODEXTACTICS_API USessionRecordingSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& World) override;
	virtual void Deinitialize() override;
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

	static USessionRecordingSubsystem* GetIfRecording(const UObject* WorldContext);
	void SuppressSession(FName Why);
	void RecordMode(EDemoMode Mode, bool bOn);
	void MarkSession(const FString& Mark) { Session.Mark = Mark.Left(128); }

#if WITH_DEV_AUTOMATION_TESTS
	void StartForTest(const FString& Directory);
	void RecordOrderAt(double Time, const FDemoFacts& Facts);
	void SnapshotAt(double Time);
	void FinishAt(double Time);
#endif

private:
	friend class FDemoOrderScope;
	friend class FDemoSuppressScope;

	struct FLastOrder
	{
		FName Id;
		FName Order;
		double Time = 0.;
	};

	void Start(const FString& Directory);
	void Flush(double Duration);
	void TakeSnapshot();
	void Capture(FDemoFrame& Frame, const AOperativeCharacter* Actor);
	void AppendSnapshot(FDemoFrame Frame);
	int32 AppendOrder(FDemoFrame Frame, const AOperativeCharacter* Actor);
	void SelectActor(FDemoFrame& Frame, FName Actor);
	int32 BeginOrder(EDemoOrderKind Kind, const AOperativeCharacter* Actor, const TOptional<FVector>& Destination,
		const FCoverSlot* Cover, const AActor* Target, bool bSprint, bool bPlan);
	void EndOrder(int32 Index);
	void Stamp(FDemoFrame& Frame) const;
	UFUNCTION()
	void HandleFlow(ECodexGamePhase Phase, ECodexCombatMode Mode);

	FDemoSession Session;
	TArray<FLastOrder> LastOrders;
	FString OutputDirectory;
	FName Suppressed;
	FTimerHandle SnapshotTimer;
	double GameStart = 0.;
	double RealStart = 0.;
	int32 OrderDepth = 0;
	int32 SuppressDepth = 0;
	bool bRecording = false;
	bool bTestSession = false;
};

class CODEXTACTICS_API FDemoOrderScope
{
public:
	FDemoOrderScope(const UObject* Context, EDemoOrderKind Kind, const AOperativeCharacter* Actor = nullptr,
		const TOptional<FVector>& Destination = {}, bool bSprint = false, bool bPlan = false,
		const FCoverSlot* Cover = nullptr, const AActor* Target = nullptr);
	~FDemoOrderScope();

	FDemoOrderScope(const FDemoOrderScope&) = delete;
	FDemoOrderScope& operator=(const FDemoOrderScope&) = delete;

	bool SetResult(bool bAccepted);
	void SetCrouch(bool bCrouch);
	void SetCease();
	void SetPlan(bool bPlan);
	void SetActor(const AOperativeCharacter* Actor);
	void SetTarget(const AActor* Target, const FVector* KnownPosition = nullptr);
	void SetSprint(bool bSprint);

private:
	USessionRecordingSubsystem* Recorder = nullptr;
	int32 Frame = INDEX_NONE;
};

class CODEXTACTICS_API FDemoSuppressScope
{
public:
	explicit FDemoSuppressScope(const UObject* Context);
	~FDemoSuppressScope();

	FDemoSuppressScope(const FDemoSuppressScope&) = delete;
	FDemoSuppressScope& operator=(const FDemoSuppressScope&) = delete;

private:
	USessionRecordingSubsystem* Recorder = nullptr;
};
