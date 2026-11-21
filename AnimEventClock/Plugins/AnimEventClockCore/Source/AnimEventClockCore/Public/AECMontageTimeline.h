// AnimEventClockCore - the pure timeline policy: which authored events a playhead crosses as an action advances.

#pragma once

#include "AECEventTypes.h"
#include "Containers/Array.h"
#include "Math/UnrealMathUtility.h"
#include "UObject/NameTypes.h"

#define UE_API ANIMEVENTCLOCKCORE_API

class UAnimMontage;

/** One authored event of a montage: an instant event, or a window's begin or end. */
struct FAECTimelineKey
{
	FName Name;
	EAECEventPhase Phase = EAECEventPhase::Instant;
	float Position = 0.f;          // montage time at which the engine would trigger the notify
	int32 PairKey = INDEX_NONE;    // a window's begin <-> end key; INDEX_NONE for an instant event
};

/** One composite section: [Start, End] in montage time, and the section that follows it (INDEX_NONE: the montage ends). */
struct FAECTimelineSection
{
	FName Name;
	float Start = 0.f;
	float End = 0.f;
	int32 Next = INDEX_NONE;
};

/** Where an action is on its timeline: the only state that changes while an action plays. It only goes forward along its
 *  path; a loop or a jump back returns to earlier montage time as a new pass. */
struct FAECPlayhead
{
	int32 Section = INDEX_NONE;
	float Position = 0.f;
	int32 Pass = 0;               // +1 each time the path returns to montage time it already passed (a loop or a jump back)
	bool bIncludeStart = true;    // the next advance also crosses a key exactly at Position (after a start, a loop or a jump)
	bool bFinished = false;
	TArray<int32, TInlineAllocator<4>> OpenWindows;   // begin keys of the windows that began and have not ended yet
};

/** A key the playhead crossed: which key, on which pass, how far into the advance (in montage time) it lay, and where the
 *  playhead was at that moment (an advance ends before anyone reacts to its crossings: this is where a reaction, such as
 *  the event clock's jump from a handler, starts from). */
struct FAECCrossing
{
	int32 Key = INDEX_NONE;
	int32 Pass = 0;
	float Offset = 0.f;
	bool bForced = false;         // a window end the path did not reach: closed by a loop, a jump, a stop or the path's end
	int32 Section = INDEX_NONE;   // the playhead's section and position when it crossed
	float Position = 0.f;
};

/**
 * A montage's authored events and section path, as plain data (no UObject pointers, so nothing to keep alive), and the
 * rule that walks them (02 §6.3). Built when an action starts, from the montage's own notifies: the same data the engine
 * fires, read with the same trigger times (GetTriggerTime / GetEndTriggerTime), so the clock and the notify agree on
 * where an event is.
 *
 * Crossing rule: an advance from position A to B crosses every key in (A, B], the engine's own rule for notifies; and
 * [A, B] when the playhead has just entered a section (a start, a loop back, a jump or a skip), so a key exactly at a
 * section's start fires on every pass. Moving on into a section that starts exactly where the playhead is, is a
 * continuation, not an entry.
 *
 * Windows: every begin is matched by exactly one end. Entering a section inside a window begins it (the engine begins a
 * notify state the playback enters partway, AnimSequenceBase.cpp:478). A window the path leaves without reaching its end
 * (a loop back, a jump or a skip from inside it) is closed right there with a forced end crossing; a stop and the end of
 * the path (the montage's end, or the empty-step guard) close the open ones the same way. Forced ends come in key order,
 * so every crossing of a path is ordered by (pass, position, key).
 */
class FAECMontageTimeline
{
public:
	/** At most this many steps in a row that make no progress: guards a cycle of zero-length sections. */
	static constexpr int32 MaxEmptyStepsPerAdvance = 256;

	/** Play rates below this count as this (02 §7: no zero or reverse playback). */
	static constexpr float MinPlayRate = 0.01f;

	/** The timeline of a montage: this plugin's markers and the montage's composite sections. */
	UE_API static FAECMontageTimeline Build(const UAnimMontage& Montage);

	/** Hand-made timelines (tests, code without a montage). Add sections in time order, keys in any order, then Finalize. */
	UE_API int32 AddSection(FName Name, float StartPosition, float EndPosition, int32 Next);
	UE_API void AddInstant(FName Name, float Position);
	UE_API void AddWindow(FName Name, float BeginPosition, float EndPosition);
	UE_API void Finalize();

	/** A playhead at a section's start (INDEX_NONE: the first section). Finished at once if there is no such section. */
	UE_API FAECPlayhead Start(int32 Section = INDEX_NONE) const;

	/**
	 * Moves the playhead forward by Delta (montage time: gameplay seconds x play rate; nothing happens unless Delta > 0)
	 * along the section path, and appends every key it crosses, in order, with its offset into Delta. Returns false if
	 * the empty-step guard stopped it (the playhead is then finished).
	 */
	UE_API bool Advance(FAECPlayhead& Head, float Delta, TArray<FAECCrossing>& Out) const;

	/** Puts the playhead at a section's start, closing the windows it leaves (forced ends, offset 0). A jump back to montage
	 *  time already passed starts a new pass. */
	UE_API void JumpToSection(FAECPlayhead& Head, int32 Section, TArray<FAECCrossing>& Out) const;

	/** A stop: appends a forced end for every open window (offset 0) and finishes the playhead. */
	UE_API void Stop(FAECPlayhead& Head, TArray<FAECCrossing>& Out) const;

	/** The section with this name, or INDEX_NONE. */
	UE_API int32 FindSection(FName Name) const;

	const TArray<FAECTimelineKey>& GetKeys() const { return Keys; }
	const TArray<FAECTimelineSection>& GetSections() const { return Sections; }

	/** The moment a crossing happened, given that the advance that found it ended at Now (02 §6.3). */
	static double DueTime(double Now, float Delta, float Offset, float PlayRate)
	{
		return Now - double(Delta - Offset) / double(FMath::Max(PlayRate, MinPlayRate));
	}

private:
	/** The first key at or after (bInclusive) / after (!bInclusive) a position. */
	int32 FirstKeyFrom(float Position, bool bInclusive) const;

	/** Appends a crossing made at Position (in the playhead's section) and keeps the playhead's open windows up to date. */
	void Cross(FAECPlayhead& Head, int32 Key, float Position, float Offset, TArray<FAECCrossing>& Out) const;

	/** Forced ends for every open window, at this offset. */
	void CloseOpenWindows(FAECPlayhead& Head, float Offset, TArray<FAECCrossing>& Out) const;

	/** Every way into a section other than a continuation (loop back, skip, jump): the open windows end at Offset, a return
	 *  to montage time already passed starts a new pass, and the section's start position is included. */
	void EnterSection(FAECPlayhead& Head, int32 Section, float Offset, TArray<FAECCrossing>& Out) const;

	TArray<FAECTimelineKey> Keys;            // sorted by position (stable: authored order for equal positions)
	TArray<FAECTimelineSection> Sections;    // in time order
};

#undef UE_API
