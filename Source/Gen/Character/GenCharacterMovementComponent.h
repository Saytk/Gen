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
 * - le serveur, pendant SpeedChangeCorrectionGrace après un changement de ralenti, accepte de petits écarts et reprend
 *   alors la position du client (pas de correction). Hors de cette fenêtre, ou au-delà du budget, la règle normale du
 *   moteur s'applique.
 *
 * Revue finale, I-1 : chaque écart accepté reprend la position du client, et le mouvement suivant est vérifié depuis
 * là. Une tolérance par mouvement laissait donc un client modifié gagner jusqu'à SpeedChangeErrorTolerance à CHAQUE
 * mouvement de la fenêtre (~+100 % de vitesse en maintenant le clic gauche). La tolérance est désormais un BUDGET :
 * la somme des écarts acceptés depuis le dernier changement de ralenti ne dépasse pas SpeedChangeErrorTolerance.
 * Chaque changement de ralenti rouvre une fenêtre et remet le budget à plein ; un écart qui dépasse le reste du budget
 * est corrigé et ferme la fenêtre. Gain maximal d'un tricheur : 10 cm par changement de ralenti (deux par boule de feu
 * de 0.40 s : ~50 cm/s, contre ~6 m/s avant).
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

	/** Serveur : somme des écarts acceptés par la grâce depuis le début (tests). */
	float GetGraceAcceptedDistance() const { return GraceAcceptedDistance; }

	/** Budget d'écart accepté par changement de ralenti (cm). */
	float GetSpeedChangeErrorBudget() const { return SpeedChangeErrorTolerance; }

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

	/**
	 * Budget d'écart accepté pendant la grâce, cumulé sur tous les mouvements de la fenêtre (une ou deux images de vitesse
	 * différente : ~5 cm chacune à 550 cm/s).
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Gen|Network", meta = (ClampMin = "0.0", Units = "cm"))
	float SpeedChangeErrorTolerance = 10.f;

private:
	double GraceEndTime = -1.0;
	/** Reste du budget d'écart de la fenêtre en cours (remis à SpeedChangeErrorTolerance à chaque changement de ralenti). */
	float GraceErrorBudget = 0.f;
	float GraceAcceptedDistance = 0.f;
	/** Le dernier écart a été accepté par la grâce : le serveur reprend la position du client. */
	bool bAcceptClientPositionInGrace = false;
	int32 ServerCorrectionCount = 0;
	int32 GraceAcceptedCount = 0;
};
