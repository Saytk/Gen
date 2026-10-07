#include "AbilitySystem/Effects/GenGE_Cooldown.h"

#include "GenGameplayTags.h"

UGenGE_Cooldown::UGenGE_Cooldown()
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;

	FSetByCallerFloat SetByCaller;
	SetByCaller.DataTag = GenGameplayTags::SetByCaller_Cooldown;
	DurationMagnitude = FGameplayEffectModifierMagnitude(SetByCaller);
}
