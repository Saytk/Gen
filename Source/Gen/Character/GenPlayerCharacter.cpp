#include "Character/GenPlayerCharacter.h"

#include "AbilitySystem/GenAbilitySystemComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Player/GenPlayerController.h"
#include "Player/GenPlayerState.h"
#include "UI/GenUISubsystem.h"

AGenPlayerCharacter::AGenPlayerCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = true;

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(GetCapsuleComponent());
	CameraBoom->SetUsingAbsoluteRotation(true); // la caméra ne tourne pas avec le personnage
	CameraBoom->SetRelativeRotation(FRotator(-60.f, 0.f, 0.f));
	CameraBoom->TargetArmLength = 1700.f;
	CameraBoom->bDoCollisionTest = false;
	CameraBoom->bInheritPitch = false;
	CameraBoom->bInheritYaw = false;
	CameraBoom->bInheritRoll = false;
	CameraBoom->bEnableCameraLag = true;
	CameraBoom->CameraLagSpeed = 12.f;

	TopDownCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("TopDownCamera"));
	TopDownCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	TopDownCamera->bUsePawnControlRotation = false;
	TopDownCamera->SetFieldOfView(50.f);
}

void AGenPlayerCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	// Serveur
	InitAbilitySystemFromPlayerState();
}

void AGenPlayerCharacter::UnPossessed()
{
	// Serveur : on retire les sorts donnés par ce corps avant qu'un nouveau pawn prenne le relais
	UninitializeAbilitySystem();

	Super::UnPossessed();
}

void AGenPlayerCharacter::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();

	// Clients
	InitAbilitySystemFromPlayerState();
}

void AGenPlayerCharacter::InitAbilitySystemFromPlayerState()
{
	AGenPlayerState* PS = GetPlayerState<AGenPlayerState>();
	if (!PS)
	{
		return;
	}

	AbilitySystemComponent = PS->GetGenAbilitySystemComponent();
	AttributeSet = PS->GetAttributeSet();

	// Owner = PlayerState (logique), Avatar = ce personnage (physique)
	AbilitySystemComponent->InitAbilityActorInfo(PS, this);

	OnAbilitySystemInitialized();

	// Interface locale (aucun widget sur un serveur dédié ni pour les autres joueurs)
	if (GetNetMode() != NM_DedicatedServer)
	{
		// Sur un client, OnRep_PlayerState peut arriver avant que Controller soit répliqué :
		// on se rabat alors sur le propriétaire du PlayerState (le PC n'existe que chez son joueur).
		const APlayerController* PC = GetController<APlayerController>();
		if (!PC)
		{
			PC = PS->GetPlayerController();
		}

		if (PC && PC->IsLocalController())
		{
			if (UGenUISubsystem* UISubsystem = UGenUISubsystem::Get(PC))
			{
				UISubsystem->NotifyAbilitySystemReady(AbilitySystemComponent);
			}
		}
	}
}

uint8 AGenPlayerCharacter::GetTeamId() const
{
	const AGenPlayerState* PS = GetPlayerState<AGenPlayerState>();
	return PS ? PS->GetTeamId() : GenNoTeam;
}

void AGenPlayerCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (IsLocallyControlled())
	{
		UpdateCameraCursorOffset(DeltaSeconds);
	}
}

void AGenPlayerCharacter::UpdateCameraCursorOffset(float DeltaSeconds)
{
	FVector DesiredOffset = FVector::ZeroVector;

	const AGenPlayerController* PC = Cast<AGenPlayerController>(GetController());
	FVector CursorLocation;
	if (PC && CameraCursorOffsetRatio > 0.f && !IsDead() && PC->GetCursorLocationOnPlane(GetActorLocation().Z, CursorLocation))
	{
		DesiredOffset = (CursorLocation - GetActorLocation()) * CameraCursorOffsetRatio;
		DesiredOffset.Z = 0.f;
		DesiredOffset = DesiredOffset.GetClampedToMaxSize(CameraCursorMaxOffset);
	}

	CameraBoom->TargetOffset = FMath::VInterpTo(CameraBoom->TargetOffset, DesiredOffset, DeltaSeconds, CameraCursorInterpSpeed);
}
