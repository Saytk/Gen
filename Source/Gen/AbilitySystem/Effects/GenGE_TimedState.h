#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "GenGE_TimedState.generated.h"

/**
 * État temporaire générique (contre, intouchable, immunité...) : durée = SetByCaller.Duration,
 * tags accordés passés dans le spec (DynamicGrantedTags), comme le cooldown générique.
 * Toujours renseigner la durée via SetDuration.
 */
UCLASS()
class GEN_API UGenGE_TimedState : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UGenGE_TimedState();

	/** Durée (s) et tags accordés tant que l'effet dure. */
	static void SetDuration(FGameplayEffectSpec& Spec, float Duration, const FGameplayTagContainer& GrantedTags);
};

/**
 * État temporaire qui multiplie aussi la vitesse (multiplicateurs composés) :
 * ralenti (0.5), hâte (1.3), étourdissement (0). Toujours renseigner via SetMagnitudes.
 */
UCLASS()
class GEN_API UGenGE_TimedMoveSpeed : public UGenGE_TimedState
{
	GENERATED_BODY()

public:
	UGenGE_TimedMoveSpeed();

	static void SetMagnitudes(FGameplayEffectSpec& Spec, float Duration, float MoveSpeedMultiplier, const FGameplayTagContainer& GrantedTags);
};
