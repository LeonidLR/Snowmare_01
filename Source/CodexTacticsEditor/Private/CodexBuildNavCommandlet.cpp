#include "CodexBuildNavCommandlet.h"

#include "EditorWorldUtils.h"
#include "Engine/World.h"
#include "Misc/PackageName.h"
#include "NavBake.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

DEFINE_LOG_CATEGORY_STATIC(LogCodexBuildNav, Log, All);

UCodexBuildNavCommandlet::UCodexBuildNavCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UCodexBuildNavCommandlet::Main(const FString& Params)
{
	FString MapArg;
	if (!FParse::Value(*Params, TEXT("Map="), MapArg) || MapArg.IsEmpty())
	{
		UE_LOG(LogCodexBuildNav, Error, TEXT("[NavBake] usage: -run=CodexTacticsEditor.CodexBuildNav -Map=<name or /Game/... path> [-NoSave] [-MinTiles=N]"));
		return 1;
	}
	const bool bNoSave = FParse::Param(*Params, TEXT("NoSave"));
	int32 MinTiles = 1;
	FParse::Value(*Params, TEXT("MinTiles="), MinTiles);

	const FString LongName = MapArg.StartsWith(TEXT("/")) ? MapArg : FString::Printf(TEXT("/Game/Maps/%s"), *MapArg);
	FString Filename;
	if (!FPackageName::TryConvertLongPackageNameToFilename(LongName, Filename, FPackageName::GetMapPackageExtension())
		|| !FPackageName::DoesPackageExist(LongName))
	{
		UE_LOG(LogCodexBuildNav, Error, TEXT("[NavBake] map package %s not found"), *LongName);
		return 1;
	}

	UPackage* Package = LoadPackage(nullptr, *LongName, LOAD_None);
	UWorld* World = Package ? UWorld::FindWorldInPackage(Package) : nullptr;
	if (!World)
	{
		UE_LOG(LogCodexBuildNav, Error, TEXT("[NavBake] could not load a world from %s"), *LongName);
		return 1;
	}
	World->AddToRoot();

	int32 ExitCode = 0;
	{
		UWorld::InitializationValues IVS;
		IVS.RequiresHitProxies(false);
		IVS.ShouldSimulatePhysics(false);
		IVS.EnableTraceCollision(false);
		IVS.CreateNavigation(true);
		IVS.CreateAISystem(false);
		IVS.AllowAudioPlayback(false);
		IVS.CreatePhysicsScene(true);
		FScopedEditorWorld EditorWorld(World, IVS);

		const double Start = FPlatformTime::Seconds();
		const FCodexNavBakeResult Result = CodexNavBake::BuildWorldNavigation(*World);
		const int32 DefaultTiles = Result.GetActiveTiles(TEXT("Default"));
		UE_LOG(LogCodexBuildNav, Display, TEXT("[NavBake] %s: built=%d releasedAsyncLoadLock=%d removedMisaligned=%d tiles: %s (%.1f s)%s%s"),
			*LongName, Result.bBuilt ? 1 : 0, Result.bReleasedAsyncLoadLock ? 1 : 0, Result.RemovedMisalignedNavData,
			*Result.Describe(), FPlatformTime::Seconds() - Start, Result.Error.IsEmpty() ? TEXT("") : TEXT(" error: "), *Result.Error);

		if (!Result.bBuilt || DefaultTiles < MinTiles)
		{
			UE_LOG(LogCodexBuildNav, Error, TEXT("[NavBake] FAIL: Default agent has %d active tiles (need %d); map not saved"), DefaultTiles, MinTiles);
			ExitCode = 2;
		}
		else if (bNoSave)
		{
			UE_LOG(LogCodexBuildNav, Display, TEXT("[NavBake] PASS (-NoSave: map not saved)"));
		}
		else
		{
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Standalone;
			SaveArgs.SaveFlags = SAVE_NoError;
			if (UPackage::SavePackage(Package, World, *Filename, SaveArgs))
			{
				UE_LOG(LogCodexBuildNav, Display, TEXT("[NavBake] PASS: saved %s"), *Filename);
			}
			else
			{
				UE_LOG(LogCodexBuildNav, Error, TEXT("[NavBake] FAIL: could not save %s"), *Filename);
				ExitCode = 3;
			}
		}
	}
	World->RemoveFromRoot();
	return ExitCode;
}
