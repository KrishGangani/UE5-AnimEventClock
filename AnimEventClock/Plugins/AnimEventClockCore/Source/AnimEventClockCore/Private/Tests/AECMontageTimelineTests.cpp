// AnimEventClockCore - unit tests of the pure timeline (no world, no montage asset). Run: Tools\run_tests.bat, filter
// "AnimEventClock.Timeline".

#include "AECMontageTimeline.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace AECTimelineTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	constexpr float Tolerance = 1e-5f;

	/** One section [0, 3] with instant keys at 1 and 2. */
	FAECMontageTimeline OneSection()
	{
		FAECMontageTimeline Timeline;
		Timeline.AddSection(TEXT("Default"), 0.f, 3.f, INDEX_NONE);
		Timeline.AddInstant(TEXT("Two"), 2.f);
		Timeline.AddInstant(TEXT("One"), 1.f);
		Timeline.Finalize();
		return Timeline;
	}

	/** A [0, 1] -> B [1, 2] -> C [2, 3], a key in each, and one exactly on the A/B boundary. */
	FAECMontageTimeline ThreeSections(int32 NextOfB = 2)
	{
		FAECMontageTimeline Timeline;
		Timeline.AddSection(TEXT("A"), 0.f, 1.f, 1);
		Timeline.AddSection(TEXT("B"), 1.f, 2.f, NextOfB);
		Timeline.AddSection(TEXT("C"), 2.f, 3.f, INDEX_NONE);
		Timeline.AddInstant(TEXT("A1"), 0.5f);
		Timeline.AddInstant(TEXT("AB"), 1.f);
		Timeline.AddInstant(TEXT("B1"), 1.5f);
		Timeline.AddInstant(TEXT("C1"), 2.5f);
		Timeline.Finalize();
		return Timeline;
	}

	/** The crossings as one "Name/phase/pass[!]" string (! = a forced end), for whole-sequence checks. */
	FString Describe(const FAECMontageTimeline& Timeline, const TArray<FAECCrossing>& Crossings)
	{
		TArray<FString> Parts;
		for (const FAECCrossing& Crossing : Crossings)
		{
			const FAECTimelineKey& Key = Timeline.GetKeys()[Crossing.Key];
			const TCHAR* Phase = (Key.Phase == EAECEventPhase::WindowBegin) ? TEXT("b") : (Key.Phase == EAECEventPhase::WindowEnd) ? TEXT("e") : TEXT("i");
			Parts.Add(FString::Printf(TEXT("%s/%s/%d%s"), *Key.Name.ToString(), Phase, Crossing.Pass, Crossing.bForced ? TEXT("!") : TEXT("")));
		}
		return FString::Join(Parts, TEXT(" "));
	}

	/** True if every crossing comes after the one before it in (pass, position, key) order: the order S7 compares cuts in. */
	bool IsInPathOrder(const TArray<FAECCrossing>& Crossings)
	{
		for (int32 Index = 1; Index < Crossings.Num(); ++Index)
		{
			const FAECCrossing& A = Crossings[Index - 1];
			const FAECCrossing& B = Crossings[Index];
			const bool bAfter = (B.Pass != A.Pass) ? (B.Pass > A.Pass) : (B.Position != A.Position) ? (B.Position > A.Position) : (B.Key > A.Key);
			if (!bAfter)
			{
				return false;
			}
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAECTimelineHalfOpenTest, "AnimEventClock.Timeline.CrossesKeysInHalfOpenRange", AECTimelineTests::Flags)
bool FAECTimelineHalfOpenTest::RunTest(const FString& Parameters)
{
	using namespace AECTimelineTests;
	const FAECMontageTimeline Timeline = OneSection();
	TestEqual(TEXT("keys are sorted by position"), Timeline.GetKeys()[0].Name, FName(TEXT("One")));

	FAECPlayhead Head = Timeline.Start();
	TArray<FAECCrossing> Out;
	Timeline.Advance(Head, 0.5f, Out);
	TestEqual(TEXT("nothing before 1"), Out.Num(), 0);
	Timeline.Advance(Head, 0.5f, Out);   // to exactly 1.0: (0.5, 1.0] holds the key
	if (TestEqual(TEXT("the key at the end of the range is crossed"), Out.Num(), 1))
	{
		TestNearlyEqual(TEXT("its offset is the full step"), Out[0].Offset, 0.5f, Tolerance);
	}
	Out.Reset();
	Timeline.Advance(Head, 0.f, Out);
	TestEqual(TEXT("a zero advance crosses nothing, not even the key at the position"), Out.Num(), 0);
	Timeline.Advance(Head, -1.f, Out);
	TestEqual(TEXT("a negative advance does nothing"), Out.Num(), 0);
	Timeline.Advance(Head, 1.25f, Out);  // (1.0, 2.25]: the key at 1 is not crossed again
	if (TestEqual(TEXT("only the key at 2"), Out.Num(), 1))
	{
		TestEqual(TEXT("it is Two"), Timeline.GetKeys()[Out[0].Key].Name, FName(TEXT("Two")));
		TestNearlyEqual(TEXT("offset 1.0 into the advance"), Out[0].Offset, 1.f, Tolerance);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAECTimelineStartEndTest, "AnimEventClock.Timeline.KeysAtStartAndEnd", AECTimelineTests::Flags)
bool FAECTimelineStartEndTest::RunTest(const FString& Parameters)
{
	FAECMontageTimeline Timeline;
	Timeline.AddSection(TEXT("Default"), 0.f, 3.f, INDEX_NONE);
	Timeline.AddInstant(TEXT("Start"), 0.f);
	Timeline.AddInstant(TEXT("End"), 3.f);
	Timeline.Finalize();

	FAECPlayhead Head = Timeline.Start();
	TArray<FAECCrossing> Out;
	Timeline.Advance(Head, 0.1f, Out);
	if (TestEqual(TEXT("a key at the start fires on the first advance"), Out.Num(), 1))
	{
		TestNearlyEqual(TEXT("at offset 0"), Out[0].Offset, 0.f, AECTimelineTests::Tolerance);
	}
	Out.Reset();
	Timeline.Advance(Head, 10.f, Out);
	TestEqual(TEXT("a key at the very end fires once"), Out.Num(), 1);
	TestTrue(TEXT("the playhead is finished at the end"), Head.bFinished);
	TestEqual(TEXT("and stands exactly on the end"), Head.Position, 3.f);
	Out.Reset();
	Timeline.Advance(Head, 1.f, Out);
	TestEqual(TEXT("a finished playhead crosses nothing"), Out.Num(), 0);

	// An advance that ends exactly on a section end, from a position whose float difference to it is inexact.
	FAECMontageTimeline Snap;
	Snap.AddSection(TEXT("X"), 0.f, 0.7f, INDEX_NONE);
	Snap.AddInstant(TEXT("AtEnd"), 0.7f);
	Snap.Finalize();
	FAECPlayhead SnapHead = Snap.Start();
	TArray<FAECCrossing> SnapOut;
	Snap.Advance(SnapHead, 0.1f, SnapOut);
	Snap.Advance(SnapHead, 0.7f - 0.1f, SnapOut);
	TestEqual(TEXT("reaching a section end crosses its key in that advance"), SnapOut.Num(), 1);
	TestTrue(TEXT("and finishes the path in that advance"), SnapHead.bFinished);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAECTimelineSectionPathTest, "AnimEventClock.Timeline.FollowsTheSectionPath", AECTimelineTests::Flags)
bool FAECTimelineSectionPathTest::RunTest(const FString& Parameters)
{
	using namespace AECTimelineTests;
	{
		const FAECMontageTimeline Timeline = ThreeSections();
		FAECPlayhead Head = Timeline.Start();
		TArray<FAECCrossing> Out;
		Timeline.Advance(Head, 5.f, Out);
		TestEqual(TEXT("A -> B -> C crosses every key once, in order, the boundary key once"), Describe(Timeline, Out),
			FString(TEXT("A1/i/0 AB/i/0 B1/i/0 C1/i/0")));
		for (int32 Index = 1; Index < Out.Num(); ++Index)
		{
			TestTrue(TEXT("offsets grow along the path"), Out[Index].Offset > Out[Index - 1].Offset);
		}
		TestTrue(TEXT("each crossing records the playhead's section (the boundary key in A, the section it ends) and position"),
			Out.Num() == 4 && Out[0].Section == 0 && Out[1].Section == 0 && Out[2].Section == 1 && Out[3].Section == 2
			&& FMath::IsNearlyEqual(Out[2].Position, 1.5f, Tolerance));
		TestTrue(TEXT("finished after C"), Head.bFinished);
	}
	{
		// A -> C (B skipped): B's key is never crossed, and C is not a continuation of A, so its start key fires.
		FAECMontageTimeline Timeline;
		Timeline.AddSection(TEXT("A"), 0.f, 1.f, 2);
		Timeline.AddSection(TEXT("B"), 1.f, 2.f, 2);
		Timeline.AddSection(TEXT("C"), 2.f, 3.f, INDEX_NONE);
		Timeline.AddInstant(TEXT("B1"), 1.5f);
		Timeline.AddInstant(TEXT("CStart"), 2.f);
		Timeline.Finalize();
		FAECPlayhead Head = Timeline.Start();
		TArray<FAECCrossing> Out;
		Timeline.Advance(Head, 1.5f, Out);
		TestEqual(TEXT("a skipped section's keys are not crossed; the next section's start key is"), Describe(Timeline, Out),
			FString(TEXT("CStart/i/0")));
		if (Out.Num() == 1)
		{
			TestNearlyEqual(TEXT("offset: the time spent in A"), Out[0].Offset, 1.f, Tolerance);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAECTimelineLoopTest, "AnimEventClock.Timeline.LoopStartsANewPass", AECTimelineTests::Flags)
bool FAECTimelineLoopTest::RunTest(const FString& Parameters)
{
	using namespace AECTimelineTests;
	{
		const FAECMontageTimeline Timeline = ThreeSections(/*NextOfB=*/ 1);   // B loops on itself
		FAECPlayhead Head = Timeline.Start();
		TArray<FAECCrossing> Out;
		Timeline.Advance(Head, 3.f, Out);   // A (1.0), then B twice: 1 -> 2, loop, 1 -> 2
		TestEqual(TEXT("through two loops of B; the key at B's start fires again on pass 1"), Describe(Timeline, Out),
			FString(TEXT("A1/i/0 AB/i/0 B1/i/0 AB/i/1 B1/i/1")));
		TestEqual(TEXT("two loops done: pass 2"), Head.Pass, 2);
		TestFalse(TEXT("a looping path never finishes"), Head.bFinished);
	}
	{
		// A window that the loop point cuts: it must end every pass, not stay open forever.
		FAECMontageTimeline Timeline;
		Timeline.AddSection(TEXT("Loop"), 0.f, 2.f, 0);
		Timeline.AddWindow(TEXT("Cut"), 1.5f, 2.5f);   // ends past the loop point: the path never reaches its end
		Timeline.Finalize();
		FAECPlayhead Head = Timeline.Start();
		TArray<FAECCrossing> Out;
		Timeline.Advance(Head, 4.f, Out);
		TestEqual(TEXT("begin and a forced end on every pass"), Describe(Timeline, Out), FString(TEXT("Cut/b/0 Cut/e/0! Cut/b/1 Cut/e/1!")));
		if (Out.Num() == 4)
		{
			TestNearlyEqual(TEXT("the forced end is at the loop point"), Out[1].Offset, 2.f, Tolerance);
			TestNearlyEqual(TEXT("where the playhead was: the loop point, before the loop"), Out[1].Position, 2.f, Tolerance);
			TestEqual(TEXT("on the pass the loop ends"), Out[1].Pass, 0);
		}
	}
	{
		// Two windows the loop cuts, the later-opened one ending first: their forced ends come in key order.
		FAECMontageTimeline Timeline;
		Timeline.AddSection(TEXT("Loop"), 0.f, 2.f, 0);
		Timeline.AddWindow(TEXT("Long"), 0.5f, 2.6f);
		Timeline.AddWindow(TEXT("Short"), 1.f, 2.4f);
		Timeline.Finalize();
		FAECPlayhead Head = Timeline.Start();
		TArray<FAECCrossing> Out;
		Timeline.Advance(Head, 2.f, Out);
		TestEqual(TEXT("forced ends in key order, not in opening order"), Describe(Timeline, Out),
			FString(TEXT("Long/b/0 Short/b/0 Short/e/0! Long/e/0!")));
		Timeline.Advance(Head, 2.f, Out);   // and a second pass
		TestTrue(TEXT("every crossing of the path in (pass, position, key) order"), IsInPathOrder(Out));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAECTimelineJumpTest, "AnimEventClock.Timeline.JumpToSection", AECTimelineTests::Flags)
bool FAECTimelineJumpTest::RunTest(const FString& Parameters)
{
	using namespace AECTimelineTests;
	FAECMontageTimeline Timeline;
	Timeline.AddSection(TEXT("A"), 0.f, 1.f, 1);
	Timeline.AddSection(TEXT("B"), 1.f, 2.f, 2);
	Timeline.AddSection(TEXT("C"), 2.f, 3.f, INDEX_NONE);
	Timeline.AddInstant(TEXT("A1"), 0.5f);
	Timeline.AddWindow(TEXT("W"), 1.2f, 1.8f);
	Timeline.Finalize();

	FAECPlayhead Head = Timeline.Start();
	TArray<FAECCrossing> Out;
	Timeline.Advance(Head, 1.5f, Out);   // into B, inside the window
	TArray<FAECCrossing> Path = Out;
	Out.Reset();
	Timeline.JumpToSection(Head, Timeline.FindSection(TEXT("C")), Out);
	TestEqual(TEXT("a jump out of a window closes it"), Describe(Timeline, Out), FString(TEXT("W/e/0!")));
	TestTrue(TEXT("recorded where the playhead was: in B, at 1.5"),
		Out.Num() == 1 && Out[0].Section == 1 && FMath::IsNearlyEqual(Out[0].Position, 1.5f, Tolerance));
	Path.Append(Out);
	TestEqual(TEXT("a jump forward keeps the pass"), Head.Pass, 0);
	Out.Reset();
	Timeline.JumpToSection(Head, Timeline.FindSection(TEXT("A")), Out);
	TestEqual(TEXT("a jump back starts a new pass"), Head.Pass, 1);
	Timeline.Advance(Head, 2.f, Out);
	TestEqual(TEXT("after the jump back, A's key and the window fire again, on the new pass"), Describe(Timeline, Out),
		FString(TEXT("A1/i/1 W/b/1 W/e/1")));
	Path.Append(Out);
	TestTrue(TEXT("advance, jumps and advance: every crossing in (pass, position, key) order"), IsInPathOrder(Path));
	TestEqual(TEXT("FindSection of an unknown name"), Timeline.FindSection(TEXT("Nope")), INDEX_NONE);

	FAECPlayhead Fresh = Timeline.Start();
	Timeline.JumpToSection(Fresh, 0, Out);
	TestEqual(TEXT("a jump onto the start of a fresh playhead is not a new pass"), Fresh.Pass, 0);

	// Entering a section inside a window begins it, as the engine begins a notify state the playback enters partway.
	FAECMontageTimeline Spanning;
	Spanning.AddSection(TEXT("A"), 0.f, 1.f, 1);
	Spanning.AddSection(TEXT("B"), 1.f, 2.f, INDEX_NONE);
	Spanning.AddWindow(TEXT("Across"), 0.8f, 1.2f);
	Spanning.Finalize();
	FAECPlayhead InB = Spanning.Start(Spanning.FindSection(TEXT("B")));
	TArray<FAECCrossing> SpanOut;
	Spanning.Advance(InB, 0.5f, SpanOut);
	TestEqual(TEXT("a start inside a window begins it at the entry, then ends it"), Describe(Spanning, SpanOut), FString(TEXT("Across/b/0 Across/e/0")));
	if (SpanOut.Num() == 2)
	{
		TestNearlyEqual(TEXT("the begin is at the entry point"), SpanOut[0].Offset, 0.f, Tolerance);
		TestTrue(TEXT("recorded at the entry point in B, not at its key's own position (0.8)"),
			SpanOut[0].Section == 1 && FMath::IsNearlyEqual(SpanOut[0].Position, 1.f, Tolerance));
	}
	TestTrue(TEXT("a window begun on entry keeps the path order"), IsInPathOrder(SpanOut));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAECTimelineGuardTest, "AnimEventClock.Timeline.ZeroLengthLoopGuard", AECTimelineTests::Flags)
bool FAECTimelineGuardTest::RunTest(const FString& Parameters)
{
	using namespace AECTimelineTests;
	{
		FAECMontageTimeline Timeline;
		Timeline.AddSection(TEXT("Stuck"), 1.f, 1.f, 0);   // zero length, loops on itself
		Timeline.AddInstant(TEXT("K"), 1.f);
		Timeline.Finalize();
		FAECPlayhead Head = Timeline.Start();
		TArray<FAECCrossing> Out;
		TestFalse(TEXT("the guard reports the stuck path"), Timeline.Advance(Head, 1.f, Out));
		TestTrue(TEXT("and finishes the playhead"), Head.bFinished);
		TestTrue(TEXT("crossings are bounded"), Out.Num() <= FAECMontageTimeline::MaxEmptyStepsPerAdvance);
	}
	{
		// A: [0, 1] -> B: [1, 1], zero length, on itself. The window [0.5, 2] is open when the guard trips: it ends there.
		FAECMontageTimeline Timeline;
		Timeline.AddSection(TEXT("A"), 0.f, 1.f, 1);
		Timeline.AddSection(TEXT("B"), 1.f, 1.f, 1);
		Timeline.AddWindow(TEXT("Open"), 0.5f, 2.f);
		Timeline.Finalize();
		FAECPlayhead Head = Timeline.Start();
		TArray<FAECCrossing> Out;
		TestFalse(TEXT("the guard trips"), Timeline.Advance(Head, 2.f, Out));
		TestEqual(TEXT("and closes the open window"), Describe(Timeline, Out), FString(TEXT("Open/b/0 Open/e/0!")));
		TestTrue(TEXT("at the point the path stopped (1 s in)"), Out.Num() == 2 && FMath::IsNearlyEqual(Out[1].Offset, 1.f, Tolerance));
		TestEqual(TEXT("no window stays open"), Head.OpenWindows.Num(), 0);
	}
	{
		// A normal loop must survive a long advance (a late join catching up): the guard counts only empty steps.
		FAECMontageTimeline Timeline;
		Timeline.AddSection(TEXT("Loop"), 0.f, 0.5f, 0);
		Timeline.AddInstant(TEXT("K"), 0.25f);
		Timeline.Finalize();
		FAECPlayhead Head = Timeline.Start();
		TArray<FAECCrossing> Out;
		TestTrue(TEXT("200 loops in one advance complete"), Timeline.Advance(Head, 100.f, Out));
		TestEqual(TEXT("one crossing per loop"), Out.Num(), 200);
		TestFalse(TEXT("and the loop goes on"), Head.bFinished);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAECTimelineEmptyTest, "AnimEventClock.Timeline.EmptyTimeline", AECTimelineTests::Flags)
bool FAECTimelineEmptyTest::RunTest(const FString& Parameters)
{
	{
		FAECMontageTimeline Timeline;
		Timeline.AddSection(TEXT("Default"), 0.f, 2.f, INDEX_NONE);
		Timeline.Finalize();
		FAECPlayhead Head = Timeline.Start();
		TArray<FAECCrossing> Out;
		Timeline.Advance(Head, 5.f, Out);
		TestEqual(TEXT("no keys: no crossings"), Out.Num(), 0);
		TestTrue(TEXT("but the path still ends"), Head.bFinished);
	}
	{
		const FAECMontageTimeline Timeline;
		const FAECPlayhead Head = Timeline.Start();
		TestTrue(TEXT("no sections: finished at once"), Head.bFinished);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAECTimelineDueTimeTest, "AnimEventClock.Timeline.DueTimeScalesWithPlayRate", AECTimelineTests::Flags)
bool FAECTimelineDueTimeTest::RunTest(const FString& Parameters)
{
	// An advance that ended at Now = 10 s covered 0.5 s of montage time. A key 0.25 s into it was 0.25 s of montage time
	// before the end: 0.125 s of game time before Now at play rate 2, 0.25 s at play rate 1. (Binary-exact inputs.)
	TestNearlyEqual(TEXT("due time at play rate 2"), FAECMontageTimeline::DueTime(10.0, 0.5f, 0.25f, 2.f), 9.875, 1e-9);
	TestNearlyEqual(TEXT("due time at play rate 1"), FAECMontageTimeline::DueTime(10.0, 0.5f, 0.25f, 1.f), 9.75, 1e-9);
	TestNearlyEqual(TEXT("a key at the end of the advance is due now"), FAECMontageTimeline::DueTime(10.0, 0.5f, 0.5f, 1.3f), 10.0, 1e-9);
	TestTrue(TEXT("a play rate of 0 is clamped, never infinite"), FMath::IsFinite(FAECMontageTimeline::DueTime(10.0, 0.5f, 0.25f, 0.f)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAECTimelineWindowsTest, "AnimEventClock.Timeline.StopClosesOpenWindows", AECTimelineTests::Flags)
bool FAECTimelineWindowsTest::RunTest(const FString& Parameters)
{
	using namespace AECTimelineTests;
	FAECMontageTimeline Timeline;
	Timeline.AddSection(TEXT("Default"), 0.f, 4.f, INDEX_NONE);
	Timeline.AddWindow(TEXT("Hit"), 1.f, 2.f);
	Timeline.AddWindow(TEXT("Hit"), 2.5f, 3.5f);   // the same window name, used twice
	Timeline.Finalize();
	TestEqual(TEXT("each window's keys are paired with each other"), Timeline.GetKeys()[0].PairKey, 1);
	TestEqual(TEXT("the second window too"), Timeline.GetKeys()[2].PairKey, 3);

	FAECPlayhead Head = Timeline.Start();
	TArray<FAECCrossing> Out;
	Timeline.Advance(Head, 1.5f, Out);   // the first window begins
	Timeline.Stop(Head, Out);
	TestEqual(TEXT("a stop inside the window closes it with its own end key, once"), Describe(Timeline, Out), FString(TEXT("Hit/b/0 Hit/e/0!")));
	TestTrue(TEXT("a stopped playhead is finished"), Head.bFinished);
	TestEqual(TEXT("and has no open window"), Head.OpenWindows.Num(), 0);

	// A: [0, 1] ends the path; the window [0.5, 1.5] ends in B, which is never played: it ends at the path's end.
	FAECMontageTimeline Cut;
	Cut.AddSection(TEXT("A"), 0.f, 1.f, INDEX_NONE);
	Cut.AddSection(TEXT("B"), 1.f, 2.f, INDEX_NONE);
	Cut.AddWindow(TEXT("Across"), 0.5f, 1.5f);
	Cut.Finalize();
	FAECPlayhead CutHead = Cut.Start();
	TArray<FAECCrossing> CutOut;
	Cut.Advance(CutHead, 2.f, CutOut);
	TestEqual(TEXT("a window open at the montage's end ends there, forced"), Describe(Cut, CutOut), FString(TEXT("Across/b/0 Across/e/0!")));
	TestTrue(TEXT("at the end of the path, not at the end of the delta"), CutOut.Num() == 2 && FMath::IsNearlyEqual(CutOut[1].Offset, 1.f, Tolerance));
	TestTrue(TEXT("recorded at A's end (1.0), not at the window's own end (1.5)"),
		CutOut.Num() == 2 && CutOut[1].Section == 0 && FMath::IsNearlyEqual(CutOut[1].Position, 1.f, Tolerance));

	// A stop with two windows open, the later-opened one ending first: their ends in key order.
	FAECMontageTimeline Two;
	Two.AddSection(TEXT("Default"), 0.f, 4.f, INDEX_NONE);
	Two.AddWindow(TEXT("Long"), 0.5f, 3.f);
	Two.AddWindow(TEXT("Short"), 1.f, 2.f);
	Two.Finalize();
	FAECPlayhead TwoHead = Two.Start();
	TArray<FAECCrossing> TwoOut;
	Two.Advance(TwoHead, 1.5f, TwoOut);
	Two.Stop(TwoHead, TwoOut);
	TestEqual(TEXT("a stop's forced ends in key order"), Describe(Two, TwoOut), FString(TEXT("Long/b/0 Short/b/0 Short/e/0! Long/e/0!")));
	TestTrue(TEXT("and in (pass, position, key) order"), IsInPathOrder(TwoOut));
	TestTrue(TEXT("and the playhead is finished"), CutHead.bFinished);

	// A window whose end the engine's trigger offsets place before its begin is still paired, and ends where it begins.
	FAECMontageTimeline Odd;
	Odd.AddSection(TEXT("Default"), 0.f, 1.f, INDEX_NONE);
	Odd.AddWindow(TEXT("Tiny"), 0.5f, 0.4999f);
	Odd.Finalize();
	TestEqual(TEXT("an inverted window is paired"), Odd.GetKeys()[0].PairKey, 1);
	FAECPlayhead OddHead = Odd.Start();
	TArray<FAECCrossing> OddOut;
	Odd.Advance(OddHead, 1.f, OddOut);
	TestEqual(TEXT("and crosses as begin then end"), Describe(Odd, OddOut), FString(TEXT("Tiny/b/0 Tiny/e/0")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
