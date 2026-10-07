#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "GenGE_Gain.generated.h"

/**
 * Gain (ou dépense, en négatif) instantané d'énergie et de ressource du champion.
 * Toujours renseigner les deux magnitudes via SetMagnitudes (0 accepté), sinon le GAS
 * signale un SetByCaller manquant.
 */
UCLASS()
class GEN_API UGenGE_Gain : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UGenGE_Gain();

	static void SetMagnitudes(FGameplayEffectSpec& Spec, float Energy, float Resource);
};
