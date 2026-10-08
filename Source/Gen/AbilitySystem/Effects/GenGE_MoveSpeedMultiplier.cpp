#include "AbilitySystem/Effects/GenGE_MoveSpeedMultiplier.h"

#include "AbilitySystem/GenAttributeSet.h"
#include "GenGameplayTags.h"

UGenGE_MoveSpeedMultiplier::UGenGE_MoveSpeedMultiplier()
{
	DurationPolicy = EGameplayEffectDurationType::Infinite;

	FSetByCallerFloat SetByCaller;
	SetByCaller.DataTag = GenGameplayTags::SetByCaller_MoveSpeedMultiplier;

	// MultiplyCompound : 0.5 => vitesse x0.5, et deux ralentis se multiplient (au lieu de s'additionner à 0)
	FGameplayModifierInfo Modifier;
	Modifier.Attribute = UGenAttributeSet::GetMoveSpeedAttribute();
	Modifier.ModifierOp = EGameplayModOp::MultiplyCompound;
	Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(SetByCaller);
	Modifiers.Add(Modifier);
}
