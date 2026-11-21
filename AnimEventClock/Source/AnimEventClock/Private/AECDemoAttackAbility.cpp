// P06 AnimEventClock (demo) - UAECDemoAttackAbility.

#include "AECDemoAttackAbility.h"

#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Animation/AnimMontage.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AECDemoAttackAbility)

UAECDemoAttackAbility::UAECDemoAttackAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerExecution;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerInitiated;
}

void UAECDemoAttackAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	// The event data holds the montage as a const pointer; the task's API takes a mutable one but never changes the asset.
	UAnimMontage* Montage = TriggerEventData ? const_cast<UAnimMontage*>(Cast<UAnimMontage>(TriggerEventData->OptionalObject.Get())) : nullptr;
	const float PlayRate = TriggerEventData ? TriggerEventData->EventMagnitude : 0.f;
	if (!Montage || PlayRate <= 0.f || !CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// bAllowInterruptAfterBlendOut: an interruption that comes after the natural blend-out has begun still reports
	// OnInterrupted, so the ability always ends (otherwise neither delegate fires, AbilityTask_PlayMontageAndWait.cpp:85-103).
	UAbilityTask_PlayMontageAndWait* Task = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(this, NAME_None, Montage, PlayRate,
		NAME_None, /*bStopWhenAbilityEnds=*/ true, /*AnimRootMotionTranslationScale=*/ 1.f, /*StartTimeSeconds=*/ 0.f, /*bAllowInterruptAfterBlendOut=*/ true);
	Task->OnCompleted.AddDynamic(this, &UAECDemoAttackAbility::HandleMontageEnded);
	Task->OnInterrupted.AddDynamic(this, &UAECDemoAttackAbility::HandleMontageEnded);
	Task->OnCancelled.AddDynamic(this, &UAECDemoAttackAbility::HandleMontageCancelled);
	Task->ReadyForActivation();
}

void UAECDemoAttackAbility::HandleMontageEnded()
{
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

void UAECDemoAttackAbility::HandleMontageCancelled()
{
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
}
