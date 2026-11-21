// P06 AnimEventClock (demo) - the demo character with an ability system, for baseline G.

#pragma once

#include "AbilitySystemInterface.h"
#include "AECDemoCharacter.h"
#include "GameplayAbilitySpecHandle.h"
#include "NativeGameplayTags.h"
#include "AECDemoGasCharacter.generated.h"

class UAbilitySystemComponent;

/** The activation's event tag. Not decoration: an owning client runs a server-initiated activation with its event data only
 *  if the event tag is valid (AbilitySystemComponent_Abilities.cpp:2453), so without a tag it would get no montage. */
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_AECDemo_Action);

/**
 * Baseline G (01 §6.2): the director starts each action by activating UAECDemoAttackAbility, which plays the montage with
 * PlayMontageAndWait. The montage then reaches clients the way GAS sends it: simulated proxies through the replicated
 * montage info (with its 0.1 s position correction, 01b #8), the owning client through its own activation of the
 * server-initiated ability. A separate class, so NQ/NB/T runs keep a character without an ability system.
 *
 * The ability system lives on the character itself (owner = avatar), the usual setup for NPCs; it initialises itself
 * that way (AbilitySystemComponent_Abilities.cpp:78-84). A player's character initialises it again once possessed, so
 * its actor info also knows the player controller.
 */
UCLASS()
class AAECDemoGasCharacter : public AAECDemoCharacter, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	AAECDemoGasCharacter(const FObjectInitializer& ObjectInitializer);

	//~ Begin IAbilitySystemInterface
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override { return AbilitySystem; }
	//~ End IAbilitySystemInterface

	//~ Begin AAECDemoCharacter
	/** The server activates the attack ability with this montage and play rate; GAS brings the montage to every client, so
	 *  a client plays nothing itself. Returns false only if the server's activation failed. */
	virtual bool PlayActionMontage(UAnimMontage& Montage, float PlayRate) override;

	/** The server stops the ability's montage; the ability then ends, and GAS stops the montage on clients. */
	virtual void StopActionMontage(UAnimMontage& Montage, float BlendOutSeconds) override;
	//~ End AAECDemoCharacter

	//~ Begin AActor / APawn
	virtual void BeginPlay() override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void OnRep_Controller() override;
	//~ End AActor / APawn

private:
	UPROPERTY(VisibleAnywhere, Category = "AEC")
	TObjectPtr<UAbilitySystemComponent> AbilitySystem;

	FGameplayAbilitySpecHandle AttackHandle;
};
