#include "Champions/Curffe/CurffeEffects.h"

#include "AbilitySystem/GenAttributeSet.h"
#include "Champions/Curffe/CurffeTuning.h"

UCurffeGE_HearthSetup::UCurffeGE_HearthSetup()
{
	DurationPolicy = EGameplayEffectDurationType::Infinite;

	FGameplayModifierInfo Modifier;
	Modifier.Attribute = UGenAttributeSet::GetMaxResourceAttribute();
	Modifier.ModifierOp = EGameplayModOp::Override;
	Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(CurffeTuning::MaxFlames));
	Modifiers.Add(Modifier);
}

UCurffeGE_HearthFill::UCurffeGE_HearthFill()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;

	FGameplayModifierInfo Modifier;
	Modifier.Attribute = UGenAttributeSet::GetResourceAttribute();
	Modifier.ModifierOp = EGameplayModOp::Override;
	Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(CurffeTuning::MaxFlames));
	Modifiers.Add(Modifier);
}

UCurffeGE_HearthRegen::UCurffeGE_HearthRegen()
{
	DurationPolicy = EGameplayEffectDurationType::Infinite;
	Period = FScalableFloat(CurffeTuning::FlameRegenPeriod);
	bExecutePeriodicEffectOnApplication = false;

	FGameplayModifierInfo Modifier;
	Modifier.Attribute = UGenAttributeSet::GetResourceAttribute();
	Modifier.ModifierOp = EGameplayModOp::AddBase;
	Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(1.f));
	Modifiers.Add(Modifier);
}
