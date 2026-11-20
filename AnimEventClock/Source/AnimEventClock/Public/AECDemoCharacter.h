// P06 AnimEventClock (demo) - the character the benchmark spawns: the engine's tutorial mannequin, playing demo montages.

#pragma once

#include "GameFramework/Character.h"
#include "AECDemoCharacter.generated.h"

/**
 * A plain ACharacter with the tutorial mannequin mesh and a graph-less UAnimInstance (montages still advance and fire
 * notifies; the pose is not needed to measure timing). Nothing possesses it: an NPC, so URO does not exempt it.
 *
 * The mesh subobject is a USkeletalMeshComponentBudgeted, so the Animation Budget Allocator can manage it when a budget
 * condition asks for it; otherwise it never registers and behaves like an ordinary skeletal mesh component.
 *
 * Start requests: the benchmark director can ask this character to start its next action from this actor's own tick.
 * That tick runs in TG_PrePhysics and is a prerequisite of the mesh's tick, so a montage started there is started
 * before the mesh's animation update of the same frame (the "pre" start order; 01 H8).
 */
UCLASS()
class AAECDemoCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AAECDemoCharacter(const FObjectInitializer& ObjectInitializer);

	/** This character's index in the director's list, or INDEX_NONE if no director spawned it. */
	int32 GetBenchIndex() const { return BenchIndex; }
	void SetBenchIndex(int32 InBenchIndex) { BenchIndex = InBenchIndex; }

	/** Asks for the director's start to run in this actor's next tick (before the mesh's tick of that frame). */
	void RequestStartInNextTick() { bStartRequested = true; }

	//~ Begin AActor
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	//~ End AActor

private:
	int32 BenchIndex = INDEX_NONE;
	bool bStartRequested = false;
};
