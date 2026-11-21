// P06 AnimEventClock (demo) - AAECDemoPlayerController.

#include "AECDemoPlayerController.h"

#include "AECBenchSubsystem.h"
#include "Engine/World.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AECDemoPlayerController)

AAECDemoPlayerController::AAECDemoPlayerController()
{
	// The director owns the view (its fixed camera); possession must not switch it to the pawn.
	bAutoManageActiveCameraTarget = false;
}

void AAECDemoPlayerController::ClientBenchFinished_Implementation()
{
	if (UAECBenchSubsystem* Bench = GetWorld()->GetSubsystem<UAECBenchSubsystem>())
	{
		Bench->FinishClientRun();
	}
}
