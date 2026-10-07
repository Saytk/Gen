#pragma once

#include "CoreMinimal.h"
#include "Abilities/Tasks/AbilityTask.h"
#include "GenAbilityTask_TargetDataUnderCursor.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGenCursorTargetDataSignature, const FGameplayAbilityTargetDataHandle&, DataHandle);

/**
 * Récupère la position visée par la souris et la synchronise avec le serveur.
 *
 * La souris n'existe que sur la machine du joueur : le client calcule le point visé,
 * l'envoie au serveur via le système de "target data" du GAS (prédiction incluse),
 * et le serveur attend de l'avoir reçu avant de continuer le sort.
 * Le point est projeté sur le plan horizontal à la hauteur du lanceur (visée style Battlerite).
 */
UCLASS()
class GEN_API UGenAbilityTask_TargetDataUnderCursor : public UAbilityTask
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Ability|Tasks", meta = (DisplayName = "Target Data Under Cursor", HidePin = "OwningAbility", DefaultToSelf = "OwningAbility", BlueprintInternalUseOnly = "true"))
	static UGenAbilityTask_TargetDataUnderCursor* CreateTargetDataUnderCursor(UGameplayAbility* OwningAbility, uint8 FedCount = 0);

	UPROPERTY(BlueprintAssignable)
	FGenCursorTargetDataSignature ValidData;

protected:
	virtual void Activate() override;

private:
	/** Unités nourries par le sort, transmises au serveur avec la visée. */
	uint8 FedCount = 0;

	void SendCursorData();
	void OnTargetDataReplicatedCallback(const FGameplayAbilityTargetDataHandle& DataHandle, FGameplayTag ActivationTag);
};
