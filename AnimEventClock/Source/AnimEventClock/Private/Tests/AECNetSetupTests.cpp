// P06 AnimEventClock (demo) - checks the network harness's class setup (S4), the parts a network run silently depends on.
// Run: Tools\run_tests.bat (editor, headless), filter "AnimEventClock.Net".

#include "AECDemoAttackAbility.h"
#include "AECDemoCharacter.h"
#include "AECDemoGameMode.h"
#include "AECDemoGasCharacter.h"
#include "AECDemoPlayerController.h"
#include "AbilitySystemComponent.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAECNetSetupTest, "AnimEventClock.Net.HarnessClassesConfigured",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FAECNetSetupTest::RunTest(const FString& Parameters)
{
	// The game mode hands out pawns itself and uses the controller that tells clients when the run is over.
	const AAECDemoGameMode* GameMode = GetDefault<AAECDemoGameMode>();
	TestTrue(TEXT("game mode uses the demo player controller"), GameMode->PlayerControllerClass.Get() == AAECDemoPlayerController::StaticClass());
	TestTrue(TEXT("game mode spawns no default pawn"), GameMode->DefaultPawnClass.Get() == nullptr);

	// Characters replicate and never leave relevancy (a client would destroy them mid-run).
	const AAECDemoCharacter* Character = GetDefault<AAECDemoCharacter>();
	TestTrue(TEXT("demo character replicates"), Character->GetIsReplicated());
	TestTrue(TEXT("demo character is always relevant"), Character->bAlwaysRelevant);

	// Baseline G: an ability system that replicates; a server-initiated ability with one instance per activation.
	const AAECDemoGasCharacter* GasCharacter = GetDefault<AAECDemoGasCharacter>();
	const UAbilitySystemComponent* AbilitySystem = GasCharacter->GetAbilitySystemComponent();
	if (TestNotNull(TEXT("GAS character has an ability system"), AbilitySystem))
	{
		TestTrue(TEXT("ability system replicates"), AbilitySystem->GetIsReplicated());
	}
	const UAECDemoAttackAbility* Ability = GetDefault<UAECDemoAttackAbility>();
	TestTrue(TEXT("attack ability is server-initiated"), Ability->GetNetExecutionPolicy() == EGameplayAbilityNetExecutionPolicy::ServerInitiated);
	TestTrue(TEXT("attack ability is instanced per execution"), Ability->GetInstancingPolicy() == EGameplayAbilityInstancingPolicy::InstancedPerExecution);

	// Without a registered event tag, the owning client would activate the ability with no montage (AECDemoGasCharacter.h).
	const FGameplayTag Tag = TAG_AECDemo_Action;
	TestTrue(TEXT("native tag AEC.Demo.Action is registered"), Tag.IsValid());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
