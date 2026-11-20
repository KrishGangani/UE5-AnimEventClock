// P06 AnimEventClock (demo) - the authored content of the demo montages, in one place.

#pragma once

#include "Containers/Array.h"
#include "Containers/UnrealString.h"

/**
 * The single source of truth for the demo montages: where they come from, what they are called, and where each marker sits.
 * The editor commandlet builds the assets from this (AnimEventClockEditor), the asset test checks the saved assets against
 * it, and the benchmark director loads the assets by these paths. The director reads marker positions from the montage
 * itself, never from here, exactly as the event clock will.
 *
 * Positions are fractions of the montage length, so the montages fit whatever length the engine's source sequence has.
 */
namespace AECDemo
{
	/** The engine's tutorial idle sequence: Starter Content, referenced by path and never copied (01b #13). */
	inline const TCHAR* const SourceSequencePath = TEXT("/Engine/Tutorial/SubEditors/TutorialAssets/Character/Tutorial_Idle.Tutorial_Idle");

	/** Where the generated montages live (cooked by DefaultGame.ini). */
	inline const TCHAR* const MontageFolder = TEXT("/Game/AEC/Montages");

	/** The notify track the markers are placed on (a display grouping in the montage editor). */
	inline const TCHAR* const NotifyTrackName = TEXT("AEC");

	/** One marker: an instant event (DurationFraction 0) or a window. */
	struct FMarkerSpec
	{
		const TCHAR* Name;
		float StartFraction;
		float DurationFraction;
	};

	/** One composite section; sections are linked in array order and played straight through. */
	struct FSectionSpec
	{
		const TCHAR* Name;
		float StartFraction;
	};

	/** One demo montage; it exists twice, as _Q (Queued markers) and _B (BranchingPoint markers). */
	struct FMontageSpec
	{
		const TCHAR* BaseName;
		TArray<FSectionSpec> Sections;
		TArray<FMarkerSpec> Markers;
	};

	/** AM_Attack and AM_Combo, each with 4 instant events and 1 window. */
	ANIMEVENTCLOCK_API const TArray<FMontageSpec>& GetMontageSpecs();

	/** "AM_Attack_Q" or "AM_Attack_B". */
	ANIMEVENTCLOCK_API FString GetMontageAssetName(const FMontageSpec& Spec, bool bBranchingPoint);

	/** "/Game/AEC/Montages/AM_Attack_Q" (the package). */
	ANIMEVENTCLOCK_API FString GetMontagePackageName(const FMontageSpec& Spec, bool bBranchingPoint);

	/** "/Game/AEC/Montages/AM_Attack_Q.AM_Attack_Q" (the object, for LoadObject). */
	ANIMEVENTCLOCK_API FString GetMontageObjectPath(const FMontageSpec& Spec, bool bBranchingPoint);
}
