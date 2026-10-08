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

	/**
	 * Bond (UGenGA_Leap::FillAimData) : distance (cm) et lacet (degrés) du bond mesurés par le client depuis SA position.
	 * < 0 = pas un bond. Envoyés en float complets : le serveur les reprend à l'identique s'il les accepte
	 * (GenAreaRules::AcceptClientLeap), les deux forces de saut sont alors égales (revue Plan 2 Tasks 7-8, I-1).
	 */
	UPROPERTY()
	float LeapDistance = -1.f;

	UPROPERTY()
	float LeapYaw = 0.f;

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
