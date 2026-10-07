#include "AbilitySystem/Effects/GenGE_MoveSpeedMultiplier.h"

#include "AbilitySystem/GenAttributeSet.h"
#include "GenGameplayTags.h"

UGenGE_MoveSpeedMultiplier::UGenGE_MoveSpeedMultiplier()
{
	DurationPolicy = EGameplayEffectDurationType::Infinite;

	FSetByCallerFloat SetByCaller;
	SetByCaller.DataTag = GenGameplayTags::SetByCaller_MoveSpeedMultiplier;

	// MultiplyAdditive : 0.5 => vitesse x0.5
	FGameplayModifierInfo Modifier;
	Modifier.Attribute = UGenAttributeSet::GetMoveSpeedAttribute();
	Modifier.ModifierOp = EGameplayModOp::MultiplyAdditive;
	Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(SetByCaller);
	Modifiers.Add(Modifier);
}
