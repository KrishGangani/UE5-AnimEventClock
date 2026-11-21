// P06 AnimEventClock (demo) - the character the benchmark spawns: the engine's tutorial mannequin, playing demo montages.

#pragma once

#include "GameFramework/Character.h"
#include "AECDemoCharacter.generated.h"

class UAECEventClockComponent;
class UAnimMontage;

/**
 * A plain ACharacter with the tutorial mannequin mesh and a graph-less UAnimInstance (montages still advance and fire
 * notifies; the pose is not needed to measure timing). An NPC, unless the demo game mode gives it to a remote player.
 *
 * The mesh subobject is a USkeletalMeshComponentBudgeted, so the Animation Budget Allocator can manage it when a budget
 * condition asks for it; otherwise it never registers and behaves like an ordinary skeletal mesh component.
 *
 * Start requests: the benchmark director can ask this character to start its next action from this actor's own tick.
 * That tick runs in TG_PrePhysics and is a prerequisite of the mesh's tick, so a montage started there is started
 * before the mesh's animation update of the same frame (the "pre" start order; 01 H8).
 *
 * Networking (network runs only; none of it changes a standalone run): the character replicates and is always relevant
 * (an actor that left relevancy would be destroyed on the client in the middle of a measurement). The server tells
 * clients about each action with two reliable multicasts that carry the server's action id: NQ/NB and T start on a
 * client when the start arrives; G gets its montage from GAS and uses the multicast only to learn the id.
 */
UCLASS()
class AAECDemoCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AAECDemoCharacter(const FObjectInitializer& ObjectInitializer);

	/** This character's index in the director's list, or INDEX_NONE if no director spawned it. Replicated once. */
	int32 GetBenchIndex() const { return BenchIndex; }
	void SetBenchIndex(int32 InBenchIndex) { BenchIndex = InBenchIndex; }

	/** True for a character the demo game mode gives to a remote player. Set before FinishSpawning; replicated once. */
	bool IsPlayerCharacter() const { return bPlayerCharacter; }
	void SetPlayerCharacter(bool bInPlayerCharacter) { bPlayerCharacter = bInPlayerCharacter; }

	/** The character's event clock (S6). Idle, and not ticking, unless a run plays actions through it. */
	UAECEventClockComponent* GetClock() const { return Clock; }

	/** Asks for the director's start to run in this actor's next tick (before the mesh's tick of that frame). */
	void RequestStartInNextTick() { bStartRequested = true; }

	/** Plays an action's montage on this machine (Montage_Play). Returns false if nothing could start. Baseline G's
	 *  character overrides it, so the character class chosen at spawn is the only place the configuration is decided. */
	virtual bool PlayActionMontage(UAnimMontage& Montage, float PlayRate);

	/** Interrupts it again (Montage_Stop). Overridden by baseline G's character too. */
	virtual void StopActionMontage(UAnimMontage& Montage, float BlendOutSeconds);

	/** Server -> every client: the action with this server id has started (demo montage index, play rate). */
	UFUNCTION(NetMulticast, Reliable)
	void MulticastStartAction(int32 ActionId, uint8 MontageIndex, float PlayRate);

	/** Server -> every client: the action with this server id was interrupted. */
	UFUNCTION(NetMulticast, Reliable)
	void MulticastStopAction(int32 ActionId);

	//~ Begin AActor
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	//~ End AActor

private:
	UPROPERTY(VisibleAnywhere, Category = "AEC")
	TObjectPtr<UAECEventClockComponent> Clock;

	UPROPERTY(Replicated)
	int32 BenchIndex = INDEX_NONE;

	UPROPERTY(Replicated)
	bool bPlayerCharacter = false;

	bool bStartRequested = false;
};
