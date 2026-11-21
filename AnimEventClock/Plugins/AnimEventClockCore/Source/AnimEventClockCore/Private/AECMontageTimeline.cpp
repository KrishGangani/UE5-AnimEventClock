// AnimEventClockCore - FAECMontageTimeline.

#include "AECMontageTimeline.h"

#include "AECEventNotify.h"
#include "AECEventWindowNotifyState.h"
#include "Algo/BinarySearch.h"
#include "Animation/AnimMontage.h"

FAECMontageTimeline FAECMontageTimeline::Build(const UAnimMontage& Montage)
{
	FAECMontageTimeline Timeline;

	// Sections in time order; each ends where the next one in time starts, the last one at the montage's end.
	// Sections exactly as the engine plays them: its own indices, start and end, and links (AnimMontage.h:812, 819).
	Timeline.Sections.Reserve(Montage.CompositeSections.Num());
	for (int32 Index = 0; Index < Montage.CompositeSections.Num(); ++Index)
	{
		float SectionStart = 0.f;
		float SectionEnd = 0.f;
		Montage.GetSectionStartAndEndTime(Index, SectionStart, SectionEnd);
		Timeline.AddSection(Montage.CompositeSections[Index].SectionName, SectionStart, SectionEnd,
			Montage.GetSectionIndex(Montage.CompositeSections[Index].NextSectionName));
	}

	// The markers, at the times the engine triggers them (they include its small offsets at segment edges).
	Timeline.Keys.Reserve(2 * Montage.Notifies.Num());
	for (const FAnimNotifyEvent& Event : Montage.Notifies)
	{
		if (const UAECEventNotify* Instant = Cast<UAECEventNotify>(Event.Notify))
		{
			Timeline.AddInstant(Instant->EventName, Event.GetTriggerTime());
		}
		else if (const UAECEventWindowNotifyState* Window = Cast<UAECEventWindowNotifyState>(Event.NotifyStateClass))
		{
			Timeline.AddWindow(Window->EventName, Event.GetTriggerTime(), Event.GetEndTriggerTime());
		}
	}
	Timeline.Finalize();
	return Timeline;
}

int32 FAECMontageTimeline::AddSection(FName Name, float StartPosition, float EndPosition, int32 Next)
{
	return Sections.Add({ Name, StartPosition, FMath::Max(StartPosition, EndPosition), Next });
}

void FAECMontageTimeline::AddInstant(FName Name, float Position)
{
	Keys.Add({ Name, EAECEventPhase::Instant, Position, INDEX_NONE });
}

void FAECMontageTimeline::AddWindow(FName Name, float BeginPosition, float EndPosition)
{
	// Paired by the notify that authored them, never by name or order: a window's two keys always find each other.
	const int32 Begin = Keys.Num();
	Keys.Add({ Name, EAECEventPhase::WindowBegin, BeginPosition, Begin + 1 });
	Keys.Add({ Name, EAECEventPhase::WindowEnd, FMath::Max(BeginPosition, EndPosition), Begin });
}

void FAECMontageTimeline::Finalize()
{
	// Sort by position (stable), then point each pair at its partner's new index.
	TArray<int32> Order;
	Order.SetNumUninitialized(Keys.Num());
	for (int32 Index = 0; Index < Keys.Num(); ++Index)
	{
		Order[Index] = Index;
	}
	Order.StableSort([this](int32 A, int32 B) { return Keys[A].Position < Keys[B].Position; });
	TArray<int32> NewIndexOf;
	NewIndexOf.SetNum(Keys.Num());
	TArray<FAECTimelineKey> Sorted;
	Sorted.Reserve(Keys.Num());
	for (int32 NewIndex = 0; NewIndex < Order.Num(); ++NewIndex)
	{
		NewIndexOf[Order[NewIndex]] = NewIndex;
		Sorted.Add(Keys[Order[NewIndex]]);
	}
	for (FAECTimelineKey& Key : Sorted)
	{
		if (Key.PairKey != INDEX_NONE)
		{
			Key.PairKey = NewIndexOf[Key.PairKey];
		}
	}
	Keys = MoveTemp(Sorted);
}

int32 FAECMontageTimeline::FindSection(FName Name) const
{
	if (Name.IsNone())
	{
		return INDEX_NONE;
	}
	return Sections.IndexOfByPredicate([Name](const FAECTimelineSection& Section) { return Section.Name == Name; });
}

FAECPlayhead FAECMontageTimeline::Start(int32 Section) const
{
	FAECPlayhead Head;
	Head.Section = (Section == INDEX_NONE) ? 0 : Section;
	Head.bFinished = !Sections.IsValidIndex(Head.Section);
	Head.Position = Head.bFinished ? 0.f : Sections[Head.Section].Start;
	return Head;
}

int32 FAECMontageTimeline::FirstKeyFrom(float Position, bool bInclusive) const
{
	return bInclusive
		? Algo::LowerBoundBy(Keys, Position, &FAECTimelineKey::Position)
		: Algo::UpperBoundBy(Keys, Position, &FAECTimelineKey::Position);
}

