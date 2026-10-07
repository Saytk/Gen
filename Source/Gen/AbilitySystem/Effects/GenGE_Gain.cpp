#include "AbilitySystem/Effects/GenGE_Gain.h"

#include "AbilitySystem/GenAttributeSet.h"
#include "GenGameplayTags.h"

UGenGE_Gain::UGenGE_Gain()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;

	auto AddModifier = [this](const FGameplayAttribute& Attribute, const FGameplayTag& DataTag)
	{
		FSetByCallerFloat SetByCaller;
		SetByCaller.DataTag = DataTag;

		FGameplayModifierInfo Modifier;
		Modifier.Attribute = Attribute;
		Modifier.ModifierOp = EGameplayModOp::AddBase;
		Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(SetByCaller);
		Modifiers.Add(Modifier);
	};

	AddModifier(UGenAttributeSet::GetEnergyAttribute(), GenGameplayTags::SetByCaller_Energy);
	AddModifier(UGenAttributeSet::GetResourceAttribute(), GenGameplayTags::SetByCaller_Resource);
}

void UGenGE_Gain::SetMagnitudes(FGameplayEffectSpec& Spec, float Energy, float Resource)
{
	Spec.SetSetByCallerMagnitude(GenGameplayTags::SetByCaller_Energy, Energy);
	Spec.SetSetByCallerMagnitude(GenGameplayTags::SetByCaller_Resource, Resource);
}
