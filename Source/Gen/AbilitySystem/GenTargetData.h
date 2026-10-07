#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbilityTargetTypes.h"
#include "GenTargetData.generated.h"

/**
 * Visée envoyée par le client : point sous le curseur + nombre d'unités nourries.
 * Le serveur borne FedCount (GenFeeding::ValidateFedCount) avant de l'utiliser.
 */
USTRUCT()
struct GEN_API FGenTargetData_Aim : public FGameplayAbilityTargetData_SingleTargetHit
{
	GENERATED_BODY()

	UPROPERTY()
	uint8 FedCount = 0;

	virtual UScriptStruct* GetScriptStruct() const override { return FGenTargetData_Aim::StaticStruct(); }

	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess);
};

template<>
struct TStructOpsTypeTraits<FGenTargetData_Aim> : public TStructOpsTypeTraitsBase2<FGenTargetData_Aim>
{
	enum
	{
		WithNetSerializer = true
	};
};
