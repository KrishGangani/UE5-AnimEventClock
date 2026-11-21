// P06 AnimEventClock (demo) - the event clock component in a real (minimal) world: due times, and a handler that destroys the
// actor in the middle of dispatch (02 §8). Run: Tools\run_tests.bat, filter
// "AnimEventClock.Clock". Needs Tools\make_assets.bat first (it uses the demo montages).

#include "AECClockTestListener.h"
#include "AECDemoMontages.h"
#include "AECEventClockComponent.h"
#include "Animation/AnimMontage.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AECClockTestListener)

void UAECClockTestListener::HandleEvent(const FAECEventInfo& Event)
{
	Received.Add(Event);
	if (bReacted || Reaction == EReaction::None || Event.EventName != ReactTo || Event.Phase != ReactToPhase)
	{
		return;
	}
	bReacted = true;
	AActor* Owner = Event.Owner;
	UAECEventClockComponent* Clock = Owner ? Owner->FindComponentByClass<UAECEventClockComponent>() : nullptr;
	switch (Reaction)
	{
	case EReaction::DestroyOwner: if (Owner) { Owner->Destroy(); } break;
	case EReaction::StopAction:   if (Clock) { Clock->StopAction(); } break;
	case EReaction::PlayAnother:  if (Clock) { Clock->PlayAction(Another, 1.f); } break;
	case EReaction::JumpTo:       if (Clock) { Clock->JumpToSection(JumpSection); } break;
	default: break;
	}
}

#if WITH_DEV_AUTOMATION_TESTS

namespace AECClockTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	/** A game world that has begun play (the engine's test world: created, played, ended and destroyed by
	 *  FTestWorldWrapper), with one actor that owns a clock (no mesh: the clock alone). */
	struct FClockWorld
	{
		FTestWorldWrapper Wrapper;   // first, so it is destroyed last
		UWorld* World = nullptr;
		AActor* Actor = nullptr;
		UAECEventClockComponent* Clock = nullptr;
		UAECClockTestListener* Listener = nullptr;

		FClockWorld()
		{
			if (!Wrapper.CreateTestWorld(EWorldType::Game) || !Wrapper.BeginPlayInTestWorld())
			{
				return;   // IsReady() is false; the wrapper keeps the reason
			}
			World = Wrapper.GetTestWorld();
			Actor = World->SpawnActor<AActor>();
			Clock = NewObject<UAECEventClockComponent>(Actor);
			Clock->bDriveMontage = false;
			Clock->RegisterComponent();
			Listener = NewObject<UAECClockTestListener>();
			Clock->OnClockEvent.AddDynamic(Listener, &UAECClockTestListener::HandleEvent);
		}

		/** False if the world could not be made; the test reports the wrapper's messages. */
		bool IsReady(FAutomationTestBase& Test) const
		{
			Wrapper.ForwardErrorMessages(&Test);
			return Clock != nullptr;
		}

		/** The clock reads the world's game time: move it, then let the clock tick once. */
		void TickAt(double Time)
		{
			World->TimeSeconds = Time;
			if (IsValid(Clock) && Clock->IsActionActive())
			{
				Clock->TickComponent(0.f, LEVELTICK_All, nullptr);
			}
		}

		/** The events received, as "Name/phase[!]" (! = interrupted); only one action's with Action set. */
		FString Sequence(int32 Action = INDEX_NONE) const
		{
			TArray<FString> Parts;
			for (const FAECEventInfo& Event : Listener->Received)
			{
				if (Action != INDEX_NONE && Event.ActionId != Action)
				{
					continue;
				}
				const TCHAR* Phase = (Event.Phase == EAECEventPhase::WindowBegin) ? TEXT("b") : (Event.Phase == EAECEventPhase::WindowEnd) ? TEXT("e") : TEXT("i");
				Parts.Add(FString::Printf(TEXT("%s/%s%s"), *Event.EventName.ToString(), Phase, Event.bInterrupted ? TEXT("!") : TEXT("")));
			}
			return FString::Join(Parts, TEXT(" "));
		}
	};

	UAnimMontage* LoadDemo(int32 SpecIndex)
	{
		return LoadObject<UAnimMontage>(nullptr, *AECDemo::GetMontageObjectPath(AECDemo::GetMontageSpecs()[SpecIndex], false));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAECClockDueTimesTest, "AnimEventClock.Clock.DueTimesAndDestroyDuringDispatch", AECClockTests::Flags)
bool FAECClockDueTimesTest::RunTest(const FString& Parameters)
{
	using namespace AECClockTests;
	UAnimMontage* Attack = LoadDemo(0);
	if (!TestNotNull(TEXT("demo montage loads"), Attack))
	{
		return false;
	}
	FClockWorld W;
	if (!TestTrue(TEXT("a test world"), W.IsReady(*this)))
	{
		return false;
	}
	const double Start = W.World->GetTimeSeconds();
	const int32 Action = W.Clock->PlayAction(Attack, 2.f);
	TestTrue(TEXT("an action id"), Action != INDEX_NONE);
	for (int32 Frame = 1; Frame <= 300; ++Frame)
	{
		W.TickAt(Start + Frame / 60.0);
	}
	TestEqual(TEXT("every key once, in order"), W.Sequence(), FString(TEXT("Wind/i HitWindow/b Hit1/i Hit2/i HitWindow/e Recover/i")));
	const FAECMontageTimeline Timeline = FAECMontageTimeline::Build(*Attack);
	for (const FAECEventInfo& Event : W.Listener->Received)
	{
		const FAECTimelineKey& Key = Timeline.GetKeys()[Event.KeyIndex];
		TestNearlyEqual(*FString::Printf(TEXT("%s due at start + position / rate"), *Event.EventName.ToString()), Event.DueTime, Start + Key.Position / 2.0, 1e-4);
		TestTrue(*FString::Printf(TEXT("%s never early, and less than one frame late"), *Event.EventName.ToString()),
			Event.LatenessSeconds >= -1e-6f && Event.LatenessSeconds < 1.f / 60.f);
		TestEqual(TEXT("the action id"), Event.ActionId, Action);
	}
	TestFalse(TEXT("the action ends at the montage's end"), W.Clock->IsActionActive());

	// A handler that destroys the actor in the middle of dispatch: dispatch stops, later frames are safe (02 §8).
	FClockWorld D;
	if (!TestTrue(TEXT("a second test world"), D.IsReady(*this)))
	{
		return false;
	}
	D.Listener->ReactOn(UAECClockTestListener::EReaction::DestroyOwner, TEXT("Wind"), EAECEventPhase::Instant);
	const double DStart = D.World->GetTimeSeconds();
	D.Clock->PlayAction(Attack, 1.f);
	D.TickAt(DStart + 3.0);   // Wind's handler destroys the actor; the window's begin and Hit1 were due in the same frame
	TestEqual(TEXT("dispatch stops with the actor"), D.Sequence(), FString(TEXT("Wind/i")));
	D.TickAt(DStart + 7.0);
	TestEqual(TEXT("nothing fires after the actor is gone"), D.Listener->Received.Num(), 1);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
