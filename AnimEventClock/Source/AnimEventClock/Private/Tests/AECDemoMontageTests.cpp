// P06 AnimEventClock (demo) - checks the generated demo montages against their spec (AECDemoMontages.h).
// Run: Tools\run_tests.bat (editor, headless), filter "AnimEventClock.Assets". Needs Tools\make_assets.bat first.

#include "AECDemoMontages.h"
#include "AECEventNotify.h"
#include "AECEventWindowNotifyState.h"
#include "Animation/AnimMontage.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace AECDemoMontageTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	/** Positions are stored as floats and the editor adds a 1e-4 s trigger offset at segment edges: allow 1 ms. */
	constexpr float ToleranceSeconds = 0.001f;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAECDemoMontagesMatchSpecTest, "AnimEventClock.Assets.DemoMontagesMatchSpec", AECDemoMontageTests::Flags)
bool FAECDemoMontagesMatchSpecTest::RunTest(const FString& Parameters)
{
	using namespace AECDemoMontageTests;

	for (const AECDemo::FMontageSpec& Spec : AECDemo::GetMontageSpecs())
	{
		for (const bool bBranchingPoint : { false, true })
		{
			const FString Path = AECDemo::GetMontageObjectPath(Spec, bBranchingPoint);
			const UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, *Path);
			if (!TestNotNull(*FString::Printf(TEXT("%s loads"), *Path), Montage))
			{
				continue;
			}
			const float Length = Montage->GetPlayLength();
			TestTrue(*FString::Printf(TEXT("%s has a length"), *Path), Length > 0.f);

			// Sections: names, start positions and the A -> B -> C chain.
			if (TestEqual(*FString::Printf(TEXT("%s section count"), *Path), Montage->CompositeSections.Num(), Spec.Sections.Num()))
			{
				for (int32 Index = 0; Index < Spec.Sections.Num(); ++Index)
				{
					const FCompositeSection& Section = Montage->CompositeSections[Index];
					TestEqual(*FString::Printf(TEXT("%s section %d name"), *Path, Index), Section.SectionName, FName(Spec.Sections[Index].Name));
					TestNearlyEqual(*FString::Printf(TEXT("%s section %d start"), *Path, Index), Section.GetTime(),
						Spec.Sections[Index].StartFraction * Length, ToleranceSeconds);
					const FName ExpectedNext = Spec.Sections.IsValidIndex(Index + 1) ? FName(Spec.Sections[Index + 1].Name) : NAME_None;
					TestEqual(*FString::Printf(TEXT("%s section %d next"), *Path, Index), Section.NextSectionName, ExpectedNext);
				}
			}

			// Markers: one notify per spec entry, of the right class, at the right time, with the variant's tick type.
			TestEqual(*FString::Printf(TEXT("%s marker count"), *Path), Montage->Notifies.Num(), Spec.Markers.Num());
			for (const AECDemo::FMarkerSpec& Marker : Spec.Markers)
			{
				const FAnimNotifyEvent* Found = nullptr;
				for (const FAnimNotifyEvent& Event : Montage->Notifies)
				{
					const UAECEventNotify* Instant = Cast<UAECEventNotify>(Event.Notify);
					const UAECEventWindowNotifyState* Window = Cast<UAECEventWindowNotifyState>(Event.NotifyStateClass);
					const FName Name = Instant ? Instant->EventName : (Window ? Window->EventName : NAME_None);
					if (Name == FName(Marker.Name))
					{
						Found = &Event;
						break;
					}
				}
				const FString What = FString::Printf(TEXT("%s marker %s"), *Path, Marker.Name);
				if (!TestNotNull(*FString::Printf(TEXT("%s exists"), *What), Found))
				{
					continue;
				}
				const bool bIsWindow = Marker.DurationFraction > 0.f;
				TestEqual(*FString::Printf(TEXT("%s is a window"), *What), Found->NotifyStateClass != nullptr, bIsWindow);
				TestNearlyEqual(*FString::Printf(TEXT("%s start"), *What), Found->GetTriggerTime(), Marker.StartFraction * Length, ToleranceSeconds);
				if (bIsWindow)
				{
					TestNearlyEqual(*FString::Printf(TEXT("%s end"), *What), Found->GetEndTriggerTime(),
						(Marker.StartFraction + Marker.DurationFraction) * Length, ToleranceSeconds);
				}
				const EMontageNotifyTickType::Type ExpectedTickType = bBranchingPoint ? EMontageNotifyTickType::BranchingPoint : EMontageNotifyTickType::Queued;
				TestEqual(*FString::Printf(TEXT("%s tick type"), *What), static_cast<int32>(Found->MontageTickType.GetValue()), static_cast<int32>(ExpectedTickType));
				TestEqual(*FString::Printf(TEXT("%s is a branching point"), *What), Found->IsBranchingPoint(), bBranchingPoint);
			}
		}
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
