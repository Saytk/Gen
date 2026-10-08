#include "AbilitySystem/Effects/GenGE_TimedState.h"

#include "AbilitySystem/GenAttributeSet.h"
#include "GenGameplayTags.h"

UGenGE_TimedState::UGenGE_TimedState()
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;

	FSetByCallerFloat SetByCaller;
	SetByCaller.DataTag = GenGameplayTags::SetByCaller_Duration;
	DurationMagnitude = FGameplayEffectModifierMagnitude(SetByCaller);
}

void UGenGE_TimedState::SetDuration(FGameplayEffectSpec& Spec, float Duration, const FGameplayTagContainer& GrantedTags)
{
	Spec.SetSetByCallerMagnitude(GenGameplayTags::SetByCaller_Duration, FMath::Max(Duration, 0.01f));
	Spec.DynamicGrantedTags.AppendTags(GrantedTags);
}

UGenGE_TimedMoveSpeed::UGenGE_TimedMoveSpeed()
{
	FSetByCallerFloat SetByCaller;
	SetByCaller.DataTag = GenGameplayTags::SetByCaller_MoveSpeedMultiplier;

	// MultiplyCompound : les effets se multiplient entre eux (0.5 et 0.5 => 0.25, 0 => immobile)
	FGameplayModifierInfo Modifier;
	Modifier.Attribute = UGenAttributeSet::GetMoveSpeedAttribute();
	Modifier.ModifierOp = EGameplayModOp::MultiplyCompound;
	Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(SetByCaller);
	Modifiers.Add(Modifier);
}

void UGenGE_TimedMoveSpeed::SetMagnitudes(FGameplayEffectSpec& Spec, float Duration, float MoveSpeedMultiplier, const FGameplayTagContainer& GrantedTags)
{
	SetDuration(Spec, Duration, GrantedTags);
	Spec.SetSetByCallerMagnitude(GenGameplayTags::SetByCaller_MoveSpeedMultiplier, FMath::Max(MoveSpeedMultiplier, 0.f));
}
