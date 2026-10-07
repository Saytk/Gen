#include "AbilitySystem/Effects/GenGE_Damage.h"

#include "AbilitySystem/GenAttributeSet.h"
#include "GenGameplayTags.h"

UGenGE_Damage::UGenGE_Damage()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;

	FSetByCallerFloat SetByCaller;
	SetByCaller.DataTag = GenGameplayTags::SetByCaller_Damage;

	FGameplayModifierInfo DamageModifier;
	DamageModifier.Attribute = UGenAttributeSet::GetIncomingDamageAttribute();
	DamageModifier.ModifierOp = EGameplayModOp::AddBase;
	DamageModifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(SetByCaller);
	Modifiers.Add(DamageModifier);
}
