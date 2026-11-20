// P06 AnimEventClock (editor) - commandlet that generates this project's demo montages from an engine animation.

#pragma once

#include "Commandlets/Commandlet.h"
#include "AECMakeDemoAssetsCommandlet.generated.h"

/**
 * Creates the demo montages under /Game/AEC/Montages/ from the engine's tutorial idle sequence (referenced by path, never
 * copied), with this project's marker notifies at the positions defined in AECDemoMontages.h:
 *
 *   AM_Attack_Q / AM_Attack_B   one section; 4 instant events + 1 window
 *   AM_Combo_Q  / AM_Combo_B    sections A -> B -> C (linked, played straight through); 4 instant events + 1 window
 *
 * _Q = every marker uses Queued notify ticking (the engine default), _B = BranchingPoint.
 *
 * The assets are built with the editor's own APIs (UAnimMontageFactory, as "Create Montage" uses, and
 * UAnimationBlueprintLibrary for notify tracks and notifies), then saved. Re-running overwrites them.
 *
 * Run (through the machine-wide queue): Tools\make_assets.bat
 *   = UnrealEditor-Cmd.exe AnimEventClock.uproject -run=AECMakeDemoAssets
 * Exit code: 0 if every asset was created and saved, 1 otherwise.
 */
UCLASS()
class UAECMakeDemoAssetsCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	//~ Begin UCommandlet
	virtual int32 Main(const FString& Params) override;
	//~ End UCommandlet
};
