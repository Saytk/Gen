#include "Character/GenCharacterMovementComponent.h"

#include "Engine/World.h"
#include "GameFramework/Character.h"

void UGenCharacterMovementComponent::NoteLocalSpeedChange()
{
	// Seul le serveur d'un client distant compare des positions
	const UWorld* World = GetWorld();
	if (World && CharacterOwner && CharacterOwner->HasAuthority() && !CharacterOwner->IsLocallyControlled())
	{
		GraceEndTime = World->GetTimeSeconds() + SpeedChangeCorrectionGrace;
	}
}

bool UGenCharacterMovementComponent::IsInCorrectionGrace() const
{
	const UWorld* World = GetWorld();
	return World && World->GetTimeSeconds() <= GraceEndTime;
}

bool UGenCharacterMovementComponent::ServerCheckClientError(float ClientTimeStamp, float DeltaTime, const FVector& Accel, const FVector& ClientWorldLocation,
	const FVector& RelativeClientLocation, FMovementBaseInterfaceData* ClientMovementBaseInterfaceData, FName ClientBaseBoneName, uint8 ClientMovementMode)
{
	bAcceptClientPositionInGrace = false;
	const bool bError = Super::ServerCheckClientError(ClientTimeStamp, DeltaTime, Accel, ClientWorldLocation, RelativeClientLocation, ClientMovementBaseInterfaceData,
		ClientBaseBoneName, ClientMovementMode);
	ServerCorrectionCount += bError ? 1 : 0;
	return bError;
}

bool UGenCharacterMovementComponent::ServerExceedsAllowablePositionError(float ClientTimeStamp, float DeltaTime, const FVector& Accel, const FVector& ClientWorldLocation,
	const FVector& RelativeClientLocation, FMovementBaseInterfaceData* ClientMovementBaseInterfaceData, FName ClientBaseBoneName, uint8 ClientMovementMode)
{
	const bool bExceeds = Super::ServerExceedsAllowablePositionError(ClientTimeStamp, DeltaTime, Accel, ClientWorldLocation, RelativeClientLocation,
		ClientMovementBaseInterfaceData, ClientBaseBoneName, ClientMovementMode);

	// Revue V6-V8, I-2 : juste après un changement de ralenti, un petit écart (même mode de mouvement) vient du mouvement
	// simulé de part et d'autre de la borne à deux vitesses : accepté, le serveur reprend la position du client
	if (bExceeds && IsInCorrectionGrace() && UpdatedComponent && ClientMovementMode == PackNetworkMovementMode()
		&& FVector::DistSquared(UpdatedComponent->GetComponentLocation(), ClientWorldLocation) <= FMath::Square(SpeedChangeErrorTolerance))
	{
		bAcceptClientPositionInGrace = true;
		++GraceAcceptedCount;
		return false;
	}
	return bExceeds;
}

bool UGenCharacterMovementComponent::ServerShouldUseAuthoritativePosition(float ClientTimeStamp, float DeltaTime, const FVector& Accel, const FVector& ClientWorldLocation,
	const FVector& RelativeClientLocation, FMovementBaseInterfaceData* ClientMovementBaseInterfaceData, FName ClientBaseBoneName, uint8 ClientMovementMode)
{
	if (bAcceptClientPositionInGrace)
	{
		bAcceptClientPositionInGrace = false;
		return true;
	}
	return Super::ServerShouldUseAuthoritativePosition(ClientTimeStamp, DeltaTime, Accel, ClientWorldLocation, RelativeClientLocation, ClientMovementBaseInterfaceData,
		ClientBaseBoneName, ClientMovementMode);
}
