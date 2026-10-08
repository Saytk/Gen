#include "Character/GenCharacterMovementComponent.h"

#include "Character/GenCharacterBase.h"
#include "Engine/NetSerialization.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"

// --- Revue PIE finale, C-1 : multiplicateur local porté par chaque mouvement ---------------------------------------

void FGenCharacterNetworkMoveData::ClientFillNetworkMoveData(const FSavedMove_Character& ClientMove, ENetworkMoveType MoveType)
{
	FCharacterNetworkMoveData::ClientFillNetworkMoveData(ClientMove, MoveType);
	// Les mouvements sauvegardés viennent tous de FGenNetworkPredictionData_Client::AllocateNewMove
	LocalSpeedMultiplier = static_cast<const FGenSavedMove&>(ClientMove).LocalSpeedMultiplier;
}

bool FGenCharacterNetworkMoveData::Serialize(UCharacterMovementComponent& CharacterMovement, FArchive& Ar, UPackageMap* PackageMap, ENetworkMoveType MoveType)
{
	const bool bSuccess = FCharacterNetworkMoveData::Serialize(CharacterMovement, Ar, PackageMap, MoveType);
	SerializeOptionalValue<float>(Ar.IsSaving(), Ar, LocalSpeedMultiplier, 1.f);
	// Valeur absurde d'un client modifié : celle sans multiplicateur (le serveur borne de toute façon l'annonce)
	if (Ar.IsLoading() && (!FMath::IsFinite(LocalSpeedMultiplier) || LocalSpeedMultiplier < 0.f))
	{
		LocalSpeedMultiplier = 1.f;
	}
	return bSuccess && !Ar.IsError();
}

FGenCharacterNetworkMoveDataContainer::FGenCharacterNetworkMoveDataContainer()
{
	NewMoveData = &GenMoveData[0];
	PendingMoveData = &GenMoveData[1];
	OldMoveData = &GenMoveData[2];
}

void FGenSavedMove::Clear()
{
	Super::Clear();
	LocalSpeedMultiplier = 1.f;
}

void FGenSavedMove::SetMoveFor(ACharacter* C, float InDeltaTime, FVector const& NewAccel, FNetworkPredictionData_Client_Character& ClientData)
{
	Super::SetMoveFor(C, InDeltaTime, NewAccel, ClientData);
	// Celui avec lequel ce mouvement est simulé (MaxWalkSpeed déjà à jour, AGenCharacterBase::RefreshMaxWalkSpeed)
	const AGenCharacterBase* GenCharacter = Cast<AGenCharacterBase>(C);
	LocalSpeedMultiplier = GenCharacter ? GenCharacter->GetLocalMoveSpeedMultiplier() : 1.f;
}

bool FGenSavedMove::CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* InCharacter, float MaxDelta) const
{
	// Un mouvement combiné est simulé d'un bloc à une seule vitesse
	if (static_cast<const FGenSavedMove&>(*NewMove).LocalSpeedMultiplier != LocalSpeedMultiplier)
	{
		return false;
	}
	return Super::CanCombineWith(NewMove, InCharacter, MaxDelta);
}

bool FGenSavedMove::IsImportantMove(const FSavedMovePtr& LastAckedMove) const
{
	// Changement de vitesse pas encore acquitté : renvoyé avec l'envoi suivant s'il se perd
	if (LastAckedMove.IsValid() && static_cast<const FGenSavedMove&>(*LastAckedMove).LocalSpeedMultiplier != LocalSpeedMultiplier)
	{
		return true;
	}
	return Super::IsImportantMove(LastAckedMove);
}

FSavedMovePtr FGenNetworkPredictionData_Client::AllocateNewMove()
{
	return FSavedMovePtr(new FGenSavedMove());
}

UGenCharacterMovementComponent::UGenCharacterMovementComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetNetworkMoveDataContainer(GenMoveDataContainer);
}

