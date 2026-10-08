#include "Player/GenPlayerController.h"

#include "AbilitySystem/GenAbilitySystemComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Character/GenCharacterBase.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Input/GenInputConfig.h"
#include "InputActionValue.h"
#include "Player/GenPlayerState.h"
#include "UI/GenUISubsystem.h"

AGenPlayerController::AGenPlayerController()
{
	bShowMouseCursor = true;
	DefaultMouseCursor = EMouseCursor::Crosshairs;
}

void AGenPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (!IsLocalController() || !InputConfig || !InputConfig->DefaultMappingContext)
	{
		return;
	}

	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		Subsystem->AddMappingContext(InputConfig->DefaultMappingContext, 0);
	}
}

void AGenPlayerController::AcknowledgePossession(APawn* P)
{
	Super::AcknowledgePossession(P);

	// Second chemin de l'événement "ASC prêt" (l'autre : AGenPlayerCharacter::InitAbilitySystemFromPlayerState).
	// Le sous-système ignore une seconde annonce pour le même ASC et le même pion.
	const AGenCharacterBase* GenCharacter = Cast<AGenCharacterBase>(P);
	UAbilitySystemComponent* ASC = GenCharacter ? GenCharacter->GetAbilitySystemComponent() : nullptr;
	if (IsLocalController() && ASC && ASC->GetAvatarActor() == P)
	{
		if (UGenUISubsystem* UISubsystem = UGenUISubsystem::Get(this))
		{
			UISubsystem->NotifyAbilitySystemReady(ASC);
		}
	}
}

void AGenPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(InputComponent);
	if (!EnhancedInput || !InputConfig)
	{
		return;
	}

	if (InputConfig->MoveAction)
	{
		EnhancedInput->BindAction(InputConfig->MoveAction, ETriggerEvent::Triggered, this, &ThisClass::Move);
	}

	if (InputConfig->CancelAction)
	{
		EnhancedInput->BindAction(InputConfig->CancelAction, ETriggerEvent::Started, this, &ThisClass::CancelCast);
	}

	for (const FGenAbilityInputAction& Binding : InputConfig->AbilityInputActions)
	{
		if (Binding.InputAction && Binding.InputTag.IsValid())
		{
			EnhancedInput->BindAction(Binding.InputAction, ETriggerEvent::Started, this, &ThisClass::AbilityInputPressed, Binding.InputTag);
			EnhancedInput->BindAction(Binding.InputAction, ETriggerEvent::Completed, this, &ThisClass::AbilityInputReleased, Binding.InputTag);
		}
	}
}

void AGenPlayerController::Move(const FInputActionValue& Value)
{
	APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn)
	{
		return;
	}

	if (const AGenCharacterBase* GenCharacter = Cast<AGenCharacterBase>(ControlledPawn); GenCharacter && GenCharacter->IsDead())
	{
		return;
	}

	// Déplacement relatif à la caméra (fixe) : Z = haut de l'écran, D = droite de l'écran
	const FVector2D Axis = Value.Get<FVector2D>();
	const float CameraYaw = PlayerCameraManager ? PlayerCameraManager->GetCameraRotation().Yaw : 0.f;
	const FRotator YawRotation(0.f, CameraYaw, 0.f);

	const FRotationMatrix RotationMatrix(YawRotation);
	ControlledPawn->AddMovementInput(RotationMatrix.GetUnitAxis(EAxis::X), Axis.Y);
	ControlledPawn->AddMovementInput(RotationMatrix.GetUnitAxis(EAxis::Y), Axis.X);
}

void AGenPlayerController::AbilityInputPressed(FGameplayTag InputTag)
{
	if (UGenAbilitySystemComponent* ASC = GetGenAbilitySystemComponent())
	{
		ASC->AbilityInputTagPressed(InputTag);
	}
}

void AGenPlayerController::AbilityInputReleased(FGameplayTag InputTag)
{
	if (UGenAbilitySystemComponent* ASC = GetGenAbilitySystemComponent())
	{
		ASC->AbilityInputTagReleased(InputTag);
	}
}

void AGenPlayerController::CancelCast()
{
	if (UGenAbilitySystemComponent* ASC = GetGenAbilitySystemComponent())
	{
		ASC->CancelPendingCasts();
	}
}

void AGenPlayerController::PostProcessInput(const float DeltaTime, const bool bGamePaused)
{
	if (UGenAbilitySystemComponent* ASC = GetGenAbilitySystemComponent())
	{
		ASC->ProcessAbilityInput(DeltaTime, bGamePaused);
	}

	Super::PostProcessInput(DeltaTime, bGamePaused);
}

void AGenPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	UpdateAimRotation();
}

void AGenPlayerController::UpdateAimRotation()
{
	const APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn || !IsLocalController())
	{
		return;
	}

	FVector CursorLocation;
	if (!GetCursorLocationOnPlane(ControlledPawn->GetActorLocation().Z, CursorLocation))
	{
		return;
	}

	const FVector ToCursor = (CursorLocation - ControlledPawn->GetActorLocation()).GetSafeNormal2D();
	if (!ToCursor.IsNearlyZero())
	{
		SetControlRotation(FRotator(0.f, ToCursor.Rotation().Yaw, 0.f));
	}
}

UGenAbilitySystemComponent* AGenPlayerController::GetGenAbilitySystemComponent() const
{
	const AGenPlayerState* PS = GetPlayerState<AGenPlayerState>();
	return PS ? PS->GetGenAbilitySystemComponent() : nullptr;
}

bool AGenPlayerController::GetCursorLocationOnPlane(float PlaneZ, FVector& OutLocation) const
{
#if !UE_BUILD_SHIPPING
	if (bDebugAimOverride)
	{
		OutLocation = FVector(DebugAimLocation.X, DebugAimLocation.Y, PlaneZ);
		return true;
	}
#endif

	FVector WorldOrigin;
	FVector WorldDirection;
	if (!DeprojectMousePositionToWorld(WorldOrigin, WorldDirection) || FMath::IsNearlyZero(WorldDirection.Z))
	{
		return false;
	}

	const float Distance = (PlaneZ - WorldOrigin.Z) / WorldDirection.Z;
	if (Distance < 0.f)
	{
		return false;
	}

	OutLocation = WorldOrigin + WorldDirection * Distance;
	return true;
}
