// P06 AnimEventClock (demo) - baseline G's ability: play the action's montage the standard GAS way.

#pragma once

#include "Abilities/GameplayAbility.h"
#include "AECDemoAttackAbility.generated.h"

/**
 * Plays one action's montage with UAbilityTask_PlayMontageAndWait (AbilityTask_PlayMontageAndWait.h:66), the standard
 * networked montage path, and ends when the montage ends or is interrupted.
 *
 * The montage and play rate come in the activation's event data (OptionalObject, EventMagnitude, with an event tag): a
 * server-initiated activation sends that data to the owning client too (ClientActivateAbilitySucceedWithEventData,
 * AbilitySystemComponent_Abilities.cpp:1895-1900), which uses it only if the tag is valid (:2453). Server-initiated,
 * because v1 measures server-started actions only (01 §8). One instance per activation (the engine default), so
 * back-to-back actions never share state.
 */
UCLASS()
class UAECDemoAttackAbility : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UAECDemoAttackAbility();

	//~ Begin UGameplayAbility
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	//~ End UGameplayAbility

private:
	UFUNCTION()
	void HandleMontageEnded();

	UFUNCTION()
	void HandleMontageCancelled();
};
