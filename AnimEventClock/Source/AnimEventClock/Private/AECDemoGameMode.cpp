// P06 AnimEventClock (demo) - AAECDemoGameMode.

#include "AECDemoGameMode.h"

#include "AECBenchSubsystem.h"
#include "AECDemoPlayerController.h"
#include "Engine/World.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AECDemoGameMode)

AAECDemoGameMode::AAECDemoGameMode()
{
	PlayerControllerClass = AAECDemoPlayerController::StaticClass();
	DefaultPawnClass = nullptr;   // pawns come from the director, never from the base class's RestartPlayer
}

void AAECDemoGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	// Replaces the base version (which would spawn a default pawn) on purpose: only remote players get a character.
	if (NewPlayer && !NewPlayer->IsLocalController())
	{
		if (UAECBenchSubsystem* Bench = GetWorld()->GetSubsystem<UAECBenchSubsystem>())
		{
			Bench->AddRemotePlayer(*NewPlayer);
		}
	}
}
