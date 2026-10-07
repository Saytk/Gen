#include "AbilitySystem/GenTargetData.h"

bool FGenTargetData_Aim::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	FGameplayAbilityTargetData_SingleTargetHit::NetSerialize(Ar, Map, bOutSuccess);
	Ar << FedCount;
	return true;
}
