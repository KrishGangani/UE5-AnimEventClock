// P06 AnimEventClock (demo) - AAECDemoCharacter.

#include "AECDemoCharacter.h"

#include "AECBenchSubsystem.h"
#include "AECEventClockComponent.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "SkeletalMeshComponentBudgeted.h"
#include "UObject/ConstructorHelpers.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AECDemoCharacter)

namespace AECDemoCharacter
{
	/** The engine's tutorial mannequin (Starter Content, referenced by path, cooked by DefaultGame.ini). */
	const TCHAR* const MeshPath = TEXT("/Engine/Tutorial/SubEditors/TutorialAssets/Character/TutorialTPP.TutorialTPP");

	/** The mannequin's origin is at its feet and it faces +Y: move it down to the capsule's base and turn it to face +X. */
	const FVector MeshOffset(0.0, 0.0, -90.0);
	const FRotator MeshRotation(0.0, -90.0, 0.0);

	/** A client's benchmark director, or null on the server (a multicast runs there too) or without one. */
	UAECBenchSubsystem* ClientBench(const AActor& Actor)
	{
		return Actor.HasAuthority() ? nullptr : Actor.GetWorld()->GetSubsystem<UAECBenchSubsystem>();
	}
}

AAECDemoCharacter::AAECDemoCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<USkeletalMeshComponentBudgeted>(ACharacter::MeshComponentName))
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;   // ACharacter's default too; stated because the start order depends on it
	AutoPossessAI = EAutoPossessAI::Disabled;      // an NPC with no controller at all
	bAlwaysRelevant = true;                        // network runs: never destroyed on a client by relevancy

	static ConstructorHelpers::FObjectFinder<USkeletalMesh> MannequinMesh(AECDemoCharacter::MeshPath);
	USkeletalMeshComponent* MeshComponent = GetMesh();
	MeshComponent->SetSkeletalMeshAsset(MannequinMesh.Object);
	MeshComponent->SetAnimInstanceClass(UAnimInstance::StaticClass());   // no anim graph: montages only
	MeshComponent->SetRelativeLocationAndRotation(AECDemoCharacter::MeshOffset, AECDemoCharacter::MeshRotation);

	// Without shadows, "not on screen" means "not rendered": an off-screen mesh casting a visible shadow would otherwise
	// count as recently rendered (SkinnedMeshComponent.cpp:1864 uses the last render time of any pass).
	MeshComponent->SetCastShadow(false);

	// The director turns this on before BeginPlay only for budget conditions (01b #5).
	CastChecked<USkeletalMeshComponentBudgeted>(MeshComponent)->SetAutoRegisterWithBudgetAllocator(false);

	Clock = CreateDefaultSubobject<UAECEventClockComponent>(TEXT("EventClock"));
}

void AAECDemoCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// Both are fixed before the character first replicates, so the initial bunch carries them, and a client has them
	// by its BeginPlay (replicated actors begin play after their initial properties arrive).
	DOREPLIFETIME_CONDITION(AAECDemoCharacter, BenchIndex, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(AAECDemoCharacter, bPlayerCharacter, COND_InitialOnly);
}

void AAECDemoCharacter::BeginPlay()
{
	Super::BeginPlay();

	// This actor's tick (where requested starts run) must come before the mesh's tick, every frame.
	GetMesh()->AddTickPrerequisiteActor(this);

	UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (bPlayerCharacter)
	{
		// A player's character keeps a live movement mode: its client sends a move every frame, and the server ticks this
		// mesh's pose only from those moves (01b #6), which is the role being measured. Flying needs no floor and, with no
		// input, never moves. Set as the default too, because a possession restart re-applies the default mode.
		Movement->DefaultLandMovementMode = MOVE_Flying;
		Movement->SetMovementMode(MOVE_Flying);
	}
	else
	{
		// NPCs never move. Without this, a montage with "root motion from everything" (the look-ahead condition) makes
		// the movement component simulate a controller-less character, and characters off the floor would fall.
		Movement->DisableMovement();
	}

	if (UAECBenchSubsystem* Bench = AECDemoCharacter::ClientBench(*this))
	{
		SetActorTickEnabled(false);   // the actor tick only runs "pre" starts, which only the server makes
		Bench->RegisterReplicatedCharacter(*this);
	}
}

void AAECDemoCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bStartRequested)
	{
		bStartRequested = false;
		if (UAECBenchSubsystem* Bench = GetWorld()->GetSubsystem<UAECBenchSubsystem>())
		{
			Bench->ExecuteRequestedStart(*this);
		}
	}
}

bool AAECDemoCharacter::PlayActionMontage(UAnimMontage& Montage, float PlayRate)
{
	UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance();
	if (!AnimInstance)
	{
		return false;
	}
	AnimInstance->Montage_Play(&Montage, PlayRate);
	return true;
}

void AAECDemoCharacter::StopActionMontage(UAnimMontage& Montage, float BlendOutSeconds)
{
	if (UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance())
	{
		AnimInstance->Montage_Stop(BlendOutSeconds, &Montage);
	}
}

void AAECDemoCharacter::MulticastStartAction_Implementation(int32 ActionId, uint8 MontageIndex, float PlayRate)
{
	if (UAECBenchSubsystem* Bench = AECDemoCharacter::ClientBench(*this))   // the server started the action itself
	{
		Bench->HandleServerStart(*this, ActionId, MontageIndex, PlayRate);
	}
}

void AAECDemoCharacter::MulticastStopAction_Implementation(int32 ActionId)
{
	if (UAECBenchSubsystem* Bench = AECDemoCharacter::ClientBench(*this))
	{
		Bench->HandleServerStop(*this, ActionId);
	}
}