FNetworkPredictionData_Client* UGenCharacterMovementComponent::GetPredictionData_Client() const
{
	if (ClientPredictionData == nullptr)
	{
		UGenCharacterMovementComponent* MutableThis = const_cast<UGenCharacterMovementComponent*>(this);
		MutableThis->ClientPredictionData = new FGenNetworkPredictionData_Client(*this);
	}
	return ClientPredictionData;
}

void UGenCharacterMovementComponent::MoveAutonomous(float ClientTimeStamp, float DeltaTime, uint8 CompressedFlags, const FVector& NewAccel)
{
	// Données du mouvement en cours : reçues du client (serveur) ou remplies depuis le mouvement rejoué (client). Toujours
	// les nôtres (SetNetworkMoveDataContainer, ClientFillNetworkMoveData)
	AGenCharacterBase* GenCharacter = Cast<AGenCharacterBase>(CharacterOwner);
	const FGenCharacterNetworkMoveData* MoveData = static_cast<const FGenCharacterNetworkMoveData*>(GetCurrentNetworkMoveData());
	const float BaseSpeed = GenCharacter ? GenCharacter->GetBaseMoveSpeed() : -1.f;
	if (!MoveData || BaseSpeed < 0.f)
	{
		Super::MoveAutonomous(ClientTimeStamp, DeltaTime, CompressedFlags, NewAccel);
		return;
	}

	float Multiplier = MoveData->LocalSpeedMultiplier;
	if (GenCharacter->HasAuthority() && !GenCharacter->IsLocallyControlled())
	{
		// Serveur : jamais au-delà de ce que son propre état permet dans la fenêtre de grâce
		const float MaxClaimable = GenCharacter->GetMaxClaimableLocalMoveSpeedMultiplier(SpeedChangeCorrectionGrace);
		if (Multiplier > MaxClaimable + KINDA_SMALL_NUMBER)
		{
			++ClampedSpeedClaimCount;
			Multiplier = MaxClaimable;
		}
	}

	MaxWalkSpeed = BaseSpeed * Multiplier;
	Super::MoveAutonomous(ClientTimeStamp, DeltaTime, CompressedFlags, NewAccel);
	// Le mouvement a pu détruire le personnage (PerformMovement) ; sinon, retour à la vitesse de cette machine
	if (IsValid(GenCharacter))
	{
		GenCharacter->RefreshMaxWalkSpeed();
	}
}

// --- Revue V6-V8, I-2 : grâce des corrections autour d'un changement de ralenti -------------------------------------

void UGenCharacterMovementComponent::NoteLocalSpeedChange()
{
	// Seul le serveur d'un client distant compare des positions
	const UWorld* World = GetWorld();
	if (World && CharacterOwner && CharacterOwner->HasAuthority() && !CharacterOwner->IsLocallyControlled())
	{
		GraceEndTime = World->GetTimeSeconds() + SpeedChangeCorrectionGrace;
		GraceErrorBudget = SpeedChangeErrorTolerance;
	}
}

bool UGenCharacterMovementComponent::IsInCorrectionGrace() const
{
	const UWorld* World = GetWorld();
	return World && World->GetTimeSeconds() <= GraceEndTime && GraceErrorBudget > 0.f;
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
	// simulé de part et d'autre de la borne à deux vitesses : accepté, le serveur reprend la position du client.
	// Revue finale, I-1 : dans la limite du budget de la fenêtre (somme des écarts acceptés), pas par mouvement
	if (bExceeds && IsInCorrectionGrace() && UpdatedComponent && ClientMovementMode == PackNetworkMovementMode())
	{
		const float Error = FVector::Dist(UpdatedComponent->GetComponentLocation(), ClientWorldLocation);
		if (Error <= GraceErrorBudget)
		{
			GraceErrorBudget -= Error;
			GraceAcceptedDistance += Error;
			bAcceptClientPositionInGrace = true;
			++GraceAcceptedCount;
			return false;
		}
		// Budget dépassé : correction normale, et la fenêtre se ferme jusqu'au prochain changement de ralenti
		GraceErrorBudget = 0.f;
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
