#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/CharacterMovementReplication.h"
#include "GenCharacterMovementComponent.generated.h"

class AGenCharacterBase;

/**
 * Revue PIE finale, C-1 : données d'un mouvement envoyé au serveur, avec le multiplicateur local de vitesse du client
 * (AGenCharacterBase::GetLocalMoveSpeedMultiplier) pendant ce mouvement. Un bit quand il vaut 1, sinon le flottant.
 */
struct FGenCharacterNetworkMoveData : public FCharacterNetworkMoveData
{
	float LocalSpeedMultiplier = 1.f;

	virtual void ClientFillNetworkMoveData(const FSavedMove_Character& ClientMove, ENetworkMoveType MoveType) override;
	virtual bool Serialize(UCharacterMovementComponent& CharacterMovement, FArchive& Ar, UPackageMap* PackageMap, ENetworkMoveType MoveType) override;
};

/** Conteneur des trois mouvements d'un envoi (nouveau, en attente, ancien important), à nos données. */
struct FGenCharacterNetworkMoveDataContainer : public FCharacterNetworkMoveDataContainer
{
	FGenCharacterNetworkMoveDataContainer();

	FGenCharacterNetworkMoveData GenMoveData[3];
};

/** Mouvement sauvegardé du client : retient le multiplicateur local, rejoué tel quel, jamais combiné à un autre. */
class FGenSavedMove : public FSavedMove_Character
{
public:
	typedef FSavedMove_Character Super;

	float LocalSpeedMultiplier = 1.f;

	virtual void Clear() override;
	virtual void SetMoveFor(ACharacter* C, float InDeltaTime, FVector const& NewAccel, FNetworkPredictionData_Client_Character& ClientData) override;
	virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* InCharacter, float MaxDelta) const override;
	virtual bool IsImportantMove(const FSavedMovePtr& LastAckedMove) const override;
};

class FGenNetworkPredictionData_Client : public FNetworkPredictionData_Client_Character
{
public:
	typedef FNetworkPredictionData_Client_Character Super;

	explicit FGenNetworkPredictionData_Client(const UCharacterMovementComponent& ClientMovement) : Super(ClientMovement) {}

	virtual FSavedMovePtr AllocateNewMove() override;
};

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
 *
 * Revue PIE finale, C-1 : sous 60 ms de latence, la fin d'un ralenti minuté (fenêtre de contre) tombait encore entre deux
 * mouvements de part et d'autre : 11 à 17 cm d'écart, au-delà du budget. Chaque mouvement porte maintenant le
 * multiplicateur local du client (FGenSavedMove, FGenCharacterNetworkMoveData) : le serveur simule ce mouvement à cette
 * vitesse, comme le client. Il ne croit pas le client sur parole : l'annonce est bornée par ce que son propre état
 * permet à SpeedChangeCorrectionGrace près (AGenCharacterBase::GetMaxClaimableLocalMoveSpeedMultiplier) ; au-delà, il
 * prend son plafond et l'écart est corrigé comme avant. La grâce bornée par un budget reste le filet de sécurité.
 */
UCLASS()
class GEN_API UGenCharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	UGenCharacterMovementComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual FNetworkPredictionData_Client* GetPredictionData_Client() const override;

	/** Serveur : mouvements dont le multiplicateur annoncé dépassait le plafond permis (ramené au plafond ; tests). */
	int32 GetClampedSpeedClaimCount() const { return ClampedSpeedClaimCount; }

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
	/** Mouvement du client simulé par le serveur (ou rejoué par le client) : à la vitesse du multiplicateur annoncé. */
	virtual void MoveAutonomous(float ClientTimeStamp, float DeltaTime, uint8 CompressedFlags, const FVector& NewAccel) override;

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
	FGenCharacterNetworkMoveDataContainer GenMoveDataContainer;
	int32 ClampedSpeedClaimCount = 0;

	double GraceEndTime = -1.0;
	/** Reste du budget d'écart de la fenêtre en cours (remis à SpeedChangeErrorTolerance à chaque changement de ralenti). */
	float GraceErrorBudget = 0.f;
	float GraceAcceptedDistance = 0.f;
	/** Le dernier écart a été accepté par la grâce : le serveur reprend la position du client. */
	bool bAcceptClientPositionInGrace = false;
	int32 ServerCorrectionCount = 0;
	int32 GraceAcceptedCount = 0;
};
