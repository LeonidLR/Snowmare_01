#include "Camera/CodexPlayerCameraManager.h"

#include "Camera/TacticalCameraPawn.h"
#include "Camera/TacticalCameraRules.h"
#include "GameFramework/PlayerController.h"

void ACodexPlayerCameraManager::DoUpdateCamera(float DeltaTime)
{
	Super::DoUpdateCamera(DeltaTime);
	if (!ATacticalCameraPawn::IsOperativeClearanceEnabled())
	{
		return;
	}
	// The capsule margin of the tactical camera when there is one, else its default (60 cm).
	const ATacticalCameraPawn* Tactical = PCOwner ? Cast<ATacticalCameraPawn>(PCOwner->GetPawn()) : nullptr;
	const float Clearance = Tactical ? Tactical->Config.CapsuleClearance : FTacticalCameraConfig().CapsuleClearance;
	FMinimalViewInfo View = GetCameraCacheView();
	const FVector Guarded = ATacticalCameraPawn::PushOutOfCharacters(GetWorld(), View.Location, Clearance);
	if (!Guarded.Equals(View.Location, 0.1))
	{
		View.Location = Guarded;
		FillCameraCache(View);
	}
}
