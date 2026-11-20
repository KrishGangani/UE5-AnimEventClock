// P06 AnimEventClock (demo) - AAECDemoCharacter.

#include "AECDemoCharacter.h"

#include "AECBenchSubsystem.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
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
}

AAECDemoCharacter::AAECDemoCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<USkeletalMeshComponentBudgeted>(ACharacter::MeshComponentName))
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;   // ACharacter's default too; stated because the start order depends on it
	AutoPossessAI = EAutoPossessAI::Disabled;      // an NPC with no controller at all

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
}

void AAECDemoCharacter::BeginPlay()
{
	Super::BeginPlay();

	// This actor's tick (where requested starts run) must come before the mesh's tick, every frame.
	GetMesh()->AddTickPrerequisiteActor(this);

	// The characters never move. Without this, a montage with "root motion from everything" (the look-ahead condition)
	// makes the movement component simulate a controller-less character, and characters off the floor would fall.
	GetCharacterMovement()->DisableMovement();
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
