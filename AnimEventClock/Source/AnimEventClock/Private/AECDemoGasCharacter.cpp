// P06 AnimEventClock (demo) - AAECDemoGasCharacter.

#include "AECDemoGasCharacter.h"

#include "AbilitySystemComponent.h"
#include "AECDemoAttackAbility.h"
#include "Animation/AnimMontage.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AECDemoGasCharacter)

UE_DEFINE_GAMEPLAY_TAG(TAG_AECDemo_Action, "AEC.Demo.Action");

AAECDemoGasCharacter::AAECDemoGasCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	AbilitySystem = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystem"));
	AbilitySystem->SetIsReplicated(true);
}

void AAECDemoGasCharacter::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority())
	{
		AttackHandle = AbilitySystem->GiveAbility(FGameplayAbilitySpec(UAECDemoAttackAbility::StaticClass()));
	}
}

void AAECDemoGasCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	AbilitySystem->InitAbilityActorInfo(this, this);   // server: the actor info now has the player controller
}

void AAECDemoGasCharacter::OnRep_Controller()
{
	Super::OnRep_Controller();
	AbilitySystem->InitAbilityActorInfo(this, this);   // owning client: the same, once the controller arrives
}

bool AAECDemoGasCharacter::PlayActionMontage(UAnimMontage& Montage, float PlayRate)
{
	if (!HasAuthority())
	{
		return true;   // GAS plays it here: replicated montage info (simulated proxy) or the client's own activation
	}
	// The montage and rate travel in the event data, which a server-initiated activation also sends to the owning client.
	// The public trigger path stamps the tag into the data, then activates (AbilitySystemComponent_Abilities.cpp:2496-2528).
	FGameplayEventData Payload;
	Payload.OptionalObject = &Montage;
	Payload.EventMagnitude = PlayRate;
	return AbilitySystem->TriggerAbilityFromGameplayEvent(AttackHandle, AbilitySystem->AbilityActorInfo.Get(), TAG_AECDemo_Action, &Payload, *AbilitySystem);
}

void AAECDemoGasCharacter::StopActionMontage(UAnimMontage& Montage, float BlendOutSeconds)
{
	if (HasAuthority())
	{
		AbilitySystem->CurrentMontageStop(BlendOutSeconds);
	}
}
