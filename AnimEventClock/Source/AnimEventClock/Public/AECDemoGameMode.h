// P06 AnimEventClock (demo) - the game mode of network runs: gives each remote player a demo character.

#pragma once

#include "GameFramework/GameModeBase.h"
#include "AECDemoGameMode.generated.h"

/**
 * Used only by network runs, through the map URL (?game=/Script/AnimEventClock.AECDemoGameMode); single-process runs
 * keep the engine's default game mode, so the M1 baselines are untouched.
 *
 * A remote player gets a demo character from the benchmark director and possesses it: that is the "remote player's
 * character" role on the server and the autonomous proxy on the client (01 §6.2). A local player (the listen-server
 * host) gets no pawn: it only watches through the director's fixed camera, like a standalone run.
 */
UCLASS()
class AAECDemoGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AAECDemoGameMode();

	//~ Begin AGameModeBase
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;
	//~ End AGameModeBase
};
