// P06 AnimEventClock (demo) - the player controller of network runs.

#pragma once

#include "GameFramework/PlayerController.h"
#include "AECDemoPlayerController.generated.h"

/**
 * The connection-owning actor of a network run: the server tells its client through it that the run is over, so the
 * client writes its files and quits before the server does. S7 adds the clock-sync component here (02 §4.1).
 */
UCLASS()
class AAECDemoPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AAECDemoPlayerController();

	/** Server -> owning client: the server's run is over; write the client's files and quit. */
	UFUNCTION(Client, Reliable)
	void ClientBenchFinished();
};