void FAECMontageTimeline::Cross(FAECPlayhead& Head, int32 Key, float Position, float Offset, TArray<FAECCrossing>& Out) const
{
	const FAECTimelineKey& Crossed = Keys[Key];
	if (Crossed.Phase == EAECEventPhase::WindowBegin)
	{
		Head.OpenWindows.AddUnique(Key);
	}
	else if (Crossed.Phase == EAECEventPhase::WindowEnd && Head.OpenWindows.RemoveSingle(Crossed.PairKey) == 0)
	{
		return;   // the end of a window that is not open (it was already closed)
	}
	Out.Add({ Key, Head.Pass, Offset, /*bForced=*/ false, Head.Section, Position });
}

void FAECMontageTimeline::CloseOpenWindows(FAECPlayhead& Head, float Offset, TArray<FAECCrossing>& Out) const
{
	// In key order (the order the ends would have come in), not in the order the windows opened: then every crossing of a
	// path is ordered by (pass, position, key), a key that a client can compare with the server's (S7).
	TArray<int32, TInlineAllocator<4>> Ends;
	for (const int32 Begin : Head.OpenWindows)
	{
		Ends.Add(Keys[Begin].PairKey);
	}
	Ends.Sort();
	for (const int32 End : Ends)
	{
		Out.Add({ End, Head.Pass, Offset, /*bForced=*/ true, Head.Section, Head.Position });
	}
	Head.OpenWindows.Reset();
}

void FAECMontageTimeline::EnterSection(FAECPlayhead& Head, int32 Section, float Offset, TArray<FAECCrossing>& Out) const
{
	const float Target = Sections[Section].Start;
	CloseOpenWindows(Head, Offset, Out);
	if (Target < Head.Position || (Target == Head.Position && !Head.bIncludeStart))
	{
		++Head.Pass;   // back to montage time already passed: its keys fire again, as a new pass
	}
	Head.Section = Section;
	Head.Position = Target;
	Head.bIncludeStart = true;
}

bool FAECMontageTimeline::Advance(FAECPlayhead& Head, float Delta, TArray<FAECCrossing>& Out) const
{
	float Remaining = Delta;
	int32 EmptySteps = 0;
	while (Remaining > 0.f && !Head.bFinished)
	{
		if (!Sections.IsValidIndex(Head.Section))
		{
			CloseOpenWindows(Head, Delta - Remaining, Out);   // no section to walk: the path ends, and with it every window
			Head.bFinished = true;
			return true;
		}

		// One step: to the end of this section (exactly onto it, so float rounding never misses a section change), or as
		// far as the delta reaches.
		const FAECTimelineSection& Section = Sections[Head.Section];
		const float From = Head.Position;
		const float ToEnd = FMath::Max(Section.End - From, 0.f);
		const bool bReachesEnd = ToEnd <= Remaining;
		const float To = bReachesEnd ? FMath::Max(Section.End, From) : From + Remaining;
		EmptySteps = (To > From) ? 0 : EmptySteps + 1;
		if (EmptySteps > MaxEmptyStepsPerAdvance)
		{
			// The guard: a cycle of zero-length sections would never let the delta run out. The path ends here, and every
			// window with it.
			CloseOpenWindows(Head, Delta - Remaining, Out);
			Head.bFinished = true;
			return false;
		}
		const float StepStart = Delta - Remaining;
		if (Head.bIncludeStart)
		{
			// Just entered this section: a window that spans the entry point begins here, as the notify state would.
			for (int32 Key = 0; Key < Keys.Num() && Keys[Key].Position < From; ++Key)
			{
				if (Keys[Key].Phase == EAECEventPhase::WindowBegin && Keys[Keys[Key].PairKey].Position > From)
				{
					Cross(Head, Key, From, StepStart, Out);
				}
			}
		}
		for (int32 Key = FirstKeyFrom(From, Head.bIncludeStart); Key < Keys.Num() && Keys[Key].Position <= To; ++Key)
		{
			Cross(Head, Key, Keys[Key].Position, StepStart + (Keys[Key].Position - From), Out);
		}
		Head.bIncludeStart = false;
		Head.Position = To;
		if (!bReachesEnd)
		{
			break;
		}
		Remaining -= ToEnd;

		if (!Sections.IsValidIndex(Section.Next))
		{
			// The montage's end: a window still open (its end lies in montage time the path never plays) ends here, as the
			// engine ends a notify state when its montage stops.
			CloseOpenWindows(Head, Delta - Remaining, Out);
			Head.bFinished = true;
			break;
		}
		if (Sections[Section.Next].Start == Head.Position)
		{
			Head.Section = Section.Next;   // a continuation: the next section starts right here
		}
		else
		{
			EnterSection(Head, Section.Next, Delta - Remaining, Out);   // a loop back or a skip (02 §6.3)
		}
	}
	return true;
}

void FAECMontageTimeline::JumpToSection(FAECPlayhead& Head, int32 Section, TArray<FAECCrossing>& Out) const
{
	if (!Sections.IsValidIndex(Section))
	{
		return;
	}
	EnterSection(Head, Section, 0.f, Out);
	Head.bFinished = false;
}

void FAECMontageTimeline::Stop(FAECPlayhead& Head, TArray<FAECCrossing>& Out) const
{
	CloseOpenWindows(Head, 0.f, Out);
	Head.bFinished = true;
}
