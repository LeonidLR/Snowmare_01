#include "Debug/StartFlickerProbe.h"

#if !UE_BUILD_SHIPPING

#include "Camera/PlayerCameraManager.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Environment/WeatherPresenterActor.h"
#include "Environment/WeatherRainFxComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "SceneView.h"
#include "SceneViewExtension.h"
#include "UnrealClient.h"

namespace StartFlickerProbe
{
	constexpr int32 GridX = 128;
	constexpr int32 GridY = 72;

	const TCHAR* BandName(int32 Index)
	{
		static const TCHAR* Names[4] = { TEXT("top"), TEXT("middle"), TEXT("bottom"), TEXT("hud") };
		return Names[FMath::Clamp(Index, 0, 3)];
	}

	UWorld* FindPlayWorld()
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if ((Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE) && Context.World() && Context.World()->HasBegunPlay())
			{
				return Context.World();
			}
		}
		return nullptr;
	}

	/** Reads the main view's eye adaptation back on the game thread (BeginRenderViewFamily), scene captures skipped. */
	class FExposureViewExtension : public FSceneViewExtensionBase
	{
	public:
		FExposureViewExtension(const FAutoRegister& AutoRegister) : FSceneViewExtensionBase(AutoRegister) {}

		virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {}
		virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override {}
		virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override
		{
			if (InViewFamily.Views.Num() > 0 && InViewFamily.Views[0] && !InViewFamily.Views[0]->bIsSceneCapture && !InViewFamily.Views[0]->bIsReflectionCapture)
			{
				Exposure = InViewFamily.Views[0]->GetLastEyeAdaptationExposure();
				SceneLuminance = InViewFamily.Views[0]->GetLastAverageSceneLuminance();
			}
		}

		float Exposure = 0.f;
		float SceneLuminance = 0.f;
	};

	void FRecorder::Begin(int32 InFrames, const FString& InThumbDir, const FString& InThumbPrefix, int32 InThumbWidth)
	{
		End();
		Stats.Reset();
		PreviousCells.Reset();
		Frames = FMath::Max(1, InFrames);
		ThumbDir = InThumbDir;
		ThumbPrefix = InThumbPrefix;
		ThumbWidth = FMath::Clamp(InThumbWidth, 64, 1920);
		if (!ThumbDir.IsEmpty())
		{
			IFileManager::Get().MakeDirectory(*ThumbDir, true);
		}
		Exposure = FSceneViewExtensions::NewExtension<FExposureViewExtension>();
		ScreenshotHandle = UGameViewportClient::OnScreenshotCaptured().AddRaw(this, &FRecorder::HandleScreenshot);
		bRunning = true;
		RequestNext();
	}

	void FRecorder::End()
	{
		if (ScreenshotHandle.IsValid())
		{
			UGameViewportClient::OnScreenshotCaptured().Remove(ScreenshotHandle);
			ScreenshotHandle.Reset();
		}
		Exposure.Reset();
		bRunning = false;
	}

	void FRecorder::RequestNext()
	{
		if (bRunning && !IsDone())
		{
			FScreenshotRequest::RequestScreenshot(/*bInShowUI*/ true);
		}
	}

	void FRecorder::HandleScreenshot(int32 Width, int32 Height, const TArray<FColor>& Pixels)
	{
		if (!bRunning || IsDone() || Width <= 0 || Height <= 0 || Pixels.Num() < Width * Height)
		{
			return;
		}
		FFrameStats& Frame = Stats.AddDefaulted_GetRef();
		Frame.Frame = Stats.Num();
		Frame.EngineFrame = GFrameCounter;
		UWorld* World = FindPlayWorld();
		Frame.GameTime = World ? World->GetTimeSeconds() : -1.;

		// Cell lumas (block means) and band means.
		TArray<float> Cells;
		Cells.SetNumZeroed(GridX * GridY);
		TArray<int32> Counts;
		Counts.SetNumZeroed(GridX * GridY);
		double BandSum[4] = { 0., 0., 0., 0. };
		int64 BandCount[4] = { 0, 0, 0, 0 };
		double Sum = 0.;
		const int32 HudTop = FMath::FloorToInt32(Height * 0.85f);
		for (int32 Y = 0; Y < Height; ++Y)
		{
			const int32 CellY = FMath::Min(Y * GridY / Height, GridY - 1);
			const int32 Third = FMath::Min(Y * 3 / Height, 2);
			double RowSum = 0.;
			const FColor* Row = Pixels.GetData() + static_cast<int64>(Y) * Width;
			for (int32 X = 0; X < Width; ++X)
			{
				const FColor& C = Row[X];
				const float Luma = 0.2126f * C.R + 0.7152f * C.G + 0.0722f * C.B;
				RowSum += Luma;
				const int32 Cell = CellY * GridX + FMath::Min(X * GridX / Width, GridX - 1);
				Cells[Cell] += Luma;
				++Counts[Cell];
			}
			Sum += RowSum;
			BandSum[Third] += RowSum;
			BandCount[Third] += Width;
			if (Y >= HudTop)
			{
				BandSum[3] += RowSum;
				BandCount[3] += Width;
			}
		}
		Frame.Mean = static_cast<float>(Sum / (static_cast<double>(Width) * Height));
		for (int32 Band = 0; Band < 4; ++Band)
		{
			Frame.Band[Band] = BandCount[Band] > 0 ? static_cast<float>(BandSum[Band] / BandCount[Band]) : 0.f;
		}
		for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
		{
			Cells[Cell] /= FMath::Max(1, Counts[Cell]);
		}
		if (PreviousCells.Num() == Cells.Num())
		{
			double DiffSum = 0.;
			double BandDiffSum[4] = { 0., 0., 0., 0. };
			int32 BandCells[4] = { 0, 0, 0, 0 };
			const int32 HudCellTop = FMath::FloorToInt32(GridY * 0.85f);
			for (int32 CellY = 0; CellY < GridY; ++CellY)
			{
				const int32 Third = FMath::Min(CellY * 3 / GridY, 2);
				for (int32 CellX = 0; CellX < GridX; ++CellX)
				{
					const int32 Cell = CellY * GridX + CellX;
					const float D = FMath::Abs(Cells[Cell] - PreviousCells[Cell]);
					DiffSum += D;
					BandDiffSum[Third] += D;
					++BandCells[Third];
					if (CellY >= HudCellTop)
					{
						BandDiffSum[3] += D;
						++BandCells[3];
					}
				}
			}
			Frame.Diff = static_cast<float>(DiffSum / Cells.Num());
			for (int32 Band = 0; Band < 4; ++Band)
			{
				Frame.BandDiff[Band] = BandCells[Band] > 0 ? static_cast<float>(BandDiffSum[Band] / BandCells[Band]) : 0.f;
			}
		}
		PreviousCells = MoveTemp(Cells);

		// The state that can change the picture.
		if (Exposure.IsValid())
		{
			Frame.Exposure = Exposure->Exposure;
			Frame.SceneLuminance = Exposure->SceneLuminance;
		}
		const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		const APlayerCameraManager* CameraManager = PC ? PC->PlayerCameraManager.Get() : nullptr;
		Frame.FadeAmount = CameraManager && CameraManager->bEnableFading ? CameraManager->FadeAmount : 0.f;
		if (const AWeatherPresenterActor* Presenter = World ? AWeatherPresenterActor::Find(World) : nullptr)
		{
			Frame.bExposureSnap = Presenter->IsExposureSnapping();
			Frame.ApplyCount = Presenter->GetApplyCount();
			const UDirectionalLightComponent* Sun = Presenter->GetSunLight();
			const UDirectionalLightComponent* Moon = Presenter->GetActiveMoon();
			const USkyLightComponent* Sky = Presenter->GetSkyLightComponent();
			Frame.SunLux = Sun && Sun->IsVisible() ? Sun->Intensity : 0.f;
			Frame.MoonLux = Moon && Moon->IsVisible() ? Moon->Intensity : 0.f;
			Frame.SkyLight = Sky && Sky->IsVisible() ? Sky->Intensity : 0.f;
			if (const UWeatherRainFxComponent* Rain = Presenter->GetRainFx())
			{
				Frame.FlashScale = Rain->GetFlashScale();
				Frame.Strikes = Rain->GetStrikeCount();
			}
		}

		if (!ThumbDir.IsEmpty())
		{
			const int32 ThumbHeight = FMath::Max(1, ThumbWidth * Height / Width);
			TArray<FColor> Thumb;
			Thumb.SetNumUninitialized(ThumbWidth * ThumbHeight);
			FImageUtils::ImageResize(Width, Height, TArrayView<const FColor>(Pixels.GetData(), Width * Height), ThumbWidth, ThumbHeight,
				TArrayView<FColor>(Thumb), /*bResizeSRGBinLinearSpace*/ false, /*bForceOpaqueOutput*/ true);
			TArray64<uint8> Png;
			FImageUtils::PNGCompressImageArray(ThumbWidth, ThumbHeight, TArrayView64<const FColor>(Thumb.GetData(), Thumb.Num()), Png);
			FFileHelper::SaveArrayToFile(Png, *(ThumbDir / FString::Printf(TEXT("%s_f%03d.png"), *ThumbPrefix, Frame.Frame)));
		}
	}

	bool FRecorder::WriteCsv(const FString& Path) const
	{
		FString Csv = TEXT("frame,engine_frame,time,mean,top,middle,bottom,hud,diff,diff_top,diff_middle,diff_bottom,diff_hud,exposure,scene_lum,fade,snap,applies,sun,moon,sky,flash,strikes\n");
		for (const FFrameStats& F : Stats)
		{
			Csv += FString::Printf(TEXT("%d,%llu,%.4f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.5f,%.5f,%.3f,%d,%d,%.3f,%.3f,%.3f,%.3f,%d\n"),
				F.Frame, F.EngineFrame, F.GameTime, F.Mean, F.Band[0], F.Band[1], F.Band[2], F.Band[3], F.Diff, F.BandDiff[0], F.BandDiff[1],
				F.BandDiff[2], F.BandDiff[3], F.Exposure, F.SceneLuminance, F.FadeAmount, F.bExposureSnap ? 1 : 0, F.ApplyCount, F.SunLux, F.MoonLux,
				F.SkyLight, F.FlashScale, F.Strikes);
		}
		return FFileHelper::SaveStringToFile(Csv, *Path);
	}

	float FRecorder::MaxMeanStep(int32 FirstFrame, int32 LastFrame, int32& OutFrame) const
	{
		float Max = 0.f;
		OutFrame = 0;
		for (int32 Index = FMath::Max(1, FirstFrame - 1); Index < Stats.Num() && Index < LastFrame; ++Index)
		{
			const FFrameStats& Previous = Stats[Index - 1];
			const FFrameStats& Current = Stats[Index];
			if (Previous.FadeAmount > 0.001f || Current.FadeAmount > 0.001f)
			{
				continue; // a camera fade (start cover / the factory's title fade) is intended, not a flicker
			}
			const float Step = FMath::Abs(Current.Mean - Previous.Mean);
			if (Step > Max)
			{
				Max = Step;
				OutFrame = Current.Frame;
			}
		}
		return Max;
	}
}

#endif // !UE_BUILD_SHIPPING
