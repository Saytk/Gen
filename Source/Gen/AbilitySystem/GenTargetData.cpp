#include "AbilitySystem/GenTargetData.h"

bool FGenTargetData_Aim::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	FGameplayAbilityTargetData_SingleTargetHit::NetSerialize(Ar, Map, bOutSuccess);
	Ar << FedCount;

	// Bond : un bit, puis la distance et le lacet seulement s'il y en a un
	uint8 bHasLeap = LeapDistance >= 0.f ? 1 : 0;
	Ar.SerializeBits(&bHasLeap, 1);
	if (bHasLeap)
	{
		Ar << LeapDistance;
		Ar << LeapYaw;
	}
	else if (Ar.IsLoading())
	{
		LeapDistance = -1.f;
		LeapYaw = 0.f;
	}
	return true;
}
