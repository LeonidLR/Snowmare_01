#pragma once

// Dev-only: per-frame luminance of the level start as the player sees it (docs/systems/environment-weather.md section 19, the start
// flicker, user report 2026-10-27: "a strange flicker when the level starts", "as if there's a thunderstorm"). UE-only.
// FStartFlickerRecorder requests a screenshot (with the UI) every frame and receives the pixels through
// UGameViewportClient::OnScreenshotCaptured (nothing is written by the engine), computes the mean luma (Rec. 709 on the 8-bit sRGB values,
// 0..255) of the whole frame and of 4 bands (top / middle / bottom third, the HUD = the bottom 15 %), the mean abs per-cell difference
// against the previous frame on a 128x72 grid, and records the state that can change the picture (camera fade, the eye adaptation exposure
// read back through a world view extension, the presenter's exposure snap / applications / sun / moon / sky light, the lightning flash).
// Used by CodexTactics.StartFlickerShots (CSV + thumbnails) and CodexTactics.StartFlickerSmoke (asserts no jump).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

class UWorld;

namespace StartFlickerProbe
{
	/** One rendered frame. */
	struct FFrameStats
	{
		int32 Frame = 0;
		uint64 EngineFrame = 0;
		double GameTime = 0.;
		float Mean = 0.f;
		/** Top / middle / bottom third, HUD band (bottom 15 %). */
		float Band[4] = { 0.f, 0.f, 0.f, 0.f };
		/** Mean abs difference of the 128x72 cell lumas against the previous frame (whole frame, then per band); 0 on frame 1. */
		float Diff = 0.f;
		float BandDiff[4] = { 0.f, 0.f, 0.f, 0.f };
		/** The viewport's eye adaptation exposure (last read back) and average scene luminance; 0 when not known. */
		float Exposure = 0.f;
		float SceneLuminance = 0.f;
		float FadeAmount = 0.f;
		bool bExposureSnap = false;
		int32 ApplyCount = 0;
		float SunLux = 0.f;
		float MoonLux = 0.f;
		float SkyLight = 0.f;
		float FlashScale = 0.f;
		int32 Strikes = 0;
	};

	/** The band names in FFrameStats::Band order. */
	const TCHAR* BandName(int32 Index);

	/** The game (or PIE) world that has begun play, if any. */
	UWorld* FindPlayWorld();

	class FRecorder
	{
	public:
		/** ThumbDir empty = no thumbnails. Starts requesting a screenshot every frame until Frames frames were received. */
		void Begin(int32 InFrames, const FString& InThumbDir, const FString& InThumbPrefix, int32 InThumbWidth = 320);
		void End();
		bool IsRunning() const { return bRunning; }
		bool IsDone() const { return Stats.Num() >= Frames; }
		const TArray<FFrameStats>& GetStats() const { return Stats; }

		/** Called at the end of every engine frame by the owner: requests the next frame's screenshot. */
		void RequestNext();

		/** CSV of all frames. */
		bool WriteCsv(const FString& Path) const;

		/** The largest frame-to-frame |mean| step among frames [FirstFrame, LastFrame] whose camera fade is 0 (and the previous one too). */
		float MaxMeanStep(int32 FirstFrame, int32 LastFrame, int32& OutFrame) const;

		~FRecorder() { End(); }

	private:
		void HandleScreenshot(int32 Width, int32 Height, const TArray<FColor>& Pixels);

		TArray<FFrameStats> Stats;
		TArray<float> PreviousCells;
		FString ThumbDir;
		FString ThumbPrefix;
		int32 ThumbWidth = 320;
		int32 Frames = 0;
		bool bRunning = false;
		FDelegateHandle ScreenshotHandle;
		TSharedPtr<class FExposureViewExtension, ESPMode::ThreadSafe> Exposure;
	};
}

#endif // !UE_BUILD_SHIPPING
