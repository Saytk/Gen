#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GenCharacterMovementComponent.generated.h"

/**
 * Mouvement des personnages de Gen (revue V6-V8, I-2).
 *
 * Les ralentis propres à chaque machine (AGenCharacterBase::SetLocalMoveSpeedMultiplier : incantation, fenêtre de contre)
 * changent MaxWalkSpeed, qui n'est pas dans les mouvements sauvegardés. Aux bornes (début et fin d'un ralenti), le
 * client et le serveur peuvent simuler un ou deux mouvements à des vitesses différentes : quelques centimètres d'écart,
 * au-dessus du seuil de correction. Deux parades :
 * - le client envoie ses mouvements en attente avant les RPC de sort qui changent le ralenti (FlushServerMoves, voir
 *   AGenCharacterBase::FlushMovesToServer) ;
 * - le serveur, pendant SpeedChangeCorrectionGrace après un changement de ralenti, accepte un écart jusqu'à
 *   SpeedChangeErrorTolerance et reprend alors la position du client (pas de correction). Hors de cette fenêtre, ou
 *   au-delà de la tolérance, la règle normale du moteur s'applique.
 */
UCLASS()
class GEN_API UGenCharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	/** Serveur : un ralenti local vient de changer ; ouvre la fenêtre de grâce des corrections. */
	void NoteLocalSpeedChange();

	/** Serveur : dans la fenêtre de grâce d'un changement de ralenti. */
	bool IsInCorrectionGrace() const;

	/** Serveur : corrections envoyées au client (tests : un ralenti ne doit pas en provoquer). */
	int32 GetServerCorrectionCount() const { return ServerCorrectionCount; }

	/** Serveur : écarts acceptés grâce à la fenêtre de grâce (tests). */
	int32 GetGraceAcceptedCount() const { return GraceAcceptedCount; }

protected:
	virtual bool ServerCheckClientError(float ClientTimeStamp, float DeltaTime, const FVector& Accel, const FVector& ClientWorldLocation, const FVector& RelativeClientLocation,
		FMovementBaseInterfaceData* ClientMovementBaseInterfaceData, FName ClientBaseBoneName, uint8 ClientMovementMode) override;

	virtual bool ServerExceedsAllowablePositionError(float ClientTimeStamp, float DeltaTime, const FVector& Accel, const FVector& ClientWorldLocation, const FVector& RelativeClientLocation,
		FMovementBaseInterfaceData* ClientMovementBaseInterfaceData, FName ClientBaseBoneName, uint8 ClientMovementMode) override;

	virtual bool ServerShouldUseAuthoritativePosition(float ClientTimeStamp, float DeltaTime, const FVector& Accel, const FVector& ClientWorldLocation, const FVector& RelativeClientLocation,
		FMovementBaseInterfaceData* ClientMovementBaseInterfaceData, FName ClientBaseBoneName, uint8 ClientMovementMode) override;

	/** Durée de la grâce après un changement de ralenti local (serveur). */
	UPROPERTY(EditDefaultsOnly, Category = "Gen|Network", meta = (ClampMin = "0.0", Units = "s"))
	float SpeedChangeCorrectionGrace = 0.2f;

	/** Écart toléré pendant la grâce (une image de vitesse différente à 4096 cm/s² d'accélération : ~5 cm). */
	UPROPERTY(EditDefaultsOnly, Category = "Gen|Network", meta = (ClampMin = "0.0", Units = "cm"))
	float SpeedChangeErrorTolerance = 10.f;

private:
	double GraceEndTime = -1.0;
	/** Le dernier écart a été accepté par la grâce : le serveur reprend la position du client. */
	bool bAcceptClientPositionInGrace = false;
	int32 ServerCorrectionCount = 0;
	int32 GraceAcceptedCount = 0;
};
