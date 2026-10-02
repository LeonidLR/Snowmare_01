#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class UWorld;

/**
 * Spatial telemetry of a bot run for the Wave Editor's tactical replay player (Godot archive
 * tools/bot/spatial_telemetry_recorder.gd, data/schemas/spatial_telemetry.schema.json): the level layout, a frame of the
 * squad and the enemies every 0.1 s of game time, the events (grenades, cover, enemy deaths, choke points) and the
 * summary (cover use, choke points, height use). Written to Saved/Telemetry/spatial_runs/run_<session>.json in Godot
 * coordinates — metres, x / z the floor, y the height (Unreal X -> x, Y -> z, feet Z -> y).
 */
class CODEXTACTICS_API FSpatialTelemetryRecorder
{
public:
	/** Unreal cm -> the Godot [x, y, z] metres of the schema. */
	static FVector ToGodot(const FVector& UnrealCm) { return FVector(UnrealCm.X, UnrealCm.Z, UnrealCm.Y) / 100.0; }

	void Start(UWorld* World, const FString& InLevelId, const FString& InProfile);
	void Tick(float DeltaTime);
	/** Godot record_event: Type is SHOT / GRENADE_THROWN / ENEMY_DEATH / COVER_ENTER / COVER_LEAVE / CHOKE_CONGESTION ... */
	void RecordEvent(const FString& Type, const FString& ActorId, const FString& TargetId, const FVector& UnrealPosition,
		const TSharedPtr<FJsonObject>& Details = nullptr);
	/** Writes the file; returns its path (empty when not recording / failed). */
	FString Finish(const FString& Result, int32 WavesCleared, int32 TotalWaves);

	bool IsRecording() const { return bRecording; }
	int32 GetFrameCount() const { return Frames.Num(); }
	int32 GetEventCount() const { return Events.Num(); }
	static FString GetDirectory();

private:
	void CaptureLayout();
	void RecordFrame();
	FString FindCoverId(const FVector& UnrealPosition) const;

	struct FCoverUse
	{
		FVector Location = FVector::ZeroVector;
		double TimeSec = 0.0;
		int32 HitsAbsorbed = 0;
		float MaxHp = 0.f;
	};
	struct FStall
	{
		FVector Anchor = FVector::ZeroVector;
		float Seconds = 0.f;
	};
	struct FCongestion
	{
		int32 Hits = 0;
		int32 PeakEnemies = 0;
		FVector2D WorldPos = FVector2D::ZeroVector;
		float DurationSec = 0.f;
		bool bSingleEnemyStuck = false;
	};

	TWeakObjectPtr<UWorld> WorldPtr;
	FString SessionId;
	FString LevelId;
	FString Profile;
	bool bRecording = false;
	double SimTime = 0.0;
	double TimeAccum = 0.0;
	static constexpr double TickInterval = 0.1;
	TSharedPtr<FJsonObject> Layout;
	TArray<TSharedPtr<class FJsonValue>> Frames;
	TArray<TSharedPtr<class FJsonValue>> Events;
	TMap<FString, FCoverUse> CoverUse;
	TMap<FString, FStall> Stalls;
	TMap<FString, FCongestion> Congestion;
	double HeightUsageTime = 0.0;
	int32 KillsFromHeight = 0;
};
